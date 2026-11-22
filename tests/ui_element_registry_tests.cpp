#include "ui_element_registry.h"

#include <uni/gui/app.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

void Expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

class CountingElement final : public Uni::GUI::UiElement {
public:
    CountingElement(int& updates, const bool keep) : m_updates(updates), m_keep(keep) {}

    Uni::GUI::UiResult<Uni::GUI::UiElementUpdate> Update(Uni::GUI::UiState&) override {
        ++m_updates;
        return Uni::GUI::UiElementUpdate{m_keep, Uni::GUI::UiFrameDemand::None};
    }

private:
    int& m_updates;
    bool m_keep{};
};

class AddingElement final : public Uni::GUI::UiElement {
public:
    AddingElement(Uni::GUI::Detail::UiElementRegistry& registry, int& child_updates)
        : m_registry(registry), m_child_updates(child_updates) {}

    Uni::GUI::UiResult<Uni::GUI::UiElementUpdate> Update(Uni::GUI::UiState&) override {
        if (!m_added) {
            m_added = true;
            auto child = m_registry.Add(std::make_unique<CountingElement>(m_child_updates, true));
            if (!child) {
                throw std::runtime_error("Failed to add deferred element");
            }
        }
        return Uni::GUI::UiElementUpdate{};
    }

private:
    Uni::GUI::Detail::UiElementRegistry& m_registry;
    int& m_child_updates;
    bool m_added{};
};

class ThrowingElement final : public Uni::GUI::UiElement {
public:
    Uni::GUI::UiResult<Uni::GUI::UiElementUpdate> Update(Uni::GUI::UiState&) override {
        throw std::runtime_error("expected test exception");
    }
};

class FailingMutator final : public Uni::GUI::UiElement {
public:
    FailingMutator(Uni::GUI::Detail::UiElementRegistry& registry, int& updates, bool throws)
        : m_registry(registry), m_updates(updates), m_throws(throws) {}

    Uni::GUI::UiResult<Uni::GUI::UiElementUpdate> Update(Uni::GUI::UiState& state) override {
        Expect(m_registry.Remove(state.element_id), "Self-removal must be accepted");
        Expect(!m_registry.Remove(state.element_id), "Deferred removal must only be accepted once");
        Expect(m_registry.Add(std::make_unique<CountingElement>(m_updates, true)).has_value(),
               "Replacement must be queued");
        if (m_throws) {
            throw std::runtime_error("expected failure after registry mutation");
        }
        return std::unexpected(Uni::GUI::UiError{Uni::GUI::UiErrorCode::InvalidArgument, "element failed"});
    }

private:
    Uni::GUI::Detail::UiElementRegistry& m_registry;
    int& m_updates;
    bool m_throws;
};

class MutatingDestructorElement final : public Uni::GUI::UiElement {
public:
    MutatingDestructorElement(Uni::GUI::Detail::UiElementRegistry& registry, int& child_updates)
        : m_registry(registry), m_child_updates(child_updates) {}

    ~MutatingDestructorElement() override {
        auto child = m_registry.Add(std::make_unique<CountingElement>(m_child_updates, true));
        if (!child) {
            std::terminate();
        }
    }

    Uni::GUI::UiResult<Uni::GUI::UiElementUpdate> Update(Uni::GUI::UiState&) override {
        return Uni::GUI::UiElementUpdate{false, Uni::GUI::UiFrameDemand::None};
    }

private:
    Uni::GUI::Detail::UiElementRegistry& m_registry;
    int& m_child_updates;
};

} // namespace

int main() {
    using Uni::GUI::Detail::UiElementRegistry;

    Uni::GUI::UiApp app;
    UiElementRegistry registry;
    int persistent_updates = 0;
    int one_shot_updates = 0;

    auto persistent_id = registry.Add(std::make_unique<CountingElement>(persistent_updates, true));
    auto one_shot_id = registry.Add(std::make_unique<CountingElement>(one_shot_updates, false));
    Expect(persistent_id && one_shot_id && *persistent_id != *one_shot_id,
           "Registry IDs must be valid and unique");

    auto updated = registry.Update(app, 1, std::chrono::nanoseconds{16});
    Expect(updated && persistent_updates == 1 && one_shot_updates == 1,
           "Registry must update all active elements");
    Expect(registry.Size() == 1, "An element returning false must be removed after iteration");

    int child_updates = 0;
    auto adding_id = registry.Add(std::make_unique<AddingElement>(registry, child_updates));
    Expect(adding_id.has_value(), "Adding element must succeed");
    Expect(registry.Update(app, 2, std::chrono::nanoseconds{16}).has_value(), "Update with deferred addition must succeed");
    Expect(child_updates == 0, "An element added during Update must start on the next frame");
    Expect(registry.Update(app, 3, std::chrono::nanoseconds{16}).has_value() && child_updates == 1,
           "A deferred element must update on the following frame");

    Expect(registry.Remove(*persistent_id), "Removing an existing element must succeed");
    Expect(!registry.Remove(*persistent_id), "Removing the same element twice must fail");

    Expect(registry.Add(std::make_unique<ThrowingElement>()).has_value(),
           "Adding throwing element must succeed");
    auto failed_update = registry.Update(app, 4, std::chrono::nanoseconds{16});
    Expect(!failed_update && failed_update.error().code == Uni::GUI::UiErrorCode::FrameRendering,
           "Element exceptions must become FrameRendering errors");

    registry.Clear();
    Expect(registry.Size() == 0, "Clear must release all owned elements");

    int destructor_child_updates = 0;
    Expect(registry.Add(std::make_unique<MutatingDestructorElement>(registry, destructor_child_updates)).has_value(),
           "Mutating destructor element must be added");
    Expect(registry.Update(app, 5, std::chrono::nanoseconds{16}).has_value(), "Registry must safely detach an element before its destructor runs");
    Expect(registry.Size() == 1, "An element added by a destructor must be committed after destruction");
    Expect(registry.Update(app, 6, std::chrono::nanoseconds{16}).has_value() && destructor_child_updates == 1,
           "Destructor-added element must update on the following frame");
    registry.Clear();
    for (const bool throws : {false, true}) {
        int child = 0;
        int later = 0;
        Expect(registry.Add(std::make_unique<FailingMutator>(registry, child, throws)).has_value() &&
                   registry.Add(std::make_unique<CountingElement>(later, true)).has_value(),
               "Failure fixtures must register");
        auto failure = registry.Update(app, 7, std::chrono::nanoseconds{});
        Expect(!failure && failure.error().code == (throws ? Uni::GUI::UiErrorCode::FrameRendering :
                                                             Uni::GUI::UiErrorCode::InvalidArgument),
               "Iteration must preserve the element error or translate its exception");
        Expect(later == 0 && child == 0 && registry.Size() == 2,
               "Failed iteration must stop updating but finish removals and additions");
        Expect(registry.Update(app, 8, std::chrono::nanoseconds{}).has_value() && later == 1 && child == 1,
               "The replacement must participate in the very next iteration");
        registry.Clear();
    }
    return EXIT_SUCCESS;
}
