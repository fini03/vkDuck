#define _USE_MATH_DEFINES
#include "pipeline_node.h"
#include "../util/logger.h"
#include "../shader/shader_reflection.h"
#include "node_graph.h"
#include "slang.h"
#include "vulkan_editor/gpu/primitives.h"
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <math.h>
#include <set>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan.h>

#include "external/utilities/builders.h"
#include "external/utilities/widgets.h"
#include <imgui.h>

namespace {
constexpr float PADDING_X = 10.0f;
}

static std::set<std::string> generatedGlobalTypes;

template <
    typename T,
    std::size_t N>
std::vector<const char*> createEnumStringList(
    const std::array<
        T,
        N>& enumValues,
    const char* (*stringFunc)(T)
) {
    std::vector<const char*> strings;
    strings.reserve(N);
    for (const auto& value : enumValues) {
        strings.push_back(stringFunc(value));
    }
    return strings;
}

constexpr std::array<VkPolygonMode, 4> polygonModesEnum{
    VK_POLYGON_MODE_FILL, VK_POLYGON_MODE_LINE, VK_POLYGON_MODE_POINT,
    VK_POLYGON_MODE_FILL_RECTANGLE_NV
};

constexpr std::array<VkCullModeFlagBits, 3> cullModesEnum{
    VK_CULL_MODE_NONE, VK_CULL_MODE_BACK_BIT, VK_CULL_MODE_FRONT_BIT
};

constexpr std::array<VkFrontFace, 2> frontFaceOptionsEnum{
    VK_FRONT_FACE_CLOCKWISE,
    VK_FRONT_FACE_COUNTER_CLOCKWISE,
};

constexpr std::array<VkCompareOp, 8> depthCompareOptionsEnum{
    VK_COMPARE_OP_NEVER,
    VK_COMPARE_OP_LESS,
    VK_COMPARE_OP_EQUAL,
    VK_COMPARE_OP_LESS_OR_EQUAL,
    VK_COMPARE_OP_GREATER,
    VK_COMPARE_OP_NOT_EQUAL,
    VK_COMPARE_OP_GREATER_OR_EQUAL,
    VK_COMPARE_OP_ALWAYS
};

constexpr std::array<VkSampleCountFlagBits, 7> sampleCountOptionsEnum{
    VK_SAMPLE_COUNT_1_BIT,  VK_SAMPLE_COUNT_2_BIT,
    VK_SAMPLE_COUNT_4_BIT,  VK_SAMPLE_COUNT_8_BIT,
    VK_SAMPLE_COUNT_16_BIT, VK_SAMPLE_COUNT_32_BIT,
    VK_SAMPLE_COUNT_64_BIT
};

constexpr std::array<VkLogicOp, 16> logicOpsEnum{
    VK_LOGIC_OP_CLEAR,         VK_LOGIC_OP_AND,
    VK_LOGIC_OP_AND_REVERSE,   VK_LOGIC_OP_COPY,
    VK_LOGIC_OP_AND_INVERTED,  VK_LOGIC_OP_NO_OP,
    VK_LOGIC_OP_XOR,           VK_LOGIC_OP_OR,
    VK_LOGIC_OP_NOR,           VK_LOGIC_OP_EQUIVALENT,
    VK_LOGIC_OP_INVERT,        VK_LOGIC_OP_OR_REVERSE,
    VK_LOGIC_OP_COPY_INVERTED, VK_LOGIC_OP_OR_INVERTED,
    VK_LOGIC_OP_NAND,          VK_LOGIC_OP_SET,
};

const std::vector<const char*> PipelineNode::polygonModes =
    createEnumStringList(polygonModesEnum, string_VkPolygonMode);

const std::vector<const char*> PipelineNode::cullModes =
    createEnumStringList(cullModesEnum, string_VkCullModeFlagBits);

const std::vector<const char*> PipelineNode::frontFaceOptions =
    createEnumStringList(frontFaceOptionsEnum, string_VkFrontFace);

const std::vector<const char*> PipelineNode::depthCompareOptions =
    createEnumStringList(depthCompareOptionsEnum, string_VkCompareOp);

const std::vector<const char*> PipelineNode::sampleCountOptions =
    createEnumStringList(
        sampleCountOptionsEnum, string_VkSampleCountFlagBits
    );

const std::vector<const char*> PipelineNode::logicOps =
    createEnumStringList(logicOpsEnum, string_VkLogicOp);

const std::vector<const char*> PipelineNode::colorWriteMaskNames = {
    "Red", "Green", "Blue", "Alpha"
};

namespace ed = ax::NodeEditor;

PipelineNode::PipelineNode()
    : Node() {

    createDefaultPins();
    // Default settings
    settings.lineWidth = 1.0f;
    settings.cullMode = 0;
    settings.frontFace = 1;

    // settings.depthTest = true;
    // settings.depthWrite = true;
}

PipelineNode::PipelineNode(int id)
    : Node(id) {

    createDefaultPins();
    // Default settings
    settings.lineWidth = 1.0f;
    settings.cullMode = 0;
    settings.frontFace = 1;
}

void PipelineNode::createDefaultPins() {
    if (!shaderReflection.vertexAttributes.empty()) {
        vertexDataPin.id = ed::PinId(GetNextGlobalId());
        vertexDataPin.type = PinType::VertexData;
        vertexDataPin.label = "Vertex data";
    }
}

PipelineNode::~PipelineNode() {}

nlohmann::json PipelineNode::toJson() const {
    nlohmann::json j;
    j["type"] = "pipeline";
    j["id"] = id;
    j["name"] = name;
    j["position"] = {Node::position.x, Node::position.y};
    j["isMainPipeline"] = isMainPipeline;
    j["settings"] = settings.toJson();

    // Store input pins (from shaderReflection.bindings)
    j["inputPins"] = nlohmann::json::array();
    for (const auto& binding : shaderReflection.bindings) {
        j["inputPins"].push_back(
            {{"id", binding.pin.id.Get()},
             {"type", static_cast<int>(binding.pin.type)},
             {"label", binding.pin.label}}
        );
    }

    // Store output pins (from attachmentConfigs)
    j["outputPins"] = nlohmann::json::array();
    for (const auto& config : shaderReflection.attachmentConfigs) {
        j["outputPins"].push_back(
            {{"id", config.pin.id.Get()},
             {"type", static_cast<int>(config.pin.type)},
             {"label", config.pin.label}}
        );
    }

    // Store extra pins (vertexDataPin, cameraInput, lightInput)
    // These are stored separately to ensure their IDs are tracked
    j["extraPins"] = nlohmann::json::array();
    if (vertexDataPin.id.Get() != 0) {
        j["extraPins"].push_back(
            {{"id", vertexDataPin.id.Get()},
             {"type", static_cast<int>(vertexDataPin.type)},
             {"label", vertexDataPin.label},
             {"pinKind", "vertexData"}}
        );
    }
    if (hasCameraInput && cameraInput.pin.id.Get() != 0) {
        j["extraPins"].push_back(
            {{"id", cameraInput.pin.id.Get()},
             {"type", static_cast<int>(cameraInput.pin.type)},
             {"label", cameraInput.pin.label},
             {"pinKind", "cameraInput"}}
        );
    }
    if (hasLightInput && lightInput.pin.id.Get() != 0) {
        j["extraPins"].push_back(
            {{"id", lightInput.pin.id.Get()},
             {"type", static_cast<int>(lightInput.pin.type)},
             {"label", lightInput.pin.label},
             {"pinKind", "lightInput"}}
        );
    }

    // Store attachment configs
    j["attachmentConfigs"] = nlohmann::json::array();
    for (const auto& config : shaderReflection.attachmentConfigs) {
        j["attachmentConfigs"].push_back(config.toJson());
    }

    return j;
}

void PipelineNode::fromJson(const nlohmann::json& j) {
    name = j.value("name", "Pipeline");
    if (j.contains("position") && j["position"].is_array() &&
        j["position"].size() == 2) {
        Node::position = ImVec2(
            j["position"][0].get<float>(), j["position"][1].get<float>()
        );
    }
    isMainPipeline = j.value("isMainPipeline", false);

    if (j.contains("settings")) {
        settings.fromJson(j["settings"]);
    }
}

void PipelineNode::restorePinIds(
    const std::unordered_map<
        std::string,
        int>& inputPinIds,
    const std::unordered_map<
        std::string,
        int>& outputPinIds
) {
    // Restore input pin IDs by label
    for (auto& binding : shaderReflection.bindings) {
        auto it = inputPinIds.find(binding.pin.label);
        if (it != inputPinIds.end()) {
            binding.pin.id = ed::PinId(it->second);
            Log::debug(
                "PipelineNode", "Restored input pin '{}' = {}",
                binding.pin.label, it->second
            );
        }
    }

    // Also update inputBindings for compatibility
    for (auto& binding : inputBindings) {
        auto it = inputPinIds.find(binding.pin.label);
        if (it != inputPinIds.end()) {
            binding.pin.id = ed::PinId(it->second);
        }
    }

    // Restore output pin IDs by label
    for (auto& config : shaderReflection.attachmentConfigs) {
        auto it = outputPinIds.find(config.pin.label);
        if (it != outputPinIds.end()) {
            config.pin.id = ed::PinId(it->second);
            Log::debug(
                "PipelineNode", "Restored output pin '{}' = {}",
                config.pin.label, it->second
            );
        }
    }

    // Restore vertexDataPin by label
    auto vertexIt = inputPinIds.find(vertexDataPin.label);
    if (vertexIt != inputPinIds.end()) {
        vertexDataPin.id = ed::PinId(vertexIt->second);
        Log::debug(
            "PipelineNode", "Restored vertexDataPin '{}' = {}",
            vertexDataPin.label, vertexIt->second
        );
    }

    // Restore cameraInput pin by label
    if (hasCameraInput) {
        auto cameraIt = inputPinIds.find(cameraInput.pin.label);
        if (cameraIt != inputPinIds.end()) {
            cameraInput.pin.id = ed::PinId(cameraIt->second);
            Log::debug(
                "PipelineNode", "Restored cameraInput pin '{}' = {}",
                cameraInput.pin.label, cameraIt->second
            );
        }
    }

    // Restore lightInput pin by label
    if (hasLightInput) {
        auto lightIt = inputPinIds.find(lightInput.pin.label);
        if (lightIt != inputPinIds.end()) {
            lightInput.pin.id = ed::PinId(lightIt->second);
            Log::debug(
                "PipelineNode", "Restored lightInput pin '{}' = {}",
                lightInput.pin.label, lightIt->second
            );
        }
    }
}

const char* getPinLabel(PinType type) {
    switch (type) {
    case PinType::UniformBuffer:
        return "Uniform Buffer";
    case PinType::Image:
        return "Image";
    case PinType::VertexData:
        return "Vertex data";
    default:
        return "Unknown";
    }
}

void PipelineNode::render(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
    const NodeGraph& graph
) const {
    // Collect all pin labels for width calculation
    std::vector<std::string> pinLabels;

    // Single camera pin (when detected and not using global)
    if (hasCameraInput && !cameraInput.useGlobal) {
        pinLabels.push_back(cameraInput.pin.label);
    }

    // Single light pin (when detected and not using global)
    if (hasLightInput && !lightInput.useGlobal) {
        pinLabels.push_back(lightInput.pin.label);
    }

    if (vertexDataPin.id.Get() != 0) {
        pinLabels.push_back(vertexDataPin.label);
    }

    for (const auto& binding : inputBindings) {
        pinLabels.push_back(binding.pin.label);
    }

    for (const auto& config : shaderReflection.attachmentConfigs) {
        pinLabels.push_back(config.pin.label);
    }

    float nodeWidth = CalculateNodeWidth(name, pinLabels);

    // Violet background for all nodes (semi-transparent)
    ed::PushStyleColor(
        ed::StyleColor_NodeBg, ImColor(138, 43, 226, 80)
    );

    builder.Begin(id);

    // Draw header - blue for all pipeline nodes
    builder.Header(ImColor(65, 105, 225));

    // Draw node name (with editing capability)
    DrawNodeHeader(nodeWidth);

    ImGui::Spring(1);
    ImGui::Dummy(ImVec2(0, 28));
    ImGui::Spring(0);
    builder.EndHeader();

    // Draw single camera pin (when detected and not using global)
    if (hasCameraInput && !cameraInput.useGlobal) {
        DrawInputPin(
            cameraInput.pin.id, cameraInput.pin.label,
            static_cast<int>(cameraInput.pin.type),
            graph.isPinLinked(cameraInput.pin.id), nodeWidth, builder
        );
    }

    // Draw single light pin (when detected and not using global)
    if (hasLightInput && !lightInput.useGlobal) {
        DrawInputPin(
            lightInput.pin.id, lightInput.pin.label,
            static_cast<int>(lightInput.pin.type),
            graph.isPinLinked(lightInput.pin.id), nodeWidth, builder
        );
    }

    // Draw vertex input pin
    if (vertexDataPin.id.Get() != 0) {
        DrawInputPin(
            vertexDataPin.id, vertexDataPin.label.c_str(),
            static_cast<int>(vertexDataPin.type),
            graph.isPinLinked(vertexDataPin.id), nodeWidth, builder
        );
    }

    // Draw input bindings
    for (const auto& binding : inputBindings) {
        DrawInputPin(
            binding.pin.id, binding.pin.label,
            static_cast<int>(binding.pin.type),
            graph.isPinLinked(binding.pin.id), nodeWidth, builder
        );
    }

    // Draw output attachments
    for (const auto& config : shaderReflection.attachmentConfigs) {
        DrawOutputPin(
            config.pin.id, config.pin.label,
            static_cast<int>(config.pin.type),
            graph.isPinLinked(config.pin.id), nodeWidth, builder
        );
    }

    builder.End();
    ed::PopStyleColor();
}

void PipelineNode::DrawNodeHeader(float nodeWidth) const {
    float availWidth = nodeWidth - PADDING_X * 2.0f;
    ImVec2 textSize = ImGui::CalcTextSize(name.c_str(), nullptr, false);

    if (!isRenaming) {
        // Center text if it fits
        if (textSize.x < availWidth) {
            float centerOffset = (availWidth - textSize.x) * 0.5f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centerOffset);
        }

        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availWidth);
        ImGui::TextUnformatted(name.c_str());
        ImGui::PopTextWrapPos();

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            const_cast<PipelineNode*>(this)->isRenaming = true;
        }
    } else {
        // Editable name
        char nameBuffer[128];
        strncpy(nameBuffer, name.c_str(), sizeof(nameBuffer));
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';

        ImGui::SetNextItemWidth(nodeWidth - PADDING_X);

        bool wrapText = (textSize.x + PADDING_X * 2.0f) > nodeWidth;
        if (wrapText) {
            ImGui::InputTextMultiline(
                "##NodeName", nameBuffer, sizeof(nameBuffer),
                ImVec2(-FLT_MIN, ImGui::GetTextLineHeight()),
                ImGuiInputTextFlags_AutoSelectAll
            );
        } else {
            ImGui::InputText(
                "##NodeName", nameBuffer, sizeof(nameBuffer),
                ImGuiInputTextFlags_AutoSelectAll
            );
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const_cast<PipelineNode*>(this)->name = nameBuffer;
            const_cast<PipelineNode*>(this)->isRenaming = false;
        }
    }
}

std::string PipelineNode::getColorWriteMaskString(uint32_t mask) const {
    std::string result;
    bool first = true;

    if (mask & VK_COLOR_COMPONENT_R_BIT) {
        result += "VK_COLOR_COMPONENT_R_BIT";
        first = false;
    }
    if (mask & VK_COLOR_COMPONENT_G_BIT) {
        if (!first)
            result += " | ";
        result += "VK_COLOR_COMPONENT_G_BIT";
        first = false;
    }
    if (mask & VK_COLOR_COMPONENT_B_BIT) {
        if (!first)
            result += " | ";
        result += "VK_COLOR_COMPONENT_B_BIT";
        first = false;
    }
    if (mask & VK_COLOR_COMPONENT_A_BIT) {
        if (!first)
            result += " | ";
        result += "VK_COLOR_COMPONENT_A_BIT";
    }

    // If no bits are set, default to "0"
    if (result.empty()) {
        result = "0";
    }

    return result;
}

std::vector<BindingInfo> PipelineNode::mergeBindings(
    const std::vector<BindingInfo>& vertexBindings,
    const std::vector<BindingInfo>& fragmentBindings
) {
    std::map<std::tuple<int, int, std::string>, BindingInfo> merged;

    auto merge = [&](const BindingInfo& b) {
        // Key now includes resourceName to keep separate UBOs
        // distinct
        auto key = std::make_tuple(
            b.vulkanSet, b.vulkanBinding, b.resourceName
        );
        auto it = merged.find(key);
        if (it != merged.end()) {
            // Same resource at same binding - merge stage flags
            // KEEP the original typeName (struct name)!
            it->second.stageFlags |=
                b.stageFlags; // Bitwise OR to combine flags

            Log::debug(
                "Pipeline",
                "Merging stage flags for {} at set {}, binding {}: {}",
                b.resourceName, b.vulkanSet, b.vulkanBinding,
                ShaderReflection::shaderStageToString(
                    it->second.stageFlags
                )
            );
        } else {
            // New binding - add it
            merged[key] = b;
        }
    };

    for (const auto& b : vertexBindings)
        merge(b);
    for (const auto& b : fragmentBindings)
        merge(b);

    std::vector<BindingInfo> result;
    for (auto& [_, binding] : merged)
        result.push_back(std::move(binding));

    Log::debug(
        "Pipeline", "Merged bindings result: {} unique bindings",
        result.size()
    );
    for (const auto& b : result) {
        Log::debug(
            "Pipeline", "  - {} at set={} binding={} type={} stages={}",
            b.resourceName, b.vulkanSet, b.vulkanBinding, b.typeName,
            ShaderReflection::shaderStageToString(b.stageFlags)
        );
    }

    return result;
}

PinType PipelineNode::bindingInfoToPinType(const BindingInfo& binding) {
    if (binding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
        return PinType::UniformBuffer;
    }

    if (binding.descriptorType ==
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        return PinType::Image;

    if (binding.typeName == "ParameterBlock")
        return PinType::UniformBuffer;

    if (binding.typeName == "_Texture")
        return PinType::Image;

    Log::warning(
        "Pipeline",
        "Unknown binding type - typeName: {}, descriptorType: {}",
        binding.typeName, static_cast<int>(binding.descriptorType)
    );
    return PinType::Unknown;
}

void PipelineNode::reconcilePins(
    const std::vector<BindingInfo>& newBindings,
    NodeGraph& graph
) {
    // Map existing pin labels to their Pin data to preserve connections
    std::unordered_map<std::string, Pin> oldPins;

    if (vertexDataPin.id.Get() != 0) {
        oldPins[vertexDataPin.label] = vertexDataPin;
    }

    // Harvest old pins from previous bindings and attachments
    for (const auto& b : inputBindings)
        oldPins[b.pin.label] = b.pin;
    for (const auto& config : shaderReflection.attachmentConfigs)
        oldPins[config.pin.label] = config.pin;

    // Also harvest camera/light pins to preserve connections
    if (cameraInput.pin.id.Get() != 0) {
        oldPins[cameraInput.pin.label] = cameraInput.pin;
        Log::debug(
            "Pipeline", "Harvested camera pin ID: {}",
            cameraInput.pin.id.Get()
        );
    }
    if (lightInput.pin.id.Get() != 0) {
        oldPins[lightInput.pin.label] = lightInput.pin;
        Log::debug(
            "Pipeline", "Harvested light pin ID: {}",
            lightInput.pin.id.Get()
        );
    }

    // Preserve single camera pin
    if (hasCameraInput) {
        auto it = oldPins.find(cameraInput.pin.label);
        if (it != oldPins.end()) {
            cameraInput.pin = it->second;
            Log::debug(
                "Pipeline", "Reusing camera pin ID: {}",
                cameraInput.pin.id.Get()
            );
        }
    }

    // Preserve single light pin
    if (hasLightInput) {
        auto it = oldPins.find(lightInput.pin.label);
        if (it != oldPins.end()) {
            lightInput.pin = it->second;
            Log::debug(
                "Pipeline", "Reusing light pin ID: {}",
                lightInput.pin.id.Get()
            );
        }
    }

    // We will rebuild inputBindings
    inputBindings.clear();
    outputBindings.clear();

    // Handle vertex data pin - only show if shader has vertex inputs (VSInput struct)
    if (!shaderReflection.vertexAttributes.empty()) {
        auto it = oldPins.find("Vertex data");
        if (it != oldPins.end()) {
            vertexDataPin = it->second;
            Log::debug(
                "Pipeline", "Reusing Vertex data pin ID: {}",
                vertexDataPin.id.Get()
            );
        } else {
            vertexDataPin.id = ed::PinId(Node::GetNextGlobalId());
            vertexDataPin.type = PinType::VertexData;
            vertexDataPin.label = "Vertex data";
        }
    } else {
        // No vertex inputs in shader - clear the vertex data pin
        vertexDataPin = Pin{};
        Log::debug("Pipeline", "No vertex inputs found - vertex data pin cleared");
    }

    // Process bindings - REUSE existing pin IDs when possible
    for (auto& binding : shaderReflection.bindings) {
        // Skip bindings that are handled by special camera/light inputs
        // to avoid duplicate pins
        if (hasCameraInput &&
            binding.resourceName == cameraInput.uniformName) {
            std::cout << "Skipping binding '" << binding.resourceName
                      << "' - handled by cameraInput pin" << std::endl;
            continue;
        }
        if (hasLightInput &&
            binding.resourceName == lightInput.uniformName) {
            std::cout << "Skipping binding '" << binding.resourceName
                      << "' - handled by lightInput pin" << std::endl;
            continue;
        }

        Pin& pin = binding.pin;
        pin.label = binding.resourceName;
        PinType newType = bindingInfoToPinType(binding);

        // Check if we have an existing pin with the same label
        auto it = oldPins.find(pin.label);
        if (it != oldPins.end() && it->second.type == newType) {
            // Reuse the existing pin ID to preserve connections
            pin.id = it->second.id;
            pin.type = it->second.type;
            Log::debug(
                "Pipeline", "Reusing pin ID for {} (ID: {})", pin.label,
                pin.id.Get()
            );
        } else {
            // Create new pin only if label changed or type is
            // incompatible
            pin.id = ed::PinId(Node::GetNextGlobalId());
            pin.type = newType;
            Log::debug(
                "Pipeline", "Creating new pin ID for {} (ID: {})",
                pin.label, pin.id.Get()
            );
        }

        if (binding.isInput)
            inputBindings.push_back(binding);
        else
            outputBindings.push_back(binding);
    }

    // Process attachment configs - REUSE existing pin IDs when possible
    for (auto& config : shaderReflection.attachmentConfigs) {
        Pin& pin = config.pin;
        pin.label = config.name;

        // Check if we have an existing pin with the same label
        auto it = oldPins.find(pin.label);
        if (it != oldPins.end() && it->second.type == PinType::Image) {
            // Reuse the existing pin ID to preserve connections
            pin.id = it->second.id;
            pin.type = PinType::Image;
            Log::debug(
                "Pipeline", "Reusing attachment pin ID for {} (ID: {})",
                pin.label, pin.id.Get()
            );
        } else {
            // Create new pin
            pin.id = ed::PinId(Node::GetNextGlobalId());
            pin.type = PinType::Image;
            Log::debug(
                "Pipeline",
                "Creating new attachment pin ID for {} (ID: {})",
                pin.label, pin.id.Get()
            );
        }
    }

    // Remove all links where either pin no longer exists
    graph.removeInvalidLinks();
}

bool PipelineNode::updateShaderReflection(
    NodeGraph& graph,
    const std::filesystem::path& projectRoot
) {
    // removeOrphanedLinks(graph);

    ShaderParsedResult vertexResult;
    ShaderParsedResult fragmentResult;

    // Compile vertex shader first (using project-relative path)
    if (!settings.vertexShaderPath.empty()) {
        // Compute absolute path from project root for shader loading
        std::filesystem::path shaderPath = settings.vertexShaderPath;
        if (!projectRoot.empty()) {
            shaderPath = projectRoot / settings.vertexShaderPath;
        }

        vertexResult = ShaderReflection::reflectShader(
            shaderPath, SLANG_STAGE_VERTEX, projectRoot
        );

        // Check for compilation errors
        if (!vertexResult.success) {
            Log::error(
                "Shader",
                "Vertex shader compilation failed for pipeline '{}': "
                "{}",
                name,
                vertexResult.errorMessage.empty()
                    ? "Unknown error"
                    : vertexResult.errorMessage
            );
            return false; // Don't update pipeline state on syntax error
        }

        if (!vertexResult.warningMessage.empty()) {
            Log::warning(
                "Shader",
                "Vertex shader warnings for pipeline '{}': {}",
                name,
                vertexResult.warningMessage
            );
        }
    }

    // Compile fragment shader (using project-relative path)
    if (!settings.fragmentShaderPath.empty()) {
        // Compute absolute path from project root for shader loading
        std::filesystem::path shaderPath = settings.fragmentShaderPath;
        if (!projectRoot.empty()) {
            shaderPath = projectRoot / settings.fragmentShaderPath;
        }

        fragmentResult = ShaderReflection::reflectShader(
            shaderPath, SLANG_STAGE_FRAGMENT, projectRoot
        );

        // Check for compilation errors
        if (!fragmentResult.success) {
            Log::error(
                "Shader",
                "Fragment shader compilation failed for pipeline '{}': "
                "{}",
                name,
                fragmentResult.errorMessage.empty()
                    ? "Unknown error"
                    : fragmentResult.errorMessage
            );
            return false; // Don't update pipeline state on syntax error
        }

        if (!fragmentResult.warningMessage.empty()) {
            Log::warning(
                "Shader",
                "Fragment shader warnings for pipeline '{}': {}",
                name,
                fragmentResult.warningMessage
            );
        }
    }

    // Both shaders compiled successfully - now update the pipeline
    // state
    shaderReflection.bindings.clear();
    shaderReflection.outputs.clear();
    shaderReflection.vertexCode.clear();
    shaderReflection.fragmentCode.clear();

    // Apply vertex shader results
    if (vertexResult.success) {
        shaderReflection.vertexAttributes =
            vertexResult.vertexAttributes;
        shaderReflection.vertexCode = std::move(vertexResult.code);
        shaderReflection.vertexEntryPoint = vertexResult.entryPointName;

        // Log detected lights
        if (!vertexResult.lightStructs.empty()) {
            Log::debug(
                "Shader",
                "Detected {} light struct(s) in vertex shader",
                vertexResult.lightStructs.size()
            );
            for (const auto& lightStruct : vertexResult.lightStructs) {
                Log::debug(
                    "Shader", "  - {} with {} lights",
                    lightStruct.instanceName, lightStruct.arraySize
                );
            }
        }
    }

    // Apply fragment shader results
    if (fragmentResult.success) {
        shaderReflection.fragmentCode = std::move(fragmentResult.code);
        shaderReflection.fragmentEntryPoint =
            fragmentResult.entryPointName;

        // Log detected lights
        if (!fragmentResult.lightStructs.empty()) {
            Log::debug(
                "Shader",
                "Detected {} light struct(s) in fragment shader",
                fragmentResult.lightStructs.size()
            );
            for (const auto& lightStruct :
                 fragmentResult.lightStructs) {
                Log::debug(
                    "Shader", "  - {} with {} lights",
                    lightStruct.instanceName, lightStruct.arraySize
                );
            }
        }
    }

    // Merge descriptor bindings
    shaderReflection.bindings =
        mergeBindings(vertexResult.bindings, fragmentResult.bindings);

    // Merge camera structs from vertex and fragment shaders
    shaderReflection.cameraStructs.clear();
    shaderReflection.cameraStructs.insert(
        shaderReflection.cameraStructs.end(),
        vertexResult.cameraStructs.begin(),
        vertexResult.cameraStructs.end()
    );
    shaderReflection.cameraStructs.insert(
        shaderReflection.cameraStructs.end(),
        fragmentResult.cameraStructs.begin(),
        fragmentResult.cameraStructs.end()
    );

    // Merge light structs from vertex and fragment shaders
    shaderReflection.lightStructs.clear();
    Log::debug(
        "Shader",
        "Merging lights - vertexResult has {} lights, fragmentResult "
        "has {}",
        vertexResult.lightStructs.size(),
        fragmentResult.lightStructs.size()
    );
    for (const auto& ls : vertexResult.lightStructs) {
        Log::debug(
            "Shader", "  vertexResult light: {} arraySize={}",
            ls.instanceName, ls.arraySize
        );
    }
    for (const auto& ls : fragmentResult.lightStructs) {
        Log::debug(
            "Shader", "  fragmentResult light: {} arraySize={}",
            ls.instanceName, ls.arraySize
        );
    }
    shaderReflection.lightStructs.insert(
        shaderReflection.lightStructs.end(),
        vertexResult.lightStructs.begin(),
        vertexResult.lightStructs.end()
    );
    shaderReflection.lightStructs.insert(
        shaderReflection.lightStructs.end(),
        fragmentResult.lightStructs.begin(),
        fragmentResult.lightStructs.end()
    );

    Log::debug(
        "Shader", "Merged structs - Cameras: {}, Lights: {}",
        shaderReflection.cameraStructs.size(),
        shaderReflection.lightStructs.size()
    );

    // Single camera input - create if shader has camera struct named
    // "camera" (not "lightViewProj" or other camera-type structs used
    // for different purposes)
    const StructInfo* mainCameraStruct = nullptr;
    for (const auto& cs : shaderReflection.cameraStructs) {
        std::string nameLower = cs.instanceName;
        std::transform(
            nameLower.begin(), nameLower.end(), nameLower.begin(),
            ::tolower
        );
        // Only use as camera input if instance name is "camera"
        // (not lightViewProj, shadowCamera, etc.)
        if (nameLower == "camera") {
            mainCameraStruct = &cs;
            break;
        }
    }
    hasCameraInput = (mainCameraStruct != nullptr);
    if (hasCameraInput) {
        const auto& cameraStruct = *mainCameraStruct;

        // Preserve existing pin ID if we have one, otherwise create new
        if (cameraInput.pin.id.Get() == 0) {
            cameraInput.pin.id =
                ax::NodeEditor::PinId(GetNextGlobalId());
        }
        cameraInput.pin.type =
            PinType::UniformBuffer; // Camera data is a UBO
        cameraInput.pin.label = cameraStruct.instanceName;

        cameraInput.uniformName = cameraStruct.instanceName;
        cameraInput.structName = cameraStruct.structName;
        cameraInput.useGlobal =
            false; // Default: require connection (pin visible)

        cameraInput.expectedMembers.clear();
        for (const auto& member : cameraStruct.members) {
            cameraInput.expectedMembers.push_back(member.name);
        }

        Log::debug(
            "Shader",
            "Detected camera input: {} (pin visible, requires "
            "connection)",
            cameraInput.uniformName
        );
    }

    // Single light input - create if shader has light struct named
    // "lights" or "light"
    const StructInfo* mainLightStruct = nullptr;
    for (const auto& ls : shaderReflection.lightStructs) {
        std::string nameLower = ls.instanceName;
        std::transform(
            nameLower.begin(), nameLower.end(), nameLower.begin(),
            ::tolower
        );
        // Only use as light input if instance name is "lights" or
        // "light"
        if (nameLower == "lights" || nameLower == "light") {
            mainLightStruct = &ls;
            break;
        }
    }
    hasLightInput = (mainLightStruct != nullptr);
    if (hasLightInput) {
        const auto& lightStruct = *mainLightStruct;

        Log::debug(
            "Shader",
            "Light setup - current pin.id={}, shader arraySize={}",
            lightInput.pin.id.Get(), lightStruct.arraySize
        );

        // Preserve existing pin ID if we have one, otherwise create new
        if (lightInput.pin.id.Get() == 0) {
            lightInput.pin.id =
                ax::NodeEditor::PinId(GetNextGlobalId());
            Log::debug(
                "Shader", "Created new light pin ID: {}",
                lightInput.pin.id.Get()
            );
        } else {
            Log::debug(
                "Shader", "Preserving existing light pin ID: {}",
                lightInput.pin.id.Get()
            );
        }
        lightInput.pin.type =
            PinType::UniformBuffer; // Light data is a UBO
        lightInput.pin.label = lightStruct.instanceName;

        lightInput.arrayMemberName = lightStruct.instanceName;
        lightInput.arraySize = lightStruct.arraySize;

        // Find the actual shader binding that contains this light data.
        // The detected struct may be nested (e.g., Light inside LightsUBO),
        // so we need the top-level binding name for pin deduplication.
        lightInput.uniformName = lightStruct.instanceName;
        for (const auto& binding : shaderReflection.bindings) {
            // Direct match: binding type IS the light struct
            if (binding.typeName == lightStruct.structName) {
                lightInput.uniformName = binding.resourceName;
                break;
            }
            // Indirect match: binding has a member of the light struct type
            bool found = false;
            for (const auto& member : binding.members) {
                if (member.typeName == lightStruct.structName) {
                    lightInput.uniformName = binding.resourceName;
                    lightInput.arraySize = member.arraySize;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        lightInput.useGlobal =
            false; // Default: require connection (pin visible)

        Log::debug(
            "Shader",
            "Detected light input: {} with {} lights (pin visible, "
            "requires connection)",
            lightInput.uniformName, lightInput.arraySize
        );
    }

    // Legacy vectors - kept for backward compatibility
    detectedCameras.clear();
    detectedLights.clear();

    // Store outputs (only fragment shader typically has them)
    if (!fragmentResult.outputs.empty())
        shaderReflection.outputs = fragmentResult.outputs;

    std::vector<AttachmentConfig> newConfigs;
    for (const auto& output : shaderReflection.outputs) {
        // Find existing config
        auto it = std::find_if(
            shaderReflection.attachmentConfigs.begin(),
            shaderReflection.attachmentConfigs.end(),
            [&](const AttachmentConfig& c) {
                return c.name == output.name;
            }
        );

        if (it != shaderReflection.attachmentConfigs.end()) {
            newConfigs.push_back(*it);
        } else {
            AttachmentConfig newConfig;
            newConfig.name = output.name;
            newConfig.semantic = output.semantic;
            // Set smart defaults based on name/semantic (position, normal, etc.)
            newConfig.initializeDefaultsFromSemantic();
            Log::debug(
                "Shader",
                "Creating new attachment config for output '{}' with "
                "semantic '{}', format {}",
                output.name, output.semantic, static_cast<int>(newConfig.format)
            );
            newConfigs.push_back(newConfig);
        }
    }
    shaderReflection.attachmentConfigs = newConfigs;
    // Note: Do NOT call createDefaultPins() here - it would overwrite
    // pin IDs that reconcilePins() needs to preserve for existing node
    // connections
    reconcilePins(shaderReflection.bindings, graph);

    // Sync connected LightNode's count when shader is updated
    if (hasLightInput && lightInput.arraySize > 0) {
        Log::debug(
            "Shader",
            "Looking for connected LightNode - lightInput.pin.id={}, "
            "arraySize={}, links count={}",
            lightInput.pin.id.Get(), lightInput.arraySize,
            graph.links.size()
        );

        // Find if there's a LightNode connected to our light input pin
        bool foundLink = false;
        for (const auto& link : graph.links) {
            Log::debug(
                "Shader", "  Checking link: start={}, end={}",
                link.startPin.Get(), link.endPin.Get()
            );

            if (link.endPin == lightInput.pin.id) {
                foundLink = true;
                auto startResult = graph.findPin(link.startPin);
                if (auto* lightNode =
                        dynamic_cast<LightNode*>(startResult.node)) {
                    Log::debug(
                        "Shader",
                        "  Found connected LightNode with {} lights",
                        lightNode->numLights
                    );
                    if (lightNode->numLights != lightInput.arraySize) {
                        Log::info(
                            "Shader",
                            "Shader updated: syncing LightNode count "
                            "from {} to {}",
                            lightNode->numLights, lightInput.arraySize
                        );
                        lightNode->numLights = lightInput.arraySize;
                        lightNode->shaderControlledCount = true;
                        lightNode->ensureLightCount();
                    }
                } else {
                    Log::debug(
                        "Shader", "  Link found but not a LightNode"
                    );
                }
                break;
            }
        }
        if (!foundLink) {
            Log::debug(
                "Shader", "  No link found matching lightInput.pin.id"
            );
        }
    }

    return true; // Shader compilation and reflection succeeded
}

void PipelineNode::clearPrimitives() {
    for (auto& config : shaderReflection.attachmentConfigs) {
        config.handle = {};
    }
    pipelineHandle = {};
    depthAttachmentHandle = {};
}

void PipelineNode::createPrimitives(primitives::Store& store) {
    std::vector<primitives::StoreHandle> renderPassAttachments;

    // Skip creating primitives if shader code is missing (e.g., due to
    // syntax errors)
    if (shaderReflection.vertexCode.empty() ||
        shaderReflection.fragmentCode.empty()) {
        Log::warning(
            "Pipeline",
            "Skipping primitive creation for '{}': missing shader code",
            name
        );
        return;
    }

    // Generate all attachments based on shader outputs
    for (auto& config : shaderReflection.attachmentConfigs) {
        // Skip if handle is already valid (e.g., from a previous failed
        // reload)
        if (config.handle.isValid()) {
            Log::warning(
                "Pipeline",
                "Attachment config '{}' already has a valid handle, "
                "skipping",
                config.semantic
            );
            continue;
        }

        primitives::StoreHandle hImageArray = store.newArray();
        primitives::StoreHandle hImage = store.newImage();
        store.arrays[hImageArray.handle].type = primitives::Type::Image;
        store.arrays[hImageArray.handle].handles = {hImage.handle};
        primitives::Image& image = store.images[hImage.handle];

        image.imageInfo.format = config.format;
        image.imageInfo.extent.width = settings.extentConfig.width;
        image.imageInfo.extent.height = settings.extentConfig.height;
        image.imageInfo.extent.depth = 1;
        image.extentType = settings.extentConfig.type;

        primitives::StoreHandle hAttachment = store.newAttachment();
        primitives::Attachment& attachment =
            store.attachments[hAttachment.handle];
        attachment.image = hImage;
        attachment.colorBlending = config.colorBlending;
        attachment.clearValue = config.clearValue;

        if (config.semantic == "SV_DEPTH") {
            image.imageInfo.usage |=
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            image.viewInfo.subresourceRange.aspectMask =
                VK_IMAGE_ASPECT_DEPTH_BIT;
        } else {
            image.imageInfo.usage |=
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            image.viewInfo.subresourceRange.aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT;
        }

        config.handle = hImageArray;
        renderPassAttachments.push_back(hAttachment);
    }

    // Check if shader already specifies depth output
    bool shaderHasDepth = false;
    for (const auto& config : shaderReflection.attachmentConfigs) {
        if (config.semantic == "SV_DEPTH") {
            shaderHasDepth = true;
            break;
        }
    }

    // Create depth attachment if user enabled it and shader doesn't
    // already have one
    if (settings.depthEnabled && !shaderHasDepth) {
        primitives::StoreHandle hImageArray = store.newArray();
        primitives::StoreHandle hImage = store.newImage();
        store.arrays[hImageArray.handle].type = primitives::Type::Image;
        store.arrays[hImageArray.handle].handles = {hImage.handle};
        primitives::Image& image = store.images[hImage.handle];

        image.imageInfo.format = settings.depthFormat;
        image.imageInfo.extent.width = settings.extentConfig.width;
        image.imageInfo.extent.height = settings.extentConfig.height;
        image.imageInfo.extent.depth = 1;
        image.extentType = settings.extentConfig.type;
        image.imageInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        image.viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

        primitives::StoreHandle hAttachment = store.newAttachment();
        primitives::Attachment& attachment =
            store.attachments[hAttachment.handle];
        attachment.image = hImage;
        attachment.clearValue.depthStencil.depth = settings.depthClearValue;
        attachment.clearValue.depthStencil.stencil = settings.stencilClearValue;

        // Store handle for the user-created depth attachment
        depthAttachmentHandle = hImageArray;
        renderPassAttachments.push_back(hAttachment);

        Log::debug(
            "Pipeline",
            "Created user-enabled depth attachment with format {}",
            static_cast<int>(settings.depthFormat)
        );
    }

    primitives::StoreHandle renderPass = store.newRenderPass();
    store.renderPasses[renderPass.handle].attachments =
        renderPassAttachments;

    primitives::StoreHandle hVertexShader = store.newShader();
    auto& vertexShader = store.shaders[hVertexShader.handle];
    vertexShader.name = std::format(
        "{}_{}", settings.vertexShaderPath.stem().string(),
        hVertexShader.handle
    );
    vertexShader.code = shaderReflection.vertexCode;
    vertexShader.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexShader.entryPoint = shaderReflection.vertexEntryPoint.empty()
                                  ? "main"
                                  : shaderReflection.vertexEntryPoint;

    primitives::StoreHandle hFragmentShader = store.newShader();
    auto& fragmentShader = store.shaders[hFragmentShader.handle];
    fragmentShader.name = std::format(
        "{}_{}", settings.fragmentShaderPath.stem().string(),
        hFragmentShader.handle
    );
    fragmentShader.code = shaderReflection.fragmentCode;
    fragmentShader.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentShader.entryPoint =
        shaderReflection.fragmentEntryPoint.empty()
            ? "main"
            : shaderReflection.fragmentEntryPoint;

    primitives::StoreHandle hPipeline = store.newPipeline();
    primitives::Pipeline& pipeline = store.pipelines[hPipeline.handle];

    pipelineHandle = hPipeline;
    pipeline.renderPass = renderPass;
    pipeline.shaders = {hVertexShader, hFragmentShader};

    // pipeline.inputAssembly.topology =
    //     ModelNode::topologyOptionsEnum[settings.inputAssembly];
    pipeline.inputAssembly.primitiveRestartEnable =
        settings.primitiveRestart ? VK_TRUE : VK_FALSE;

    pipeline.rasterizer.depthClampEnable =
        settings.depthClamp ? VK_TRUE : VK_FALSE;
    pipeline.rasterizer.rasterizerDiscardEnable =
        settings.rasterizerDiscard ? VK_TRUE : VK_FALSE;
    pipeline.rasterizer.polygonMode =
        polygonModesEnum[settings.polygonMode];
    pipeline.rasterizer.cullMode = cullModesEnum[settings.cullMode];
    pipeline.rasterizer.frontFace =
        frontFaceOptionsEnum[settings.frontFace];
    pipeline.rasterizer.depthBiasEnable =
        settings.depthBiasEnabled ? VK_TRUE : VK_FALSE;
    pipeline.rasterizer.depthBiasConstantFactor =
        settings.depthBiasConstantFactor;
    pipeline.rasterizer.depthBiasClamp = settings.depthBiasClamp;
    pipeline.rasterizer.depthBiasSlopeFactor =
        settings.depthBiasSlopeFactor;
    pipeline.rasterizer.lineWidth = settings.lineWidth;

    pipeline.multisampling.rasterizationSamples =
        sampleCountOptionsEnum[settings.rasterizationSamples];
    pipeline.multisampling.sampleShadingEnable =
        settings.sampleShading ? VK_TRUE : VK_FALSE;

    pipeline.depthStencil.depthTestEnable =
        settings.depthTest ? VK_TRUE : VK_FALSE;
    pipeline.depthStencil.depthWriteEnable =
        settings.depthWrite ? VK_TRUE : VK_FALSE;
    pipeline.depthStencil.depthCompareOp =
        depthCompareOptionsEnum[settings.depthCompareOp];
    pipeline.depthStencil.depthBoundsTestEnable =
        settings.depthBoundsTest ? VK_TRUE : VK_FALSE;
    pipeline.depthStencil.stencilTestEnable =
        settings.stencilTest ? VK_TRUE : VK_FALSE;

    pipeline.colorBlending.logicOpEnable =
        settings.logicOpEnable ? VK_TRUE : VK_FALSE;
    pipeline.colorBlending.logicOp = logicOpsEnum[settings.logicOp];
    std::copy(
        std::begin(settings.blendConstants),
        std::end(settings.blendConstants),
        std::begin(pipeline.colorBlending.blendConstants)
    );

    std::vector<primitives::StoreHandle> descriptorSets;
    for (auto& binding : shaderReflection.bindings) {
        // Skip invalid bindings (can occur if shader reflection
        // partially failed)
        if (binding.vulkanSet < 0 || binding.vulkanBinding < 0) {
            Log::warning(
                "Pipeline",
                "Skipping binding '{}' with invalid set/binding "
                "indices ({}/{})",
                binding.resourceName, binding.vulkanSet,
                binding.vulkanBinding
            );
            continue;
        }
        size_t newSize = binding.vulkanSet + 1;
        if (newSize > descriptorSets.size())
            descriptorSets.resize(newSize);

        primitives::StoreHandle& hDs =
            descriptorSets[binding.vulkanSet];
        if (!hDs.isValid())
            hDs = store.newDescriptorSet();

        primitives::DescriptorSet& ds =
            store.descriptorSets[hDs.handle];
        if (!ds.pool.isValid()) {
            ds.pool = store.defaultDescriptorPool();
            primitives::DescriptorPool& pool =
                store.descriptorPools[ds.pool.handle];
            pool.registerSet(hDs);
        }

        primitives::DescriptorInfo info{
            .binding = static_cast<uint32_t>(binding.vulkanBinding),
            .stages = binding.stageFlags,
            .arrayCount = binding.arrayCount
        };

        switch (binding.descriptorType) {
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            info.type = primitives::Type::Image;
            info.samplerInfo = {
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter = VK_FILTER_LINEAR,
                .minFilter = VK_FILTER_LINEAR,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .anisotropyEnable = VK_FALSE,
                // TODO: Anisotropy
                //.anisotropyEnable = VK_TRUE,
                //.maxAnisotropy =
                // properties.limits.maxSamplerAnisotropy,
                .compareEnable = VK_FALSE,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
                .unnormalizedCoordinates = VK_FALSE,
            };
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            info.type = primitives::Type::UniformBuffer;
            Log::debug(
                "Pipeline",
                "Created UniformBuffer descriptor binding: {} at set "
                "{} binding {}",
                binding.resourceName, binding.vulkanSet,
                binding.vulkanBinding
            );
            break;
        default:
            Log::error(
                "Pipeline", "Unhandled descriptor type: {}",
                string_VkDescriptorType(binding.descriptorType)
            );
            std::unreachable();
        }

        binding.descriptorSetSlot = {
            .handle = hDs,
            .slot = static_cast<uint32_t>(ds.expectedBindings.size())
        };
        ds.expectedBindings.push_back(std::move(info));
    }

    pipeline.descriptorSetHandles = std::move(descriptorSets);
}

void PipelineNode::getOutputPrimitives(
    const primitives::Store& store,
    std::vector<std::pair<
        ax::NodeEditor::PinId,
        primitives::StoreHandle>>& outputs
) const {
    for (const auto& config : shaderReflection.attachmentConfigs) {
        // Skip invalid handles (can occur if shader compilation failed)
        if (!config.handle.isValid()) {
            Log::warning(
                "Pipeline",
                "Skipping output '{}' with invalid handle in pipeline "
                "'{}'",
                config.semantic, name
            );
            continue;
        }
        if (config.handle.type != primitives::Type::Array) {
            Log::warning(
                "Pipeline",
                "Skipping output '{}' with wrong handle type in "
                "pipeline '{}'",
                config.semantic, name
            );
            continue;
        }
        if (store.arrays[config.handle.handle].type !=
            primitives::Type::Image) {
            Log::warning(
                "Pipeline",
                "Skipping output '{}' with wrong array type in "
                "pipeline '{}'",
                config.semantic, name
            );
            continue;
        }
        outputs.push_back({config.pin.id, config.handle});
    }
}

void PipelineNode::getInputPrimitives(
    const primitives::Store& store,
    std::vector<std::pair<
        ax::NodeEditor::PinId,
        primitives::LinkSlot>>& inputs
) const {
    // Skip if pipeline handle is invalid (can occur if shader
    // compilation failed)
    if (!pipelineHandle.isValid()) {
        Log::warning(
            "Pipeline",
            "Skipping input primitives for '{}': invalid pipeline "
            "handle",
            name
        );
        return;
    }

    // Handle vertex data input
    if (vertexDataPin.id.Get() != 0) {
        primitives::LinkSlot slot{
            .handle = pipelineHandle,
            .slot = 0 // Vertex data connects to slot 0
        };
        inputs.push_back({vertexDataPin.id, slot});
    }
    // Handle single camera connection (when not using global)
    if (hasCameraInput && !cameraInput.useGlobal) {
        // Find the corresponding binding for camera data
        for (const auto& binding : shaderReflection.bindings) {
            if (binding.resourceName == cameraInput.uniformName) {
                if (!binding.descriptorSetSlot.handle.isValid()) {
                    Log::warning(
                        "Pipeline",
                        "Skipping camera input '{}': invalid "
                        "descriptor set slot",
                        binding.resourceName
                    );
                    break;
                }
                inputs.push_back(
                    {cameraInput.pin.id, binding.descriptorSetSlot}
                );
                break;
            }
        }
    }

    // Handle single light connection (when not using global)
    if (hasLightInput && !lightInput.useGlobal) {
        // Find the corresponding binding for light data
        for (const auto& binding : shaderReflection.bindings) {
            if (binding.resourceName == lightInput.uniformName) {
                if (!binding.descriptorSetSlot.handle.isValid()) {
                    Log::warning(
                        "Pipeline",
                        "Skipping light input '{}': invalid descriptor "
                        "set slot",
                        binding.resourceName
                    );
                    break;
                }
                inputs.push_back(
                    {lightInput.pin.id, binding.descriptorSetSlot}
                );
                break;
            }
        }
    }

    for (const auto& binding : shaderReflection.bindings) {
        if (!binding.descriptorSetSlot.handle.isValid()) {
            Log::warning(
                "Pipeline",
                "Skipping binding '{}': invalid descriptor set slot",
                binding.resourceName
            );
            continue;
        }
        inputs.push_back({binding.pin.id, binding.descriptorSetSlot});
    }
}