#include "internal/frame.h"

namespace Uni::GUI::Nodes {

EditorResult DrawEditor(
    EditorContext& context,
    GraphDocument& document,
    GraphPresentation& presentation,
    CommandStack& commands,
    const RegistryCatalog& registry,
    const NodeUiRegistry& ui,
    const LinkRouterRegistry& routers,
    const Vec2 size,
    const EditorStyle& style,
    const EditorConfig& config,
    const EditorCallbacks& callbacks,
    const GraphPolicy& policy) {
    return EditorDetail::EditorFrame{
        context,
        document,
        presentation,
        commands,
        registry,
        ui,
        routers,
        size,
        style,
        config,
        callbacks,
        policy,
    }.Draw();
}

} // namespace Uni::GUI::Nodes
