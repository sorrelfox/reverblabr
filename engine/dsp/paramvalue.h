// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rb::dsp
{

/// What a parameter holds. Every one of these has an obvious JSON equivalent,
/// which is what a session file stores: a number, a true or false, or a
/// string. Nothing here depends on the user's locale.
enum class ParamType : std::uint8_t {
    Float, ///< A number, for example a gain in decibels.
    Int, ///< A whole number, for example a count of bands.
    Bool, ///< On or off.
    Enum, ///< One of a fixed set of choices, held as the choice's stable id.
    String, ///< Free text the user typed, for example a file-name pattern.
    DeviceRef, ///< A device, held as its `node.name`, or "default".
    /// Several strings, in the order the user put them: the sinks an
    /// application's audio is taken from, the applications a capture leaves
    /// out. **A list, not a joined string**, because a name with a comma or
    /// a newline in it is a name, not a separator.
    StringList,
};

/// The name of a type, for diagnostics and for the facade: "float", "int",
/// "bool", "enum", "string", "deviceRef", "stringList".
[[nodiscard]] std::string_view paramTypeName(ParamType type) noexcept;

/// The value of one parameter.
///
/// An enum is held as the **stable id of its choice**, never as the position
/// in the list, so that adding or reordering choices in a later version does
/// not change what an old session file means. For the same reason nothing
/// here is ever translated; the interface looks the id up in its own table
/// (see docs/i18n.md).
class ParamValue
{
public:
    /// A value that holds nothing. Reading a parameter that does not exist
    /// gives one of these.
    ParamValue() = default;

    [[nodiscard]] static ParamValue ofFloat(float value) noexcept;
    [[nodiscard]] static ParamValue ofInt(std::int64_t value) noexcept;
    [[nodiscard]] static ParamValue ofBool(bool value) noexcept;
    [[nodiscard]] static ParamValue ofEnum(std::string choiceId);
    [[nodiscard]] static ParamValue ofString(std::string text);
    [[nodiscard]] static ParamValue ofDeviceRef(std::string deviceName);
    [[nodiscard]] static ParamValue ofStringList(std::vector<std::string> values);

    /// Which of the types this holds, or nothing if it is empty.
    [[nodiscard]] std::optional<ParamType> type() const noexcept
    {
        return m_type;
    }

    [[nodiscard]] bool isEmpty() const noexcept
    {
        return !m_type.has_value();
    }

    /// Whether this holds exactly \a type. A float value is not an int value,
    /// even when the number would fit.
    [[nodiscard]] bool is(ParamType type) const noexcept
    {
        return m_type == type;
    }

    /// The value, or nothing if this holds another type. There is no silent
    /// conversion: a parameter declared as an int is stored, saved and read
    /// back as an int.
    [[nodiscard]] std::optional<float> asFloat() const noexcept;
    [[nodiscard]] std::optional<std::int64_t> asInt() const noexcept;
    [[nodiscard]] std::optional<bool> asBool() const noexcept;
    /// The stable id of the chosen enum member.
    [[nodiscard]] std::optional<std::string_view> asEnum() const & noexcept;
    [[nodiscard]] std::optional<std::string_view> asString() const & noexcept;
    /// The device's `node.name`, or "default".
    [[nodiscard]] std::optional<std::string_view> asDeviceRef() const & noexcept;

    /// The strings, in order. Empty is a list of nothing, which is a value
    /// and not a mistake: "these outputs" with none chosen captures nothing.
    [[nodiscard]] std::optional<std::vector<std::string>> asStringList() const noexcept;

    /// The text of any of the three string-shaped types.
    [[nodiscard]] std::optional<std::string_view> asText() const & noexcept;

    // **The four views above are deleted on a temporary, on purpose.** They
    // look into this object's own string, so `store.value(id).asText()` —
    // which reads exactly like the safe thing to write — leaves a view onto
    // a string that died at the semicolon. A debug build leaves the bytes
    // where they were and appears to work; an optimised one reuses the
    // storage and routes the wrong channel. It cost us that bug twice, in
    // both device blocks, and it was invisible until the release preset was
    // run. Deleting the rvalue overloads turns the next one into a compile
    // error: name the value first, or take the text by value.
    std::optional<std::string_view> asEnum() const && = delete;
    std::optional<std::string_view> asString() const && = delete;
    std::optional<std::string_view> asDeviceRef() const && = delete;
    std::optional<std::string_view> asText() const && = delete;

    /// The text of any string-shaped type, copied, and empty for anything
    /// else. **Safe on a value nobody named**, which is what the views
    /// above are not: `store.value(id).text()` owns what it returns.
    /// Prefer a view when the value is named and the copy would be waste.
    [[nodiscard]] std::string text() const;

    /// The number of a float or an int, for a range check that does not care
    /// which of the two it is. A bool counts as 0 or 1.
    [[nodiscard]] std::optional<double> asNumber() const noexcept;

    [[nodiscard]] bool operator==(const ParamValue &other) const = default;

private:
    std::optional<ParamType> m_type;
    double m_number = 0.0;
    bool m_boolean = false;
    std::string m_text;
    std::vector<std::string> m_list;
};

/// A block's whole state: parameter id to value, in id order so that two
/// equal states are written the same way. This is what saveState() returns
/// and what a session file's `params` object becomes.
using ParamValueMap = std::map<std::string, ParamValue, std::less<>>;

} // namespace rb::dsp
