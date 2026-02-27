#include "pin_registry.h"
#include "../util/logger.h"
#include "node.h"

// ============================================================================
// Registration
// ============================================================================

PinHandle PinRegistry::registerPin(
    int nodeId,
    PinType type,
    PinKind kind,
    const std::string& label
) {
    // Generate a new editor ID using the global counter
    ed::PinId editorId(Node::GetNextGlobalId());
    return registerPinWithId(nodeId, editorId, type, kind, label);
}

PinHandle PinRegistry::registerPinWithId(
    int nodeId,
    ed::PinId editorId,
    PinType type,
    PinKind kind,
    const std::string& label
) {
    PinHandle handle;

    // Reuse a slot from free list if available
    if (!freeList.empty()) {
        handle = freeList.back();
        freeList.pop_back();
        entries[handle] = PinEntry{
            editorId, type, kind, label, nodeId, -1, -1, true
        };
    } else {
        // Allocate new slot
        handle = static_cast<PinHandle>(entries.size());
        entries.push_back(
            PinEntry{editorId, type, kind, label, nodeId, -1, -1, true}
        );
    }

    // Update indices
    idToHandle[editorId] = handle;
    nodeToHandles.emplace(nodeId, handle);

    Log::debug(
        "PinRegistry",
        "Registered pin '{}' (handle={}, editorId={}, nodeId={})",
        label,
        handle,
        static_cast<int>(editorId.Get()),
        nodeId
    );

    return handle;
}

void PinRegistry::unregisterPin(PinHandle handle) {
    if (handle >= entries.size() || !entries[handle].valid) {
        return;
    }

    PinEntry& entry = entries[handle];

    Log::debug(
        "PinRegistry",
        "Unregistering pin '{}' (handle={}, editorId={})",
        entry.label,
        handle,
        static_cast<int>(entry.id.Get())
    );

    // Remove from indices
    idToHandle.erase(entry.id);

    // Remove from node-to-handles multimap
    auto range = nodeToHandles.equal_range(entry.ownerNodeId);
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second == handle) {
            nodeToHandles.erase(it);
            break;
        }
    }

    // Mark as invalid and add to free list
    entry.valid = false;
    freeList.push_back(handle);
}

void PinRegistry::unregisterPinsForNode(int nodeId) {
    // Collect handles first (can't modify while iterating)
    std::vector<PinHandle> handles;
    auto range = nodeToHandles.equal_range(nodeId);
    for (auto it = range.first; it != range.second; ++it) {
        handles.push_back(it->second);
    }

    Log::debug(
        "PinRegistry",
        "Unregistering {} pins for node {}",
        handles.size(),
        nodeId
    );

    // Unregister each pin
    for (PinHandle handle : handles) {
        unregisterPin(handle);
    }
}

// ============================================================================
// Lookup
// ============================================================================

PinEntry* PinRegistry::get(PinHandle handle) {
    if (handle >= entries.size() || !entries[handle].valid) {
        return nullptr;
    }
    return &entries[handle];
}

const PinEntry* PinRegistry::get(PinHandle handle) const {
    if (handle >= entries.size() || !entries[handle].valid) {
        return nullptr;
    }
    return &entries[handle];
}

PinEntry* PinRegistry::findByEditorId(ed::PinId id) {
    auto it = idToHandle.find(id);
    if (it == idToHandle.end()) {
        return nullptr;
    }
    return get(it->second);
}

const PinEntry* PinRegistry::findByEditorId(ed::PinId id) const {
    auto it = idToHandle.find(id);
    if (it == idToHandle.end()) {
        return nullptr;
    }
    return get(it->second);
}

PinHandle PinRegistry::getHandleForEditorId(ed::PinId id) const {
    auto it = idToHandle.find(id);
    if (it == idToHandle.end()) {
        return INVALID_PIN_HANDLE;
    }
    return it->second;
}

// ============================================================================
// Node Lookup
// ============================================================================

int PinRegistry::getOwnerNodeId(PinHandle handle) const {
    const PinEntry* entry = get(handle);
    return entry ? entry->ownerNodeId : -1;
}

int PinRegistry::getOwnerNodeIdByEditorId(ed::PinId id) const {
    const PinEntry* entry = findByEditorId(id);
    return entry ? entry->ownerNodeId : -1;
}

// ============================================================================
// Iteration
// ============================================================================

void PinRegistry::forEachPin(
    int nodeId, std::function<void(PinHandle, PinEntry&)> fn
) {
    auto range = nodeToHandles.equal_range(nodeId);
    for (auto it = range.first; it != range.second; ++it) {
        PinHandle handle = it->second;
        if (entries[handle].valid) {
            fn(handle, entries[handle]);
        }
    }
}

void PinRegistry::forEachPin(
    int nodeId, std::function<void(PinHandle, const PinEntry&)> fn
) const {
    auto range = nodeToHandles.equal_range(nodeId);
    for (auto it = range.first; it != range.second; ++it) {
        PinHandle handle = it->second;
        if (entries[handle].valid) {
            fn(handle, entries[handle]);
        }
    }
}

std::vector<PinHandle> PinRegistry::getPinsForNode(int nodeId) const {
    std::vector<PinHandle> result;
    auto range = nodeToHandles.equal_range(nodeId);
    for (auto it = range.first; it != range.second; ++it) {
        if (entries[it->second].valid) {
            result.push_back(it->second);
        }
    }
    return result;
}

void PinRegistry::forEachPin(std::function<void(PinHandle, PinEntry&)> fn) {
    for (PinHandle i = 0; i < entries.size(); ++i) {
        if (entries[i].valid) {
            fn(i, entries[i]);
        }
    }
}

void PinRegistry::forEachPin(
    std::function<void(PinHandle, const PinEntry&)> fn
) const {
    for (PinHandle i = 0; i < entries.size(); ++i) {
        if (entries[i].valid) {
            fn(i, entries[i]);
        }
    }
}

// ============================================================================
// Serialization Support
// ============================================================================

void PinRegistry::setNextPinId(int id) {
    nextPinId = id;
    // Also update the global counter to avoid conflicts
    if (id > Node::s_GlobalIdCounter) {
        Node::SetNextGlobalId(id);
    }
}

// ============================================================================
// Utility
// ============================================================================

void PinRegistry::clear() {
    entries.clear();
    freeList.clear();
    idToHandle.clear();
    nodeToHandles.clear();
    nextPinId = 1;
}

size_t PinRegistry::size() const {
    return entries.size() - freeList.size();
}

bool PinRegistry::isValid(PinHandle handle) const {
    return handle < entries.size() && entries[handle].valid;
}
