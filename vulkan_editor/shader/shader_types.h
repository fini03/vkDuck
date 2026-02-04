#pragma once

#include "../util/logger.h"
#include "../io/serialization.h"
#include "../gpu/primitives.h"
#include <imgui_node_editor.h>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

/**
 * @namespace ShaderTypes
 * @brief Common type definitions shared between shader reflection and the node system.
 */
namespace ShaderTypes {

enum class PinType {
    UniformBuffer,
    Image,
    VertexData,
    Camera,
    Light,
    ModelCameras,
    Unknown
};

struct VertexInputAttribute {
    std::string name;
    std::string semantic;
    std::string typeName;
    uint32_t location = 0;
    uint32_t binding = 0;
    uint32_t offset = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
};

struct Pin {
    ax::NodeEditor::PinId id;
    PinType type;
    std::string label;
};

struct MemberInfo {
    std::string name;
    std::string typeName;
    std::string typeKind;
    int offset = 0;
    int arraySize = 0;
};

struct StructInfo {
    std::string structName;
    std::string instanceName;
    std::vector<MemberInfo> members;
    int arraySize = 0;
};

struct OutputInfo {
    std::string name;
    std::string semantic;
    std::string typeName;
};

struct AttachmentConfig : public ISerializable {
    std::string name;
    std::string semantic;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

    VkPipelineColorBlendAttachmentState colorBlending;

    VkClearValue clearValue = {{{0.1f, 0.1f, 0.5f, 1.0f}}};

    primitives::StoreHandle handle;
    Pin pin;

    AttachmentConfig()
        : format(VK_FORMAT_R8G8B8A8_UNORM) {
        colorBlending.blendEnable = VK_FALSE;
        colorBlending.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlending.dstColorBlendFactor =
            VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlending.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlending.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlending.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlending.alphaBlendOp = VK_BLEND_OP_ADD;
        colorBlending.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        initializeClearValue();
    }

    void initializeDefaultsFromSemantic() {
        std::string semanticLower = semantic;
        std::transform(semanticLower.begin(), semanticLower.end(), semanticLower.begin(), ::tolower);

        // Strip numeric suffix (e.g., "position0" -> "position")
        while (!semanticLower.empty() && std::isdigit(semanticLower.back())) {
            semanticLower.pop_back();
        }

        // Depth attachment
        if (semanticLower == "sv_depth") {
            format = VK_FORMAT_D32_SFLOAT;
            clearValue.depthStencil.depth = 1.0f;
            clearValue.depthStencil.stencil = 0;
            return;
        }

        // Position attachment (world-space positions need HDR precision)
        if (semanticLower == "position" || semanticLower == "sv_position" ||
            semanticLower == "worldposition" || semanticLower == "positionws") {
            format = VK_FORMAT_R16G16B16A16_SFLOAT;
            clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
            colorBlending.blendEnable = VK_FALSE;
            return;
        }

        // Normal attachment (world-space normals need precision)
        if (semanticLower == "normal" || semanticLower == "worldnormal" ||
            semanticLower == "normalws") {
            format = VK_FORMAT_R16G16B16A16_SFLOAT;
            clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
            colorBlending.blendEnable = VK_FALSE;
            return;
        }

        // Texture coordinates (standard precision is fine)
        if (semanticLower == "texcoord" || semanticLower == "uv") {
            format = VK_FORMAT_R16G16B16A16_SFLOAT;
            clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};
            colorBlending.blendEnable = VK_FALSE;
            return;
        }

        // Albedo/diffuse color (standard 8-bit is fine)
        if (semanticLower == "albedo" || semanticLower == "diffuse" ||
            semanticLower == "color" || semanticLower == "basecolor") {
            format = VK_FORMAT_R8G8B8A8_UNORM;
            clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            colorBlending.blendEnable = VK_FALSE;
            return;
        }

        // Default for SV_Target or unrecognized semantics
        format = VK_FORMAT_R8G8B8A8_UNORM;
        initializeClearValue();
    }

    void initializeClearValue() {
        bool isDepth =
            (format == VK_FORMAT_D32_SFLOAT ||
             format == VK_FORMAT_D24_UNORM_S8_UINT ||
             format == VK_FORMAT_D16_UNORM ||
             format == VK_FORMAT_D32_SFLOAT_S8_UINT);

        if (isDepth) {
            Log::debug(
                "ShaderTypes",
                "Initializing depth clear value for format {}",
                static_cast<int>(format)
            );
            clearValue.depthStencil.depth = 1.0f;
            clearValue.depthStencil.stencil = 0;
        } else {
            Log::debug(
                "ShaderTypes",
                "Initializing color clear value for format {}",
                static_cast<int>(format)
            );
            clearValue.color = {{0.1f, 0.1f, 0.5f, 1.0f}};
        }
    }

    nlohmann::json toJson() const override {
        nlohmann::json j;
        j["name"] = name;
        j["semantic"] = semantic;
        j["format"] = static_cast<int>(format);
        // Serialize clear value based on format
        bool isDepth =
            (format == VK_FORMAT_D32_SFLOAT ||
             format == VK_FORMAT_D24_UNORM_S8_UINT ||
             format == VK_FORMAT_D16_UNORM ||
             format == VK_FORMAT_D32_SFLOAT_S8_UINT);

        if (isDepth) {
            j["clearValue"]["depth"] = clearValue.depthStencil.depth;
            j["clearValue"]["stencil"] =
                clearValue.depthStencil.stencil;
        } else {
            j["clearValue"]["color"] = {
                clearValue.color.float32[0],
                clearValue.color.float32[1],
                clearValue.color.float32[2], clearValue.color.float32[3]
            };
        }

        // Serialize color blending
        j["colorBlending"]["blendEnable"] =
            colorBlending.blendEnable == VK_TRUE;
        j["colorBlending"]["srcColorBlendFactor"] =
            static_cast<int>(colorBlending.srcColorBlendFactor);
        j["colorBlending"]["dstColorBlendFactor"] =
            static_cast<int>(colorBlending.dstColorBlendFactor);
        j["colorBlending"]["colorBlendOp"] =
            static_cast<int>(colorBlending.colorBlendOp);
        j["colorBlending"]["srcAlphaBlendFactor"] =
            static_cast<int>(colorBlending.srcAlphaBlendFactor);
        j["colorBlending"]["dstAlphaBlendFactor"] =
            static_cast<int>(colorBlending.dstAlphaBlendFactor);
        j["colorBlending"]["alphaBlendOp"] =
            static_cast<int>(colorBlending.alphaBlendOp);
        j["colorBlending"]["colorWriteMask"] =
            colorBlending.colorWriteMask;

        return j;
    }

    void fromJson(const nlohmann::json& j) override {
        name = j.value("name", "");
        semantic = j.value("semantic", "");
        format = static_cast<VkFormat>(j.value(
            "format", static_cast<int>(VK_FORMAT_R8G8B8A8_UNORM)
        ));

        // Deserialize color blending
        if (j.contains("colorBlending")) {
            const auto& cb = j["colorBlending"];
            colorBlending.blendEnable =
                cb.value("blendEnable", false) ? VK_TRUE : VK_FALSE;
            colorBlending.srcColorBlendFactor =
                static_cast<VkBlendFactor>(cb.value(
                    "srcColorBlendFactor",
                    (int)VK_BLEND_FACTOR_SRC_ALPHA
                ));
            colorBlending.dstColorBlendFactor =
                static_cast<VkBlendFactor>(cb.value(
                    "dstColorBlendFactor",
                    (int)VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
                ));
            colorBlending.colorBlendOp = static_cast<VkBlendOp>(
                cb.value("colorBlendOp", (int)VK_BLEND_OP_ADD)
            );
            colorBlending.srcAlphaBlendFactor =
                static_cast<VkBlendFactor>(cb.value(
                    "srcAlphaBlendFactor", (int)VK_BLEND_FACTOR_ONE
                ));
            colorBlending.dstAlphaBlendFactor =
                static_cast<VkBlendFactor>(cb.value(
                    "dstAlphaBlendFactor", (int)VK_BLEND_FACTOR_ZERO
                ));
            colorBlending.alphaBlendOp = static_cast<VkBlendOp>(
                cb.value("alphaBlendOp", (int)VK_BLEND_OP_ADD)
            );
            colorBlending.colorWriteMask =
                static_cast<VkColorComponentFlags>(
                    cb.value("colorWriteMask", 0xF)
                ); // 0xF is RGBA
        }

        // Deserialize clear value based on format
        if (j.contains("clearValue")) {
            const auto& cv = j["clearValue"];
            bool isDepth =
                (format == VK_FORMAT_D32_SFLOAT ||
                 format == VK_FORMAT_D24_UNORM_S8_UINT ||
                 format == VK_FORMAT_D16_UNORM ||
                 format == VK_FORMAT_D32_SFLOAT_S8_UINT);

            if (isDepth) {
                clearValue.depthStencil.depth = cv.value("depth", 1.0f);
                clearValue.depthStencil.stencil =
                    cv.value("stencil", 0u);
            } else if (cv.contains("color") && cv["color"].is_array() &&
                       cv["color"].size() >= 4) {
                clearValue.color.float32[0] =
                    cv["color"][0].get<float>();
                clearValue.color.float32[1] =
                    cv["color"][1].get<float>();
                clearValue.color.float32[2] =
                    cv["color"][2].get<float>();
                clearValue.color.float32[3] =
                    cv["color"][3].get<float>();
            }
        }
    }
};

struct BindingInfo {
    std::string resourceName;
    std::string typeName;
    std::string typeKind;
    int vulkanSet = -1;
    int vulkanBinding = -1;
    VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    VkShaderStageFlags stageFlags = 0;
    uint32_t arrayCount = 1;
    std::vector<MemberInfo> members;
    bool isInput = false;
    bool isOutput = false;
    primitives::LinkSlot descriptorSetSlot;
    Pin pin;
};

struct ShaderParsedResult {
    std::vector<BindingInfo> bindings;
    std::vector<OutputInfo> outputs;
    std::vector<StructInfo> lightStructs;
    std::vector<StructInfo> cameraStructs;
    std::vector<StructInfo> customStructs;
    std::vector<AttachmentConfig> attachmentConfigs;
    std::vector<VertexInputAttribute> vertexAttributes;
    std::vector<uint32_t> code;
    std::vector<uint32_t> vertexCode;
    std::vector<uint32_t> fragmentCode;
    std::string entryPointName;
    std::string vertexEntryPoint;
    std::string fragmentEntryPoint;
    bool success = false;
    std::string errorMessage;
    std::string warningMessage;

    bool hasLights() const { return !lightStructs.empty(); }
    bool hasCameras() const { return !cameraStructs.empty(); }
    bool isValid() const { return success && !code.empty(); }

    std::vector<StructInfo> getAllStructs() const {
        std::vector<StructInfo> all;
        all.insert(all.end(), lightStructs.begin(), lightStructs.end());
        all.insert(
            all.end(), cameraStructs.begin(), cameraStructs.end()
        );
        all.insert(
            all.end(), customStructs.begin(), customStructs.end()
        );
        return all;
    }

    const BindingInfo* findBinding(const std::string& name) const {
        for (const auto& b : bindings) {
            if (b.resourceName == name) return &b;
        }
        return nullptr;
    }

    std::vector<BindingInfo> getInputBindings() const {
        std::vector<BindingInfo> inputs;
        for (const auto& b : bindings) {
            if (b.isInput) inputs.push_back(b);
        }
        return inputs;
    }

    std::vector<BindingInfo> getOutputBindings() const {
        std::vector<BindingInfo> outputs;
        for (const auto& b : bindings) {
            if (b.isOutput) outputs.push_back(b);
        }
        return outputs;
    }
};

}