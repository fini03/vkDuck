#include "multi_vertex_data_node.h"
#include "node_graph.h"
#include "vulkan_editor/util/logger.h"
#include <imgui.h>
#include <imgui_node_editor.h>

#include <vkDuck/model_loader.h>

#include "external/utilities/builders.h"
#include "external/utilities/widgets.h"

namespace {
constexpr const char* LOG_CATEGORY = "MultiVertexDataNode";
}

namespace ed = ax::NodeEditor;

MultiVertexDataNode::MultiVertexDataNode()
    : MultiModelNodeBase() {
    name = "Multi Vertex Data";
    createDefaultPins();
}

MultiVertexDataNode::MultiVertexDataNode(int id)
    : MultiModelNodeBase(id) {
    name = "Multi Vertex Data";
    createDefaultPins();
}

MultiVertexDataNode::~MultiVertexDataNode() = default;

void MultiVertexDataNode::createDefaultPins() {
    vertexDataPin.id = ed::PinId(GetNextGlobalId());
    vertexDataPin.type = PinType::VertexData;
    vertexDataPin.label = "Vertex data";
}

void MultiVertexDataNode::registerPins(PinRegistry& registry) {
    vertexDataPinHandle = registry.registerPinWithId(
        id,
        vertexDataPin.id,
        vertexDataPin.type,
        PinKind::Output,
        vertexDataPin.label
    );
    usesRegistry_ = true;
}

nlohmann::json MultiVertexDataNode::toJson() const {
    nlohmann::json j = MultiModelNodeBase::toJson();
    j["type"] = "multi_vertex_data";

    j["outputPins"] = nlohmann::json::array();
    j["outputPins"].push_back({
        {"id", vertexDataPin.id.Get()},
        {"type", static_cast<int>(vertexDataPin.type)},
        {"label", vertexDataPin.label}
    });

    return j;
}

void MultiVertexDataNode::fromJson(const nlohmann::json& j) {
    MultiModelNodeBase::fromJson(j);

    if (j.contains("outputPins") && j["outputPins"].is_array()) {
        auto& pins = j["outputPins"];
        if (pins.size() > 0) {
            vertexDataPin.id = ed::PinId(pins[0]["id"].get<int>());
        }
    }
}

void MultiVertexDataNode::render(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
    const NodeGraph& nodeGraph
) const {
    std::vector<std::string> pinLabels = {vertexDataPin.label};
    float nodeWidth = calculateMultiModelNodeWidth(name, pinLabels);

    renderMultiModelNodeHeader(builder, nodeWidth);

    // Draw output pin
    DrawOutputPin(
        vertexDataPin.id,
        vertexDataPin.label,
        static_cast<int>(vertexDataPin.type),
        nodeGraph.isPinLinked(vertexDataPin.id),
        nodeWidth,
        builder
    );

    builder.End();
    ed::PopStyleColor();
}

void MultiVertexDataNode::clearPrimitives() {
    vertexDataArray_ = {};
}

void MultiVertexDataNode::createPrimitives(primitives::Store& store) {
    const auto& ranges = getConsolidatedRanges();
    const auto& vertices = getConsolidatedVertices();
    const auto& indices = getConsolidatedIndices();

    if (ranges.empty()) {
        Log::warning(LOG_CATEGORY, "Cannot create primitives: no models loaded");
        return;
    }

    // Create vertex data array
    vertexDataArray_ = store.newArray();
    auto& vertexArray = store.arrays[vertexDataArray_.handle];
    vertexArray.type = primitives::Type::VertexData;
    vertexArray.handles.resize(ranges.size());

    for (size_t i = 0; i < ranges.size(); ++i) {
        const auto& range = ranges[i];

        primitives::StoreHandle hVertexData = store.newVertexData();
        primitives::VertexData& vertexData =
            store.vertexDatas[hVertexData.handle];

        size_t vertexSize = range.vertexCount * sizeof(Vertex);
        size_t indexSize = range.indexCount * sizeof(uint32_t);

        // Set up vertex data span
        auto* vertexDataPtr = reinterpret_cast<uint8_t*>(
            const_cast<Vertex*>(vertices.data() + range.firstVertex)
        );
        vertexData.vertexData = std::span<uint8_t>(vertexDataPtr, vertexSize);
        vertexData.vertexDataSize = vertexSize;
        vertexData.vertexCount = range.vertexCount;

        // Set up index data span
        auto* indexDataPtr =
            const_cast<uint32_t*>(indices.data() + range.firstIndex);
        vertexData.indexData =
            std::span<uint32_t>(indexDataPtr, range.indexCount);
        vertexData.indexDataSize = indexSize;
        vertexData.indexCount = range.indexCount;

        vertexData.bindingDescription = Vertex::getBindingDescription();
        vertexData.attributeDescriptions = Vertex::getAttributeDescriptions();

        // Use first model's path for file reference
        if (!models_.empty()) {
            vertexData.modelFilePath = models_[0].path;
        }
        vertexData.geometryIndex = static_cast<uint32_t>(i);

        vertexArray.handles[i] = hVertexData.handle;

        Log::debug(
            LOG_CATEGORY,
            "Created VertexData for range {}: {} verts, {} indices",
            i,
            range.vertexCount,
            range.indexCount
        );
    }

    Log::info(
        LOG_CATEGORY,
        "Created {} VertexData primitives from {} models",
        ranges.size(),
        models_.size()
    );
}

void MultiVertexDataNode::getOutputPrimitives(
    const primitives::Store& store,
    std::vector<std::pair<ax::NodeEditor::PinId, primitives::StoreHandle>>&
        outputs
) const {
    if (vertexDataArray_.isValid()) {
        outputs.push_back({vertexDataPin.id, vertexDataArray_});
    }
}
