#pragma once

namespace Uni::GUI::Nodes {

ConnectionResult Detail::ValidateConnection(
    const GraphDocument &document, const GraphPresentation &presentation,
    const ConnectionRequest &request, const RegistrySnapshot &registry,
    const GraphPolicy &policy, const std::span<const PinInstance> pending_pins,
    const std::span<const NodeInstance> pending_nodes) {
  const auto *graph = document.FindGraph(request.graph);
  if (graph == nullptr)
    return Rejected("Graph does not exist", ErrorCode::GraphNotFound);
  if (graph->read_only)
    return Rejected("Graph is read-only", ErrorCode::ReadOnly);
  const auto find_pin = [&](const PinId id) -> const PinInstance * {
    if (const auto *pin = document.FindPin(request.graph, id))
      return pin;
    const auto found = std::ranges::find(pending_pins, id, &PinInstance::id);
    return found != pending_pins.end() ? &*found : nullptr;
  };
  const auto *first_pin = find_pin(request.first);
  const auto *second_pin = find_pin(request.second);
  if (first_pin == nullptr || second_pin == nullptr) {
    return Rejected("Pin does not exist in this graph", ErrorCode::PinNotFound);
  }
  if (request.first == request.second ||
      first_pin->direction == second_pin->direction) {
    return Rejected("A link must connect an output to an input",
                    ErrorCode::InvalidDirection);
  }

  const auto *output =
      first_pin->direction == PinDirection::Output ? first_pin : second_pin;
  const auto *input =
      first_pin->direction == PinDirection::Input ? first_pin : second_pin;
  if (output->kind != input->kind)
    return Rejected("Data and execution pins cannot be connected");

  const auto find_node = [&](const NodeId id) -> const NodeInstance * {
    if (const auto *node = document.FindNode(request.graph, id))
      return node;
    const auto found = std::ranges::find(pending_nodes, id, &NodeInstance::id);
    return found != pending_nodes.end() ? &*found : nullptr;
  };
  const auto *output_node = find_node(output->node);
  const auto *input_node = find_node(input->node);
  if (output_node == nullptr || input_node == nullptr) {
    return Rejected("Pin owner does not exist in this graph",
                    ErrorCode::NodeNotFound);
  }
  if (output->read_only || input->read_only || output_node->read_only ||
      input_node->read_only) {
    return Rejected("Connection endpoint is read-only", ErrorCode::ReadOnly);
  }

  std::optional<Link> replacing;
  if (request.replacing) {
    const auto found = graph->links.find(request.replacing);
    if (found == graph->links.end()) {
      return Rejected("Reconnected link does not exist",
                      ErrorCode::LinkNotFound);
    }
    const auto *previous_output =
        document.FindPin(request.graph, found->second.output);
    const auto *previous_input =
        document.FindPin(request.graph, found->second.input);
    const auto *previous_output_node =
        previous_output != nullptr
            ? document.FindNode(request.graph, previous_output->node)
            : nullptr;
    const auto *previous_input_node =
        previous_input != nullptr
            ? document.FindNode(request.graph, previous_input->node)
            : nullptr;
    if (found->second.read_only ||
        (previous_output != nullptr && previous_output->read_only) ||
        (previous_input != nullptr && previous_input->read_only) ||
        (previous_output_node != nullptr && previous_output_node->read_only) ||
        (previous_input_node != nullptr && previous_input_node->read_only)) {
      return Rejected("Reconnected link or endpoint is read-only",
                      ErrorCode::ReadOnly);
    }
    if (const auto *state = presentation.FindLink(request.replacing);
        state != nullptr && state->Style().locked) {
      return Rejected("Reconnected link presentation is locked",
                      ErrorCode::Locked);
    }
    const bool same_output = found->second.output == output->id;
    const bool same_input = found->second.input == input->id;
    if (!same_output && !same_input) {
      return Rejected("Reconnect must preserve at least one endpoint",
                      ErrorCode::InvalidArgument);
    }
    replacing = found->second;
  }
  const LinkId existing = document.FindLinkBetween(output->id, input->id);
  if (existing && existing != request.replacing) {
    return Rejected("The pins are already connected", ErrorCode::DuplicateId);
  }
  const auto output_incident = document.IncidentLinks(output->id);
  const std::size_t output_connections =
      output_incident.size() -
      static_cast<std::size_t>(output_incident.contains(request.replacing));
  if (output->cardinality == PinCardinality::Single &&
      output_connections != 0) {
    return Rejected("Pin accepts only one connection",
                    ErrorCode::CardinalityExceeded);
  }
  const auto input_incident = document.IncidentLinks(input->id);
  const std::size_t input_connections =
      input_incident.size() -
      static_cast<std::size_t>(input_incident.contains(request.replacing));
  if (input->cardinality == PinCardinality::Single && input_connections != 0) {
    return Rejected("Pin accepts only one connection",
                    ErrorCode::CardinalityExceeded);
  }
  auto compatibility = registry.Check(output->type, input->type, output->kind);
  if (compatibility.status == ConnectionResult::Status::Rejected)
    return compatibility;
  const auto policy_decision =
      policy.CheckConnection(document, presentation,
                             ConnectionPolicyRequest{
                                 .graph = request.graph,
                                 .output_node = *output_node,
                                 .output = *output,
                                 .input_node = *input_node,
                                 .input = *input,
                                 .replacing = std::move(replacing),
                             });
  if (const auto *denied = std::get_if<DenyOperation>(&policy_decision)) {
    return Rejected(denied->reason, ErrorCode::PolicyRejected);
  }
  return compatibility;
}

ConnectionResult ValidateConnection(
    const GraphDocument &document, const GraphPresentation &presentation,
    const ConnectionRequest &request, const RegistryCatalog &registry,
    const GraphPolicy &policy, const std::span<const PinInstance> pending_pins,
    const std::span<const NodeInstance> pending_nodes) {
  const auto invocation = Detail::RegistryAccess::Invoke(registry);
  return Detail::ValidateConnection(document, presentation, request,
                                    invocation.Snapshot(), policy, pending_pins,
                                    pending_nodes);
}

std::vector<ValidationIssue> ValidateGraph(const GraphDocument &document,
                                           const GraphId graph_id,
                                           const RegistryCatalog &catalog) {
  const auto invocation = Detail::RegistryAccess::Invoke(catalog);
  const RegistrySnapshot &registry = invocation.Snapshot();
  const GraphDocumentSnapshot snapshot = CaptureGraphDocumentSnapshot(document);
  std::vector<ValidationIssue> issues;
  const auto *graph = snapshot.FindGraph(graph_id);
  if (graph == nullptr) {
    AddIssue(issues, ValidationSeverity::Error, "Graph does not exist",
             graph_id);
    return issues;
  }
  if (graph->id != graph_id) {
    AddIssue(issues, ValidationSeverity::Error,
             "Graph key and stored ID differ", graph_id);
  }
  if (auto hierarchy = snapshot.ValidateStructure(); !hierarchy) {
    AddIssue(issues, ValidationSeverity::Error, hierarchy.error().message,
             graph_id);
  }

  std::unordered_set<PinId, IdHash> referenced_pins;
  std::unordered_set<NodeId, IdHash> external_nodes;
  std::unordered_set<PinId, IdHash> external_pins;
  std::unordered_set<LinkId, IdHash> external_links;
  for (const auto &graph_reference : snapshot.Graphs()) {
    const auto &other_graph = graph_reference.get();
    if (other_graph.id == graph_id)
      continue;
    for (const auto &[node_id, node] : other_graph.nodes) {
      (void)node;
      external_nodes.insert(node_id);
    }
    for (const auto &[pin_id, pin] : other_graph.pins) {
      (void)pin;
      external_pins.insert(pin_id);
    }
    for (const auto &[link_id, link] : other_graph.links) {
      (void)link;
      external_links.insert(link_id);
    }
  }
  for (const auto &[node_id, node] : graph->nodes) {
    if (!node_id || !node.id || node.id != node_id ||
        external_nodes.contains(node_id)) {
      AddIssue(issues, ValidationSeverity::Error,
               "Node ID is invalid or not document-unique", graph_id, node_id);
    }
    if (node.type.Empty() || node.type_version == 0) {
      AddIssue(issues, ValidationSeverity::Error,
               "Node type or version is invalid", graph_id, node_id);
    }
    if (!ValidProperties(node.properties)) {
      AddIssue(issues, ValidationSeverity::Error, "Node properties are invalid",
               graph_id, node_id);
    }
    if (const auto local = Detail::LocalSubgraph(node.subgraph)) {
      if (*local == graph_id || snapshot.FindGraph(*local) == nullptr) {
        AddIssue(issues, ValidationSeverity::Error,
                 "Node references an invalid subgraph", graph_id, node_id);
      } else if (snapshot.HasDependencyPath(*local, graph_id)) {
        AddIssue(issues, ValidationSeverity::Error,
                 "Node creates a cyclic subgraph reference", graph_id, node_id);
      }
    }
    const auto descriptor = registry.Find(node.type);
    const bool core_structural_node = node.role == NodeRole::BoundaryInput ||
                                      node.role == NodeRole::BoundaryOutput ||
                                      node.role == NodeRole::IntergraphInput ||
                                      node.role == NodeRole::IntergraphOutput;
    if (!descriptor && !core_structural_node) {
      AddIssue(issues, ValidationSeverity::Warning,
               "Node type '" + node.type.Value() + "' is not registered",
               graph_id, node_id);
    } else if (descriptor && node.type_version > descriptor->version) {
      AddIssue(issues, ValidationSeverity::Warning,
               "Node was created by a newer descriptor version", graph_id,
               node_id);
    }

    std::unordered_set<PinId, IdHash> node_pin_ids;
    std::unordered_map<std::string, const PinInstance *> pins_by_key;
    std::vector<PinInstance> node_pins;
    node_pins.reserve(node.pins.size());
    for (const auto pin_id : node.pins) {
      if (!node_pin_ids.insert(pin_id).second) {
        AddIssue(issues, ValidationSeverity::Error,
                 "Node references a pin more than once", graph_id, node_id,
                 pin_id);
        continue;
      }
      referenced_pins.insert(pin_id);
      const auto found = graph->pins.find(pin_id);
      if (found == graph->pins.end()) {
        AddIssue(issues, ValidationSeverity::Error,
                 "Node references a missing pin", graph_id, node_id, pin_id);
      } else if (found->second.node != node_id) {
        AddIssue(issues, ValidationSeverity::Error,
                 "Pin belongs to a different node", graph_id, node_id, pin_id);
      } else if (!pins_by_key.emplace(found->second.key, &found->second)
                      .second) {
        AddIssue(issues, ValidationSeverity::Error,
                 "Node contains duplicate pin semantic keys", graph_id, node_id,
                 pin_id);
      } else {
        node_pins.push_back(found->second);
      }
    }
    if (descriptor && node.type_version <= descriptor->version) {
      std::vector<std::string_view> actual_static_order;
      for (const auto &pin : node_pins) {
        if (pin.storage == PinStorage::Static)
          actual_static_order.push_back(pin.key);
      }
      const bool static_order_matches =
          actual_static_order.size() == descriptor->static_pins.size() &&
          std::ranges::equal(
              actual_static_order, descriptor->static_pins,
              [](const std::string_view actual, const PinDescriptor &expected) {
                return actual == expected.key;
              });
      if (!static_order_matches) {
        AddIssue(issues, ValidationSeverity::Error,
                 "Descriptor-owned static pin order does not match the node "
                 "descriptor",
                 graph_id, node_id);
      }
      for (const auto &expected : descriptor->static_pins) {
        const auto actual = pins_by_key.find(expected.key);
        if (actual == pins_by_key.end()) {
          AddIssue(issues, ValidationSeverity::Error,
                   "Node is missing static pin '" + expected.key + "'",
                   graph_id, node_id);
          continue;
        }
        const auto &pin = *actual->second;
        if (pin.storage != PinStorage::Static || pin.type != expected.type ||
            pin.direction != expected.direction || pin.kind != expected.kind ||
            pin.cardinality != expected.cardinality) {
          AddIssue(issues, ValidationSeverity::Error,
                   "Static pin '" + expected.key +
                       "' does not match its descriptor",
                   graph_id, node_id, pin.id);
        }
      }
      for (const auto &[key, pin] : pins_by_key) {
        const bool declared_static = std::ranges::any_of(
            descriptor->static_pins,
            [&](const PinDescriptor &expected) { return expected.key == key; });
        if (pin->storage == PinStorage::Static && !declared_static) {
          AddIssue(issues, ValidationSeverity::Error,
                   "Static pin '" + key +
                       "' is not declared by the node descriptor",
                   graph_id, node_id, pin->id);
        }
      }
      if (descriptor->behavior && descriptor->behavior->validate) {
        const ValidateNodeFn validate = descriptor->behavior->validate;
        try {
          for (auto message : validate(node, node_pins)) {
            if (message.empty())
              message = "Node-specific validation failed";
            AddIssue(issues, ValidationSeverity::Error, std::move(message),
                     graph_id, node_id);
          }
        } catch (const std::exception &exception) {
          AddIssue(issues, ValidationSeverity::Error,
                   std::string{"Node validator failed: "} + exception.what(),
                   graph_id, node_id);
        } catch (...) {
          AddIssue(issues, ValidationSeverity::Error,
                   "Node validator failed with an unknown exception", graph_id,
                   node_id);
        }
      }
    }
  }

  for (const auto &[pin_id, pin] : graph->pins) {
    if (!pin_id || !pin.id || pin.id != pin_id ||
        external_pins.contains(pin_id)) {
      AddIssue(issues, ValidationSeverity::Error,
               "Pin ID is invalid or not document-unique", graph_id, pin.node,
               pin_id);
    }
    if (!graph->nodes.contains(pin.node)) {
      AddIssue(issues, ValidationSeverity::Error,
               "Pin references a missing node", graph_id, pin.node, pin_id);
    }
    if (!referenced_pins.contains(pin_id)) {
      AddIssue(issues, ValidationSeverity::Error,
               "Pin is not listed by its node", graph_id, pin.node, pin_id);
    }
    if (pin.key.empty() || pin.type.Empty() || !ValidDirection(pin.direction) ||
        !ValidKind(pin.kind) || !ValidCardinality(pin.cardinality) ||
        !ValidStorage(pin.storage)) {
      AddIssue(issues, ValidationSeverity::Error, "Pin metadata is invalid",
               graph_id, pin.node, pin_id);
    }
  }

  std::unordered_map<PinId, std::size_t, IdHash> connection_counts;
  std::set<std::pair<PinId, PinId>> connected_pairs;
  for (const auto &[link_id, link] : graph->links) {
    if (!link_id || !link.id || link.id != link_id ||
        external_links.contains(link_id)) {
      AddIssue(issues, ValidationSeverity::Error,
               "Link ID is invalid or not document-unique", graph_id, {}, {},
               link_id);
    }
    const auto output = graph->pins.find(link.output);
    const auto input = graph->pins.find(link.input);
    if (output == graph->pins.end() || input == graph->pins.end()) {
      AddIssue(issues, ValidationSeverity::Error,
               "Link references a missing pin", graph_id, {}, {}, link_id);
      continue;
    }
    if (!connected_pairs.insert({link.output, link.input}).second) {
      AddIssue(issues, ValidationSeverity::Error, "Duplicate link endpoints",
               graph_id, {}, {}, link_id);
    }
    if (output->second.direction != PinDirection::Output ||
        input->second.direction != PinDirection::Input) {
      AddIssue(issues, ValidationSeverity::Error, "Link direction is invalid",
               graph_id, {}, {}, link_id);
    }
    if (output->second.kind != input->second.kind) {
      AddIssue(issues, ValidationSeverity::Error,
               "Link mixes data and execution pins", graph_id, {}, {}, link_id);
    }
    const auto compatibility = registry.Check(
        output->second.type, input->second.type, output->second.kind);
    if (compatibility.status != ConnectionResult::Status::Allowed) {
      AddIssue(issues, ValidationSeverity::Error, compatibility.reason,
               graph_id, {}, {}, link_id);
    }
    ++connection_counts[link.output];
    ++connection_counts[link.input];
  }
  for (const auto &[pin_id, count] : connection_counts) {
    const auto &pin = graph->pins.at(pin_id);
    if (pin.cardinality == PinCardinality::Single && count > 1) {
      AddIssue(issues, ValidationSeverity::Error,
               "Single-connection pin has multiple links", graph_id, pin.node,
               pin_id);
    }
  }
  return issues;
}

} // namespace Uni::GUI::Nodes
