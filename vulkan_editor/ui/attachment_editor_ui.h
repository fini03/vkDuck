#pragma once
#include <imgui.h>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

class PipelineNode;

class AttachmentEditorUI {
public:
    static void Draw(PipelineNode* pipeline);
    static const char* FormatToString(VkFormat format);
    static std::vector<VkFormat> GetImageFormats();

    static const std::vector<const char*> srcBlendFactorStrings;
    static const std::vector<const char*> dstBlendFactorStrings;
    static const std::vector<const char*> alphaBlendFactorStrings;
    static const std::vector<const char*> blendOpStrings;
    static const std::vector<const char*> colorComponentStrings;
};