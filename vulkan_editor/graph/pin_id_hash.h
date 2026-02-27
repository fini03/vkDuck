#pragma once

#include <functional>
#include <imgui_node_editor.h>

namespace ed = ax::NodeEditor;

// ============================================================================
// Hash Specializations for ImGui Node Editor IDs
// Include this header before using unordered containers with PinId or LinkId
// ============================================================================

namespace std {

template <>
struct hash<ed::PinId> {
    std::size_t operator()(const ed::PinId& id) const noexcept {
        return std::hash<uintptr_t>()(id.Get());
    }
};

template <>
struct equal_to<ed::PinId> {
    bool operator()(const ed::PinId& lhs, const ed::PinId& rhs) const noexcept {
        return lhs == rhs;
    }
};

template <>
struct hash<ed::LinkId> {
    std::size_t operator()(const ed::LinkId& id) const noexcept {
        return std::hash<uintptr_t>()(id.Get());
    }
};

template <>
struct equal_to<ed::LinkId> {
    bool operator()(const ed::LinkId& lhs, const ed::LinkId& rhs) const noexcept {
        return lhs == rhs;
    }
};

}  // namespace std
