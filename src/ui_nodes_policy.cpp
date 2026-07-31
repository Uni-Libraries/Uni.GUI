#include <uni/gui/nodes/policy.h>

#include <exception>

namespace Uni::GUI::Nodes {

bool GraphPolicy::Empty() const noexcept {
    return !evaluate_operation && !evaluate_batch;
}

OperationPolicyDecision GraphPolicy::EvaluateOperation(
    const OperationPolicyContext& context,
    const OperationIntent& intent) const {
    if (!evaluate_operation) {
        return AllowOperation{};
    }
    try {
        auto decision = evaluate_operation(context, intent);
        if (auto* denied = std::get_if<DenyOperation>(&decision); denied != nullptr && denied->reason.empty()) {
            denied->reason = "Graph policy rejected the operation";
        }
        if (auto* deferred = std::get_if<DeferOperation>(&decision);
            deferred != nullptr && !deferred->request.has_value()) {
            return DenyOperation{"Graph policy returned an empty defer request"};
        }
        return decision;
    } catch (const std::exception& exception) {
        return DenyOperation{std::string{"Graph policy failed: "} + exception.what()};
    } catch (...) {
        return DenyOperation{"Graph policy failed with an unknown exception"};
    }
}

BatchPolicyDecision GraphPolicy::EvaluateBatch(
    const BatchPolicyContext& context,
    const std::span<const OperationIntent> batch) const {
    if (!evaluate_batch) return AllowBatch{};
    try {
        auto decision = evaluate_batch(context, batch);
        if (auto* denied = std::get_if<DenyBatch>(&decision); denied != nullptr && denied->reason.empty()) {
            denied->reason = "Graph batch policy rejected the operation";
        }
        if (auto* deferred = std::get_if<DeferBatch>(&decision);
            deferred != nullptr && !deferred->request.has_value()) {
            return DenyBatch{"Graph batch policy returned an empty defer request"};
        }
        if (auto* replacement = std::get_if<ReplaceBatch>(&decision);
            replacement != nullptr && !replacement->make_command) {
            return DenyBatch{"Graph batch policy returned an empty replacement"};
        }
        return decision;
    } catch (const std::exception& exception) {
        return DenyBatch{std::string{"Graph batch policy failed: "} + exception.what()};
    } catch (...) {
        return DenyBatch{"Graph batch policy failed with an unknown exception"};
    }
}

namespace {

[[nodiscard]] OperationPolicyContext PreviewContext(
    const GraphDocument& document,
    const GraphPresentation& presentation) {
    return OperationPolicyContext{
        .before_document = document,
        .before_presentation = presentation,
        .staged_document = document,
        .staged_presentation = presentation,
        .phase = OperationPhase::Preview,
        .batch_size = 1,
    };
}

} // namespace

OperationPolicyDecision GraphPolicy::CheckCreateNode(const GraphDocument& document,
                                                      const GraphPresentation& presentation,
                                                      const CreateNodePolicyRequest& request) const {
    const OperationIntent intent{
        OperationKind::AddNode,
        OperationAction::Set,
        NodeOperation{request.graph, {}, {}, std::make_shared<const TypeId>(request.type)}};
    return EvaluateOperation(PreviewContext(document, presentation), intent);
}
OperationPolicyDecision GraphPolicy::CheckDeleteNode(const GraphDocument& document,
                                                      const GraphPresentation& presentation,
                                                      const DeleteNodePolicyRequest& request) const {
    const auto* graph = document.FindGraph(request.graph);
    const OperationIntent intent{
        OperationKind::DeleteElements,
        OperationAction::Erase,
        NodeOperation{request.graph, request.node, graph ? graph->nodes.SharedAt(request.node) : nullptr, {}}};
    return EvaluateOperation(PreviewContext(document, presentation), intent);
}
OperationPolicyDecision GraphPolicy::CheckConnection(const GraphDocument& document,
                                                      const GraphPresentation& presentation,
                                                      const ConnectionPolicyRequest& request) const {
    OperationIntent intent{
        OperationKind::Connect,
        OperationAction::Set,
        LinkOperation{
            request.graph,
            Link{
                .id = request.replacing ? request.replacing->id : LinkId{},
                .output = request.output.id,
                .input = request.input.id,
            },
        }};
    return EvaluateOperation(PreviewContext(document, presentation), intent);
}
OperationPolicyDecision GraphPolicy::CheckGroupLifecycle(
    const GraphDocument& document,
    const GraphPresentation& presentation,
    const GroupLifecyclePolicyRequest& request) const {
    if ((request.before == nullptr) == (request.after == nullptr)) {
        return DenyOperation{"Group lifecycle preview requires exactly one before/after value"};
    }
    const GroupPresentation* group = request.after != nullptr ? request.after : request.before;
    const OperationIntent intent{
        request.after != nullptr ? OperationKind::AddGroup : OperationKind::RemoveGroup,
        request.after != nullptr ? OperationAction::Set : OperationAction::Erase,
        GroupLifecycleOperation{request.graph,
                                group != nullptr ? group->id : GroupId{},
                                group != nullptr ? std::make_shared<const GroupPresentation>(*group) : nullptr}};
    return EvaluateOperation(PreviewContext(document, presentation), intent);
}

} // namespace Uni::GUI::Nodes
