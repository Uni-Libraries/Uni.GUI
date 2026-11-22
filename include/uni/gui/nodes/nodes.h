#pragma once

/** @file
 * Public Nodes API: semantic graph authoring, transactional edits, persistence and editor integration.
 * Live owners and callbacks are for the UI thread. Capture a GraphDocumentSnapshot on that thread
 * to read committed semantic data on a worker. Graphs describe authoring state, not an evaluator.
 * Include the narrower headers instead when only the headless model is needed.
 */

#include <uni/gui/nodes/commands.h>
#include <uni/gui/nodes/editor.h>
#include <uni/gui/nodes/error.h>
#include <uni/gui/nodes/graph.h>
#include <uni/gui/nodes/graph_asset.h>
#include <uni/gui/nodes/ids.h>
#include <uni/gui/nodes/io.h>
#include <uni/gui/nodes/layout.h>
#include <uni/gui/nodes/node_ui.h>
#include <uni/gui/nodes/presentation.h>
#include <uni/gui/nodes/policy.h>
#include <uni/gui/nodes/property.h>
#include <uni/gui/nodes/registry.h>
#include <uni/gui/nodes/routing.h>
#include <uni/gui/nodes/selection.h>
#include <uni/gui/nodes/snapshot.h>
#include <uni/gui/nodes/workspace.h>
