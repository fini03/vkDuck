#pragma once

#include "shader_types.h"
#include "slang.h"
#include <slang-com-ptr.h>
#include <string>
#include <vulkan/vulkan.h>

static Slang::ComPtr<slang::IGlobalSession> globalSession = nullptr;

using ShaderTypes::BindingInfo;
using ShaderTypes::MemberInfo;
using ShaderTypes::OutputInfo;
using ShaderTypes::ShaderParsedResult;
using ShaderTypes::StructInfo;
using ShaderTypes::VertexInputAttribute;

/**
 * @class ShaderReflection
 * @brief Analyzes compiled Slang shaders to extract binding and descriptor information.
 *
 * Performs shader reflection via Slang to extract uniform buffer layouts,
 * texture bindings, vertex attributes, and output semantics. Automatically
 * detects camera and light structures for editor integration.
 */
class ShaderReflection {
public:
    static Slang::ComPtr<slang::IGlobalSession> initializeSlang();
    static void resetSession();
    static ShaderParsedResult reflectShader(
        const std::filesystem::path& moduleName,
        SlangStage stage,
        const std::filesystem::path& projectRoot = {}
    );
    static void printParsedResult(const ShaderParsedResult& result);
    static std::string descriptorTypeToString(VkDescriptorType type);
    static std::string shaderStageToString(VkShaderStageFlags flags);
    static MemberInfo extractMemberInfo(slang::VariableReflection* member);
    static std::string getFullTypeName(slang::TypeReflection* type);

private:
    static std::vector<BindingInfo> parseBindings(slang::ProgramLayout* layout, SlangStage stage);
    static std::string getVkFormatString(VkFormat format);
    static VkFormat getVkFormatFromTypeName(const std::string& typeName);
    static uint32_t getTypeSize(const std::string& typeName);
    static std::vector<StructInfo> detectStructs(
        slang::TypeLayoutReflection* typeLayout, const std::string& structFilter
    );
    static std::vector<OutputInfo> collectOutputs(slang::VariableLayoutReflection* varLayout);
    static std::vector<VertexInputAttribute> collectVertexInputs(slang::EntryPointReflection* entryPoint);
    static const char* getTypeKindName(slang::TypeReflection::Kind kind);
    static VkShaderStageFlags getVkStageFlags(SlangStage stage);
    static void diagnoseIfNeeded(slang::IBlob* diagnosticsBlob, bool isError = true);
};