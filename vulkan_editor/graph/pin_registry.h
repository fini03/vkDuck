#pragma once

#include "../shader/shader_types.h"
#include "pin_id_hash.h"  // Hash specializations for PinId/LinkId
#include <functional>
#include <unordered_map>
#include <vector>

using ShaderTypes::PinType;
// PinHandle and INVALID_PIN_HANDLE are now defined in shader_types.h

enum class PinKind { Input, Output };

// Complete pin information stored in registry
struct PinEntry {
    ed::PinId id;        // ImGui node editor ID
    PinType type;        // Semantic type (Image, Camera, etc.)
    PinKind kind;        // Input or Output
    std::string label;   // Display name
    int ownerNodeId;     // Node that owns this pin

    // Optional metadata for shader bindings
    int bindingSet = -1;
    int bindingSlot = -1;

    // Check if this entry is valid (not deleted)
    bool valid = true;
};

/**
 * @class PinRegistry
 * @brief Centralized registry that owns all pin data in the graph.
 *
 * Provides O(1) lookup by PinId and automatic cleanup when nodes are removed.
 * Nodes store lightweight PinHandle references instead of full Pin objects.
 */
class PinRegistry {
public:
    PinRegistry() = default;

    // ========================================================================
    // Registration
    // ========================================================================

    /**
     * Register a new pin and return its handle.
     * The pin ID is auto-generated.
     */
    PinHandle registerPin(
        int nodeId,
        PinType type,
        PinKind kind,
        const std::string& label
    );

    /**
     * Register a pin with a specific editor ID (for deserialization).
     */
    PinHandle registerPinWithId(
        int nodeId,
        ed::PinId editorId,
        PinType type,
        PinKind kind,
        const std::string& label
    );

    /**
     * Unregister a single pin by handle.
     */
    void unregisterPin(PinHandle handle);

    /**
     * Unregister all pins owned by a node.
     */
    void unregisterPinsForNode(int nodeId);

    // ========================================================================
    // Lookup - O(1)
    // ========================================================================

    /**
     * Get pin entry by handle. Returns nullptr if invalid.
     */
    PinEntry* get(PinHandle handle);
    const PinEntry* get(PinHandle handle) const;

    /**
     * Find pin by ImGui node editor ID. Returns nullptr if not found.
     */
    PinEntry* findByEditorId(ed::PinId id);
    const PinEntry* findByEditorId(ed::PinId id) const;

    /**
     * Get the handle for a given editor ID. Returns INVALID_PIN_HANDLE if not found.
     */
    PinHandle getHandleForEditorId(ed::PinId id) const;

    // ========================================================================
    // Node Lookup
    // ========================================================================

    /**
     * Get the node that owns a pin (by handle).
     */
    int getOwnerNodeId(PinHandle handle) const;

    /**
     * Get the node that owns a pin (by editor ID).
     */
    int getOwnerNodeIdByEditorId(ed::PinId id) const;

    // ========================================================================
    // Iteration
    // ========================================================================

    /**
     * Iterate over all pins owned by a node.
     */
    void forEachPin(int nodeId, std::function<void(PinHandle, PinEntry&)> fn);
    void forEachPin(
        int nodeId, std::function<void(PinHandle, const PinEntry&)> fn
    ) const;

    /**
     * Get all pin handles for a node.
     */
    std::vector<PinHandle> getPinsForNode(int nodeId) const;

    /**
     * Iterate over all valid pins in the registry.
     */
    void forEachPin(std::function<void(PinHandle, PinEntry&)> fn);
    void forEachPin(std::function<void(PinHandle, const PinEntry&)> fn) const;

    // ========================================================================
    // Serialization Support
    // ========================================================================

    /**
     * Set the next pin ID to use (for deserialization).
     * Call this before registering pins to ensure IDs don't conflict.
     */
    void setNextPinId(int id);

    /**
     * Get the current next pin ID.
     */
    int getNextPinId() const { return nextPinId; }

    // ========================================================================
    // Utility
    // ========================================================================

    /**
     * Clear all pins from the registry.
     */
    void clear();

    /**
     * Get the total number of valid pins.
     */
    size_t size() const;

    /**
     * Check if a pin handle is valid.
     */
    bool isValid(PinHandle handle) const;

private:
    // Slot-based storage with free list for O(1) operations
    std::vector<PinEntry> entries;
    std::vector<PinHandle> freeList;

    // Fast lookups
    std::unordered_map<ed::PinId, PinHandle> idToHandle;
    std::unordered_multimap<int, PinHandle> nodeToHandles;

    // ID generation
    int nextPinId = 1;
};
