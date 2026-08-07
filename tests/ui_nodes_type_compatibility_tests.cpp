#include <uni/gui/nodes/nodes.h>

#include "ui_nodes_internal.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace Uni::GUI::Nodes;

[[noreturn]] void Fail(const std::string_view message) {
  std::cerr << "FAIL: " << message << '\n';
  std::exit(EXIT_FAILURE);
}

void Expect(const bool condition, const std::string_view message) {
  if (!condition)
    Fail(message);
}

struct RegisteredConversion final {
  ConversionDescriptor descriptor;
  ConversionRegistrationToken registration;
};

RegisteredConversion RegisterConverter(RegistryCatalog &registry,
                                       const std::string_view node_type,
                                       const TypeId &source,
                                       const TypeId &destination) {
  auto registered_node = registry.RegisterNodeType(NodeTypeDescriptor{
      .type = TypeId{node_type},
      .display_name = std::string{node_type},
      .pin_schema =
          {
              PinDescriptor{.key = "input", .type = source},
              PinDescriptor{
                  .key = "output",
                  .type = destination,
                  .direction = PinDirection::Output,
              },
          },
  });
  if (!registered_node)
    Fail(registered_node.error().message);

  ConversionDescriptor descriptor{
      .key = ConversionKey{source, destination, PinKind::Data},
      .node_type = TypeId{node_type},
      .input_pin = "input",
      .output_pin = "output",
  };
  auto registration = registry.RegisterConversion(descriptor);
  if (!registration)
    Fail(registration.error().message);
  return RegisteredConversion{std::move(descriptor), *registration};
}

class CompatibilityDependencyCommand final : public Command {
public:
  CompatibilityDependencyCommand(TypeId output, TypeId input,
                                 const ConnectionResult::Status expected)
      : m_output(std::move(output)), m_input(std::move(input)),
        m_expected(expected) {}

  [[nodiscard]] std::string_view Name() const noexcept override {
    return "Compatibility dependency";
  }

private:
  [[nodiscard]] Result<void>
  Apply(GraphTransaction &transaction,
        const RegistrySnapshot &registry) override {
    if (registry.Check(m_output, m_input, PinKind::Data).status != m_expected) {
      return std::unexpected(
          Error{ErrorCode::CommandFailed, "Unexpected compatibility result"});
    }
    return transaction.SetSchemaVersion(2);
  }

  [[nodiscard]] Result<void> Revert(GraphTransaction &transaction) override {
    return transaction.SetSchemaVersion(1);
  }

  TypeId m_output;
  TypeId m_input;
  ConnectionResult::Status m_expected;
};

GraphPolicy DeferBatchPolicy() {
  GraphPolicy policy;
  policy.evaluate_batch =
      [](const BatchPolicyContext &,
         std::span<const OperationIntent>) -> BatchPolicyDecision {
    return DeferBatch{std::uint64_t{1}};
  };
  return policy;
}

void TestBuiltInPrecedenceAndDirectCompatibility() {
  RegistryCatalog registry;
  int policy_calls = 0;
  Expect(registry
             .SetTypeCompatibility(
                 [&](const TypeId &output, const TypeId &input,
                     const PinKind kind) {
                   ++policy_calls;
                   return kind == PinKind::Data &&
                          output == TypeId{"direct.source"} &&
                          input == TypeId{"direct.destination"};
                 })
             .has_value(),
         "Custom compatibility policy must register");

  Expect(registry.Check(TypeId{"same"}, TypeId{"same"}, PinKind::Data)
                 .status == ConnectionResult::Status::Allowed &&
             registry.Check(TypeId{"*"}, TypeId{"anything"}, PinKind::Data)
                     .status == ConnectionResult::Status::Allowed &&
             registry.Check(TypeId{"anything"}, TypeId{"*"}, PinKind::Data)
                     .status == ConnectionResult::Status::Allowed &&
             policy_calls == 0,
         "Exact and wildcard compatibility must bypass the custom policy");
  Expect(registry
                 .Check(TypeId{"direct.source"},
                        TypeId{"direct.destination"}, PinKind::Data)
                 .status == ConnectionResult::Status::Allowed &&
             policy_calls == 1,
         "Custom compatibility must allow a direct data connection");
  Expect(registry
                 .Check(TypeId{"direct.source"},
                        TypeId{"direct.destination"}, PinKind::Execution)
                 .status == ConnectionResult::Status::Rejected,
         "Compatibility policies must receive and respect the pin kind");
}

void TestGeneralizedConversionAndExactRecipePrecedence() {
  RegistryCatalog registry;
  const auto generalized =
      RegisterConverter(registry, "compat.converter.generalized",
                        TypeId{"integer"}, TypeId{"floating"});
  Expect(registry
             .SetTypeCompatibility(
                 [](const TypeId &output, const TypeId &input,
                    const PinKind kind) {
                   return kind == PinKind::Data &&
                          ((output == TypeId{"small-integer"} &&
                            input == TypeId{"integer"}) ||
                           (output == TypeId{"floating"} &&
                            input == TypeId{"number"}));
                 })
             .has_value(),
         "Generalized conversion policy must register");

  const auto generalized_result = registry.Check(
      TypeId{"small-integer"}, TypeId{"number"}, PinKind::Data);
  Expect(generalized_result.status ==
                 ConnectionResult::Status::RequiresConversion &&
             generalized_result.recipe &&
             generalized_result.recipe->Descriptor() == generalized.descriptor,
         "Compatibility must generalize both registered conversion endpoints");

  const auto exact = RegisterConverter(
      registry, "compat.converter.exact", TypeId{"small-integer"},
      TypeId{"number"});
  const auto exact_result = registry.Check(
      TypeId{"small-integer"}, TypeId{"number"}, PinKind::Data);
  Expect(exact_result.status ==
                 ConnectionResult::Status::RequiresConversion &&
             exact_result.recipe &&
             exact_result.recipe->Descriptor() == exact.descriptor,
         "An exact conversion recipe must precede generalized recipes");
}

std::string AmbiguityReason(const bool reverse_registration_order) {
  RegistryCatalog registry;
  const auto register_first = [&] {
    (void)RegisterConverter(registry, "compat.converter.first",
                            TypeId{"integer"}, TypeId{"floating"});
  };
  const auto register_second = [&] {
    (void)RegisterConverter(registry, "compat.converter.second",
                            TypeId{"numeric"}, TypeId{"double"});
  };
  if (reverse_registration_order) {
    register_second();
    register_first();
  } else {
    register_first();
    register_second();
  }
  Expect(registry
             .SetTypeCompatibility(
                 [](const TypeId &output, const TypeId &input,
                    const PinKind kind) {
                   return kind == PinKind::Data &&
                          ((output == TypeId{"small-integer"} &&
                            (input == TypeId{"integer"} ||
                             input == TypeId{"numeric"})) ||
                           ((output == TypeId{"floating"} ||
                             output == TypeId{"double"}) &&
                            input == TypeId{"number"}));
                 })
             .has_value(),
         "Ambiguity policy must register");
  const auto result = registry.Check(TypeId{"small-integer"},
                                     TypeId{"number"}, PinKind::Data);
  Expect(result.status == ConnectionResult::Status::Rejected &&
             !result.recipe,
         "Several generalized conversion recipes must be rejected");
  return result.reason;
}

void TestDeterministicAmbiguityAndExceptions() {
  const std::string forward = AmbiguityReason(false);
  const std::string reverse = AmbiguityReason(true);
  Expect(!forward.empty() && forward == reverse,
         "Ambiguous conversion rejection must not depend on registration order");

  RegistryCatalog registry;
  (void)RegisterConverter(registry, "compat.converter.exception",
                          TypeId{"throw.source"},
                          TypeId{"throw.destination"});
  Expect(registry
             .SetTypeCompatibility(
                 [](const TypeId &, const TypeId &, PinKind) -> bool {
                   throw std::runtime_error{"policy failure"};
                 })
             .has_value(),
         "Throwing compatibility policy must register");
  const auto result = registry.Check(TypeId{"throw.source"},
                                     TypeId{"throw.destination"},
                                     PinKind::Data);
  Expect(result.status == ConnectionResult::Status::Rejected &&
             !result.recipe,
         "A policy exception must reject instead of falling back to a converter");
}

void TestSnapshotsClearingAndNoOpSemantics() {
  RegistryCatalog registry;
  const std::uint64_t empty_generation = registry.Generation();
  Expect(registry.ClearTypeCompatibility().has_value() &&
             registry.Generation() == empty_generation,
         "Clearing an empty policy must be a no-op");

  TypeCompatibilityFn policy = [](const TypeId &output, const TypeId &input,
                                  const PinKind kind) {
    return kind == PinKind::Data && output == TypeId{"snapshot.source"} &&
           input == TypeId{"snapshot.destination"};
  };
  Expect(registry.SetTypeCompatibility(policy).has_value(),
         "Snapshot policy must register");
  const RegistrySnapshot retained = registry.Snapshot();
  const std::uint64_t first_generation = registry.Generation();
  Expect(registry.SetTypeCompatibility(policy).has_value() &&
             registry.Generation() == first_generation + 1,
         "Replacing a non-empty std::function policy may publish a change");
  Expect(registry.ClearTypeCompatibility().has_value(),
         "Live compatibility policy must clear");
  const std::uint64_t cleared_generation = registry.Generation();
  Expect(retained
                 .Check(TypeId{"snapshot.source"},
                        TypeId{"snapshot.destination"}, PinKind::Data)
                 .status == ConnectionResult::Status::Allowed &&
             registry
                     .Check(TypeId{"snapshot.source"},
                            TypeId{"snapshot.destination"}, PinKind::Data)
                     .status == ConnectionResult::Status::Rejected,
         "Retained snapshots must keep their immutable policy generation");
  Expect(registry.ClearTypeCompatibility().has_value() &&
             registry.Generation() == cleared_generation,
         "Repeated policy clearing must remain a no-op");

  NodeEditorWorkspace workspace;
  Expect(workspace
             .SetTypeCompatibility(
                 [](const TypeId &output, const TypeId &input, PinKind) {
                   return output == TypeId{"workspace.source"} &&
                          input == TypeId{"workspace.destination"};
                 })
             .has_value() &&
             workspace.Registry()
                     .Check(TypeId{"workspace.source"},
                            TypeId{"workspace.destination"}, PinKind::Data)
                     .status == ConnectionResult::Status::Allowed &&
             workspace.ClearTypeCompatibility().has_value() &&
             workspace.Registry()
                     .Check(TypeId{"workspace.source"},
                            TypeId{"workspace.destination"}, PinKind::Data)
                     .status == ConnectionResult::Status::Rejected,
         "Workspace must expose policy setting and clearing");
}

void TestAtomicUpdates() {
  RegistryCatalog registry;
  const RegistrySnapshot before = registry.Snapshot();
  auto update = registry.BeginUpdate();
  Expect(update.has_value(), "Atomic compatibility update must begin");

  const TypeId node_type{"compat.converter.atomic"};
  const TypeId source{"atomic.integer"};
  const TypeId destination{"atomic.floating"};
  const ConversionDescriptor conversion{
      .key = ConversionKey{source, destination, PinKind::Data},
      .node_type = node_type,
      .input_pin = "input",
      .output_pin = "output",
  };
  Expect(update
             ->RegisterNodeType(NodeTypeDescriptor{
                 .type = node_type,
                 .display_name = "Atomic converter",
                 .pin_schema =
                     {
                         PinDescriptor{.key = "input", .type = source},
                         PinDescriptor{
                             .key = "output",
                             .type = destination,
                             .direction = PinDirection::Output,
                         },
                     },
             })
             .has_value() &&
             update->RegisterConversion(conversion).has_value() &&
             update
                 ->SetTypeCompatibility(
                     [](const TypeId &output, const TypeId &input,
                        const PinKind kind) {
                       return kind == PinKind::Data &&
                              ((output == TypeId{"atomic.small"} &&
                                input == TypeId{"atomic.integer"}) ||
                               (output == TypeId{"atomic.floating"} &&
                                input == TypeId{"atomic.number"}));
                     })
                 .has_value(),
         "Descriptor, conversion, and policy must stage together");
  Expect(!registry.Find(node_type) &&
             registry.Check(TypeId{"atomic.small"}, TypeId{"atomic.number"},
                            PinKind::Data)
                     .status == ConnectionResult::Status::Rejected,
         "A staged compatibility batch must not affect the live generation");

  const auto committed = update->Commit();
  Expect(committed && committed->generation == 1 &&
             committed->node_revision == 1 &&
             committed->conversion_revision == 1 &&
             committed->statistics.published_generations == 1 &&
             committed->registrations.size() == 1,
         "An atomic compatibility batch must publish one combined generation");
  Expect(registry.Check(TypeId{"atomic.small"}, TypeId{"atomic.number"},
                        PinKind::Data)
                 .status == ConnectionResult::Status::RequiresConversion &&
             before.Check(TypeId{"atomic.small"}, TypeId{"atomic.number"},
                          PinKind::Data)
                     .status == ConnectionResult::Status::Rejected,
         "Atomic publication must preserve the prior snapshot");

  const std::uint64_t generation = registry.Generation();
  auto failing = registry.BeginUpdate();
  Expect(failing.has_value(), "Failing atomic update must begin");
  const TypeId invalid_node{"compat.converter.invalid-atomic"};
  Expect(failing
             ->RegisterNodeType(NodeTypeDescriptor{
                 .type = invalid_node,
                 .display_name = "Invalid atomic converter",
                 .pin_schema =
                     {
                         PinDescriptor{
                             .key = "input",
                             .type = TypeId{"invalid.source"},
                         },
                         PinDescriptor{
                             .key = "output",
                             .type = TypeId{"invalid.destination"},
                             .direction = PinDirection::Output,
                         },
                     },
             })
             .has_value() &&
             failing
                 ->RegisterConversion(ConversionDescriptor{
                     .key = ConversionKey{TypeId{"invalid.source"},
                                          TypeId{"invalid.destination"},
                                          PinKind::Data},
                     .node_type = invalid_node,
                     .input_pin = "missing",
                     .output_pin = "output",
                 })
                 .has_value() &&
             failing
                 ->SetTypeCompatibility(
                     [](const TypeId &output, const TypeId &input, PinKind) {
                       return output == TypeId{"failed.source"} &&
                              input == TypeId{"failed.destination"};
                     })
                 .has_value(),
         "Failing compatibility batch must stage before validation");
  const auto failed = failing->Commit();
  Expect(!failed && registry.Generation() == generation &&
             !registry.Find(invalid_node) &&
             registry
                     .Check(TypeId{"failed.source"},
                            TypeId{"failed.destination"}, PinKind::Data)
                     .status == ConnectionResult::Status::Rejected,
         "A failed batch must publish none of its staged policy or records");
}

void TestPolicyRevisionOverflow() {
  RegistryCatalog registry;
  Detail::RegistryAccess::SetRevisionsForTesting(
      registry, 0, std::numeric_limits<std::uint64_t>::max(), 7, 1, false);
  const auto conversion_overflow = registry.SetTypeCompatibility(
      [](const TypeId &, const TypeId &, PinKind) { return true; });
  Expect(!conversion_overflow &&
             conversion_overflow.error().code ==
                 ErrorCode::GenerationOverflow &&
             registry.Generation() == 7 &&
             registry.Check(TypeId{"overflow.source"},
                            TypeId{"overflow.destination"}, PinKind::Data)
                     .status == ConnectionResult::Status::Rejected,
         "Policy replacement must not publish after conversion revision overflow");

  Detail::RegistryAccess::SetRevisionsForTesting(
      registry, 0, 0, std::numeric_limits<std::uint64_t>::max(), 1, false);
  const auto generation_overflow = registry.SetTypeCompatibility(
      [](const TypeId &, const TypeId &, PinKind) { return true; });
  Expect(!generation_overflow &&
             generation_overflow.error().code ==
                 ErrorCode::GenerationOverflow &&
             registry.ConversionRevision() == 0 &&
             registry.Check(TypeId{"overflow.source"},
                            TypeId{"overflow.destination"}, PinKind::Data)
                     .status == ConnectionResult::Status::Rejected,
         "Policy replacement must not publish after generation overflow");
}

void TestDeferredDependencies() {
  RegistryCatalog registry;
  (void)RegisterConverter(registry, "compat.converter.deferred",
                          TypeId{"deferred.integer"},
                          TypeId{"deferred.floating"});
  const auto policy = [](const TypeId &output, const TypeId &input,
                         const PinKind kind) {
    return kind == PinKind::Data &&
           ((output == TypeId{"deferred.small"} &&
             input == TypeId{"deferred.integer"}) ||
            (output == TypeId{"deferred.floating"} &&
             input == TypeId{"deferred.number"}) ||
            (output == TypeId{"policy.source"} &&
             input == TypeId{"policy.destination"}));
  };
  Expect(registry.SetTypeCompatibility(policy).has_value(),
         "Deferred compatibility policy must register");

  {
    GraphDocument document;
    GraphPresentation presentation;
    CommandStack commands;
    auto pending = commands.Execute(
        std::make_unique<CompatibilityDependencyCommand>(
            TypeId{"deferred.small"}, TypeId{"deferred.number"},
            ConnectionResult::Status::RequiresConversion),
        document, presentation, registry, DeferBatchPolicy());
    Expect(pending && pending->deferred,
           "Generalized conversion dependency must defer");
    const auto resumed = commands.Resume(pending->deferred->id, document,
                                         presentation,
                                         ResumeMode::CommitPrepared);
    Expect(resumed && document.SchemaVersion() == 2,
           "A generalized recipe must not be recorded under its absent requested key");
  }

  {
    const auto unrelated = RegisterConverter(
        registry, "compat.converter.deferred-unrelated",
        TypeId{"deferred.text"}, TypeId{"deferred.boolean"});
    GraphDocument document;
    GraphPresentation presentation;
    CommandStack commands;
    auto pending = commands.Execute(
        std::make_unique<CompatibilityDependencyCommand>(
            TypeId{"deferred.small"}, TypeId{"deferred.number"},
            ConnectionResult::Status::RequiresConversion),
        document, presentation, registry, DeferBatchPolicy());
    Expect(pending && pending->deferred,
           "Conversion-root dependency must defer");
    const auto removed =
        registry.UnregisterConversion(unrelated.registration);
    Expect(removed && *removed,
           "Unrelated conversion mutation must publish");
    const auto resumed = commands.Resume(pending->deferred->id, document,
                                         presentation,
                                         ResumeMode::CommitPrepared);
    Expect(!resumed && resumed.error().code == ErrorCode::RevisionConflict &&
               commands.Cancel(pending->deferred->id).has_value(),
           "A generalized lookup must depend on the full conversion root");
  }

  {
    GraphDocument document;
    GraphPresentation presentation;
    CommandStack commands;
    auto pending = commands.Execute(
        std::make_unique<CompatibilityDependencyCommand>(
            TypeId{"policy.source"}, TypeId{"policy.destination"},
            ConnectionResult::Status::Allowed),
        document, presentation, registry, DeferBatchPolicy());
    Expect(pending && pending->deferred,
           "Policy identity dependency must defer");
    Expect(registry.SetTypeCompatibility(policy).has_value(),
           "Policy replacement must publish while a command is deferred");
    const auto resumed = commands.Resume(pending->deferred->id, document,
                                         presentation,
                                         ResumeMode::CommitPrepared);
    Expect(!resumed && resumed.error().code == ErrorCode::RevisionConflict &&
               commands.Cancel(pending->deferred->id).has_value(),
           "Deferred dependencies must detect compatibility policy replacement");
  }
}

} // namespace

int main() {
  TestBuiltInPrecedenceAndDirectCompatibility();
  TestGeneralizedConversionAndExactRecipePrecedence();
  TestDeterministicAmbiguityAndExceptions();
  TestSnapshotsClearingAndNoOpSemantics();
  TestAtomicUpdates();
  TestPolicyRevisionOverflow();
  TestDeferredDependencies();
  return EXIT_SUCCESS;
}
