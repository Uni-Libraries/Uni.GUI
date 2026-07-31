#pragma once

#include <uni/gui/nodes/io.h>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace Uni::GUI::Nodes::Detail {

struct Json final {
    struct Number final {
        std::string text;
    };

    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json, std::less<>>;
    std::variant<std::nullptr_t, bool, Number, std::string, Array, Object> value;
};

struct JsonFailure final {
    Error error;
};

[[noreturn]] inline void Fail(const ErrorCode code, std::string message) {
    throw JsonFailure{Error{code, std::move(message)}};
}

[[nodiscard]] inline Json Null() { return Json{.value = nullptr}; }
[[nodiscard]] inline Json Bool(const bool value) { return Json{.value = value}; }
[[nodiscard]] inline Json Array(Json::Array value = {}) { return Json{.value = std::move(value)}; }
[[nodiscard]] inline Json Object(Json::Object value = {}) { return Json{.value = std::move(value)}; }

[[nodiscard]] Json String(std::string value);
[[nodiscard]] Json Number(double value);
[[nodiscard]] Json Number(float value);
[[nodiscard]] Json Number(std::uint32_t value);
[[nodiscard]] bool ValidUtf8(std::string_view text) noexcept;
[[nodiscard]] Json ParseJson(std::string_view input, const GraphIoLimits& limits);
[[nodiscard]] std::string DumpJson(const Json& value);

} // namespace Uni::GUI::Nodes::Detail
