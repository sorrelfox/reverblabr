// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pw/virtualdeviceconf.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QUuid>
#include <QVariantMap>
#include <fcntl.h>
#include <spa/utils/json.h>
#include <unistd.h>

#include <array>
#include <optional>

using namespace Qt::StringLiterals;

namespace rb::pw
{

namespace
{

constexpr auto kUserFileName = "90-reverblabr-virtual-devices.conf"_L1;
constexpr auto kSystemFileName = "90-reverblabr-virtual-devices-system.conf"_L1;
constexpr auto kDropInDirectory = "pipewire/pipewire.conf.d"_L1;
constexpr auto kNullSinkFactory = "support.null-audio-sink"_L1;
constexpr auto kSinkClass = "Audio/Sink"_L1;
constexpr auto kVirtualSourceClass = "Audio/Source/Virtual"_L1;

[[nodiscard]] QByteArray bytes(std::string_view text)
{
    return {text.data(), static_cast<qsizetype>(text.size())};
}

[[nodiscard]] QString string(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

/// A line break of any kind, or anything else that has no business in the
/// name of a device: Unicode's control characters, and its line and paragraph
/// separators.
[[nodiscard]] bool isControl(char32_t codePoint)
{
    const QChar::Category category = QChar::category(codePoint);
    return category == QChar::Other_Control || category == QChar::Separator_Line
        || category == QChar::Separator_Paragraph;
}

/// \a text as a quoted SPA-JSON string. Printable characters beyond ASCII are
/// written as UTF-8, which PipeWire keeps as it is (spike §1); the control
/// characters validation refuses are escaped all the same, so a list nobody
/// validated still cannot break out of its string.
[[nodiscard]] QByteArray quoted(const QString &text)
{
    QByteArray out;
    out.reserve(text.size() + 2);
    out += '"';
    for (const char32_t codePoint : text.toUcs4()) {
        switch (codePoint) {
        case U'"':
            out += "\\\"";
            break;
        case U'\\':
            out += "\\\\";
            break;
        case U'\n':
            out += "\\n";
            break;
        case U'\r':
            out += "\\r";
            break;
        case U'\t':
            out += "\\t";
            break;
        case U'\b':
            out += "\\b";
            break;
        case U'\f':
            out += "\\f";
            break;
        default:
            if (codePoint < 0x20 || codePoint == 0x7f) {
                out += "\\u00";
                out += QByteArray::number(static_cast<int>(codePoint), 16).rightJustified(2, '0');
            } else {
                out += QString::fromUcs4(&codePoint, 1).toUtf8();
            }
        }
    }
    out += '"';
    return out;
}

[[nodiscard]] QByteArray header(VirtualDeviceScope scope)
{
    QByteArray text = bytes(kVirtualDeviceFileHeader);
    if (scope == VirtualDeviceScope::User) {
        text += ": this user's virtual audio devices.\n";
    } else {
        text += ": virtual audio devices for every user of this computer.\n";
    }
    text +=
        "# Reverblabr writes this file whole, so changes made here are lost.\n"
        "# Remove a device in Reverblabr, or delete this file to remove them all.\n";
    return text;
}

// ---- Reading ---------------------------------------------------------------

/// A value spa_json handed back, decoded: a string's escapes undone, a bare
/// word as it is.
[[nodiscard]] QString decoded(const char *value, int length)
{
    QByteArray buffer(length + 1, '\0');
    if (spa_json_parse_stringn(value, length, buffer.data(), static_cast<int>(buffer.size())) < 0) {
        return {};
    }
    return QString::fromUtf8(buffer.constData());
}

/// The next `key = value` pair of an object, like spa_json_object_next(), but
/// strict where that is lenient: a key with no value after it, or a key too
/// long for \a key, is an error rather than the end or a pair skipped.
/// Returns the value's length, 0 at the end, or -1.
[[nodiscard]] int nextPair(spa_json *object, std::array<char, 256> &key, const char **value)
{
    const int keyLength = spa_json_get_string(object, key.data(), static_cast<int>(key.size()));
    if (keyLength == 0) {
        return 0;
    }
    if (keyLength < 0) {
        return -1;
    }
    const int length = spa_json_next(object, value);
    return length > 0 ? length : -1;
}

/// Reads the elements of an array of plain values, such as `[ FL FR ]`.
[[nodiscard]] std::optional<QStringList> plainArray(spa_json *parent)
{
    spa_json array;
    spa_json_enter(parent, &array);
    QStringList items;
    const char *value = nullptr;
    int length = 0;
    while ((length = spa_json_next(&array, &value)) > 0) {
        if (spa_json_is_container(value, length) != 0) {
            return std::nullopt;
        }
        items += decoded(value, length);
    }
    if (length < 0) {
        return std::nullopt;
    }
    return items;
}

/// `args = { key = value … }`, every value a plain value or an array of them
/// (joined with commas).
[[nodiscard]] std::optional<QHash<QString, QString>> argsOf(spa_json *parent)
{
    spa_json object;
    spa_json_enter(parent, &object);
    QHash<QString, QString> args;
    std::array<char, 256> key{};
    const char *value = nullptr;
    int length = 0;
    while ((length = nextPair(&object, key, &value)) > 0) {
        if (spa_json_is_array(value, length)) {
            const std::optional<QStringList> items = plainArray(&object);
            if (!items) {
                return std::nullopt;
            }
            args.insert(QString::fromUtf8(key.data()), items->join(u','));
        } else if (spa_json_is_container(value, length) != 0) {
            return std::nullopt;
        } else {
            args.insert(QString::fromUtf8(key.data()), decoded(value, length));
        }
    }
    if (length < 0) {
        return std::nullopt;
    }
    return args;
}

/// True for exactly the condition a disabled device carries:
/// `[ { reverblabr.show-disabled-virtual-devices = true } ]`.
[[nodiscard]] bool isDisabledCondition(spa_json *parent)
{
    spa_json array;
    spa_json_enter(parent, &array);
    const char *value = nullptr;
    int length = spa_json_next(&array, &value);
    if (length <= 0 || !spa_json_is_object(value, length)) {
        return false;
    }
    spa_json object;
    spa_json_enter(&array, &object);
    std::array<char, 256> key{};
    int pairs = 0;
    bool ours = false;
    while ((length = nextPair(&object, key, &value)) > 0) {
        ++pairs;
        ours = std::string_view(key.data()) == kShowDisabledVirtualDevices && spa_json_is_true(value, length);
    }
    if (length < 0 || pairs != 1 || !ours) {
        return false;
    }
    // Nothing may follow the one object.
    return spa_json_next(&array, &value) == 0;
}

[[nodiscard]] std::optional<VirtualDevice> entryOf(spa_json *parent, VirtualDeviceScope scope)
{
    spa_json object;
    spa_json_enter(parent, &object);
    std::array<char, 256> key{};
    const char *value = nullptr;
    int length = 0;
    QString factory;
    std::optional<QHash<QString, QString>> args;
    bool enabled = true;
    while ((length = nextPair(&object, key, &value)) > 0) {
        const std::string_view name(key.data());
        if (name == "factory") {
            factory = decoded(value, length);
        } else if (name == "args" && spa_json_is_object(value, length)) {
            args = argsOf(&object);
            if (!args) {
                return std::nullopt;
            }
        } else if (name == "flags" && spa_json_is_array(value, length)) {
            if (!plainArray(&object)) {
                return std::nullopt;
            }
        } else if (name == "condition" && spa_json_is_array(value, length)) {
            if (!isDisabledCondition(&object)) {
                return std::nullopt;
            }
            enabled = false;
        } else {
            return std::nullopt;
        }
    }
    if (length < 0 || factory != "adapter"_L1 || !args) {
        return std::nullopt;
    }

    const QString nodeName = args->value(u"node.name"_s);
    const QString prefix = string(kVirtualDeviceNodePrefix);
    if (args->value(u"factory.name"_s) != kNullSinkFactory || !nodeName.startsWith(prefix)
        || args->value(string(kVirtualDeviceMarker)) != virtualDeviceScopeName(scope)) {
        return std::nullopt;
    }

    VirtualDevice device;
    device.id = nodeName.mid(prefix.size());
    device.name = args->value(u"node.description"_s);
    device.enabled = enabled;
    device.scope = scope;

    const QString mediaClass = args->value(u"media.class"_s);
    if (mediaClass == kSinkClass) {
        device.direction = VirtualDeviceDirection::Output;
    } else if (mediaClass == kVirtualSourceClass) {
        device.direction = VirtualDeviceDirection::Input;
    } else {
        return std::nullopt;
    }

    const QString position = args->value(u"audio.position"_s);
    if (position == "FL,FR"_L1) {
        device.channels = VirtualDeviceChannels::Stereo;
    } else if (position == "MONO"_L1) {
        device.channels = VirtualDeviceChannels::Mono;
    } else {
        return std::nullopt;
    }
    return device;
}

// ---- Writing ---------------------------------------------------------------

[[nodiscard]] QByteArray entry(const VirtualDevice &device, VirtualDeviceScope scope)
{
    const auto line = [](std::string_view key, const QByteArray &value) {
        return "        "_ba + bytes(key).leftJustified(25) + " = "_ba + value + '\n';
    };
    QByteArray text =
        "    { factory = adapter\n"
        "      args = {\n"_ba;
    text += line("factory.name", QByteArray(kNullSinkFactory.data(), kNullSinkFactory.size()));
    text += line("node.name", quoted(virtualDeviceNodeName(device.id)));
    text += line("node.description", quoted(device.name));
    text += line("media.class", virtualDeviceMediaClass(device.direction).toUtf8());
    text += line("audio.position", device.channels == VirtualDeviceChannels::Stereo ? "[ FL FR ]"_ba : "[ MONO ]"_ba);
    text += line("node.virtual", "true"_ba);
    text += line("monitor.channel-volumes", "true"_ba);
    text += line(kVirtualDeviceMarker, quoted(virtualDeviceScopeName(scope)));
    text +=
        "      }\n"
        "      flags = [ nofail ]\n"_ba;
    if (!device.enabled) {
        text += "      condition = [ { "_ba + bytes(kShowDisabledVirtualDevices) + " = true } ]\n"_ba;
    }
    text += "    }\n"_ba;
    return text;
}

/// Flushes a directory entry to the disk, so that a rename or a removal in it
/// survives a power cut.
void syncDirectory(const QString &path)
{
    if (const int directory = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_DIRECTORY); directory >= 0) {
        ::fsync(directory);
        ::close(directory);
    }
}

} // namespace

std::string_view virtualDeviceErrorName(VirtualDeviceError error) noexcept
{
    switch (error) {
    case VirtualDeviceError::EmptyName:
        return "EmptyName";
    case VirtualDeviceError::NameTooLong:
        return "NameTooLong";
    case VirtualDeviceError::NameHasControlCharacter:
        return "NameHasControlCharacter";
    case VirtualDeviceError::NameNotTrimmed:
        return "NameNotTrimmed";
    case VirtualDeviceError::InvalidId:
        return "InvalidId";
    case VirtualDeviceError::DuplicateId:
        return "DuplicateId";
    case VirtualDeviceError::WrongScope:
        return "WrongScope";
    case VirtualDeviceError::TooManyDevices:
        return "TooManyDevices";
    case VirtualDeviceError::NotOurs:
        return "NotOurs";
    case VirtualDeviceError::Malformed:
        return "Malformed";
    case VirtualDeviceError::FileNotReadable:
        return "FileNotReadable";
    case VirtualDeviceError::FileNotWritable:
        return "FileNotWritable";
    case VirtualDeviceError::NotFound:
        return "NotFound";
    case VirtualDeviceError::ReadOnlyScope:
        return "ReadOnlyScope";
    }
    return "Unknown";
}

std::expected<QString, VirtualDeviceError> validVirtualDeviceName(const QString &name)
{
    // Half of a surrogate pair is not a character at all; toUcs4() would
    // quietly turn it into a replacement character, so look first.
    for (qsizetype i = 0; i < name.size(); ++i) {
        const QChar unit = name.at(i);
        if (unit.isHighSurrogate() && i + 1 < name.size() && name.at(i + 1).isLowSurrogate()) {
            ++i;
        } else if (unit.isSurrogate()) {
            return std::unexpected(VirtualDeviceError::NameHasControlCharacter);
        }
    }

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return std::unexpected(VirtualDeviceError::EmptyName);
    }
    const QList<uint> codePoints = trimmed.toUcs4();
    for (const uint codePoint : codePoints) {
        if (isControl(codePoint)) {
            return std::unexpected(VirtualDeviceError::NameHasControlCharacter);
        }
    }
    if (codePoints.size() > kMaxVirtualDeviceNameLength) {
        return std::unexpected(VirtualDeviceError::NameTooLong);
    }
    return trimmed;
}

bool isValidVirtualDeviceId(const QString &id) noexcept
{
    if (id.size() != 8) {
        return false;
    }
    for (const QChar digit : id) {
        const char16_t unit = digit.unicode();
        if (!((unit >= u'0' && unit <= u'9') || (unit >= u'a' && unit <= u'f'))) {
            return false;
        }
    }
    return true;
}

std::expected<void, VirtualDeviceError> validateVirtualDevices(const QList<VirtualDevice> &devices,
                                                               VirtualDeviceScope scope)
{
    if (devices.size() > kMaxVirtualDevices) {
        return std::unexpected(VirtualDeviceError::TooManyDevices);
    }
    QSet<QString> ids;
    for (const VirtualDevice &device : devices) {
        if (device.scope != scope) {
            return std::unexpected(VirtualDeviceError::WrongScope);
        }
        if (!isValidVirtualDeviceId(device.id)) {
            return std::unexpected(VirtualDeviceError::InvalidId);
        }
        if (ids.contains(device.id)) {
            return std::unexpected(VirtualDeviceError::DuplicateId);
        }
        ids.insert(device.id);
        const std::expected<QString, VirtualDeviceError> name = validVirtualDeviceName(device.name);
        if (!name) {
            return std::unexpected(name.error());
        }
        // A name is kept the way validation leaves it; one with white space
        // around it was never validated.
        if (*name != device.name) {
            return std::unexpected(VirtualDeviceError::NameNotTrimmed);
        }
    }
    return {};
}

QString newVirtualDeviceId(const QList<VirtualDevice> &taken)
{
    QSet<QString> used;
    for (const VirtualDevice &device : taken) {
        used.insert(device.id);
    }
    while (true) {
        const QString id = QUuid::createUuid().toString(QUuid::Id128).left(8);
        if (!used.contains(id)) {
            return id;
        }
    }
}

QString virtualDeviceNodeName(const QString &id)
{
    return string(kVirtualDeviceNodePrefix) + id;
}

QString virtualDeviceMediaClass(VirtualDeviceDirection direction)
{
    return direction == VirtualDeviceDirection::Output ? QString(kSinkClass) : QString(kVirtualSourceClass);
}

QString virtualDeviceScopeName(VirtualDeviceScope scope)
{
    return scope == VirtualDeviceScope::User ? u"user"_s : u"system"_s;
}

QList<std::pair<QByteArray, QByteArray>> virtualDeviceNodeProperties(const VirtualDevice &device,
                                                                     VirtualDeviceScope scope)
{
    return {
        {"factory.name"_ba, QByteArray(kNullSinkFactory.data(), kNullSinkFactory.size())},
        {"node.name"_ba, virtualDeviceNodeName(device.id).toUtf8()},
        {"node.description"_ba, device.name.toUtf8()},
        {"media.class"_ba, virtualDeviceMediaClass(device.direction).toUtf8()},
        // The comma form: a property set is not parsed as JSON, and
        // PipeWire's audio.position reader takes both.
        {"audio.position"_ba, device.channels == VirtualDeviceChannels::Stereo ? "FL,FR"_ba : "MONO"_ba},
        {"node.virtual"_ba, "true"_ba},
        {"monitor.channel-volumes"_ba, "true"_ba},
        {bytes(kVirtualDeviceMarker), virtualDeviceScopeName(scope).toUtf8()},
    };
}

QByteArray virtualDevicesToConf(const QList<VirtualDevice> &devices, VirtualDeviceScope scope)
{
    QByteArray text = header(scope);
    text += "\ncontext.objects = [\n"_ba;
    for (const VirtualDevice &device : devices) {
        text += entry(device, scope);
    }
    text += "]\n"_ba;
    return text;
}

std::expected<QList<VirtualDevice>, VirtualDeviceError> virtualDevicesFromConf(const QByteArray &text,
                                                                               VirtualDeviceScope scope)
{
    if (!text.startsWith(bytes(kVirtualDeviceFileHeader))) {
        return std::unexpected(VirtualDeviceError::NotOurs);
    }

    // The file is a bare list of `key = value` pairs, the way PipeWire's own
    // configuration is written: an object without its braces.
    spa_json top;
    if (spa_json_begin_object_relax(&top, text.constData(), static_cast<size_t>(text.size())) <= 0) {
        return std::unexpected(VirtualDeviceError::Malformed);
    }
    QList<VirtualDevice> devices;
    std::array<char, 256> key{};
    const char *value = nullptr;
    int length = 0;
    bool seenObjects = false;
    while ((length = nextPair(&top, key, &value)) > 0) {
        if (std::string_view(key.data()) != "context.objects" || seenObjects || !spa_json_is_array(value, length)) {
            return std::unexpected(VirtualDeviceError::Malformed);
        }
        seenObjects = true;
        spa_json array;
        spa_json_enter(&top, &array);
        const char *element = nullptr;
        int elementLength = 0;
        while ((elementLength = spa_json_next(&array, &element)) > 0) {
            if (!spa_json_is_object(element, elementLength)) {
                return std::unexpected(VirtualDeviceError::Malformed);
            }
            std::optional<VirtualDevice> device = entryOf(&array, scope);
            if (!device) {
                return std::unexpected(VirtualDeviceError::Malformed);
            }
            devices += std::move(*device);
        }
        if (elementLength < 0) {
            return std::unexpected(VirtualDeviceError::Malformed);
        }
    }
    if (length < 0 || spa_json_get_error(&top, text.constData(), nullptr)) {
        return std::unexpected(VirtualDeviceError::Malformed);
    }
    if (!validateVirtualDevices(devices, scope)) {
        return std::unexpected(VirtualDeviceError::Malformed);
    }
    return devices;
}

QVariantList virtualDevicesToVariant(const QList<VirtualDevice> &devices)
{
    QVariantList list;
    list.reserve(devices.size());
    for (const VirtualDevice &device : devices) {
        list.append(QVariantMap{
            {u"id"_s, device.id},
            {u"name"_s, device.name},
            {u"direction"_s, device.direction == VirtualDeviceDirection::Output ? u"output"_s : u"input"_s},
            {u"channels"_s, device.channels == VirtualDeviceChannels::Stereo ? u"stereo"_s : u"mono"_s},
            {u"enabled"_s, device.enabled},
        });
    }
    return list;
}

std::expected<QList<VirtualDevice>, VirtualDeviceError> virtualDevicesFromVariant(const QVariant &list,
                                                                                  VirtualDeviceScope scope)
{
    // QVariant::canConvert and toList() would take a string, a map or a
    // number for a list; only the real thing will do here.
    if (list.typeId() != QMetaType::QVariantList) {
        return std::unexpected(VirtualDeviceError::Malformed);
    }
    const QVariantList entries = list.toList();
    if (entries.size() > kMaxVirtualDevices) {
        return std::unexpected(VirtualDeviceError::TooManyDevices);
    }
    // Two UTF-16 units a character is the most a valid name can take.
    constexpr qsizetype kLongestName = kMaxVirtualDeviceNameLength * 2;
    static const QStringList keys{u"channels"_s, u"direction"_s, u"enabled"_s, u"id"_s, u"name"_s};

    QList<VirtualDevice> devices;
    devices.reserve(entries.size());
    for (const QVariant &entry : entries) {
        if (entry.typeId() != QMetaType::QVariantMap) {
            return std::unexpected(VirtualDeviceError::Malformed);
        }
        const QVariantMap map = entry.toMap();
        // QVariantMap keeps its keys sorted, as keys is.
        if (map.keys() != keys) {
            return std::unexpected(VirtualDeviceError::Malformed);
        }
        const QVariant id = map.value(u"id"_s);
        const QVariant name = map.value(u"name"_s);
        const QVariant direction = map.value(u"direction"_s);
        const QVariant channels = map.value(u"channels"_s);
        const QVariant enabled = map.value(u"enabled"_s);
        if (id.typeId() != QMetaType::QString || name.typeId() != QMetaType::QString
            || direction.typeId() != QMetaType::QString || channels.typeId() != QMetaType::QString
            || enabled.typeId() != QMetaType::Bool) {
            return std::unexpected(VirtualDeviceError::Malformed);
        }
        if (name.toString().size() > kLongestName) {
            return std::unexpected(VirtualDeviceError::NameTooLong);
        }

        VirtualDevice device;
        device.id = id.toString();
        device.name = name.toString();
        device.enabled = enabled.toBool();
        device.scope = scope;
        if (direction.toString() == "output"_L1) {
            device.direction = VirtualDeviceDirection::Output;
        } else if (direction.toString() == "input"_L1) {
            device.direction = VirtualDeviceDirection::Input;
        } else {
            return std::unexpected(VirtualDeviceError::Malformed);
        }
        if (channels.toString() == "stereo"_L1) {
            device.channels = VirtualDeviceChannels::Stereo;
        } else if (channels.toString() == "mono"_L1) {
            device.channels = VirtualDeviceChannels::Mono;
        } else {
            return std::unexpected(VirtualDeviceError::Malformed);
        }
        devices.append(device);
    }
    return devices;
}

VirtualDeviceStore::VirtualDeviceStore(VirtualDeviceScope scope)
    : VirtualDeviceStore(scope, scope == VirtualDeviceScope::User ? userFile() : systemFile())
{
}

VirtualDeviceStore::VirtualDeviceStore(VirtualDeviceScope scope, QString path)
    : m_scope(scope)
    , m_path(std::move(path))
{
}

QString VirtualDeviceStore::userFile()
{
    // PipeWire reads $XDG_CONFIG_HOME, or ~/.config without it, which is
    // exactly what Qt answers here.
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + u'/' + kDropInDirectory + u'/'
        + kUserFileName;
}

QString VirtualDeviceStore::systemFile(const QString &root)
{
    return QDir::cleanPath(root + "/etc/"_L1 + kDropInDirectory + u'/' + kSystemFileName);
}

std::expected<QList<VirtualDevice>, VirtualDeviceError> VirtualDeviceStore::load() const
{
    QFile file(m_path);
    if (!file.exists()) {
        return QList<VirtualDevice>{};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(VirtualDeviceError::FileNotReadable);
    }
    return virtualDevicesFromConf(file.readAll(), m_scope);
}

std::expected<void, VirtualDeviceError> VirtualDeviceStore::save(const QList<VirtualDevice> &devices) const
{
    if (const std::expected<void, VirtualDeviceError> valid = validateVirtualDevices(devices, m_scope); !valid) {
        return valid;
    }
    // Whatever is there must be a file we wrote and can still read: anything
    // else may hold devices, or somebody's own configuration, that replacing
    // it would lose.
    if (const std::expected<QList<VirtualDevice>, VirtualDeviceError> current = load(); !current) {
        return std::unexpected(current.error());
    }

    const QString directoryPath = QFileInfo(m_path).absolutePath();
    if (devices.isEmpty()) {
        if (QFile::exists(m_path)) {
            if (!QFile::remove(m_path)) {
                return std::unexpected(VirtualDeviceError::FileNotWritable);
            }
            syncDirectory(directoryPath);
        }
        return {};
    }

    if (!QDir().mkpath(directoryPath)) {
        return std::unexpected(VirtualDeviceError::FileNotWritable);
    }
    const QByteArray text = virtualDevicesToConf(devices, m_scope);

    // Beside the real file, and not ending in .conf, so that a PipeWire
    // starting at this moment never reads it.
    const QString temporaryPath = m_path + ".tmp"_L1;
    {
        QFile temporary(temporaryPath);
        if (!temporary.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return std::unexpected(VirtualDeviceError::FileNotWritable);
        }
        // Every user's PipeWire reads the system file, whatever umask the
        // writer runs with.
        const bool written = temporary.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                      | QFileDevice::ReadGroup | QFileDevice::ReadOther)
            && temporary.write(text) == text.size() && temporary.flush()
            && ::fsync(static_cast<int>(temporary.handle())) == 0;
        if (!written) {
            temporary.close();
            QFile::remove(temporaryPath);
            return std::unexpected(VirtualDeviceError::FileNotWritable);
        }
    }
    // rename(2) replaces the old file in one step; there is no moment without
    // one, which QFile::rename (remove, then rename) would leave.
    if (::rename(QFile::encodeName(temporaryPath).constData(), QFile::encodeName(m_path).constData()) != 0) {
        QFile::remove(temporaryPath);
        return std::unexpected(VirtualDeviceError::FileNotWritable);
    }
    syncDirectory(directoryPath);
    return {};
}

} // namespace rb::pw
