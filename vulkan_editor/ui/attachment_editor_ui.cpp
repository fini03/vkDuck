#include "attachment_editor_ui.h"
#include "../util/logger.h"
#include "../graph/pipeline_node.h"
#include "pipeline_settings.h"
#include <algorithm>
#include <vulkan/vk_enum_string_helper.h>

// Valid factors when referring to SOURCE
constexpr std::array<VkBlendFactor, 7> srcBlendFactors{
    VK_BLEND_FACTOR_ZERO,
    VK_BLEND_FACTOR_ONE,
    VK_BLEND_FACTOR_SRC_COLOR,
    VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    VK_BLEND_FACTOR_SRC_ALPHA,
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    VK_BLEND_FACTOR_SRC_ALPHA_SATURATE
};

// Valid factors when referring to DESTINATION
constexpr std::array<VkBlendFactor, 6> dstBlendFactors{
    VK_BLEND_FACTOR_ZERO,      VK_BLEND_FACTOR_ONE,
    VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
    VK_BLEND_FACTOR_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA
};

// Alpha-specific factors
constexpr std::array<VkBlendFactor, 5> alphaBlendFactors{
    VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE,
    VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    VK_BLEND_FACTOR_DST_ALPHA
};

constexpr std::array<VkBlendOp, 5> blendOpsEnum = {
    VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT, VK_BLEND_OP_REVERSE_SUBTRACT,
    VK_BLEND_OP_MIN, VK_BLEND_OP_MAX
};

constexpr std::array<VkColorComponentFlagBits, 4> colorComponentFlags{
    VK_COLOR_COMPONENT_R_BIT, VK_COLOR_COMPONENT_G_BIT,
    VK_COLOR_COMPONENT_B_BIT, VK_COLOR_COMPONENT_A_BIT
};

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

const std::vector<const char*>
    AttachmentEditorUI::srcBlendFactorStrings =
        createEnumStringList(srcBlendFactors, string_VkBlendFactor);

const std::vector<const char*>
    AttachmentEditorUI::dstBlendFactorStrings =
        createEnumStringList(dstBlendFactors, string_VkBlendFactor);

const std::vector<const char*>
    AttachmentEditorUI::alphaBlendFactorStrings =
        createEnumStringList(alphaBlendFactors, string_VkBlendFactor);

const std::vector<const char*> AttachmentEditorUI::blendOpStrings =
    createEnumStringList(blendOpsEnum, string_VkBlendOp);

const std::vector<const char*>
    AttachmentEditorUI::colorComponentStrings = createEnumStringList(
        colorComponentFlags, string_VkColorComponentFlagBits
    );

const char* AttachmentEditorUI::FormatToString(VkFormat format) {
    switch (format) {
    // Color formats
    case VK_FORMAT_R8G8B8A8_UNORM:
        return "R8G8B8A8_UNORM (32-bit RGBA)";
    case VK_FORMAT_R8G8B8A8_SRGB:
        return "R8G8B8A8_SRGB (32-bit sRGB)";
    case VK_FORMAT_B8G8R8A8_UNORM:
        return "B8G8R8A8_UNORM (32-bit BGRA)";
    case VK_FORMAT_B8G8R8A8_SRGB:
        return "B8G8R8A8_SRGB (32-bit sRGB BGRA)";
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return "R16G16B16A16_SFLOAT (64-bit HDR)";
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return "R32G32B32A32_SFLOAT (128-bit HDR)";
    case VK_FORMAT_R16G16B16A16_UNORM:
        return "R16G16B16A16_UNORM (64-bit)";
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        return "A2B10G10R10_UNORM (10-bit RGB)";

    // Depth formats
    case VK_FORMAT_D32_SFLOAT:
        return "D32_SFLOAT (32-bit Depth)";
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return "D24_UNORM_S8_UINT (24-bit Depth + 8-bit Stencil)";
    case VK_FORMAT_D16_UNORM:
        return "D16_UNORM (16-bit Depth)";
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return "D32_SFLOAT_S8_UINT (32-bit Depth + Stencil)";

    default:
        return "Unknown Format";
    }
}

std::vector<VkFormat> AttachmentEditorUI::GetImageFormats() {
    return {VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_R8G8B8A8_SRGB,
            VK_FORMAT_B8G8R8A8_UNORM,
            VK_FORMAT_B8G8R8A8_SRGB,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_FORMAT_R16G16B16A16_UNORM,
            VK_FORMAT_A2B10G10R10_UNORM_PACK32,
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D24_UNORM_S8_UINT,
            VK_FORMAT_D16_UNORM,
            VK_FORMAT_D32_SFLOAT_S8_UINT};
}

static bool IsDepthFormat(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT ||
           format == VK_FORMAT_D24_UNORM_S8_UINT ||
           format == VK_FORMAT_D16_UNORM ||
           format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

// Depth-only formats for the depth attachment selector
constexpr std::array<VkFormat, 4> depthFormats{
    VK_FORMAT_D32_SFLOAT,
    VK_FORMAT_D24_UNORM_S8_UINT,
    VK_FORMAT_D16_UNORM,
    VK_FORMAT_D32_SFLOAT_S8_UINT
};

// Depth compare operations
constexpr std::array<VkCompareOp, 8> depthCompareOps{
    VK_COMPARE_OP_NEVER,
    VK_COMPARE_OP_LESS,
    VK_COMPARE_OP_EQUAL,
    VK_COMPARE_OP_LESS_OR_EQUAL,
    VK_COMPARE_OP_GREATER,
    VK_COMPARE_OP_NOT_EQUAL,
    VK_COMPARE_OP_GREATER_OR_EQUAL,
    VK_COMPARE_OP_ALWAYS
};

void AttachmentEditorUI::Draw(PipelineNode* pipeline) {
    if (!pipeline) {
        ImGui::TextWrapped("No pipeline selected.");
        return;
    }

    // Check if shader specifies depth output (SV_Depth semantic)
    bool shaderSpecifiesDepth = false;
    for (const auto& output : pipeline->shaderReflection.outputs) {
        std::string semanticLower = output.semantic;
        std::transform(
            semanticLower.begin(), semanticLower.end(),
            semanticLower.begin(), ::tolower
        );
        if (semanticLower == "sv_depth") {
            shaderSpecifiesDepth = true;
            break;
        }
    }

    // Depth is active if user enabled it OR shader specifies it
    bool depthActive =
        pipeline->settings.depthEnabled || shaderSpecifiesDepth;

    // Count color attachments (non-depth)
    size_t colorAttachmentCount = 0;
    for (const auto& config : pipeline->shaderReflection.attachmentConfigs) {
        if (!IsDepthFormat(config.format)) {
            colorAttachmentCount++;
        }
    }

    ImGui::TextColored(
        ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Framebuffer Attachments (%zu)",
        colorAttachmentCount + (depthActive ? 1 : 0)
    );
    ImGui::Separator();
    ImGui::Spacing();

    // ========== DEPTH ATTACHMENT SECTION (Always shown) ==========
    ImGui::PushID("DepthAttachment");

    ImVec4 depthHeaderColor = ImVec4(0.5f, 0.5f, 0.7f, 1.0f); // Blue for depth

    ImGui::PushStyleColor(ImGuiCol_Header, depthHeaderColor);
    ImGui::PushStyleColor(
        ImGuiCol_HeaderHovered,
        ImVec4(
            depthHeaderColor.x * 1.2f, depthHeaderColor.y * 1.2f,
            depthHeaderColor.z * 1.2f, depthHeaderColor.w
        )
    );
    ImGui::PushStyleColor(
        ImGuiCol_HeaderActive,
        ImVec4(
            depthHeaderColor.x * 0.8f, depthHeaderColor.y * 0.8f,
            depthHeaderColor.z * 0.8f, depthHeaderColor.w
        )
    );

    bool depthNodeOpen = ImGui::CollapsingHeader(
        "Depth Attachment", ImGuiTreeNodeFlags_DefaultOpen
    );

    ImGui::PopStyleColor(3);

    if (depthNodeOpen) {
        ImGui::Indent();

        // Enable checkbox
        if (shaderSpecifiesDepth) {
            // Shader specifies depth - checkbox disabled, shown as checked
            ImGui::BeginDisabled(true);
            bool alwaysTrue = true;
            ImGui::Checkbox("Enable", &alwaysTrue);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("(specified in shader)");
        } else {
            // User can toggle depth
            ImGui::Checkbox("Enable", &pipeline->settings.depthEnabled);
        }

        // Show depth settings only when depth is active
        if (depthActive) {
            ImGui::Spacing();

            // Format selection
            ImGui::TextDisabled("Format:");

            int currentFormatIdx = 0;
            for (size_t j = 0; j < depthFormats.size(); ++j) {
                if (depthFormats[j] == pipeline->settings.depthFormat) {
                    currentFormatIdx = static_cast<int>(j);
                    break;
                }
            }

            if (ImGui::BeginCombo(
                    "##DepthFormat",
                    FormatToString(pipeline->settings.depthFormat)
                )) {
                for (size_t j = 0; j < depthFormats.size(); ++j) {
                    bool isSelected = (static_cast<int>(j) == currentFormatIdx);
                    if (ImGui::Selectable(
                            FormatToString(depthFormats[j]), isSelected
                        )) {
                        pipeline->settings.depthFormat = depthFormats[j];
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Clear Value
            ImGui::TextDisabled("Clear Value:");

            ImGui::SliderFloat(
                "##DepthClear", &pipeline->settings.depthClearValue, 0.0f,
                1.0f, "%.3f"
            );
            ImGui::SameLine();
            ImGui::TextDisabled("Depth");

            // Stencil clear value (if format has stencil)
            if (pipeline->settings.depthFormat ==
                    VK_FORMAT_D24_UNORM_S8_UINT ||
                pipeline->settings.depthFormat ==
                    VK_FORMAT_D32_SFLOAT_S8_UINT) {
                int stencilValue =
                    static_cast<int>(pipeline->settings.stencilClearValue);
                if (ImGui::InputInt("##StencilClear", &stencilValue)) {
                    pipeline->settings.stencilClearValue =
                        static_cast<uint32_t>(stencilValue);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("Stencil");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Depth Test Parameters
            ImGui::TextDisabled("Depth Testing:");

            ImGui::Checkbox("Depth Test", &pipeline->settings.depthTest);
            ImGui::Checkbox("Depth Write", &pipeline->settings.depthWrite);

            // Depth Compare Operation
            if (ImGui::BeginCombo(
                    "Compare Op",
                    string_VkCompareOp(
                        depthCompareOps[pipeline->settings.depthCompareOp]
                    )
                )) {
                for (size_t j = 0; j < depthCompareOps.size(); ++j) {
                    bool isSelected =
                        (static_cast<int>(j) ==
                         pipeline->settings.depthCompareOp);
                    if (ImGui::Selectable(
                            string_VkCompareOp(depthCompareOps[j]), isSelected
                        )) {
                        pipeline->settings.depthCompareOp =
                            static_cast<int>(j);
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Checkbox(
                "Depth Bounds Test", &pipeline->settings.depthBoundsTest
            );
            ImGui::Checkbox("Stencil Test", &pipeline->settings.stencilTest);
        }

        ImGui::Unindent();
        ImGui::Spacing();
    }

    ImGui::PopID();

    // ========== COLOR ATTACHMENTS ==========
    if (pipeline->shaderReflection.attachmentConfigs.empty()) {
        ImGui::TextDisabled(
            "No color attachments detected. Compile shaders with "
            "fragment outputs to see color attachments here."
        );
        return;
    }

    for (auto& config : pipeline->shaderReflection.attachmentConfigs) {
        // Skip depth attachments from shader reflection (handled above)
        if (IsDepthFormat(config.format)) {
            continue;
        }
        ImGui::PushID(config.name.c_str());

        bool isDepth = IsDepthFormat(config.format);

        // Header with color based on type
        ImVec4 headerColor =
            isDepth
                ? ImVec4(0.5f, 0.5f, 0.7f, 1.0f)  // Blue for depth
                : ImVec4(0.8f, 0.5f, 0.3f, 1.0f); // Orange for color

        ImGui::PushStyleColor(ImGuiCol_Header, headerColor);
        ImGui::PushStyleColor(
            ImGuiCol_HeaderHovered,
            ImVec4(
                headerColor.x * 1.2f, headerColor.y * 1.2f,
                headerColor.z * 1.2f, headerColor.w
            )
        );
        ImGui::PushStyleColor(
            ImGuiCol_HeaderActive,
            ImVec4(
                headerColor.x * 0.8f, headerColor.y * 0.8f,
                headerColor.z * 0.8f, headerColor.w
            )
        );

        bool nodeOpen = ImGui::CollapsingHeader(
            config.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen
        );

        ImGui::PopStyleColor(3);

        if (nodeOpen) {
            ImGui::Indent();

            // Semantic info (read-only)
            ImGui::TextDisabled("Semantic:");
            ImGui::SameLine();
            ImGui::Text("%s", config.semantic.c_str());

            ImGui::Spacing();

            // Format selection
            ImGui::TextDisabled("Format:");

            std::vector<VkFormat> formats = GetImageFormats();

            // Find current format index
            int currentIdx = 0;
            for (size_t j = 0; j < formats.size(); ++j) {
                if (formats[j] == config.format) {
                    currentIdx = static_cast<int>(j);
                    break;
                }
            }

            // Format combo box
            if (ImGui::BeginCombo(
                    "##Format", FormatToString(config.format)
                )) {
                for (size_t j = 0; j < formats.size(); ++j) {
                    bool isSelected =
                        (static_cast<int>(j) == currentIdx);
                    if (ImGui::Selectable(
                            FormatToString(formats[j]), isSelected
                        )) {
                        config.format = formats[j];
                        config.initializeClearValue(); // Re-initialize
                                                       // clear value
                                                       // for new format
                        Log::debug(
                            "AttachmentEditor",
                            "Changed attachment '{}' format to {}",
                            config.name, static_cast<int>(formats[j])
                        );
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Clear Value Section
            ImGui::TextDisabled("Clear Value:");

            if (isDepth) {
                // Depth clear value
                float depthValue = config.clearValue.depthStencil.depth;
                if (ImGui::SliderFloat(
                        "##DepthClear", &depthValue, 0.0f, 1.0f, "%.3f"
                    )) {
                    config.clearValue.depthStencil.depth = depthValue;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("Depth");

                // Stencil clear value (if applicable)
                if (config.format == VK_FORMAT_D24_UNORM_S8_UINT ||
                    config.format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
                    int stencilValue =
                        config.clearValue.depthStencil.stencil;
                    if (ImGui::InputInt(
                            "##StencilClear", &stencilValue
                        )) {
                        config.clearValue.depthStencil.stencil =
                            static_cast<uint32_t>(stencilValue);
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("Stencil");
                }
            } else {
                // Color clear value
                ImGui::ColorEdit4(
                    "##ColorClear", config.clearValue.color.float32,
                    ImGuiColorEditFlags_Float
                );
            }

            // Color Blending Section (only for color attachments)
            if (!isDepth) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::TextDisabled("Color Blending:");

                bool blendEnable =
                    config.colorBlending.blendEnable == VK_TRUE;
                if (ImGui::Checkbox(
                        "Enable Blending##blend", &blendEnable
                    )) {
                    config.colorBlending.blendEnable =
                        blendEnable ? VK_TRUE : VK_FALSE;
                }

                if (blendEnable) {
                    ImGui::Indent();

                    // Helper lambda to draw a combo for any enum list
                    auto DrawEnumCombo =
                        [&](
                            const char* label, auto& currentValue,
                            const auto& enumArray,
                            const std::vector<const char*>& stringList
                        ) {
                            int currentIdx = 0;
                            // Find current index in the enum array to
                            // keep the UI in sync with the actual value
                            for (size_t i = 0; i < enumArray.size();
                                 ++i) {
                                if (enumArray[i] == currentValue) {
                                    currentIdx = static_cast<int>(i);
                                    break;
                                }
                            }

                            if (ImGui::Combo(
                                    label, &currentIdx,
                                    stringList.data(),
                                    static_cast<int>(stringList.size())
                                )) {
                                currentValue = enumArray[currentIdx];
                            }
                        };

                    DrawEnumCombo(
                        "Src Color Factor",
                        config.colorBlending.srcColorBlendFactor,
                        srcBlendFactors, srcBlendFactorStrings
                    );

                    DrawEnumCombo(
                        "Dst Color Factor",
                        config.colorBlending.dstColorBlendFactor,
                        dstBlendFactors, dstBlendFactorStrings
                    );

                    DrawEnumCombo(
                        "Color Blend Op",
                        config.colorBlending.colorBlendOp, blendOpsEnum,
                        blendOpStrings
                    );

                    ImGui::Spacing();

                    DrawEnumCombo(
                        "Src Alpha Factor",
                        config.colorBlending.srcAlphaBlendFactor,
                        alphaBlendFactors, alphaBlendFactorStrings
                    );

                    DrawEnumCombo(
                        "Dst Alpha Factor",
                        config.colorBlending.dstAlphaBlendFactor,
                        alphaBlendFactors, alphaBlendFactorStrings
                    );

                    DrawEnumCombo(
                        "Alpha Blend Op",
                        config.colorBlending.alphaBlendOp, blendOpsEnum,
                        blendOpStrings
                    );

                    ImGui::Unindent();
                }

                ImGui::Spacing();
                ImGui::TextDisabled("Color Write Mask:");

                for (size_t i = 0;
                     const auto& flag : colorComponentFlags) {
                    bool enabled =
                        (config.colorBlending.colorWriteMask & flag) !=
                        0;
                    if (ImGui::Checkbox(
                            colorComponentStrings[i], &enabled
                        )) {
                        if (enabled) {
                            config.colorBlending.colorWriteMask |= flag;
                        } else {
                            config.colorBlending.colorWriteMask &=
                                ~flag;
                        }
                    }
                    i++;
                }
            }

            ImGui::Unindent();
            ImGui::Spacing();
        }

        ImGui::PopID();
    }
}