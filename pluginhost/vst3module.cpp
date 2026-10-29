// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "vst3module.h"

#include <dlfcn.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivsthostapplication.h>
#include <sys/utsname.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

// As in the header: the format's interfaces are released, not deleted.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

namespace rb::pluginhost
{

namespace fs = std::filesystem;
using namespace Steinberg;

namespace
{

/// A stepped parameter with more steps than this is a number, whatever its
/// words, and its steps are not asked for one by one.
constexpr int32 kMostChoices = 64;
/// A stepped parameter with up to this many steps has a point a step.
constexpr int32 kMostSteps = 1024;
/// Points along a parameter that is not stepped.
constexpr int kPoints = 257;

[[nodiscard]] bool same(const TUID left, const TUID right)
{
    return FUnknownPrivate::iidEqual(left, right);
}

/// **Text in one of the factory's fields of a fixed size**: up to its
/// first NUL, or the whole field when a plugin filled it to the end.
template <std::size_t Size>
[[nodiscard]] std::string fixedText(const char8 (&field)[Size])
{
    return std::string(field, ::strnlen(field, Size));
}

[[nodiscard]] std::string lowered(std::string text)
{
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// Whether \a words are only a number.
[[nodiscard]] bool isNumber(const std::string &words)
{
    if (words.empty()) {
        return false;
    }
    char *end = nullptr;
    (void)std::strtod(words.c_str(), &end);
    while (end != nullptr && *end != '\0' && std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    return end != nullptr && end != words.c_str() && *end == '\0';
}

// --- what a host makes for a plugin ---

/// **An attribute list a plugin made through the host**, to send in a
/// message from one of its halves to the other.
class HostAttributes final : public Vst::IAttributeList
{
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void **object) override
    {
        if (same(iid, FUnknown_iid) || same(iid, Vst::IAttributeList_iid)) {
            addRef();
            *object = static_cast<Vst::IAttributeList *>(this);
            return kResultOk;
        }
        *object = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override
    {
        return ++m_count;
    }
    uint32 PLUGIN_API release() override
    {
        const uint32 left = --m_count;
        if (left == 0) {
            delete this;
        }
        return left;
    }

    tresult PLUGIN_API setInt(AttrID id, int64 value) override
    {
        return put(id, value);
    }
    tresult PLUGIN_API getInt(AttrID id, int64 &value) override
    {
        return take(id, value);
    }
    tresult PLUGIN_API setFloat(AttrID id, double value) override
    {
        return put(id, value);
    }
    tresult PLUGIN_API getFloat(AttrID id, double &value) override
    {
        return take(id, value);
    }
    tresult PLUGIN_API setString(AttrID id, const Vst::TChar *text) override
    {
        if (text == nullptr) {
            return kInvalidArgument;
        }
        return put(id, std::u16string{text});
    }
    tresult PLUGIN_API getString(AttrID id, Vst::TChar *text, uint32 sizeInBytes) override
    {
        const auto found = id != nullptr ? m_values.find(id) : m_values.end();
        const auto *kept = found != m_values.end() ? std::get_if<std::u16string>(&found->second) : nullptr;
        const std::size_t room = sizeInBytes / sizeof(Vst::TChar);
        if (kept == nullptr || text == nullptr || room == 0) {
            return kResultFalse;
        }
        const std::size_t copied = std::min(kept->size(), room - 1);
        std::copy_n(kept->data(), copied, text);
        text[copied] = 0;
        return kResultOk;
    }
    tresult PLUGIN_API setBinary(AttrID id, const void *data, uint32 size) override
    {
        if (data == nullptr && size > 0) {
            return kInvalidArgument;
        }
        return put(id, std::string(static_cast<const char *>(data), size));
    }
    tresult PLUGIN_API getBinary(AttrID id, const void *&data, uint32 &size) override
    {
        const auto found = id != nullptr ? m_values.find(id) : m_values.end();
        const auto *kept = found != m_values.end() ? std::get_if<std::string>(&found->second) : nullptr;
        if (kept == nullptr) {
            return kResultFalse;
        }
        data = kept->data();
        size = static_cast<uint32>(kept->size());
        return kResultOk;
    }

private:
    using Value = std::variant<int64, double, std::u16string, std::string>;

    tresult put(AttrID id, Value value)
    {
        if (id == nullptr) {
            return kInvalidArgument;
        }
        m_values[id] = std::move(value);
        return kResultOk;
    }
    template <typename T>
    tresult take(AttrID id, T &value) const
    {
        const auto found = id != nullptr ? m_values.find(id) : m_values.end();
        const auto *kept = found != m_values.end() ? std::get_if<T>(&found->second) : nullptr;
        if (kept == nullptr) {
            return kResultFalse;
        }
        value = *kept;
        return kResultOk;
    }

    std::atomic<uint32> m_count{1};
    std::map<std::string, Value> m_values;
};

/// **A message a plugin made through the host**: an id and attributes.
class HostMessage final : public Vst::IMessage
{
public:
    ~HostMessage()
    {
        m_attributes->release();
    }

    tresult PLUGIN_API queryInterface(const TUID iid, void **object) override
    {
        if (same(iid, FUnknown_iid) || same(iid, Vst::IMessage_iid)) {
            addRef();
            *object = static_cast<Vst::IMessage *>(this);
            return kResultOk;
        }
        *object = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override
    {
        return ++m_count;
    }
    uint32 PLUGIN_API release() override
    {
        const uint32 left = --m_count;
        if (left == 0) {
            delete this;
        }
        return left;
    }

    FIDString PLUGIN_API getMessageID() override
    {
        return m_id.c_str();
    }
    void PLUGIN_API setMessageID(FIDString id) override
    {
        m_id = id != nullptr ? id : "";
    }
    Vst::IAttributeList *PLUGIN_API getAttributes() override
    {
        return m_attributes;
    }

private:
    std::atomic<uint32> m_count{1};
    std::string m_id;
    HostAttributes *m_attributes = new HostAttributes();
};

/// **The host itself**: its name, and the two kinds of object a plugin may
/// ask it to make.
class HostApplication final : public Vst::IHostApplication
{
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void **object) override
    {
        if (same(iid, FUnknown_iid) || same(iid, Vst::IHostApplication_iid)) {
            *object = static_cast<Vst::IHostApplication *>(this);
            return kResultOk;
        }
        *object = nullptr;
        return kNoInterface;
    }
    // **Never let go**: there is one, for as long as the process runs.
    uint32 PLUGIN_API addRef() override
    {
        return 1;
    }
    uint32 PLUGIN_API release() override
    {
        return 1;
    }

    tresult PLUGIN_API getName(Vst::String128 name) override
    {
        vst3SetText(name, 128, "Reverblabr");
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(TUID classId, TUID iid, void **object) override
    {
        if (same(classId, Vst::IMessage_iid) && same(iid, Vst::IMessage_iid)) {
            *object = static_cast<Vst::IMessage *>(new HostMessage());
            return kResultOk;
        }
        if (same(classId, Vst::IAttributeList_iid) && same(iid, Vst::IAttributeList_iid)) {
            *object = static_cast<Vst::IAttributeList *>(new HostAttributes());
            return kResultOk;
        }
        *object = nullptr;
        return kNoInterface;
    }
};

/// The library inside a bundle: `Contents/<machine>-linux/<name>.so`, or
/// the one library there under another name; a library given straight.
[[nodiscard]] std::string libraryOf(const std::string &path)
{
    std::error_code error;
    if (fs::is_regular_file(path, error)) {
        return path;
    }
    utsname machine{};
    const std::string architecture = ::uname(&machine) == 0 ? std::string{machine.machine} : std::string{"x86_64"};
    const fs::path folder = fs::path{path} / "Contents" / (architecture + "-linux");
    const fs::path named = folder / (fs::path{path}.stem().string() + ".so");
    if (fs::is_regular_file(named, error)) {
        return named.string();
    }
    for (const fs::directory_entry &entry : fs::directory_iterator(folder, error)) {
        if (entry.is_regular_file(error) && entry.path().extension() == ".so") {
            return entry.path().string();
        }
    }
    return {};
}

// --- moduleinfo.json, read without loading anything ---

/// **Just enough of JSON for a module's description**: objects, arrays,
/// strings and the rest skipped over. The SDK writes the file with
/// comments and trailing commas allowed, so they are too.
class JsonReader
{
public:
    struct Value {
        enum class Kind : std::uint8_t {
            Other,
            String,
            Array,
            Object,
        };
        Kind kind = Kind::Other;
        std::string text;
        /// An array's items, or an object's members' values.
        std::vector<Value> items;
        /// An object's members' names, in the order of their values.
        std::vector<std::string> keys;

        [[nodiscard]] const Value *member(std::string_view name) const
        {
            for (std::size_t at = 0; at < keys.size() && at < items.size(); ++at) {
                if (keys[at] == name) {
                    return &items[at];
                }
            }
            return nullptr;
        }
        [[nodiscard]] std::string string(std::string_view name) const
        {
            const Value *found = member(name);
            return found != nullptr && found->kind == Kind::String ? found->text : std::string{};
        }
    };

    explicit JsonReader(std::string_view text)
        : m_text(text)
    {
    }

    [[nodiscard]] std::optional<Value> read()
    {
        Value value;
        if (!readValue(value, 0)) {
            return std::nullopt;
        }
        return value;
    }

private:
    static constexpr int kDeepest = 32;

    void skipSpace()
    {
        while (m_at < m_text.size()) {
            const char c = m_text[m_at];
            if (std::isspace(static_cast<unsigned char>(c)) != 0) {
                ++m_at;
            } else if (m_text.substr(m_at, 2) == "//") {
                const std::size_t end = m_text.find('\n', m_at);
                m_at = end == std::string_view::npos ? m_text.size() : end;
            } else if (m_text.substr(m_at, 2) == "/*") {
                const std::size_t end = m_text.find("*/", m_at + 2);
                m_at = end == std::string_view::npos ? m_text.size() : end + 2;
            } else {
                return;
            }
        }
    }

    [[nodiscard]] bool readString(std::string &into)
    {
        ++m_at; // the opening quote
        while (m_at < m_text.size()) {
            const char c = m_text[m_at++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                into += c;
                continue;
            }
            if (m_at >= m_text.size()) {
                return false;
            }
            const char escaped = m_text[m_at++];
            switch (escaped) {
            case 'n':
                into += '\n';
                break;
            case 't':
                into += '\t';
                break;
            case 'r':
                into += '\r';
                break;
            case 'b':
            case 'f':
                break;
            case 'u': {
                if (m_at + 4 > m_text.size()) {
                    return false;
                }
                const auto code =
                    static_cast<char32_t>(std::strtoul(std::string{m_text.substr(m_at, 4)}.c_str(), nullptr, 16));
                m_at += 4;
                const char16_t unit[2] = {static_cast<char16_t>(code), 0};
                into += vst3Text(unit, 1);
                break;
            }
            default:
                into += escaped;
                break;
            }
        }
        return false;
    }

    [[nodiscard]] bool readValue(Value &value, int depth)
    {
        skipSpace();
        if (m_at >= m_text.size() || depth > kDeepest) {
            return false;
        }
        const char c = m_text[m_at];
        if (c == '"') {
            value.kind = Value::Kind::String;
            return readString(value.text);
        }
        if (c == '[' || c == '{') {
            const bool object = c == '{';
            value.kind = object ? Value::Kind::Object : Value::Kind::Array;
            ++m_at;
            for (;;) {
                skipSpace();
                if (m_at >= m_text.size()) {
                    return false;
                }
                if (m_text[m_at] == (object ? '}' : ']')) {
                    ++m_at;
                    return true;
                }
                std::string key;
                if (object) {
                    if (m_text[m_at] != '"' || !readString(key)) {
                        return false;
                    }
                    skipSpace();
                    if (m_at >= m_text.size() || m_text[m_at] != ':') {
                        return false;
                    }
                    ++m_at;
                }
                Value item;
                if (!readValue(item, depth + 1)) {
                    return false;
                }
                if (object) {
                    value.keys.push_back(std::move(key));
                }
                value.items.push_back(std::move(item));
                skipSpace();
                if (m_at < m_text.size() && m_text[m_at] == ',') {
                    ++m_at;
                }
            }
        }
        // A number, true, false or null: skipped to the next separator.
        const std::size_t start = m_at;
        while (m_at < m_text.size() && std::string_view{",]} \t\r\n"}.find(m_text[m_at]) == std::string_view::npos) {
            ++m_at;
        }
        return m_at > start;
    }

    std::string_view m_text;
    std::size_t m_at = 0;
};

/// **The plugin's own word for what it is**, from its sub-categories: the
/// first that says what it does, rather than that it is an effect or how
/// many channels it has. "Fx|Dynamics" is "Dynamics".
[[nodiscard]] std::string categoryOf(const std::string &subCategories)
{
    static constexpr std::array<std::string_view, 9> kKinds = {
        "Fx", "Instrument", "Mono", "Stereo", "Surround", "Ambisonics", "Up-Downmix", "Spatial", "OnlyRT"};
    std::string found;
    std::stringstream parts(subCategories);
    for (std::string part; std::getline(parts, part, '|');) {
        if (!part.empty() && std::ranges::find(kKinds, std::string_view{part}) == kKinds.end()) {
            found = part;
            break;
        }
    }
    return found;
}

/// **How a parameter's values map**, asked of its controller.
[[nodiscard]] Vst3Curve curveOf(Vst::IEditController *controller, const Vst::ParameterInfo &info)
{
    Vst3Curve curve;
    curve.stepped = info.stepCount > 0 && info.stepCount <= kMostSteps;
    const int points = curve.stepped ? info.stepCount + 1 : kPoints;
    for (int point = 0; point < points; ++point) {
        curve.plain.push_back(
            controller->normalizedParamToPlain(info.id, static_cast<double>(point) / static_cast<double>(points - 1)));
    }
    // **One way only**: a parameter whose values turn back on themselves
    // cannot be found again from its value, so it is taken as the plugin's
    // own 0 to 1.
    const bool rising = curve.plain.back() >= curve.plain.front();
    bool monotonic = std::ranges::all_of(curve.plain, [](double value) { return std::isfinite(value); });
    for (std::size_t at = 1; monotonic && at < curve.plain.size(); ++at) {
        monotonic = rising ? curve.plain[at] >= curve.plain[at - 1] : curve.plain[at] <= curve.plain[at - 1];
    }
    if (!monotonic) {
        for (int point = 0; point < points; ++point) {
            curve.plain[static_cast<std::size_t>(point)] = static_cast<double>(point) / static_cast<double>(points - 1);
        }
        return curve;
    }
    // **Logarithmic**: its middle is where the two ends' geometric mean is,
    // and much nearer that than their average.
    const double low = curve.plain.front();
    const double high = curve.plain.back();
    if (!curve.stepped && low > 0.0 && high > 0.0 && std::abs(high / low) > 4.0) {
        const double middle = curve.plain[curve.plain.size() / 2];
        const double geometric = std::sqrt(low * high);
        const double arithmetic = (low + high) / 2.0;
        curve.logarithmic = std::abs(middle - geometric) < 0.05 * std::abs(arithmetic - geometric);
    }
    return curve;
}

} // namespace

// --- class ids and text ---

std::string vst3ClassId(const TUID id)
{
    static constexpr std::string_view kDigits = "0123456789ABCDEF";
    std::string text;
    for (int at = 0; at < 16; ++at) {
        const auto byte = static_cast<unsigned char>(id[at]);
        text += kDigits[byte >> 4];
        text += kDigits[byte & 0x0f];
    }
    return text;
}

bool vst3ClassIdFrom(std::string_view text, TUID id)
{
    if (text.size() != 32) {
        return false;
    }
    for (int at = 0; at < 16; ++at) {
        unsigned value = 0;
        for (int half = 0; half < 2; ++half) {
            const char c = text[static_cast<std::size_t>(at * 2 + half)];
            const int digit = std::isdigit(static_cast<unsigned char>(c)) != 0 ? c - '0'
                : (c >= 'a' && c <= 'f')                                       ? c - 'a' + 10
                : (c >= 'A' && c <= 'F')                                       ? c - 'A' + 10
                                                                               : -1;
            if (digit < 0) {
                return false;
            }
            value = value * 16 + static_cast<unsigned>(digit);
        }
        id[at] = static_cast<char>(value);
    }
    return true;
}

std::string vst3Text(const char16 *text, std::size_t size)
{
    std::string out;
    if (text == nullptr) {
        return out;
    }
    for (std::size_t at = 0; (size == 0 || at < size) && text[at] != 0; ++at) {
        char32_t code = text[at];
        if (code >= 0xd800 && code < 0xdc00 && (size == 0 || at + 1 < size) && text[at + 1] >= 0xdc00
            && text[at + 1] < 0xe000) {
            code = 0x10000 + ((code - 0xd800) << 10) + (static_cast<char32_t>(text[at + 1]) - 0xdc00);
            ++at;
        }
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xc0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3f));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xe0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        }
    }
    return out;
}

void vst3SetText(char16 *into, std::size_t size, std::string_view text)
{
    if (into == nullptr || size == 0) {
        return;
    }
    std::size_t written = 0;
    std::size_t at = 0;
    while (at < text.size() && written + 1 < size) {
        const auto lead = static_cast<unsigned char>(text[at]);
        const int length = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        char32_t code = length == 1 ? lead : length == 2 ? (lead & 0x1f) : length == 3 ? (lead & 0x0f) : (lead & 0x07);
        for (int more = 1; more < length && at + static_cast<std::size_t>(more) < text.size(); ++more) {
            code = (code << 6) | (static_cast<unsigned char>(text[at + static_cast<std::size_t>(more)]) & 0x3f);
        }
        at += static_cast<std::size_t>(length);
        if (code >= 0x10000) {
            if (written + 2 >= size) {
                break;
            }
            code -= 0x10000;
            into[written++] = static_cast<char16>(0xd800 + (code >> 10));
            into[written++] = static_cast<char16>(0xdc00 + (code & 0x3ff));
        } else {
            into[written++] = static_cast<char16>(code);
        }
    }
    into[written] = 0;
}

FUnknown *vst3HostContext()
{
    static HostApplication host;
    return &host;
}

// --- Vst3Stream ---

tresult PLUGIN_API Vst3Stream::queryInterface(const TUID iid, void **object)
{
    if (same(iid, FUnknown_iid) || same(iid, IBStream_iid)) {
        *object = static_cast<IBStream *>(this);
        return kResultOk;
    }
    if (same(iid, ISizeableStream_iid)) {
        *object = static_cast<ISizeableStream *>(this);
        return kResultOk;
    }
    *object = nullptr;
    return kNoInterface;
}

// **On the stack of whoever reads or writes it**: a plugin does not keep
// a stream past the call it was given in.
uint32 PLUGIN_API Vst3Stream::addRef()
{
    return 1;
}

uint32 PLUGIN_API Vst3Stream::release()
{
    return 1;
}

tresult PLUGIN_API Vst3Stream::read(void *buffer, int32 count, int32 *done)
{
    if (buffer == nullptr || count < 0) {
        return kInvalidArgument;
    }
    const std::size_t left = m_at < m_bytes.size() ? m_bytes.size() - m_at : 0;
    const std::size_t taken = std::min<std::size_t>(left, static_cast<std::size_t>(count));
    std::memcpy(buffer, m_bytes.data() + m_at, taken);
    m_at += taken;
    if (done != nullptr) {
        *done = static_cast<int32>(taken);
    }
    return kResultOk;
}

tresult PLUGIN_API Vst3Stream::write(void *buffer, int32 count, int32 *done)
{
    if (buffer == nullptr || count < 0) {
        return kInvalidArgument;
    }
    const auto size = static_cast<std::size_t>(count);
    if (m_at + size > m_bytes.size()) {
        m_bytes.resize(m_at + size);
    }
    std::memcpy(m_bytes.data() + m_at, buffer, size);
    m_at += size;
    if (done != nullptr) {
        *done = count;
    }
    return kResultOk;
}

tresult PLUGIN_API Vst3Stream::seek(int64 position, int32 mode, int64 *result)
{
    int64 from = 0;
    if (mode == kIBSeekCur) {
        from = static_cast<int64>(m_at);
    } else if (mode == kIBSeekEnd) {
        from = static_cast<int64>(m_bytes.size());
    } else if (mode != kIBSeekSet) {
        return kInvalidArgument;
    }
    const int64 to = from + position;
    if (to < 0) {
        return kResultFalse;
    }
    m_at = static_cast<std::size_t>(to);
    if (result != nullptr) {
        *result = to;
    }
    return kResultOk;
}

tresult PLUGIN_API Vst3Stream::tell(int64 *position)
{
    if (position == nullptr) {
        return kInvalidArgument;
    }
    *position = static_cast<int64>(m_at);
    return kResultOk;
}

tresult PLUGIN_API Vst3Stream::getStreamSize(int64 &size)
{
    size = static_cast<int64>(m_bytes.size());
    return kResultOk;
}

tresult PLUGIN_API Vst3Stream::setStreamSize(int64 size)
{
    if (size < 0) {
        return kInvalidArgument;
    }
    m_bytes.resize(static_cast<std::size_t>(size));
    m_at = std::min(m_at, m_bytes.size());
    return kResultOk;
}

// --- Vst3Module ---

std::shared_ptr<Vst3Module> Vst3Module::open(const std::string &path)
{
    const std::string library = libraryOf(path);
    if (library.empty()) {
        std::fprintf(stderr, "no library for this machine in the VST3 bundle %s\n", path.c_str());
        return nullptr;
    }
    void *handle = ::dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        std::fprintf(stderr, "not a loadable VST3 library: %s\n", ::dlerror());
        return nullptr;
    }
    using Entry = bool (*)(void *);
    using Exit = bool (*)();
    using Factory = IPluginFactory *(*)();
    const auto entry = reinterpret_cast<Entry>(::dlsym(handle, "ModuleEntry"));
    const auto exit = reinterpret_cast<Exit>(::dlsym(handle, "ModuleExit"));
    const auto factoryOf = reinterpret_cast<Factory>(::dlsym(handle, "GetPluginFactory"));
    // **Entered before anything else is asked of it**, as the format
    // says a Linux library is; one without the entry is let in, as other
    // hosts let it.
    if (factoryOf == nullptr || (entry != nullptr && !entry(handle))) {
        std::fprintf(stderr, "not a VST3 library this host can use: %s\n", library.c_str());
        ::dlclose(handle);
        return nullptr;
    }
    IPluginFactory *factory = factoryOf();
    if (factory == nullptr) {
        if (exit != nullptr) {
            (void)exit();
        }
        ::dlclose(handle);
        return nullptr;
    }
    if (const Vst3Ref<IPluginFactory3> third = vst3Query<IPluginFactory3>(factory, IPluginFactory3_iid)) {
        (void)third->setHostContext(vst3HostContext());
    }
    std::shared_ptr<Vst3Module> module(new Vst3Module());
    module->m_path = path;
    module->m_handle = handle;
    module->m_factory = factory;
    module->m_exit = exit;
    return module;
}

Vst3Module::~Vst3Module()
{
    m_factory->release();
    // **Left loaded**, as a CLAP file is: what a plugin leaves running must
    // still have its code there. The host ends soon after anyway.
    if (m_exit != nullptr) {
        (void)m_exit();
    }
}

std::vector<Vst3Module::Class> Vst3Module::classes() const
{
    std::vector<Class> found;
    const Vst3Ref<IPluginFactory2> second = vst3Query<IPluginFactory2>(m_factory, IPluginFactory2_iid);
    const int32 count = m_factory->countClasses();
    for (int32 index = 0; index < count; ++index) {
        Class entry;
        if (second) {
            PClassInfo2 info{};
            if (second->getClassInfo2(index, &info) != kResultOk) {
                continue;
            }
            entry.id = vst3ClassId(info.cid);
            entry.name = fixedText(info.name);
            entry.vendor = fixedText(info.vendor);
            entry.category = fixedText(info.category);
            entry.subCategories = fixedText(info.subCategories);
        } else {
            PClassInfo info{};
            if (m_factory->getClassInfo(index, &info) != kResultOk) {
                continue;
            }
            entry.id = vst3ClassId(info.cid);
            entry.name = fixedText(info.name);
            entry.category = fixedText(info.category);
        }
        found.push_back(std::move(entry));
    }
    return found;
}

std::string Vst3Module::vendor() const
{
    PFactoryInfo info{};
    return m_factory->getFactoryInfo(&info) == kResultOk ? fixedText(info.vendor) : std::string{};
}

void *Vst3Module::create(const TUID id, const TUID iid) const
{
    void *made = nullptr;
    if (m_factory->createInstance(id, iid, &made) != kResultOk) {
        return nullptr;
    }
    return made;
}

std::optional<std::vector<Vst3Module::Class>> vst3ModuleInfo(const std::string &bundle)
{
    std::ifstream in(fs::path{bundle} / "Contents" / "Resources" / "moduleinfo.json", std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    const std::optional<JsonReader::Value> read = JsonReader(text).read();
    if (!read || read->kind != JsonReader::Value::Kind::Object) {
        return std::nullopt;
    }
    const JsonReader::Value *classes = read->member("Classes");
    if (classes == nullptr || classes->kind != JsonReader::Value::Kind::Array) {
        return std::nullopt;
    }
    std::string vendor;
    if (const JsonReader::Value *factory = read->member("Factory Info"); factory != nullptr) {
        vendor = factory->string("Vendor");
    }
    std::vector<Vst3Module::Class> found;
    for (const JsonReader::Value &item : classes->items) {
        Vst3Module::Class entry;
        entry.id = vst3CanonicalId(item.string("CID")).value_or(std::string{});
        entry.name = item.string("Name");
        entry.vendor = item.string("Vendor");
        if (entry.vendor.empty()) {
            entry.vendor = vendor;
        }
        entry.category = item.string("Category");
        if (const JsonReader::Value *sub = item.member("Sub Categories"); sub != nullptr) {
            for (const JsonReader::Value &part : sub->items) {
                if (!entry.subCategories.empty()) {
                    entry.subCategories += '|';
                }
                entry.subCategories += part.text;
            }
        }
        TUID check{};
        if (vst3ClassIdFrom(entry.id, check)) {
            found.push_back(std::move(entry));
        }
    }
    return found;
}

// --- a plugin's two halves ---

Vst3Relay::Vst3Relay()
    : m_main(std::this_thread::get_id())
{
}

void Vst3Relay::add(Vst::IConnectionPoint *to)
{
    if (to != nullptr) {
        m_to.push_back(to);
    }
}

void Vst3Relay::remove(Vst::IConnectionPoint *to)
{
    std::erase(m_to, to);
}

tresult PLUGIN_API Vst3Relay::queryInterface(const TUID iid, void **object)
{
    if (same(iid, FUnknown_iid) || same(iid, Vst::IConnectionPoint_iid)) {
        *object = static_cast<Vst::IConnectionPoint *>(this);
        return kResultOk;
    }
    *object = nullptr;
    return kNoInterface;
}

// **The host's own**, living as long as the halves it joins.
uint32 PLUGIN_API Vst3Relay::addRef()
{
    return 1;
}

uint32 PLUGIN_API Vst3Relay::release()
{
    return 1;
}

tresult PLUGIN_API Vst3Relay::connect(Vst::IConnectionPoint *)
{
    return kResultOk;
}

tresult PLUGIN_API Vst3Relay::disconnect(Vst::IConnectionPoint *)
{
    return kResultOk;
}

tresult PLUGIN_API Vst3Relay::notify(Vst::IMessage *message)
{
    if (message == nullptr) {
        return kInvalidArgument;
    }
    if (std::this_thread::get_id() != m_main) {
        m_dropped.fetch_add(1, std::memory_order_relaxed);
        return kResultFalse;
    }
    // A copy: a half told may join or leave another while it is told.
    const std::vector<Vst::IConnectionPoint *> to = m_to;
    for (Vst::IConnectionPoint *point : to) {
        (void)point->notify(message);
    }
    return kResultOk;
}

Vst3Halves::~Vst3Halves()
{
    if (componentPoint && toController) {
        (void)componentPoint->disconnect(toController.get());
    }
    if (controllerPoint && toComponents) {
        (void)controllerPoint->disconnect(toComponents.get());
    }
    componentPoint.reset();
    controllerPoint.reset();
    if (controller) {
        (void)controller->setComponentHandler(nullptr);
        if (!oneObject) {
            (void)controller->terminate();
        }
        controller.reset();
    }
    if (component) {
        (void)component->terminate();
        component.reset();
    }
}

Vst3Ref<Vst::IComponent> makeVst3Component(const Vst3Module &module, const std::string &id)
{
    TUID classId{};
    if (!vst3ClassIdFrom(id, classId)) {
        return {};
    }
    Vst3Ref<Vst::IComponent> component(static_cast<Vst::IComponent *>(module.create(classId, Vst::IComponent_iid)));
    if (!component || component->initialize(vst3HostContext()) != kResultOk) {
        return {};
    }
    return component;
}

std::optional<Vst3Halves>
makeVst3Halves(const Vst3Module &module, const std::string &id, Vst::IComponentHandler *handler)
{
    Vst3Halves halves;
    halves.component = makeVst3Component(module, id);
    if (!halves.component) {
        return std::nullopt;
    }
    // **One object, or a controller of its own class**, which the
    // component names.
    halves.controller = vst3Query<Vst::IEditController>(halves.component.get(), Vst::IEditController_iid);
    halves.oneObject = static_cast<bool>(halves.controller);
    if (!halves.controller) {
        TUID controllerId{};
        if (halves.component->getControllerClassId(controllerId) == kResultOk) {
            Vst3Ref<Vst::IEditController> made(
                static_cast<Vst::IEditController *>(module.create(controllerId, Vst::IEditController_iid)));
            if (made && made->initialize(vst3HostContext()) == kResultOk) {
                halves.controller = std::move(made);
            }
        }
    }
    if (!halves.controller) {
        // Without one there are no settings to show or keep.
        return std::nullopt;
    }
    if (handler != nullptr) {
        (void)halves.controller->setComponentHandler(handler);
    }
    if (!halves.oneObject) {
        // **Joined through the host's relays**, which pass on only what
        // is sent on the main thread: a component may send from inside its
        // process call, and the controller must not run there.
        halves.componentPoint = vst3Query<Vst::IConnectionPoint>(halves.component.get(), Vst::IConnectionPoint_iid);
        halves.controllerPoint = vst3Query<Vst::IConnectionPoint>(halves.controller.get(), Vst::IConnectionPoint_iid);
        if (halves.componentPoint && halves.controllerPoint) {
            halves.toController = std::make_unique<Vst3Relay>();
            halves.toComponents = std::make_unique<Vst3Relay>();
            halves.toController->add(halves.controllerPoint.get());
            halves.toComponents->add(halves.componentPoint.get());
            (void)halves.componentPoint->connect(halves.toController.get());
            (void)halves.controllerPoint->connect(halves.toComponents.get());
        } else {
            halves.componentPoint.reset();
            halves.controllerPoint.reset();
        }
    }
    // **What the component starts with, the controller shows**; an empty
    // state is none, which some plugins cannot be given.
    Vst3Stream state;
    if (halves.component->getState(&state) == kResultOk && !state.bytes().empty()) {
        Vst3Stream reading(state.bytes());
        (void)halves.controller->setComponentState(&reading);
    }
    return halves;
}

std::uint32_t vst3MainChannels(Vst::IComponent *component, Vst::BusDirection direction)
{
    const int32 count = component->getBusCount(Vst::kAudio, direction);
    for (int32 index = 0; index < count; ++index) {
        Vst::BusInfo info{};
        if (component->getBusInfo(Vst::kAudio, direction, index, info) == kResultOk && info.busType == Vst::kMain) {
            return static_cast<std::uint32_t>(std::max(info.channelCount, 0));
        }
    }
    return 0;
}

// --- describing ---

double Vst3Curve::normalizedOf(double value) const noexcept RB_NONBLOCKING
{
    const std::size_t points = plain.size();
    if (points < 2) {
        return 0.0;
    }
    const std::size_t last = points - 1;
    const bool rising = plain[last] >= plain[0];
    if (stepped) {
        // The nearest step.
        std::size_t nearest = 0;
        double distance = std::abs(plain[0] - value);
        for (std::size_t at = 1; at < points; ++at) {
            if (const double away = std::abs(plain[at] - value); away < distance) {
                nearest = at;
                distance = away;
            }
        }
        return static_cast<double>(nearest) / static_cast<double>(last);
    }
    if (rising ? value <= plain[0] : value >= plain[0]) {
        return 0.0;
    }
    if (rising ? value >= plain[last] : value <= plain[last]) {
        return 1.0;
    }
    // The two points it is between, found by halving.
    std::size_t low = 0;
    std::size_t high = last;
    while (high - low > 1) {
        const std::size_t middle = (low + high) / 2;
        if (rising ? plain[middle] <= value : plain[middle] >= value) {
            low = middle;
        } else {
            high = middle;
        }
    }
    double from = plain[low];
    double to = plain[high];
    double at = value;
    if (logarithmic && from > 0.0 && to > 0.0 && value > 0.0) {
        from = std::log(from);
        to = std::log(to);
        at = std::log(value);
    }
    const double part = to != from ? (at - from) / (to - from) : 0.0;
    return (static_cast<double>(low) + part) / static_cast<double>(last);
}

double Vst3Curve::plainOf(double normalized) const noexcept RB_NONBLOCKING
{
    const std::size_t points = plain.size();
    if (points == 0) {
        return normalized;
    }
    const double clamped = std::clamp(normalized, 0.0, 1.0);
    const std::size_t last = points - 1;
    if (stepped) {
        // As the format counts steps: a step is an equal share of 0 to 1.
        const auto step = static_cast<std::size_t>(clamped * static_cast<double>(points));
        return plain[std::min(step, last)];
    }
    const double position = clamped * static_cast<double>(last);
    const auto low = std::min(static_cast<std::size_t>(position), last > 0 ? last - 1 : 0);
    const std::size_t high = std::min(low + 1, last);
    const double part = position - static_cast<double>(low);
    if (logarithmic && plain[low] > 0.0 && plain[high] > 0.0) {
        return std::exp(std::log(plain[low]) + part * (std::log(plain[high]) - std::log(plain[low])));
    }
    return plain[low] + part * (plain[high] - plain[low]);
}

std::vector<plugin::PluginParam>
vst3Params(Vst::IEditController *controller, std::vector<Vst::ParamID> *ids, std::vector<Vst3Curve> *curves)
{
    std::vector<plugin::PluginParam> found;
    const int32 count = controller->getParameterCount();
    for (int32 index = 0; index < count; ++index) {
        Vst::ParameterInfo info{};
        if (controller->getParameterInfo(index, info) != kResultOk
            || (info.flags
                & (Vst::ParameterInfo::kIsHidden | Vst::ParameterInfo::kIsReadOnly
                   | Vst::ParameterInfo::kIsProgramChange))
                != 0) {
            continue;
        }
        Vst3Curve curve = curveOf(controller, info);
        plugin::PluginParam param;
        param.id = std::to_string(info.id);
        param.name = vst3Text(info.title, 128);
        param.minimum = static_cast<float>(std::min(curve.plain.front(), curve.plain.back()));
        param.maximum = static_cast<float>(std::max(curve.plain.front(), curve.plain.back()));
        param.defaultValue =
            std::clamp(static_cast<float>(curve.plainOf(info.defaultNormalizedValue)), param.minimum, param.maximum);
        param.logarithmic = curve.logarithmic;
        const auto words = [&](double normalized) {
            Vst::String128 text{};
            if (controller->getParamStringByValue(info.id, normalized, text) != kResultOk) {
                return std::string{};
            }
            return vst3Text(text, 128);
        };
        if (info.stepCount > 0 && info.stepCount <= kMostChoices) {
            // **What its steps are, from their words.**
            bool numbers = true;
            std::vector<std::pair<float, std::string>> choices;
            for (int32 step = 0; step <= info.stepCount; ++step) {
                std::string said = words(static_cast<double>(step) / static_cast<double>(info.stepCount));
                numbers = numbers && isNumber(said);
                choices.emplace_back(static_cast<float>(curve.plain[static_cast<std::size_t>(step)]), std::move(said));
            }
            const bool onOff = info.stepCount == 1 && choices[0].first == 0.0f && choices[1].first == 1.0f
                && lowered(choices[0].second) == "off" && lowered(choices[1].second) == "on";
            if (onOff) {
                param.kind = plugin::PluginParam::Kind::Toggle;
            } else if (numbers && (info.flags & Vst::ParameterInfo::kIsList) == 0) {
                param.kind = plugin::PluginParam::Kind::Int;
            } else {
                param.kind = plugin::PluginParam::Kind::Choice;
                param.choices = std::move(choices);
            }
        } else if (info.stepCount > 0) {
            const bool whole = param.minimum == std::floor(param.minimum) && param.maximum == std::floor(param.maximum);
            param.kind = whole ? plugin::PluginParam::Kind::Int : plugin::PluginParam::Kind::Float;
        }
        found.push_back(std::move(param));
        if (ids != nullptr) {
            ids->push_back(info.id);
        }
        if (curves != nullptr) {
            curves->push_back(std::move(curve));
        }
    }
    return found;
}

bool describeVst3(const std::string &path, const std::function<void(const plugin::PluginDescription &)> &found)
{
    // **What the bundle says of itself first**: one that says it has no
    // plugin's sound in it is not loaded.
    const std::optional<std::vector<Vst3Module::Class>> listed = vst3ModuleInfo(path);
    if (listed && std::ranges::none_of(*listed, [](const Vst3Module::Class &entry) {
            return entry.category == kVstAudioEffectClass;
        })) {
        return true;
    }
    const std::shared_ptr<Vst3Module> module = Vst3Module::open(path);
    if (!module) {
        return false;
    }
    const std::string vendor = module->vendor();
    // **Each plugin is let go once the next is made**, for the reason a
    // CLAP file's are: what a bundle's plugins share is started once.
    std::optional<Vst3Halves> previous;
    for (const Vst3Module::Class &entry : listed ? *listed : module->classes()) {
        if (entry.category != kVstAudioEffectClass) {
            continue;
        }
        plugin::PluginDescription description;
        description.format = "vst3";
        description.id = entry.id;
        description.name = entry.name.empty() ? entry.id : entry.name;
        description.vendor = entry.vendor.empty() ? vendor : entry.vendor;
        description.category = categoryOf(entry.subCategories);
        std::optional<Vst3Halves> halves = makeVst3Halves(*module, entry.id, nullptr);
        previous.reset();
        if (!halves) {
            // One that will not start is not offered: its block could not
            // run it.
            std::fprintf(stderr, "the plugin %s would not start\n", entry.id.c_str());
            continue;
        }
        description.audioInputs = vst3MainChannels(halves->component.get(), Vst::kInput);
        description.audioOutputs = vst3MainChannels(halves->component.get(), Vst::kOutput);
        description.params = vst3Params(halves->controller.get());
        description.parameters = static_cast<std::uint32_t>(description.params.size());
        previous.emplace(std::move(*halves));
        found(description);
    }
    return true;
}

// --- presets ---

namespace
{

constexpr std::string_view kPresetMagic = "VST3";
constexpr std::int32_t kPresetVersion = 1;
/// The header: the magic, the version, the class id as text, and where
/// the list of chunks is.
constexpr std::size_t kHeaderSize = 4 + 4 + 32 + 8;
constexpr std::string_view kComponentChunk = "Comp";
constexpr std::string_view kControllerChunk = "Cont";
constexpr std::string_view kListChunk = "List";

template <typename T>
void putLittle(std::string &out, T value)
{
    for (std::size_t at = 0; at < sizeof(T); ++at) {
        out += static_cast<char>((static_cast<std::uint64_t>(value) >> (8 * at)) & 0xff);
    }
}

template <typename T>
[[nodiscard]] std::optional<T> getLittle(const std::string &in, std::size_t at)
{
    if (at + sizeof(T) > in.size()) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (std::size_t part = 0; part < sizeof(T); ++part) {
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(in[at + part])) << (8 * part);
    }
    return static_cast<T>(value);
}

} // namespace

std::string writeVst3Preset(const Vst3Preset &preset)
{
    std::string out;
    out += kPresetMagic;
    putLittle<std::int32_t>(out, kPresetVersion);
    std::string id = preset.classId;
    id.resize(32, '0');
    out += id;
    putLittle<std::int64_t>(out, 0); // where the list is, once it is known
    struct Chunk {
        std::string_view id;
        std::int64_t offset;
        std::int64_t size;
    };
    std::vector<Chunk> chunks;
    chunks.push_back(
        {kComponentChunk, static_cast<std::int64_t>(out.size()), static_cast<std::int64_t>(preset.component.size())});
    out += preset.component;
    if (preset.controller) {
        chunks.push_back({kControllerChunk,
                          static_cast<std::int64_t>(out.size()),
                          static_cast<std::int64_t>(preset.controller->size())});
        out += *preset.controller;
    }
    const auto list = static_cast<std::int64_t>(out.size());
    out += kListChunk;
    putLittle<std::int32_t>(out, static_cast<std::int32_t>(chunks.size()));
    for (const Chunk &chunk : chunks) {
        out += chunk.id;
        putLittle<std::int64_t>(out, chunk.offset);
        putLittle<std::int64_t>(out, chunk.size);
    }
    std::string where;
    putLittle<std::int64_t>(where, list);
    out.replace(kHeaderSize - 8, 8, where);
    return out;
}

std::optional<Vst3Preset> readVst3Preset(const std::string &bytes)
{
    if (bytes.size() < kHeaderSize || std::string_view{bytes}.substr(0, 4) != kPresetMagic) {
        return std::nullopt;
    }
    Vst3Preset preset;
    preset.classId = vst3CanonicalId(std::string_view{bytes}.substr(8, 32)).value_or(bytes.substr(8, 32));
    const std::optional<std::int64_t> list = getLittle<std::int64_t>(bytes, 40);
    if (!list || *list < static_cast<std::int64_t>(kHeaderSize) || static_cast<std::size_t>(*list) + 8 > bytes.size()
        || std::string_view{bytes}.substr(static_cast<std::size_t>(*list), 4) != kListChunk) {
        return std::nullopt;
    }
    const auto at = static_cast<std::size_t>(*list);
    const std::optional<std::int32_t> count = getLittle<std::int32_t>(bytes, at + 4);
    if (!count || *count < 0) {
        return std::nullopt;
    }
    bool component = false;
    for (std::int32_t index = 0; index < *count; ++index) {
        const std::size_t entry = at + 8 + static_cast<std::size_t>(index) * 20;
        const std::optional<std::int64_t> offset = getLittle<std::int64_t>(bytes, entry + 4);
        const std::optional<std::int64_t> size = getLittle<std::int64_t>(bytes, entry + 12);
        if (!offset || !size || *offset < 0 || *size < 0
            || static_cast<std::size_t>(*offset) + static_cast<std::size_t>(*size) > bytes.size()) {
            return std::nullopt;
        }
        const std::string_view id = std::string_view{bytes}.substr(entry, 4);
        std::string data = bytes.substr(static_cast<std::size_t>(*offset), static_cast<std::size_t>(*size));
        if (id == kComponentChunk) {
            preset.component = std::move(data);
            component = true;
        } else if (id == kControllerChunk) {
            preset.controller = std::move(data);
        }
    }
    if (!component) {
        return std::nullopt;
    }
    return preset;
}

std::string vst3FileName(std::string_view name)
{
    std::string text{name};
    for (char &c : text) {
        if (c == '/' || static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
            c = '_';
        }
    }
    return text.empty() || text == "." || text == ".." ? std::string{"_"} : text;
}

std::optional<std::string> vst3CanonicalId(std::string_view text)
{
    TUID id{};
    if (!vst3ClassIdFrom(text, id)) {
        return std::nullopt;
    }
    return vst3ClassId(id);
}

std::optional<std::string> vst3PresetClassOf(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    std::string header(40, '\0');
    if (!in || !in.read(header.data(), static_cast<std::streamsize>(header.size()))
        || std::string_view{header}.substr(0, 4) != kPresetMagic) {
        return std::nullopt;
    }
    return vst3CanonicalId(std::string_view{header}.substr(8, 32));
}

std::vector<std::string> vst3PresetFolders(const std::string &vendor, const std::string &name)
{
    // A name is one folder, whatever it has in it.
    const auto folderName = [](const std::string &text) { return vst3FileName(text); };
    std::vector<std::string> folders;
    if (const char *home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        folders.push_back((fs::path{home} / ".vst3" / "presets" / folderName(vendor) / folderName(name)).string());
    }
    for (const char *root : {"/usr/share/vst3/presets", "/usr/local/share/vst3/presets"}) {
        folders.push_back((fs::path{root} / folderName(vendor) / folderName(name)).string());
    }
    return folders;
}

} // namespace rb::pluginhost

#pragma GCC diagnostic pop
