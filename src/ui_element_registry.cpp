#include "ui_element_registry.h"

#include <uni/gui/app.h>

#include <algorithm>
#include <exception>
#include <limits>
#include <string>
#include <utility>

namespace Uni::GUI::Detail {

std::expected<UiElementId, UiError> UiElementRegistry::Add(std::unique_ptr<UiElement> element) {
    if (!element) {
        return std::unexpected(UiError{UiErrorCode::InvalidArgument, "UiElement must not be null"});
    }
    if (m_next_id == InvalidUiElementId || m_next_id == std::numeric_limits<UiElementId>::max()) {
        return std::unexpected(UiError{UiErrorCode::InvalidState, "UiElement ID space is exhausted"});
    }

    const UiElementId id = m_next_id++;
    Entry entry{id, std::move(element), false};
    if (m_updating) {
        m_pending_elements.push_back(std::move(entry));
    } else {
        m_elements.push_back(std::move(entry));
    }
    return id;
}

bool UiElementRegistry::Remove(const UiElementId id) {
    if (id == InvalidUiElementId) {
        return false;
    }

    const auto mark = [id](std::vector<Entry>& entries) {
        const auto iterator = std::find_if(entries.begin(), entries.end(), [id](const Entry& entry) {
            return entry.id == id;
        });
        if (iterator == entries.end()) {
            return false;
        }
        iterator->remove_requested = true;
        return true;
    };

    const bool found = mark(m_elements) || mark(m_pending_elements);
    if (found && !m_updating) {
        Commit();
    }
    return found;
}

UiResult<UiFrameDemand> UiElementRegistry::Update(
    UiApp& app,
    const std::uint64_t frame_index,
    const std::chrono::nanoseconds delta_time) {
    UiFrameDemand demand = UiFrameDemand::None;
    m_updating = true;
    try {
        for (auto& entry : m_elements) {
            if (!entry.remove_requested) {
                UiState state{app, entry.id, frame_index, delta_time};
                auto updated = entry.element->Update(state);
                if (!updated) {
                    m_updating = false;
                    return std::unexpected(std::move(updated.error()));
                }
                if (!updated->keep_alive) {
                    entry.remove_requested = true;
                }
                if (updated->frame_demand == UiFrameDemand::Continuous ||
                    (updated->frame_demand == UiFrameDemand::OneMoreFrame && demand == UiFrameDemand::None)) {
                    demand = updated->frame_demand;
                }
            }
        }
    } catch (const std::exception& exception) {
        m_updating = false;
        return std::unexpected(UiError{
            UiErrorCode::FrameRendering,
            std::string{"UiElement::Update threw an exception: "}.append(exception.what()),
        });
    } catch (...) {
        m_updating = false;
        return std::unexpected(UiError{UiErrorCode::FrameRendering, "UiElement::Update threw an unknown exception"});
    }
    m_updating = false;
    Commit();
    return demand;
}

void UiElementRegistry::Clear() noexcept {
    m_updating = true;
    while (!m_elements.empty() || !m_pending_elements.empty()) {
        auto elements = std::move(m_elements);
        auto pending_elements = std::move(m_pending_elements);
        m_elements.clear();
        m_pending_elements.clear();
        elements.clear();
        pending_elements.clear();
    }
    m_next_id = 1;
    m_updating = false;
}

std::size_t UiElementRegistry::Size() const noexcept {
    return m_elements.size() + m_pending_elements.size();
}

void UiElementRegistry::Commit() {
    while (true) {
        std::vector<Entry> survivors;
        std::vector<Entry> removed;
        survivors.reserve(m_elements.size() + m_pending_elements.size());

        const auto detach = [&survivors, &removed](std::vector<Entry>& entries) {
            for (auto& entry : entries) {
                if (entry.remove_requested) {
                    removed.push_back(std::move(entry));
                } else {
                    survivors.push_back(std::move(entry));
                }
            }
            entries.clear();
        };

        detach(m_elements);
        detach(m_pending_elements);
        m_elements = std::move(survivors);

        if (!removed.empty()) {
            // User destructors run only after registry storage is stable. Their mutations are deferred.
            m_updating = true;
            removed.clear();
            m_updating = false;
        }

        const bool has_removals = std::any_of(m_elements.begin(), m_elements.end(), [](const Entry& entry) {
            return entry.remove_requested;
        });
        if (m_pending_elements.empty() && !has_removals) {
            break;
        }
    }
}

} // namespace Uni::GUI::Detail
