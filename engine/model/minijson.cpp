// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "model/minijson.h"

#include <charconv>

namespace rb::model
{

namespace
{

constexpr int kDeepest = 8;
constexpr std::size_t kMostValues = 4096;

class Parser
{
public:
    explicit Parser(std::string_view text)
        : m_text(text)
    {
    }

    [[nodiscard]] std::optional<JsonValue> document()
    {
        JsonValue value;
        if (!parseValue(value, 0)) {
            return std::nullopt;
        }
        skipSpace();
        return m_at == m_text.size() ? std::optional{std::move(value)} : std::nullopt;
    }

private:
    void skipSpace() noexcept
    {
        while (m_at < m_text.size()
               && (m_text[m_at] == ' ' || m_text[m_at] == '\n' || m_text[m_at] == '\r' || m_text[m_at] == '\t')) {
            ++m_at;
        }
    }

    [[nodiscard]] bool take(char wanted) noexcept
    {
        skipSpace();
        if (m_at < m_text.size() && m_text[m_at] == wanted) {
            ++m_at;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool word(std::string_view wanted) noexcept
    {
        if (m_text.substr(m_at, wanted.size()) == wanted) {
            m_at += wanted.size();
            return true;
        }
        return false;
    }

    [[nodiscard]] bool parseValue(JsonValue &value, int depth)
    {
        if (depth > kDeepest || ++m_values > kMostValues) {
            return false;
        }
        skipSpace();
        if (m_at >= m_text.size()) {
            return false;
        }
        switch (m_text[m_at]) {
        case '{':
            return parseObject(value, depth);
        case '[':
            return parseArray(value, depth);
        case '"':
            value.kind = JsonValue::Kind::String;
            return parseString(value.text);
        case 't':
            value.kind = JsonValue::Kind::Bool;
            value.boolean = true;
            return word("true");
        case 'f':
            value.kind = JsonValue::Kind::Bool;
            return word("false");
        case 'n':
            return word("null");
        default:
            value.kind = JsonValue::Kind::Number;
            return parseNumber(value.number);
        }
    }

    [[nodiscard]] bool parseObject(JsonValue &value, int depth)
    {
        value.kind = JsonValue::Kind::Object;
        ++m_at;
        if (take('}')) {
            return true;
        }
        do {
            skipSpace();
            std::string key;
            JsonValue member;
            if (!parseString(key) || !take(':') || !parseValue(member, depth + 1)) {
                return false;
            }
            value.members.emplace_back(std::move(key), std::move(member));
        } while (take(','));
        return take('}');
    }

    [[nodiscard]] bool parseArray(JsonValue &value, int depth)
    {
        value.kind = JsonValue::Kind::Array;
        ++m_at;
        if (take(']')) {
            return true;
        }
        do {
            JsonValue item;
            if (!parseValue(item, depth + 1)) {
                return false;
            }
            value.items.push_back(std::move(item));
        } while (take(','));
        return take(']');
    }

    [[nodiscard]] bool parseNumber(double &number) noexcept
    {
        const char *const first = m_text.data() + m_at;
        const char *const last = m_text.data() + m_text.size();
        const auto [end, fault] = std::from_chars(first, last, number);
        if (fault != std::errc{} || end == first) {
            return false;
        }
        m_at += static_cast<std::size_t>(end - first);
        return true;
    }

    [[nodiscard]] bool parseString(std::string &text)
    {
        if (m_at >= m_text.size() || m_text[m_at] != '"') {
            return false;
        }
        ++m_at;
        while (m_at < m_text.size()) {
            const char c = m_text[m_at++];
            if (c == '"') {
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return false;
            }
            if (c != '\\') {
                text += c;
                continue;
            }
            if (m_at >= m_text.size()) {
                return false;
            }
            const char escaped = m_text[m_at++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                text += escaped;
                break;
            case 'n':
                text += '\n';
                break;
            case 't':
                text += '\t';
                break;
            case 'r':
                text += '\r';
                break;
            default:
                // \b, \f and \uXXXX have no use in a configuration: a file
                // name or an input's name in those is not one to trust.
                return false;
            }
        }
        return false;
    }

    std::string_view m_text;
    std::size_t m_at = 0;
    std::size_t m_values = 0;
};

} // namespace

const JsonValue *JsonValue::find(std::string_view key) const noexcept
{
    for (const auto &[name, value] : members) {
        if (name == key) {
            return &value;
        }
    }
    return nullptr;
}

std::optional<JsonValue> parseJson(std::string_view text)
{
    if (text.size() > kMostJsonBytes) {
        return std::nullopt;
    }
    return Parser(text).document();
}

} // namespace rb::model
