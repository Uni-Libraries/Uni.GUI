#include <uni/gui/nodes/nodes.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace Uni::GUI::Nodes;

void Expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void Execute(
    CommandStack& commands,
    std::unique_ptr<Command> command,
    GraphDocument& document,
    GraphPresentation& presentation,
    RegistryCatalog& types) {
    const auto result = commands.Execute(std::move(command), document, presentation, types);
    Expect(result.has_value(), "Asset fixture command must execute");
}

GraphInterface VersionedInterface(const std::uint32_t version) {
    return GraphInterface{
        .version = version,
        .pins = {
            GraphInterfacePin{
                .key = "value",
                .label = "Value",
                .type = TypeId{"float"},
                .direction = PinDirection::Input,
            },
        },
    };
}

GraphAsset MakeAsset(const char* id, const GraphInterface& interface = {}) {
    GraphAsset asset;
    asset.id = GraphAssetId{id};
    RegistryCatalog types;
    CommandStack commands;
    Execute(
        commands,
        std::make_unique<SetGraphInterfaceCommand>(asset.document.RootGraph(), interface),
        asset.document,
        asset.presentation,
        types);
    return asset;
}

void AddMarker(GraphAsset& asset, const char* marker) {
    RegistryCatalog types;
    CommandStack commands;
    const NodeId node = asset.document.AllocateNodeId();
    NodeCreation creation;
    creation.node.id = node;
    creation.node.type = TypeId{"asset.marker"};
    creation.node.display_name = marker;
    Execute(
        commands,
        std::make_unique<AddNodeCommand>(
            asset.document.RootGraph(),
            std::move(creation)),
        asset.document,
        asset.presentation,
        types);
}

GraphAsset MakeContentAsset(const char* id, const char* marker, const GraphInterface& interface = {}) {
    auto asset = MakeAsset(id, interface);
    AddMarker(asset, marker);
    return asset;
}

GraphAsset MakeDependent(const char* id, const char* dependency, const GraphInterface& interface = {}) {
    GraphAsset asset = MakeAsset(id);
    RegistryCatalog types;
    CommandStack commands;
    const NodeId node = asset.document.AllocateNodeId();
    NodeCreation creation;
    creation.node.id = node;
    creation.node.type = TypeId{"asset.call"};
    Execute(
        commands,
        std::make_unique<AddNodeCommand>(
            asset.document.RootGraph(),
            std::move(creation)),
        asset.document,
        asset.presentation,
        types);
    Execute(
        commands,
        std::make_unique<SetNodeSubgraphCommand>(
            asset.document.RootGraph(),
            node,
            SubgraphReference{
                .ownership = SubgraphOwnership::Referenced,
                .target = GraphAssetTarget{GraphAssetId{dependency}, interface},
            }),
        asset.document,
        asset.presentation,
        types);
    return asset;
}

void SetSchemaVersion(GraphAsset& asset, const std::uint32_t version) {
    RegistryCatalog types;
    CommandStack commands;
    Execute(
        commands,
        std::make_unique<SetSchemaVersionCommand>(version),
        asset.document,
        asset.presentation,
        types);
}

void TestLifecycleMetadataAndQueries() {
    GraphAssetRegistry assets;
    std::vector<GraphAssetChange> events;
    auto subscription = assets.Subscribe([&](const GraphAssetChange& event) { events.push_back(event); });

    auto dependency = assets.Write(MakeAsset("asset.base"), GraphAssetWriteMode::Insert);
    Expect(
        dependency && dependency->status == GraphAssetWriteStatus::Inserted && dependency->record &&
            dependency->record->Generation() == 1 &&
            dependency->record->ContentHash().ToHex().size() == 64,
        "Insert must return an immutable generation-one record");
    auto dependent = assets.Write(
        MakeDependent("asset.consumer", "asset.base"),
        GraphAssetWriteMode::Insert);
    const auto direct_dependents = assets.DirectDependents(GraphAssetId{"asset.base"});
    Expect(
        dependent && direct_dependents &&
            *direct_dependents == std::vector<GraphAssetId>{GraphAssetId{"asset.consumer"}} &&
            dependent->record->DirectDependencies().size() == 1 &&
            dependent->record->DependencyUses().size() == 1 &&
            dependent->record->DependencyUses().front().dependency == GraphAssetId{"asset.base"},
        "Records must cache dependency IDs/uses and registry must build the reverse index");
    const auto closure = assets.DependencyClosure(GraphAssetId{"asset.consumer"});
    Expect(
        closure && *closure == std::vector<GraphAssetId>{GraphAssetId{"asset.base"}},
        "Registry must expose the transitive dependency closure");

    auto blocked = assets.Unregister(GraphAssetId{"asset.base"});
    Expect(
        !blocked && blocked.error().code == ErrorCode::AssetInUse,
        "Unregister must reject assets with registered dependents");
    auto missing = assets.Write(
        MakeDependent("asset.missing", "asset.unknown"),
        GraphAssetWriteMode::Insert);
    Expect(
        !missing && missing.error().code == ErrorCode::AssetNotFound &&
            !assets.Find(GraphAssetId{"asset.missing"}),
        "Missing dependencies must fail without mutating the registry");

    const auto old_record = assets.Find(GraphAssetId{"asset.base"});
    auto uri_replacement = MakeAsset("asset.base");
    uri_replacement.source_uri = "memory://base-v2";
    const auto event_count = events.size();
    auto uri_replaced = assets.Write(std::move(uri_replacement), GraphAssetWriteMode::Replace);
    Expect(
        uri_replaced && uri_replaced->status == GraphAssetWriteStatus::Replaced &&
            uri_replaced->record->Generation() == 2 &&
            uri_replaced->changes == GraphAssetChangeFlags::SourceUri &&
            events.size() == event_count + 1 &&
            events.back().kind == GraphAssetChangeKind::Replaced &&
            events.back().before == old_record && events.back().after == uri_replaced->record,
        "URI-only replacement must be distinct and must not notify dependents");
    Expect(
        old_record->Asset().source_uri.empty() &&
            uri_replaced->record->Asset().source_uri == "memory://base-v2",
        "Old immutable records must retain their original metadata");

    auto content_replacement = MakeContentAsset("asset.base", "v3");
    content_replacement.source_uri = "memory://base-v2";
    const auto before_content = uri_replaced->record;
    const auto before_content_events = events.size();
    auto content_replaced = assets.Write(std::move(content_replacement), GraphAssetWriteMode::Replace);
    Expect(
        content_replaced && content_replaced->record->Generation() == 3 &&
            HasGraphAssetChange(content_replaced->changes, GraphAssetChangeFlags::Content) &&
            !HasGraphAssetChange(content_replaced->changes, GraphAssetChangeFlags::Dependencies) &&
            events.size() == before_content_events + 2 &&
            events.back().kind == GraphAssetChangeKind::DependencyChanged &&
            events.back().asset == GraphAssetId{"asset.consumer"} &&
            events.back().dependency_before == before_content &&
            events.back().dependency_after == content_replaced->record,
        "Content replacement must notify the affected reverse closure with owning snapshots");

    auto noop = MakeContentAsset("asset.base", "v3");
    noop.source_uri = "memory://base-v2";
    const auto no_op_events = events.size();
    auto unchanged = assets.Write(std::move(noop), GraphAssetWriteMode::Upsert);
    Expect(
        unchanged && unchanged->status == GraphAssetWriteStatus::Unchanged &&
            unchanged->changes == GraphAssetChangeFlags::None &&
            unchanged->record == content_replaced->record && events.size() == no_op_events,
        "Identical upsert must preserve the immutable record and emit no event");

    const GraphAssetId absent{"asset.absent"};
    const auto missing_direct = assets.DirectDependencies(absent);
    const auto missing_closure = assets.DependencyClosure(absent);
    const auto missing_reverse = assets.DirectDependents(absent);
    const auto missing_reverse_closure = assets.DependentClosure(absent);
    Expect(
        !missing_direct && !missing_closure && !missing_reverse && !missing_reverse_closure &&
            missing_direct.error().code == ErrorCode::AssetNotFound &&
            missing_closure.error().code == ErrorCode::AssetNotFound &&
            missing_reverse.error().code == ErrorCode::AssetNotFound &&
            missing_reverse_closure.error().code == ErrorCode::AssetNotFound,
        "All dependency queries must report a missing starting ID consistently");
    Expect(assets.ValidateAll().has_value(), "Registry-wide validation must accept a consistent state");
}

void TestReverseDeltaAndAtomicReplacementValidation() {
    GraphAssetRegistry assets;
    Expect(assets.Write(MakeAsset("asset.a"), GraphAssetWriteMode::Insert).has_value(), "A must insert");
    Expect(assets.Write(MakeAsset("asset.b"), GraphAssetWriteMode::Insert).has_value(), "B must insert");
    Expect(
        assets.Write(MakeDependent("asset.consumer", "asset.a"), GraphAssetWriteMode::Insert).has_value(),
        "Consumer must insert");
    const auto record_a = assets.Find(GraphAssetId{"asset.a"});
    const auto record_b = assets.Find(GraphAssetId{"asset.b"});
    const auto old_consumer = assets.Find(GraphAssetId{"asset.consumer"});

    auto delta = assets.Write(
        MakeDependent("asset.consumer", "asset.b"),
        GraphAssetWriteMode::Replace);
    const auto users_a = assets.DirectDependents(GraphAssetId{"asset.a"});
    const auto users_b = assets.DirectDependents(GraphAssetId{"asset.b"});
    Expect(
        delta && HasGraphAssetChange(delta->changes, GraphAssetChangeFlags::Dependencies) &&
            users_a && users_a->empty() && users_b &&
            *users_b == std::vector<GraphAssetId>{GraphAssetId{"asset.consumer"}} &&
            assets.Find(GraphAssetId{"asset.a"}) == record_a &&
            assets.Find(GraphAssetId{"asset.b"}) == record_b &&
            old_consumer != delta->record,
        "Dependency replacement must update only the reverse-index delta and one record");

    Expect(
        assets.Write(MakeDependent("asset.b", "asset.a"), GraphAssetWriteMode::Replace).has_value(),
        "B to A must remain acyclic");
    const auto before_cycle_a = assets.Find(GraphAssetId{"asset.a"});
    const auto before_cycle_users = assets.DirectDependents(GraphAssetId{"asset.b"});
    auto cycle_candidate = MakeDependent("asset.a", "asset.b");
    Expect(
        !assets.ValidateWrite(cycle_candidate, GraphAssetWriteMode::Replace),
        "ValidateWrite must reject a replacement cycle through the candidate record");
    auto cycle = assets.Write(std::move(cycle_candidate), GraphAssetWriteMode::Replace);
    const auto after_cycle_users = assets.DirectDependents(GraphAssetId{"asset.b"});
    Expect(
        !cycle && cycle.error().code == ErrorCode::InvalidGraph &&
            assets.Find(GraphAssetId{"asset.a"}) == before_cycle_a &&
            before_cycle_users && after_cycle_users && *before_cycle_users == *after_cycle_users,
        "Cycle replacement must preserve records and reverse indexes atomically");

    GraphAssetRegistry interfaces;
    const auto v1 = VersionedInterface(1);
    const auto v2 = VersionedInterface(2);
    Expect(
        interfaces.Write(MakeAsset("asset.interface", v1), GraphAssetWriteMode::Insert).has_value(),
        "Interface dependency must insert");
    Expect(
        interfaces.Write(
            MakeDependent("asset.interface.consumer", "asset.interface", v1),
            GraphAssetWriteMode::Insert).has_value(),
        "Interface consumer must insert");
    const auto old_interface = interfaces.Find(GraphAssetId{"asset.interface"});
    const auto old_interface_users = interfaces.DirectDependents(GraphAssetId{"asset.interface"});
    auto incompatible_candidate = MakeAsset("asset.interface", v2);
    Expect(
        !interfaces.ValidateWrite(incompatible_candidate, GraphAssetWriteMode::Replace),
        "ValidateWrite must check the affected reverse closure");
    auto incompatible = interfaces.Write(
        std::move(incompatible_candidate),
        GraphAssetWriteMode::Replace);
    const auto current_interface_users = interfaces.DirectDependents(GraphAssetId{"asset.interface"});
    Expect(
        !incompatible && incompatible.error().code == ErrorCode::InvalidGraph &&
            interfaces.Find(GraphAssetId{"asset.interface"}) == old_interface &&
            old_interface_users && current_interface_users &&
            *old_interface_users == *current_interface_users,
        "Incompatible interface replacement must be atomic");
    Expect(interfaces.ValidateAll().has_value(), "Failed replacement must leave a fully valid registry");

    GraphAssetRegistry root_flags;
    Expect(
        root_flags.Write(MakeAsset("asset.root.flags", v1), GraphAssetWriteMode::Insert).has_value(),
        "Root flag fixture must insert");
    auto changed_root = root_flags.Write(
        MakeAsset("asset.root.flags", v2),
        GraphAssetWriteMode::Replace);
    Expect(
        changed_root && HasGraphAssetChange(changed_root->changes, GraphAssetChangeFlags::Content) &&
            HasGraphAssetChange(changed_root->changes, GraphAssetChangeFlags::RootInterface) &&
            !HasGraphAssetChange(changed_root->changes, GraphAssetChangeFlags::Dependencies) &&
            changed_root->record->RootInterface() == v2,
        "Root-interface changes must have a distinct flag and cached accessor");
}

void TestRecordLifetimeAndNoInternalRoundTrip() {
    GraphAssetRecordPtr first;
    GraphAssetRecordPtr second;
    GraphAssetChange removal;
    {
        GraphAssetRegistry assets;
        auto inserted = assets.Write(MakeContentAsset("asset.snapshot", "v1"), GraphAssetWriteMode::Insert);
        Expect(inserted.has_value(), "Snapshot fixture must insert");
        first = inserted->record;
        auto replaced = assets.Write(
            MakeContentAsset("asset.snapshot", "v2"),
            GraphAssetWriteMode::Replace);
        Expect(replaced.has_value(), "Snapshot fixture must replace");
        second = replaced->record;
        auto subscription = assets.Subscribe([&](const GraphAssetChange& event) { removal = event; });
        Expect(assets.Unregister(GraphAssetId{"asset.snapshot"}).has_value(), "Snapshot fixture must remove");
        Expect(!assets.Find(GraphAssetId{"asset.snapshot"}), "Removed record must no longer resolve");
    }
    Expect(
        first && second && first->Generation() == 1 && second->Generation() == 2 &&
            first->Asset().id == GraphAssetId{"asset.snapshot"} &&
            second->Asset().id == GraphAssetId{"asset.snapshot"} &&
            removal.kind == GraphAssetChangeKind::Removed && removal.before == second,
        "Record and event snapshots must outlive replacement, removal, and registry destruction");

    GraphAssetRegistry migrated;
    auto schema_two = MakeAsset("asset.schema.two");
    SetSchemaVersion(schema_two, 2);
    auto schema_insert = migrated.Write(std::move(schema_two), GraphAssetWriteMode::Insert);
    Expect(schema_insert.has_value(), "Schema-two asset must insert");
    const auto schema_record = schema_insert->record;
    auto unrelated = migrated.Write(MakeAsset("asset.unrelated"), GraphAssetWriteMode::Insert);
    Expect(
        unrelated && migrated.Find(GraphAssetId{"asset.schema.two"}) == schema_record &&
            schema_record->Asset().document.SchemaVersion() == 2,
        "Unchanged schema-two records must not pass through an internal JSON round-trip");
}

void TestMoveAndDestroyDuringDispatch() {
    {
        GraphAssetRegistry assets;
        int calls = 0;
        bool rejected = false;
        auto subscription = assets.Subscribe([&](const GraphAssetChange&) {
            ++calls;
            GraphAssetRegistry moved = std::move(assets);
            const auto reentrant = moved.Unregister(GraphAssetId{"asset.move.construct"});
            rejected = !reentrant && reentrant.error().code == ErrorCode::CommandFailed;
        });
        auto written = assets.Write(MakeAsset("asset.move.construct"), GraphAssetWriteMode::Insert);
        Expect(
            written && calls == 1 && rejected && !assets.Find(GraphAssetId{"asset.move.construct"}),
            "Move construction and destruction of the active owner must be safe during callback dispatch");
    }

    {
        GraphAssetRegistry assets;
        int calls = 0;
        auto subscription = assets.Subscribe([&](const GraphAssetChange&) {
            ++calls;
            assets = GraphAssetRegistry{};
        });
        auto written = assets.Write(MakeAsset("asset.move.assign"), GraphAssetWriteMode::Insert);
        Expect(
            written && calls == 1 && !assets.Find(GraphAssetId{"asset.move.assign"}) &&
                assets.ValidateAll().has_value(),
            "Move assignment of the active owner must be safe during callback dispatch");
    }

    {
        auto assets = std::make_unique<GraphAssetRegistry>();
        Expect(assets->Write(MakeAsset("asset.destroy.base"), GraphAssetWriteMode::Insert).has_value(),
               "Destroy base must insert");
        Expect(
            assets->Write(
                MakeDependent("asset.destroy.consumer", "asset.destroy.base"),
                GraphAssetWriteMode::Insert).has_value(),
            "Destroy consumer must insert");
        int destroying_calls = 0;
        int observing_calls = 0;
        std::vector<GraphAssetChange> saved_events;
        auto destroying = assets->Subscribe([&](const GraphAssetChange& event) {
            ++destroying_calls;
            saved_events.push_back(event);
            if (assets) assets.reset();
        });
        auto observing = assets->Subscribe([&](const GraphAssetChange&) { ++observing_calls; });
        auto written = assets->Write(
            MakeContentAsset("asset.destroy.base", "changed"),
            GraphAssetWriteMode::Replace);
        Expect(
            written && !assets && destroying_calls == 2 && observing_calls == 2 &&
                saved_events.size() == 2 &&
                saved_events[1].kind == GraphAssetChangeKind::DependencyChanged &&
                saved_events[0].after == written->record,
            "Owner destruction must not stop the snapshotted callback/event batch");
    }
}

void TestCallbackIsolationAndSubscriptionSnapshots() {
    GraphAssetRegistry assets;
    Expect(assets.Write(MakeAsset("asset.callback.base"), GraphAssetWriteMode::Insert).has_value(),
           "Callback base must insert");
    Expect(
        assets.Write(
            MakeDependent("asset.callback.consumer", "asset.callback.base"),
            GraphAssetWriteMode::Insert).has_value(),
        "Callback consumer must insert");

    int throwing_calls = 0;
    int reset_calls = 0;
    int creator_calls = 0;
    int late_calls = 0;
    int observer_calls = 0;
    bool late_created = false;
    bool write_rejected = false;
    bool unregister_rejected = false;
    GraphAssetSubscription reset_subscription;
    GraphAssetSubscription late_subscription;
    auto throwing = assets.Subscribe([&](const GraphAssetChange&) {
        ++throwing_calls;
        throw std::runtime_error{"callback failure"};
    });
    reset_subscription = assets.Subscribe([&](const GraphAssetChange&) {
        ++reset_calls;
        reset_subscription.Reset();
    });
    auto creator = assets.Subscribe([&](const GraphAssetChange&) {
        ++creator_calls;
        if (!late_created) {
            late_created = true;
            late_subscription = assets.Subscribe([&](const GraphAssetChange&) { ++late_calls; });
        }
    });
    auto reentrant = assets.Subscribe([&](const GraphAssetChange&) {
        if (write_rejected) return;
        const auto write = assets.Write(MakeAsset("asset.callback.nested"), GraphAssetWriteMode::Insert);
        const auto unregister = assets.Unregister(GraphAssetId{"asset.callback.base"});
        write_rejected = !write && write.error().code == ErrorCode::CommandFailed;
        unregister_rejected = !unregister && unregister.error().code == ErrorCode::CommandFailed;
    });
    auto observer = assets.Subscribe([&](const GraphAssetChange&) { ++observer_calls; });

    auto content = assets.Write(
        MakeContentAsset("asset.callback.base", "changed"),
        GraphAssetWriteMode::Replace);
    Expect(
        content && throwing_calls == 2 && reset_calls == 2 && creator_calls == 2 &&
            late_calls == 0 && observer_calls == 2 && write_rejected && unregister_rejected &&
            !assets.Find(GraphAssetId{"asset.callback.nested"}),
        "Exceptions and Reset/Subscribe mutations must not alter the current callback snapshot");

    auto uri = MakeContentAsset("asset.callback.base", "changed");
    uri.source_uri = "memory://callback";
    auto next = assets.Write(std::move(uri), GraphAssetWriteMode::Replace);
    Expect(
        next && next->changes == GraphAssetChangeFlags::SourceUri && throwing_calls == 3 &&
            reset_calls == 2 && creator_calls == 3 && late_calls == 1 && observer_calls == 3,
        "Reset and new subscriptions must take effect on the next batch and dispatch guard must recover");
}

} // namespace

int main() {
    TestLifecycleMetadataAndQueries();
    TestReverseDeltaAndAtomicReplacementValidation();
    TestRecordLifetimeAndNoInternalRoundTrip();
    TestMoveAndDestroyDuringDispatch();
    TestCallbackIsolationAndSubscriptionSnapshots();
    return EXIT_SUCCESS;
}
