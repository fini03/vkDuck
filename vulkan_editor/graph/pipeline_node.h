#pragma once
#include "../shader/shader_types.h"
#include "camera_node.h"
#include "light_node.h"
#include "node.h"
#include "slang.h"
#include "vulkan/vulkan_core.h"
#include "vulkan_editor/io/serialization.h"
#include "vulkan_editor/gpu/primitives.h"
#include "vulkan_editor/ui/light_editor_ui.h"
#include "vulkan_editor/ui/pipeline_settings.h"
#include <fstream>
#include <iostream>
#include <slang-com-ptr.h>
#include <slang.h>
#include <string>
#include <unordered_map>

using namespace ShaderTypes;

struct GlobalSceneConfig;
class NodeGraph;
extern const std::vector<const char*> topologyOptions;
extern const std::array<VkPrimitiveTopology, 11> topologyOptionsEnum;

struct ProviderInfo {
    std::string provider;
    std::string imageViewMember;
    std::string samplerMember;
    std::string imageLayout;
};

struct SeparatedBindings {
    std::vector<BindingInfo> globalBindings;
    std::vector<BindingInfo> objectBindings;
};

/**
 * @class PipelineNode
 * @brief Represents a Vulkan graphics pipeline in the visual editor.
 *
 * Manages shader loading, reflection, pipeline settings (rasterization, blending,
 * depth testing, multisampling), and creates GPU pipeline primitives for rendering.
 * Automatically detects camera and light uniforms from shaders.
 */
class PipelineNode : public Node, public ISerializable {
public:
    PipelineSettings settings;
    ShaderParsedResult shaderReflection;
    Pin vertexDataPin;

    PipelineNode();
    PipelineNode(int id);
    void createDefaultPins();

    nlohmann::json toJson() const override;
    void fromJson(const nlohmann::json& j) override;
    void restorePinIds(
        const std::unordered_map<
            std::string,
            int>& inputPinIds,
        const std::unordered_map<
            std::string,
            int>& outputPinIds
    );

    void createPinsFromBindings(
        std::vector<BindingInfo>& bindings,
        NodeGraph& graph
    );
    ~PipelineNode() override;

    void render(
        ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
        const NodeGraph& graph
    ) const override;

    void DrawNodeHeader(float nodeWidth) const;

    static const std::vector<const char*> polygonModes;
    static const std::vector<const char*> cullModes;
    static const std::vector<const char*> frontFaceOptions;
    static const std::vector<const char*> depthCompareOptions;
    static const std::vector<const char*> sampleCountOptions;
    static const std::vector<const char*> colorWriteMaskNames;
    static const std::vector<const char*> logicOps;

    void reconcilePins(
        const std::vector<BindingInfo>& newBindings,
        NodeGraph& graph
    );

    PinType bindingInfoToPinType(const BindingInfo& binding);
    bool updateShaderReflection(NodeGraph& graph, const std::filesystem::path& projectRoot = {});

    std::vector<BindingInfo> mergeBindings(
        const std::vector<BindingInfo>& vertexBindings,
        const std::vector<BindingInfo>& fragmentBindings
    );

    std::string normalizeType(const std::string& slangType) {
        if (slangType.find("vector<float,4>") != std::string::npos ||
            slangType == "float4")
            return "vec4";
        if (slangType.find("vector<float,3>") != std::string::npos ||
            slangType == "float3")
            return "vec3";
        if (slangType.find("vector<float,2>") != std::string::npos ||
            slangType == "float2")
            return "vec2";
        if (slangType == "float")
            return "float";
        if (slangType.find("matrix<float,4,4>") != std::string::npos ||
            slangType == "float4x4")
            return "mat4";
        return "unknown";
    }

    struct DetectedCamera {
        std::string uniformName;
        std::string structName;
        std::vector<std::string> expectedMembers;
        bool useGlobal{false};
        Pin pin;
    };

    struct DetectedLight {
        std::string uniformName;
        std::string arrayMemberName;
        int arraySize{0};
        bool useGlobal{false};
        Pin pin;
    };

    bool hasCameraInput{false};
    DetectedCamera cameraInput;

    bool hasLightInput{false};
    DetectedLight lightInput;

    std::vector<DetectedCamera> detectedCameras;
    std::vector<DetectedLight> detectedLights;

    void clearPrimitives() override;
    void createPrimitives(primitives::Store& store) override;
    virtual void getOutputPrimitives(
        const primitives::Store& store,
        std::vector<std::pair<
            ax::NodeEditor::PinId,
            primitives::StoreHandle>>& outputs
    ) const override;
    virtual void getInputPrimitives(
        const primitives::Store& store,
        std::vector<std::pair<
            ax::NodeEditor::PinId,
            primitives::LinkSlot>>& inputs
    ) const override;

    primitives::StoreHandle pipelineHandle{};
    primitives::StoreHandle depthAttachmentHandle{};

protected:
    std::string getColorWriteMaskString(uint32_t mask) const;

public:
    bool isMainPipeline = false;
    mutable bool isShadowMap = false;
    mutable bool deferred = false;
};
