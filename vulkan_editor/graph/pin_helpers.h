// Pin creation and registration helper utilities
// Reduces boilerplate across node implementations
#pragma once

#include "../shader/shader_types.h"
#include "node.h"
#include "pin_registry.h"
#include <imgui_node_editor.h>

namespace PinHelpers {

using ShaderTypes::Pin;
using ShaderTypes::PinType;

/**
 * Create a pin with auto-generated ID.
 * Reduces 3 lines of boilerplate to 1 line.
 *
 * Before:
 *   cameraPin.id = ed::PinId(GetNextGlobalId());
 *   cameraPin.type = PinType::UniformBuffer;
 *   cameraPin.label = "Camera";
 *
 * After:
 *   cameraPin = createPin(PinType::UniformBuffer, "Camera");
 */
inline Pin createPin(PinType type, const std::string& label) {
    return Pin{
        .id = ax::NodeEditor::PinId(Node::GetNextGlobalId()),
        .type = type,
        .label = label
    };
}

/**
 * Register a legacy Pin struct with the registry.
 * Convenience wrapper that extracts fields from the Pin struct.
 *
 * @param registry The pin registry
 * @param nodeId The owning node's ID
 * @param pin The legacy Pin struct
 * @param kind Input or Output
 * @return The new PinHandle for storage
 */
inline PinHandle registerPin(
    PinRegistry& registry,
    int nodeId,
    const Pin& pin,
    PinKind kind
) {
    return registry.registerPinWithId(
        nodeId,
        pin.id,
        pin.type,
        kind,
        pin.label
    );
}

/**
 * Create a pin and register it in one step.
 * For nodes that don't need to store the legacy Pin struct.
 *
 * @param registry The pin registry
 * @param nodeId The owning node's ID
 * @param type The pin type
 * @param kind Input or Output
 * @param label The display label
 * @return The new PinHandle
 */
inline PinHandle createAndRegisterPin(
    PinRegistry& registry,
    int nodeId,
    PinType type,
    PinKind kind,
    const std::string& label
) {
    return registry.registerPin(nodeId, type, kind, label);
}

} // namespace PinHelpers
