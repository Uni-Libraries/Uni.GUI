#include "ui_nodes_json.h"

#include <nlohmann_json/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <system_error>
#include <utility>

namespace Uni::GUI::Nodes::Detail {
namespace {

using NlohmannJson = nlohmann::json;
using NlohmannFloatJson = nlohmann::basic_json<
    std::map,
    std::vector,
    std::string,
    bool,
    std::int64_t,
    std::uint64_t,
    float>;

struct NumberToken final {
    std::size_t begin;
    std::size_t end;
    std::string_view text;
};

struct JsonScan final {
    std::vector<NumberToken> numbers;
    bool normalize_numbers{false};
};

[[noreturn]] void ScanError(const std::string_view message, const std::size_t position) {
    Fail(ErrorCode::InvalidFormat, std::string(message) + " at byte " + std::to_string(position));
}

void CountDecodedBytes(std::size_t& count, const std::size_t added, const GraphIoLimits& limits) {
    if (count > limits.max_string_bytes || added > limits.max_string_bytes - count) {
        Fail(ErrorCode::SizeLimitExceeded, "JSON string exceeds max_string_bytes");
    }
    count += added;
}

[[nodiscard]] std::uint32_t Hex4(const std::string_view input, std::size_t& position) {
    if (input.size() - position < 4) ScanError("Incomplete JSON unicode escape", position);
    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index) {
        const char digit = input[position++];
        value <<= 4U;
        if (digit >= '0' && digit <= '9') value |= static_cast<std::uint32_t>(digit - '0');
        else if (digit >= 'a' && digit <= 'f') value |= static_cast<std::uint32_t>(digit - 'a' + 10);
        else if (digit >= 'A' && digit <= 'F') value |= static_cast<std::uint32_t>(digit - 'A' + 10);
        else ScanError("Invalid JSON unicode escape", position - 1);
    }
    return value;
}

[[nodiscard]] std::size_t Utf8Bytes(const std::uint32_t codepoint) noexcept {
    if (codepoint <= 0x7FU) return 1;
    if (codepoint <= 0x7FFU) return 2;
    if (codepoint <= 0xFFFFU) return 3;
    return 4;
}

void ScanString(const std::string_view input, std::size_t& position, const GraphIoLimits& limits) {
    ++position;
    std::size_t decoded_bytes = 0;
    while (position < input.size()) {
        const auto value = static_cast<unsigned char>(input[position++]);
        if (value == '"') return;
        if (value < 0x20U) ScanError("Unescaped control character in JSON string", position - 1);
        if (value != '\\') {
            CountDecodedBytes(decoded_bytes, 1, limits);
            continue;
        }
        if (position == input.size()) ScanError("Incomplete JSON escape", position);
        const char escape = input[position++];
        if (escape == '"' || escape == '\\' || escape == '/' || escape == 'b' || escape == 'f' ||
            escape == 'n' || escape == 'r' || escape == 't') {
            CountDecodedBytes(decoded_bytes, 1, limits);
            continue;
        }
        if (escape != 'u') ScanError("Invalid JSON escape", position - 1);
        std::uint32_t codepoint = Hex4(input, position);
        if (codepoint >= 0xD800U && codepoint <= 0xDBFFU) {
            if (input.size() - position < 6 || input[position] != '\\' || input[position + 1] != 'u') {
                ScanError("High surrogate is missing its low surrogate", position);
            }
            position += 2;
            const std::uint32_t low = Hex4(input, position);
            if (low < 0xDC00U || low > 0xDFFFU) ScanError("Invalid low surrogate", position - 4);
            codepoint = 0x10000U + ((codepoint - 0xD800U) << 10U) + (low - 0xDC00U);
        } else if (codepoint >= 0xDC00U && codepoint <= 0xDFFFU) {
            ScanError("Unexpected low surrogate", position - 4);
        }
        CountDecodedBytes(decoded_bytes, Utf8Bytes(codepoint), limits);
    }
    ScanError("Unterminated JSON string", position);
}

[[nodiscard]] bool IsDigit(const char value) noexcept { return value >= '0' && value <= '9'; }

[[nodiscard]] bool IsValueDelimiter(const char value) noexcept {
    return value == ' ' || value == '\n' || value == '\r' || value == '\t' || value == ',' || value == ']' ||
           value == '}';
}

[[nodiscard]] NumberToken ScanNumber(const std::string_view input, std::size_t& position) {
    const std::size_t begin = position;
    if (input[position] == '-') ++position;
    if (position == input.size()) ScanError("Incomplete JSON number", position);
    if (input[position] == '0') {
        ++position;
        if (position < input.size() && IsDigit(input[position])) {
            ScanError("JSON number contains a leading zero", position);
        }
    } else {
        if (input[position] < '1' || input[position] > '9') ScanError("Invalid JSON number", position);
        while (position < input.size() && IsDigit(input[position])) ++position;
    }
    if (position < input.size() && input[position] == '.') {
        ++position;
        const std::size_t fraction = position;
        while (position < input.size() && IsDigit(input[position])) ++position;
        if (fraction == position) ScanError("JSON fraction has no digits", position);
    }
    if (position < input.size() && (input[position] == 'e' || input[position] == 'E')) {
        ++position;
        if (position < input.size() && (input[position] == '+' || input[position] == '-')) ++position;
        const std::size_t exponent = position;
        while (position < input.size() && IsDigit(input[position])) ++position;
        if (exponent == position) ScanError("JSON exponent has no digits", position);
    }
    if (position < input.size() && !IsValueDelimiter(input[position])) {
        ScanError("Invalid character after JSON number", position);
    }
    return NumberToken{.begin = begin, .end = position, .text = input.substr(begin, position - begin)};
}

[[nodiscard]] JsonScan ScanJson(const std::string_view input, const GraphIoLimits& limits) {
    JsonScan result;
    for (std::size_t position = 0; position < input.size();) {
        if (input[position] == '"') {
            ScanString(input, position, limits);
            continue;
        }
        if (input[position] != '-' && !IsDigit(input[position])) {
            ++position;
            continue;
        }
        if (result.numbers.size() == limits.max_json_values) {
            Fail(ErrorCode::SizeLimitExceeded, "JSON value count exceeds max_json_values");
        }
        const NumberToken token = ScanNumber(input, position);
        double parsed = 0.0;
        const auto converted = std::from_chars(
            token.text.data(),
            token.text.data() + token.text.size(),
            parsed,
            std::chars_format::general);
        if (converted.ec == std::errc::result_out_of_range) result.normalize_numbers = true;
        result.numbers.push_back(token);
    }
    return result;
}

[[nodiscard]] std::string NormalizeNumbers(const std::string_view input, const std::vector<NumberToken>& numbers) {
    std::string normalized{input};
    for (const NumberToken& number : numbers) {
        normalized[number.begin] = '0';
        std::fill(normalized.begin() + static_cast<std::ptrdiff_t>(number.begin + 1),
                  normalized.begin() + static_cast<std::ptrdiff_t>(number.end), ' ');
    }
    return normalized;
}

class SaxBuilder final : public nlohmann::json_sax<NlohmannJson> {
public:
    SaxBuilder(const GraphIoLimits& limits, const std::vector<NumberToken>& numbers)
        : m_limits(limits), m_numbers(numbers) {}

    bool null() override { return Add(Null()); }
    bool boolean(const bool value) override { return Add(Bool(value)); }
    bool number_integer(const number_integer_t) override { return AddNumber(); }
    bool number_unsigned(const number_unsigned_t) override { return AddNumber(); }
    bool number_float(const number_float_t, const string_t&) override { return AddNumber(); }
    bool string(string_t& value) override {
        if (value.size() > m_limits.max_string_bytes) return Limit("JSON string exceeds max_string_bytes");
        return Add(Json{.value = std::move(value)});
    }
    bool binary(binary_t&) override { return Error("Binary values are not valid JSON text"); }

    bool start_object(const std::size_t) override { return Start(Object()); }
    bool key(string_t& value) override {
        if (value.size() > m_limits.max_string_bytes) return Limit("JSON key exceeds max_string_bytes");
        if (m_stack.empty() || !std::holds_alternative<Json::Object>(m_stack.back().container.value) ||
            m_stack.back().key.has_value()) {
            return Error("Unexpected JSON object key");
        }
        const auto& object = std::get<Json::Object>(m_stack.back().container.value);
        if (object.contains(value)) return Error("Duplicate JSON object key");
        m_stack.back().key = std::move(value);
        return true;
    }
    bool end_object() override { return End<Json::Object>(); }
    bool start_array(const std::size_t) override { return Start(Array()); }
    bool end_array() override { return End<Json::Array>(); }

    bool parse_error(
        const std::size_t position,
        const std::string&,
        const nlohmann::detail::exception& exception) override {
        m_error = std::string(exception.what()) + " at byte " + std::to_string(position);
        return false;
    }

    [[nodiscard]] Json TakeResult() {
        if (!m_error.empty()) Fail(m_limit_error ? ErrorCode::SizeLimitExceeded : ErrorCode::InvalidFormat, m_error);
        if (!m_root || !m_stack.empty()) Fail(ErrorCode::InvalidFormat, "JSON document is incomplete");
        if (m_number_index != m_numbers.size()) Fail(ErrorCode::InvalidFormat, "JSON numeric token mismatch");
        return std::move(*m_root);
    }

private:
    struct Frame final {
        Json container;
        std::optional<std::string> key;
    };

    bool CountValue() {
        if (m_value_count == m_limits.max_json_values) return Limit("JSON value count exceeds max_json_values");
        ++m_value_count;
        return true;
    }

    bool Start(Json container) {
        if (!CountValue()) return false;
        if (m_stack.size() + 1 > m_limits.max_depth) return Limit("JSON nesting exceeds max_depth");
        m_stack.push_back(Frame{.container = std::move(container)});
        return true;
    }

    template<typename Container>
    bool End() {
        if (m_stack.empty() || !std::holds_alternative<Container>(m_stack.back().container.value)) {
            return Error("Mismatched JSON container terminator");
        }
        Json value = std::move(m_stack.back().container);
        m_stack.pop_back();
        return AddCompleted(std::move(value));
    }

    bool Add(Json value) {
        if (m_stack.size() + 1 > m_limits.max_depth) return Limit("JSON nesting exceeds max_depth");
        if (!CountValue()) return false;
        return AddCompleted(std::move(value));
    }

    bool AddNumber() {
        if (m_number_index == m_numbers.size()) return Error("JSON numeric token mismatch");
        return Add(Json{.value = Json::Number{std::string(m_numbers[m_number_index++].text)}});
    }

    bool AddCompleted(Json value) {
        if (m_stack.empty()) {
            if (m_root) return Error("JSON contains multiple root values");
            m_root = std::move(value);
            return true;
        }
        auto& frame = m_stack.back();
        if (auto* array = std::get_if<Json::Array>(&frame.container.value)) {
            array->push_back(std::move(value));
            return true;
        }
        auto* object = std::get_if<Json::Object>(&frame.container.value);
        if (object == nullptr || !frame.key) return Error("JSON object value has no key");
        if (!object->emplace(std::move(*frame.key), std::move(value)).second) {
            return Error("Duplicate JSON object key");
        }
        frame.key.reset();
        return true;
    }

    bool Error(std::string message) {
        m_error = std::move(message);
        return false;
    }

    bool Limit(std::string message) {
        m_limit_error = true;
        return Error(std::move(message));
    }

    const GraphIoLimits& m_limits;
    const std::vector<NumberToken>& m_numbers;
    std::vector<Frame> m_stack;
    std::optional<Json> m_root;
    std::size_t m_value_count{0};
    std::size_t m_number_index{0};
    std::string m_error;
    bool m_limit_error{false};
};

void AppendEscaped(std::string& output, const std::string_view text) {
    output.push_back('"');
    constexpr char Hex[] = "0123456789abcdef";
    for (const unsigned char value : text) {
        switch (value) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (value < 0x20U) {
                output += "\\u00";
                output.push_back(Hex[value >> 4U]);
                output.push_back(Hex[value & 0x0FU]);
            } else {
                output.push_back(static_cast<char>(value));
            }
        }
    }
    output.push_back('"');
}

void AppendJson(std::string& output, const Json& value) {
    std::visit([&](const auto& item) {
        using Value = std::remove_cvref_t<decltype(item)>;
        if constexpr (std::same_as<Value, std::nullptr_t>) {
            output += "null";
        } else if constexpr (std::same_as<Value, bool>) {
            output += item ? "true" : "false";
        } else if constexpr (std::same_as<Value, Json::Number>) {
            output += item.text;
        } else if constexpr (std::same_as<Value, std::string>) {
            AppendEscaped(output, item);
        } else if constexpr (std::same_as<Value, Json::Array>) {
            output.push_back('[');
            bool first = true;
            for (const auto& child : item) {
                if (!first) output.push_back(',');
                first = false;
                AppendJson(output, child);
            }
            output.push_back(']');
        } else {
            output.push_back('{');
            bool first = true;
            for (const auto& [key, child] : item) {
                if (!first) output.push_back(',');
                first = false;
                AppendEscaped(output, key);
                output.push_back(':');
                AppendJson(output, child);
            }
            output.push_back('}');
        }
    }, value.value);
}

} // namespace

Json String(std::string value) {
    if (!ValidUtf8(value)) Fail(ErrorCode::InvalidArgument, "A serialized string is not valid UTF-8");
    return Json{.value = std::move(value)};
}

Json Number(const double value) {
    if (!std::isfinite(value)) Fail(ErrorCode::InvalidArgument, "JSON numbers must be finite");
    return Json{.value = Json::Number{value == 0.0 ? "0" : NlohmannJson(value).dump()}};
}

Json Number(const float value) {
    if (!std::isfinite(value)) Fail(ErrorCode::InvalidArgument, "JSON numbers must be finite");
    return Json{.value = Json::Number{value == 0.0f ? "0" : NlohmannFloatJson(value).dump()}};
}

Json Number(const std::uint32_t value) {
    return Json{.value = Json::Number{std::to_string(value)}};
}

bool ValidUtf8(const std::string_view text) noexcept {
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first < 0x80U) {
            ++index;
            continue;
        }
        std::size_t count = 0;
        std::uint32_t value = 0;
        if ((first & 0xE0U) == 0xC0U) {
            count = 2;
            value = first & 0x1FU;
        } else if ((first & 0xF0U) == 0xE0U) {
            count = 3;
            value = first & 0x0FU;
        } else if ((first & 0xF8U) == 0xF0U) {
            count = 4;
            value = first & 0x07U;
        } else {
            return false;
        }
        if (index + count > text.size()) return false;
        for (std::size_t offset = 1; offset < count; ++offset) {
            const auto next = static_cast<unsigned char>(text[index + offset]);
            if ((next & 0xC0U) != 0x80U) return false;
            value = (value << 6U) | (next & 0x3FU);
        }
        if ((count == 2 && value < 0x80U) || (count == 3 && value < 0x800U) ||
            (count == 4 && value < 0x10000U) || value > 0x10FFFFU ||
            (value >= 0xD800U && value <= 0xDFFFU)) {
            return false;
        }
        index += count;
    }
    return true;
}

Json ParseJson(const std::string_view input, const GraphIoLimits& limits) {
    if (input.size() > limits.max_bytes) Fail(ErrorCode::SizeLimitExceeded, "JSON input exceeds max_bytes");
    const JsonScan scan = ScanJson(input, limits);
    std::string normalized;
    std::string_view parser_input = input;
    if (scan.normalize_numbers) {
        normalized = NormalizeNumbers(input, scan.numbers);
        parser_input = normalized;
    }
    SaxBuilder builder(limits, scan.numbers);
    if (!NlohmannJson::sax_parse(parser_input, &builder, nlohmann::json::input_format_t::json, true, false)) {
        return builder.TakeResult();
    }
    return builder.TakeResult();
}

std::string DumpJson(const Json& value) {
    std::string output;
    output.reserve(4096);
    AppendJson(output, value);
    output.push_back('\n');
    return output;
}

} // namespace Uni::GUI::Nodes::Detail
