#pragma once

#include <uni/gui/element.h>
#include <uni/gui/error.h>

#include <expected>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace Uni::GUI {

class UiApp;

namespace Detail {

class UiElementRegistry final {
public:
    [[nodiscard]] std::expected<UiElementId, UiError> Add(std::unique_ptr<UiElement> element);
    [[nodiscard]] bool Remove(UiElementId id);
    [[nodiscard]] UiResult<UiFrameDemand> Update(
        UiApp& app,
        std::uint64_t frame_index,
        std::chrono::nanoseconds delta_time);
    void Clear() noexcept;

    [[nodiscard]] std::size_t Size() const noexcept;

private:
    struct Entry final {
        UiElementId id{InvalidUiElementId};
        std::unique_ptr<UiElement> element;
        bool remove_requested{};
    };

    void Commit();

    std::vector<Entry> m_elements;
    std::vector<Entry> m_pending_elements;
    UiElementId m_next_id{1};
    bool m_updating{};
};

} // namespace Detail
} // namespace Uni::GUI
