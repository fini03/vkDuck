#pragma once
#include "../io/serialization.h"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <vulkan/vulkan.h>

enum class ExtentType { SwapchainRelative, Custom };

struct ExtentConfig {
    ExtentType type = ExtentType::SwapchainRelative;
    int width = 2048;
    int height = 2048;

    static ExtentConfig GetDefault(ExtentType type) {
        switch (type) {
        case ExtentType::Custom:
            return {type, 2048, 2048};
        default:
            return {ExtentType::SwapchainRelative, 0, 0};
        }
    }

    // Serialization helpers
    nlohmann::json toJson() const {
        return {
            {"type", static_cast<int>(type)},
            {"width", width},
            {"height", height}
        };
    }

    void fromJson(const nlohmann::json& j) {
        type = static_cast<ExtentType>(j.value("type", 0));
        width = j.value("width", 2048);
        height = j.value("height", 2048);
    }
};

/// Simple struct representing configurable Vulkan pipeline states.
/// Matches what your PipelineNode uses internally.
struct PipelineSettings : public ISerializable {
    ExtentConfig extentConfig;

    // Input Assembly
    // Default to triangle list
    int inputAssembly = 3;
    bool primitiveRestart = false;

    // Rasterizer
    bool depthClamp = false;
    bool rasterizerDiscard = false;
    int polygonMode = 0;
    float lineWidth = 1.0f;
    int cullMode = 0;
    int frontFace = 0;
    bool depthBiasEnabled = false;
    float depthBiasConstantFactor = 0.0f;
    float depthBiasClamp = 0.0f;
    float depthBiasSlopeFactor = 0.0f;

    // Depth / Stencil
    bool depthEnabled = false;  // User can enable/disable depth attachment
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;  // Default depth format
    float depthClearValue = 1.0f;  // Default depth clear value
    uint32_t stencilClearValue = 0;  // Default stencil clear value
    bool depthTest = true;
    bool depthWrite = true;
    int depthCompareOp = 1;  // VK_COMPARE_OP_LESS
    bool depthBoundsTest = false;
    bool stencilTest = false;

    // Multisampling
    bool sampleShading = false;
    int rasterizationSamples = 0;

    // Color blending
    bool logicOpEnable = false;
    int logicOp = 0;
    int attachmentCount = 1; // we should kick this from here
    float blendConstants[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // Shader info (optional) - all paths are project-relative
    std::filesystem::path vertexShaderPath;
    std::filesystem::path fragmentShaderPath;
    std::filesystem::path compiledVertexShaderPath;
    std::filesystem::path compiledFragmentShaderPath;

    // Serialization
    nlohmann::json toJson() const override {
        nlohmann::json j;

        // Extent configuration
        j["extentConfig"] = extentConfig.toJson();

        // Core settings
        j["inputAssembly"] = inputAssembly;
        j["primitiveRestart"] = primitiveRestart;
        j["depthClamp"] = depthClamp;
        j["rasterizerDiscard"] = rasterizerDiscard;
        j["polygonMode"] = polygonMode;
        j["lineWidth"] = lineWidth;
        j["cullMode"] = cullMode;
        j["frontFace"] = frontFace;
        j["depthBiasEnabled"] = depthBiasEnabled;
        j["depthBiasConstantFactor"] = depthBiasConstantFactor;
        j["depthBiasClamp"] = depthBiasClamp;
        j["depthBiasSlopeFactor"] = depthBiasSlopeFactor;
        j["depthEnabled"] = depthEnabled;
        j["depthFormat"] = static_cast<int>(depthFormat);
        j["depthClearValue"] = depthClearValue;
        j["stencilClearValue"] = stencilClearValue;
        j["depthTest"] = depthTest;
        j["depthWrite"] = depthWrite;
        j["depthCompareOp"] = depthCompareOp;
        j["depthBoundsTest"] = depthBoundsTest;
        j["stencilTest"] = stencilTest;
        j["sampleShading"] = sampleShading;
        j["rasterizationSamples"] = rasterizationSamples;
        j["logicOpEnable"] = logicOpEnable;
        j["logicOp"] = logicOp;

        j["blendConstants"] = {
            blendConstants[0], blendConstants[1], blendConstants[2],
            blendConstants[3]
        };

        // Shader paths (all project-relative)
        j["vertexShaderPath"] = vertexShaderPath.string();
        j["compiledVertexShaderPath"] = compiledVertexShaderPath.string();
        j["fragmentShaderPath"] = fragmentShaderPath.string();
        j["compiledFragmentShaderPath"] = compiledFragmentShaderPath.string();

        return j;
    }

    // Deserialization
    void fromJson(const nlohmann::json& j) override {
        // Extent configuration
        if (j.contains("extentConfig")) {
            extentConfig.fromJson(j["extentConfig"]);
        }

        // Core settings with defaults
        inputAssembly = j.value("inputAssembly", 0);
        primitiveRestart = j.value("primitiveRestart", false);
        depthClamp = j.value("depthClamp", false);
        rasterizerDiscard = j.value("rasterizerDiscard", false);
        polygonMode = j.value("polygonMode", 0);
        lineWidth = j.value("lineWidth", 1.0f);
        cullMode = j.value("cullMode", 0);
        frontFace = j.value("frontFace", 0);
        depthBiasEnabled = j.value("depthBiasEnabled", false);
        depthBiasConstantFactor = j.value("depthBiasConstantFactor", 0.0f);
        depthBiasClamp = j.value("depthBiasClamp", 0.0f);
        depthBiasSlopeFactor = j.value("depthBiasSlopeFactor", 0.0f);
        depthEnabled = j.value("depthEnabled", false);
        depthFormat = static_cast<VkFormat>(
            j.value("depthFormat", static_cast<int>(VK_FORMAT_D32_SFLOAT))
        );
        depthClearValue = j.value("depthClearValue", 1.0f);
        stencilClearValue = j.value("stencilClearValue", 0u);
        depthTest = j.value("depthTest", true);
        depthWrite = j.value("depthWrite", true);
        depthCompareOp = j.value("depthCompareOp", 0);
        depthBoundsTest = j.value("depthBoundsTest", false);
        stencilTest = j.value("stencilTest", false);
        sampleShading = j.value("sampleShading", false);
        rasterizationSamples = j.value("rasterizationSamples", 0);
        logicOpEnable = j.value("logicOpEnable", false);
        logicOp = j.value("logicOp", 0);

        // Blend constants
        if (j.contains("blendConstants") &&
            j["blendConstants"].is_array()) {
            for (size_t i = 0; i < 4 && i < j["blendConstants"].size();
                 ++i) {
                blendConstants[i] = j["blendConstants"][i].get<float>();
            }
        }

        // Shader paths (all project-relative)
        vertexShaderPath = j.value("vertexShaderPath", "");
        compiledVertexShaderPath = j.value("compiledVertexShaderPath", "");
        fragmentShaderPath = j.value("fragmentShaderPath", "");
        compiledFragmentShaderPath = j.value("compiledFragmentShaderPath", "");
    }
};
