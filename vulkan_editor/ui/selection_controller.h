#pragma once

#include "../graph/node.h"
#include <functional>
#include <vector>

namespace ui {

/// Manages node selection state in the pipeline editor.
/// Replaces the multiple typed selection pointers with a unified system.
///
/// Usage:
///   SelectionController selection;
///   selection.setSelected(node);
///
///   if (auto* pipeline = selection.getSelectedAs<PipelineNode>()) {
///       // Draw pipeline settings
///   }
class SelectionController {
public:
    using SelectionChangedCallback = std::function<void(Node*)>;

    /// Set the selected node
    void setSelected(Node* node) {
        if (selected != node) {
            selected = node;
            notifyListeners();
        }
    }

    /// Clear the selection
    void clearSelection() {
        setSelected(nullptr);
    }

    /// Get the selected node (or nullptr)
    Node* getSelected() const {
        return selected;
    }

    /// Check if anything is selected
    bool hasSelection() const {
        return selected != nullptr;
    }

    /// Get selected node cast to a specific type (returns nullptr if wrong type)
    template<typename T>
    T* getSelectedAs() const {
        return dynamic_cast<T*>(selected);
    }

    /// Check if the selected node is of a specific type
    template<typename T>
    bool isSelectedType() const {
        return dynamic_cast<T*>(selected) != nullptr;
    }

    /// Add a listener that's called when selection changes
    void addSelectionChangedListener(SelectionChangedCallback callback) {
        listeners.push_back(std::move(callback));
    }

    /// Clear all listeners
    void clearListeners() {
        listeners.clear();
    }

private:
    void notifyListeners() {
        for (const auto& listener : listeners) {
            listener(selected);
        }
    }

    Node* selected{nullptr};
    std::vector<SelectionChangedCallback> listeners;
};

} // namespace ui
