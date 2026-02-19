// shader_reflection.cpp
#include "shader_reflection.h"
#include "../util/logger.h"
#include "../graph/node.h"
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

using namespace Slang;
using namespace ShaderTypes;

// ============================================================================
// Constants
// ============================================================================

static constexpr const char* LOG_TAG = "ShaderReflection";

// ============================================================================
// Slang Session Management
// ============================================================================

ComPtr<slang::IGlobalSession> ShaderReflection::initializeSlang() {
    if (globalSession)
        return globalSession;
    createGlobalSession(globalSession.writeRef());
    return globalSession;
}

void ShaderReflection::resetSession() {
    globalSession = nullptr;
    Log::debug(LOG_TAG, "Session reset for hot reload");
}

// ============================================================================
// Diagnostic Helpers
// ============================================================================

void ShaderReflection::diagnoseIfNeeded(slang::IBlob* diagnosticsBlob, bool isError) {
    if (!diagnosticsBlob)
        return;

    const char* message = static_cast<const char*>(diagnosticsBlob->getBufferPointer());
    if (isError) {
        Log::error(LOG_TAG, "Shader compilation error:\n{}", message);
    } else {
        Log::warning(LOG_TAG, "Shader compilation warning:\n{}", message);
    }
}

static std::string getDiagnosticMessage(slang::IBlob* diagnosticsBlob) {
    if (!diagnosticsBlob)
        return "";
    return std::string(static_cast<const char*>(diagnosticsBlob->getBufferPointer()));
}

// ============================================================================
// Type Name Conversion
// ============================================================================

const char* ShaderReflection::getTypeKindName(slang::TypeReflection::Kind kind) {
    using Kind = slang::TypeReflection::Kind;
    switch (kind) {
    case Kind::None:               return "None";
    case Kind::Struct:             return "Struct";
    case Kind::Array:              return "Array";
    case Kind::Matrix:             return "Matrix";
    case Kind::Vector:             return "Vector";
    case Kind::Scalar:             return "Scalar";
    case Kind::ConstantBuffer:     return "ConstantBuffer";
    case Kind::Resource:           return "Resource";
    case Kind::SamplerState:       return "SamplerState";
    case Kind::TextureBuffer:      return "TextureBuffer";
    case Kind::ShaderStorageBuffer: return "ShaderStorageBuffer";
    case Kind::ParameterBlock:     return "ParameterBlock";
    case Kind::GenericTypeParameter: return "GenericTypeParameter";
    case Kind::Interface:          return "Interface";
    case Kind::OutputStream:       return "OutputStream";
    case Kind::Specialized:        return "Specialized";
    case Kind::Feedback:           return "Feedback";
    case Kind::Pointer:            return "Pointer";
    case Kind::DynamicResource:    return "DynamicResource";
    default:                       return "Unknown";
    }
}

static const char* getScalarTypeName(slang::TypeReflection::ScalarType scalar) {
    using Scalar = slang::TypeReflection::ScalarType;
    switch (scalar) {
    case Scalar::Float32: return "float";
    case Scalar::Int32:   return "int";
    case Scalar::UInt32:  return "uint";
    case Scalar::Bool:    return "bool";
    default:              return "unknown";
    }
}

std::string ShaderReflection::getFullTypeName(slang::TypeReflection* type) {
    std::string base = getScalarTypeName(type->getScalarType());

    switch (type->getKind()) {
    case slang::TypeReflection::Kind::Scalar:
        return base;

    case slang::TypeReflection::Kind::Vector:
        return base + std::to_string(type->getElementCount());

    case slang::TypeReflection::Kind::Matrix:
        return base + std::to_string(type->getRowCount()) + "x" +
               std::to_string(type->getColumnCount());

    case slang::TypeReflection::Kind::Array: {
        auto elementType = type->getElementType();
        return getFullTypeName(elementType) + "[" +
               std::to_string(type->getElementCount()) + "]";
    }

    case slang::TypeReflection::Kind::Struct: {
        const char* name = type->getName();
        return name ? name : "struct";
    }

    default:
        return "unknown";
    }
}

// ============================================================================
// Vulkan Format Conversion
// ============================================================================

std::string ShaderReflection::getVkFormatString(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R32_SFLOAT:         return "R32_SFLOAT (float)";
    case VK_FORMAT_R32G32_SFLOAT:      return "R32G32_SFLOAT (float2)";
    case VK_FORMAT_R32G32B32_SFLOAT:   return "R32G32B32_SFLOAT (float3)";
    case VK_FORMAT_R32G32B32A32_SFLOAT: return "R32G32B32A32_SFLOAT (float4)";
    default:                           return "UNDEFINED";
    }
}

VkFormat ShaderReflection::getVkFormatFromTypeName(const std::string& typeName) {
    if (typeName == "float")  return VK_FORMAT_R32_SFLOAT;
    if (typeName == "float2") return VK_FORMAT_R32G32_SFLOAT;
    if (typeName == "float3") return VK_FORMAT_R32G32B32_SFLOAT;
    if (typeName == "float4") return VK_FORMAT_R32G32B32A32_SFLOAT;
    return VK_FORMAT_UNDEFINED;
}

uint32_t ShaderReflection::getTypeSize(const std::string& typeName) {
    if (typeName == "float")  return sizeof(float) * 1;
    if (typeName == "float2") return sizeof(float) * 2;
    if (typeName == "float3") return sizeof(float) * 3;
    if (typeName == "float4") return sizeof(float) * 4;
    return 0;
}

VkShaderStageFlags ShaderReflection::getVkStageFlags(SlangStage stage) {
    switch (stage) {
    case SLANG_STAGE_VERTEX:   return VK_SHADER_STAGE_VERTEX_BIT;
    case SLANG_STAGE_FRAGMENT: return VK_SHADER_STAGE_FRAGMENT_BIT;
    case SLANG_STAGE_COMPUTE:  return VK_SHADER_STAGE_COMPUTE_BIT;
    default:                   return 0;
    }
}

std::string ShaderReflection::descriptorTypeToString(VkDescriptorType type) {
    switch (type) {
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:        return "VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER";
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return "VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER";
    case VK_DESCRIPTOR_TYPE_SAMPLER:               return "VK_DESCRIPTOR_TYPE_SAMPLER";
    default:                                       return "VK_DESCRIPTOR_TYPE_UNKNOWN";
    }
}

std::string ShaderReflection::shaderStageToString(VkShaderStageFlags flags) {
    if (flags == (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)) {
        return "VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT";
    }
    switch (flags) {
    case VK_SHADER_STAGE_VERTEX_BIT:   return "VK_SHADER_STAGE_VERTEX_BIT";
    case VK_SHADER_STAGE_FRAGMENT_BIT: return "VK_SHADER_STAGE_FRAGMENT_BIT";
    case VK_SHADER_STAGE_COMPUTE_BIT:  return "VK_SHADER_STAGE_COMPUTE_BIT";
    default:                           return "VK_SHADER_STAGE_UNKNOWN";
    }
}

// ============================================================================
// Member/Struct Extraction Helpers
// ============================================================================

MemberInfo ShaderReflection::extractMemberInfo(slang::VariableReflection* member) {
    MemberInfo info;
    info.name = member->getName() ? member->getName() : "unnamed";

    slang::TypeReflection* type = member->getType();
    if (type->getKind() == slang::TypeReflection::Kind::Array) {
        slang::TypeReflection* elementType = type->getElementType();
        info.typeName = getFullTypeName(elementType);
        info.arraySize = static_cast<int>(type->getElementCount());
        info.typeKind = getTypeKindName(elementType->getKind());
    } else {
        info.typeName = getFullTypeName(type);
        info.arraySize = 0;
        info.typeKind = getTypeKindName(type->getKind());
    }

    return info;
}

static StructInfo extractStructFromType(
    slang::TypeReflection* structType,
    const std::string& instanceName,
    int arraySize
) {
    StructInfo info;
    info.structName = structType->getName() ? structType->getName() : "";
    info.instanceName = instanceName;
    info.arraySize = arraySize;

    SlangInt memberCount = structType->getFieldCount();
    for (SlangInt i = 0; i < memberCount; ++i) {
        slang::VariableReflection* member = structType->getFieldByIndex(i);
        if (member) {
            info.members.push_back(ShaderReflection::extractMemberInfo(member));
        }
    }
    return info;
}

// ============================================================================
// Debug Layout Logging
// ============================================================================

static void logFieldLayout(
    slang::VariableLayoutReflection* field,
    slang::TypeLayoutReflection* fieldTypeLayout,
    slang::TypeReflection* fieldType,
    int index,
    int indentLevel
) {
    std::string indent(indentLevel * 2, ' ');
    const char* fieldName = field->getName();
    size_t offset = field->getOffset();
    size_t size = fieldTypeLayout->getSize();
    size_t alignment = fieldTypeLayout->getAlignment();

    std::string typeStr;
    const char* typeName = fieldType->getName();
    if (typeName) {
        typeStr = typeName;
    } else {
        typeStr = ShaderReflection::getFullTypeName(fieldType);
    }

    if (fieldType->getKind() == slang::TypeReflection::Kind::Array) {
        size_t elementCount = fieldType->getElementCount();
        size_t stride = fieldTypeLayout->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
        slang::TypeReflection* elementType = fieldType->getElementType();
        std::string elementTypeName = elementType
            ? ShaderReflection::getFullTypeName(elementType)
            : "unknown";

        Log::debug(LOG_TAG,
            "{}  [{}] {} : {}[{}] (offset: {}, size: {}, stride: {}, align: {})",
            indent, index, fieldName ? fieldName : "unnamed",
            elementTypeName, elementCount, offset, size, stride, alignment);
    } else {
        Log::debug(LOG_TAG,
            "{}  [{}] {} : {} (offset: {}, size: {}, align: {})",
            indent, index, fieldName ? fieldName : "unnamed",
            typeStr, offset, size, alignment);
    }
}

static void logStructLayout(
    slang::TypeLayoutReflection* typeLayout,
    const std::string& structName,
    int indentLevel = 0
);

static void logFieldRecursive(
    slang::TypeLayoutReflection* fieldTypeLayout,
    slang::TypeReflection* fieldType,
    int indentLevel
) {
    if (fieldType->getKind() == slang::TypeReflection::Kind::Array) {
        slang::TypeReflection* elementType = fieldType->getElementType();
        if (elementType && elementType->getKind() == slang::TypeReflection::Kind::Struct) {
            slang::TypeLayoutReflection* elementLayout = fieldTypeLayout->getElementTypeLayout();
            if (elementLayout) {
                std::string elementTypeName = ShaderReflection::getFullTypeName(elementType);
                logStructLayout(elementLayout, elementTypeName, indentLevel + 1);
            }
        }
    } else if (fieldType->getKind() == slang::TypeReflection::Kind::Struct) {
        std::string typeStr = ShaderReflection::getFullTypeName(fieldType);
        logStructLayout(fieldTypeLayout, typeStr, indentLevel + 1);
    }
}

static void logStructLayout(
    slang::TypeLayoutReflection* typeLayout,
    const std::string& structName,
    int indentLevel
) {
    if (!typeLayout) return;

    std::string indent(indentLevel * 2, ' ');
    Log::debug(LOG_TAG, "{}=== STRUCT: {} (size: {}, align: {}) ===",
        indent, structName, typeLayout->getSize(), typeLayout->getAlignment());

    SlangInt fieldCount = typeLayout->getFieldCount();
    for (SlangInt i = 0; i < fieldCount; ++i) {
        slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);
        if (!field) continue;

        slang::TypeLayoutReflection* fieldTypeLayout = field->getTypeLayout();
        if (!fieldTypeLayout) continue;

        slang::TypeReflection* fieldType = fieldTypeLayout->getType();
        if (!fieldType) continue;

        logFieldLayout(field, fieldTypeLayout, fieldType, static_cast<int>(i), indentLevel);
        logFieldRecursive(fieldTypeLayout, fieldType, indentLevel);
    }

    Log::debug(LOG_TAG, "{}=== END {} ===", indent, structName);
}

static void logBufferLayout(
    const std::string& name,
    int set,
    int binding,
    slang::TypeLayoutReflection* layout
) {
    Log::debug(LOG_TAG, "=== UNIFORM BUFFER: {} (set={}, binding={}) ===",
        name, set, binding);
    Log::debug(LOG_TAG, "  Size: {} bytes, Alignment: {} bytes",
        layout->getSize(), layout->getAlignment());

    SlangInt memberCount = layout->getFieldCount();
    for (SlangInt i = 0; i < memberCount; ++i) {
        slang::VariableLayoutReflection* memberVar = layout->getFieldByIndex(i);
        if (!memberVar) continue;

        slang::TypeLayoutReflection* memberTypeLayout = memberVar->getTypeLayout();
        slang::TypeReflection* memberType = memberTypeLayout->getType();

        const char* memberName = memberVar->getName();
        size_t offset = memberVar->getOffset();
        size_t size = memberTypeLayout->getSize();
        size_t align = memberTypeLayout->getAlignment();

        if (memberType->getKind() == slang::TypeReflection::Kind::Array) {
            slang::TypeReflection* elementType = memberType->getElementType();
            std::string elementTypeName = ShaderReflection::getFullTypeName(elementType);
            size_t stride = memberTypeLayout->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);

            Log::debug(LOG_TAG, "  [{}] {} : {}[{}] (offset: {}, size: {}, stride: {}, align: {})",
                i, memberName ? memberName : "unnamed",
                elementTypeName, memberType->getElementCount(),
                offset, size, stride, align);

            if (elementType->getKind() == slang::TypeReflection::Kind::Struct) {
                slang::TypeLayoutReflection* elemLayout = memberTypeLayout->getElementTypeLayout();
                if (elemLayout) {
                    logStructLayout(elemLayout, elementTypeName, 2);
                }
            }
        } else {
            std::string typeName = ShaderReflection::getFullTypeName(memberType);
            Log::debug(LOG_TAG, "  [{}] {} : {} (offset: {}, size: {}, align: {})",
                i, memberName ? memberName : "unnamed", typeName, offset, size, align);

            if (memberType->getKind() == slang::TypeReflection::Kind::Struct) {
                logStructLayout(memberTypeLayout, typeName, 2);
            }
        }
    }
    Log::debug(LOG_TAG, "=== END {} ===", name);
}

// ============================================================================
// Nested Struct Detection
// ============================================================================

static std::vector<StructInfo> extractNestedStructTypes(
    slang::TypeReflection* parentStruct,
    std::set<std::string>& seenStructs
) {
    std::vector<StructInfo> nested;
    if (!parentStruct) return nested;

    SlangInt memberCount = parentStruct->getFieldCount();
    for (SlangInt i = 0; i < memberCount; ++i) {
        slang::VariableReflection* member = parentStruct->getFieldByIndex(i);
        if (!member) continue;

        slang::TypeReflection* memberType = member->getType();
        if (!memberType) continue;

        slang::TypeReflection* structType = nullptr;

        // Check for array of structs
        if (memberType->getKind() == slang::TypeReflection::Kind::Array) {
            slang::TypeReflection* elementType = memberType->getElementType();
            if (elementType && elementType->getKind() == slang::TypeReflection::Kind::Struct) {
                structType = elementType;
            }
        }
        // Check for direct struct member
        else if (memberType->getKind() == slang::TypeReflection::Kind::Struct) {
            structType = memberType;
        }

        if (!structType) continue;

        const char* name = structType->getName();
        std::string structName = name ? name : "";

        if (structName.empty() || seenStructs.count(structName)) continue;

        seenStructs.insert(structName);

        std::string memberName = member->getName() ? member->getName() : structName;
        nested.push_back(extractStructFromType(structType, memberName, 0));

        // Recursively extract deeper nested structs
        auto deeper = extractNestedStructTypes(structType, seenStructs);
        nested.insert(nested.end(), deeper.begin(), deeper.end());
    }

    return nested;
}

// ============================================================================
// Output Collection
// ============================================================================

std::vector<OutputInfo> ShaderReflection::collectOutputs(
    slang::VariableLayoutReflection* varLayout
) {
    std::vector<OutputInfo> outputs;
    if (!varLayout) return outputs;

    slang::TypeLayoutReflection* typeLayout = varLayout->getTypeLayout();
    if (!typeLayout) return outputs;

    SlangInt fieldCount = typeLayout->getFieldCount();
    for (SlangInt i = 0; i < fieldCount; ++i) {
        slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);
        if (!field) continue;

        OutputInfo out;
        out.name = field->getName() ? field->getName() : "";
        out.semantic = field->getSemanticName() ? field->getSemanticName() : "";
        out.typeName = field->getTypeLayout()->getName()
            ? field->getTypeLayout()->getName()
            : "";
        outputs.push_back(std::move(out));
    }

    return outputs;
}

// ============================================================================
// Struct Detection (detectStructs helper functions)
// ============================================================================

static bool matchesFilter(const std::string& name, const std::string& filterLower) {
    std::string nameLower = name;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
    return nameLower.find(filterLower) != std::string::npos;
}

static slang::TypeReflection* getUnderlyingStructType(
    slang::TypeReflection* type,
    slang::TypeLayoutReflection* typeLayout,
    slang::TypeLayoutReflection** outStructLayout,
    int* outArraySize
) {
    *outArraySize = 0;
    *outStructLayout = nullptr;

    auto kind = type->getKind();

    // Direct struct
    if (kind == slang::TypeReflection::Kind::Struct) {
        *outStructLayout = typeLayout;
        return type;
    }

    // Array of structs
    if (kind == slang::TypeReflection::Kind::Array) {
        slang::TypeReflection* elementType = type->getElementType();
        *outArraySize = static_cast<int>(type->getElementCount());

        if (elementType->getKind() == slang::TypeReflection::Kind::Struct) {
            *outStructLayout = typeLayout->getElementTypeLayout();
            return elementType;
        }

        // Array of ConstantBuffers containing structs
        if (elementType->getKind() == slang::TypeReflection::Kind::ConstantBuffer) {
            slang::TypeReflection* cbElement = elementType->getElementType();
            if (cbElement && cbElement->getKind() == slang::TypeReflection::Kind::Struct) {
                auto cbLayout = typeLayout->getElementTypeLayout();
                if (cbLayout) {
                    *outStructLayout = cbLayout->getElementTypeLayout();
                }
                return cbElement;
            }
        }
    }

    // ParameterBlock or ConstantBuffer wrapping a struct
    if (kind == slang::TypeReflection::Kind::ParameterBlock ||
        kind == slang::TypeReflection::Kind::ConstantBuffer) {
        slang::TypeReflection* elementType = type->getElementType();
        if (elementType && elementType->getKind() == slang::TypeReflection::Kind::Struct) {
            *outStructLayout = typeLayout->getElementTypeLayout();
            return elementType;
        }
    }

    return nullptr;
}

std::vector<StructInfo> ShaderReflection::detectStructs(
    slang::TypeLayoutReflection* typeLayout,
    const std::string& structFilter
) {
    std::vector<StructInfo> structs;
    if (!typeLayout) return structs;

    std::string filterLower = structFilter;
    std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);

    std::set<std::string> seenStructs;
    SlangInt fieldCount = typeLayout->getFieldCount();

    for (SlangInt i = 0; i < fieldCount; ++i) {
        slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);
        if (!field) continue;

        slang::TypeLayoutReflection* fieldTypeLayout = field->getTypeLayout();
        slang::TypeReflection* fieldType = fieldTypeLayout->getType();
        const char* fieldName = field->getName();

        // Try to extract underlying struct type
        slang::TypeLayoutReflection* structLayout = nullptr;
        int arraySize = 0;
        slang::TypeReflection* structType = getUnderlyingStructType(
            fieldType, fieldTypeLayout, &structLayout, &arraySize);

        if (structType) {
            const char* structName = structType->getName();
            std::string structNameStr = structName ? structName : "";

            if (matchesFilter(structNameStr, filterLower)) {
                if (seenStructs.count(structNameStr) == 0) {
                    seenStructs.insert(structNameStr);

                    // Log struct layout for debugging
                    if (structLayout) {
                        logStructLayout(structLayout, structNameStr);
                    }

                    // Extract nested structs first (they need to be defined before parent)
                    auto nestedStructs = extractNestedStructTypes(structType, seenStructs);
                    structs.insert(structs.end(), nestedStructs.begin(), nestedStructs.end());

                    // Add the parent struct
                    std::string instanceName = fieldName ? fieldName : "unnamed";
                    structs.push_back(extractStructFromType(structType, instanceName, arraySize));
                }
                continue;
            }
        }

        // Recursively check wrapped types (ParameterBlock, ConstantBuffer, Struct)
        auto fieldKind = fieldType->getKind();
        if (fieldKind == slang::TypeReflection::Kind::ParameterBlock ||
            fieldKind == slang::TypeReflection::Kind::ConstantBuffer ||
            fieldKind == slang::TypeReflection::Kind::Struct) {

            slang::TypeLayoutReflection* layoutToRecurse = nullptr;
            if (fieldKind == slang::TypeReflection::Kind::ParameterBlock ||
                fieldKind == slang::TypeReflection::Kind::ConstantBuffer) {
                layoutToRecurse = fieldTypeLayout->getElementTypeLayout();
            } else {
                layoutToRecurse = fieldTypeLayout;
            }

            if (layoutToRecurse) {
                auto nested = detectStructs(layoutToRecurse, structFilter);
                structs.insert(structs.end(), nested.begin(), nested.end());
            }
        }
    }

    return structs;
}

// ============================================================================
// Binding Parsing (parseBindings helper functions)
// ============================================================================

static VkDescriptorType mapBindingTypeToVulkan(slang::BindingType bindingType) {
    SlangBindingType raw = static_cast<SlangBindingType>(bindingType);
    switch (raw & SLANG_BINDING_TYPE_BASE_MASK) {
    case SLANG_BINDING_TYPE_CONSTANT_BUFFER:
    case SLANG_BINDING_TYPE_PARAMETER_BLOCK:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case SLANG_BINDING_TYPE_COMBINED_TEXTURE_SAMPLER:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case SLANG_BINDING_TYPE_TEXTURE:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case SLANG_BINDING_TYPE_SAMPLER:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case SLANG_BINDING_TYPE_RAW_BUFFER:
    case SLANG_BINDING_TYPE_TYPED_BUFFER:
        if (raw & SLANG_BINDING_TYPE_MUTABLE_FLAG) {
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    default:
        return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }
}

static const char* slangBindingTypeToString(slang::BindingType bindingType) {
    SlangBindingType raw = static_cast<SlangBindingType>(bindingType);
    switch (raw & SLANG_BINDING_TYPE_BASE_MASK) {
    case SLANG_BINDING_TYPE_SAMPLER:              return "sampler";
    case SLANG_BINDING_TYPE_TEXTURE:              return "texture";
    case SLANG_BINDING_TYPE_CONSTANT_BUFFER:      return "constant buffer";
    case SLANG_BINDING_TYPE_PARAMETER_BLOCK:      return "parameter block";
    case SLANG_BINDING_TYPE_TYPED_BUFFER:         return "typed buffer";
    case SLANG_BINDING_TYPE_RAW_BUFFER:           return "raw buffer";
    case SLANG_BINDING_TYPE_COMBINED_TEXTURE_SAMPLER: return "combined texture sampler";
    case SLANG_BINDING_TYPE_INPUT_RENDER_TARGET:  return "input render target";
    case SLANG_BINDING_TYPE_INLINE_UNIFORM_DATA:  return "inline uniform data";
    case SLANG_BINDING_TYPE_RAY_TRACING_ACCELERATION_STRUCTURE: return "acceleration structure";
    case SLANG_BINDING_TYPE_PUSH_CONSTANT:        return "push constant";
    default:                                      return "unknown";
    }
}

static const char* slangCategoryToString(slang::ParameterCategory category) {
    switch (static_cast<SlangParameterCategory>(category)) {
    case SLANG_PARAMETER_CATEGORY_NONE:                return "none";
    case SLANG_PARAMETER_CATEGORY_MIXED:               return "mixed";
    case SLANG_PARAMETER_CATEGORY_CONSTANT_BUFFER:     return "constant buffer";
    case SLANG_PARAMETER_CATEGORY_SHADER_RESOURCE:     return "shader resource";
    case SLANG_PARAMETER_CATEGORY_UNORDERED_ACCESS:    return "unordered access";
    case SLANG_PARAMETER_CATEGORY_VARYING_INPUT:       return "varying input";
    case SLANG_PARAMETER_CATEGORY_VARYING_OUTPUT:      return "varying output";
    case SLANG_PARAMETER_CATEGORY_SAMPLER_STATE:       return "sampler state";
    case SLANG_PARAMETER_CATEGORY_UNIFORM:             return "uniform";
    case SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT: return "descriptor table slot";
    case SLANG_PARAMETER_CATEGORY_PUSH_CONSTANT_BUFFER: return "push constant buffer";
    case SLANG_PARAMETER_CATEGORY_REGISTER_SPACE:      return "register space";
    default:                                           return "unknown";
    }
}

static std::string extractStructTypeName(slang::TypeLayoutReflection* leafTypeLayout) {
    if (!leafTypeLayout) return "";

    auto kind = leafTypeLayout->getKind();
    if (kind == slang::TypeReflection::Kind::ParameterBlock ||
        kind == slang::TypeReflection::Kind::ConstantBuffer) {
        slang::TypeLayoutReflection* elementLayout = leafTypeLayout->getElementTypeLayout();
        if (elementLayout && elementLayout->getType() && elementLayout->getType()->getName()) {
            return elementLayout->getType()->getName();
        }
    }

    const char* name = leafTypeLayout->getName();
    return name ? name : "";
}

static void extractBufferMembers(
    slang::TypeLayoutReflection* elementLayout,
    const std::string& resourceName,
    int vulkanSet,
    int vulkanBinding,
    std::vector<MemberInfo>& outMembers
) {
    if (!elementLayout) return;

    logBufferLayout(resourceName, vulkanSet, vulkanBinding, elementLayout);

    SlangInt memberCount = elementLayout->getFieldCount();
    for (SlangInt m = 0; m < memberCount; ++m) {
        slang::VariableLayoutReflection* memberVar = elementLayout->getFieldByIndex(m);
        if (!memberVar) continue;

        MemberInfo memberInfo;
        memberInfo.name = memberVar->getName() ? memberVar->getName() : "unnamed";
        memberInfo.offset = static_cast<int>(memberVar->getOffset());

        slang::TypeReflection* memberType = memberVar->getTypeLayout()->getType();

        if (memberType->getKind() == slang::TypeReflection::Kind::Array) {
            slang::TypeReflection* elementType = memberType->getElementType();
            memberInfo.typeName = ShaderReflection::getFullTypeName(elementType);
            memberInfo.arraySize = static_cast<int>(memberType->getElementCount());
            memberInfo.typeKind = ShaderReflection::getTypeKindName(elementType->getKind());
        } else {
            memberInfo.typeName = ShaderReflection::getFullTypeName(memberType);
            memberInfo.arraySize = 0;
            memberInfo.typeKind = ShaderReflection::getTypeKindName(memberType->getKind());
        }

        outMembers.push_back(std::move(memberInfo));
    }
}

// Extract Vulkan set/binding from variable layout
// Handles both regular bindings (DescriptorTableSlot) and ParameterBlocks (SubElementRegisterSpace)
static void extractVulkanBinding(
    slang::VariableLayoutReflection* varLayout,
    slang::TypeLayoutReflection* typeLayout,
    int& outSet,
    int& outBinding
) {
    outSet = 0;
    outBinding = 0;

    if (!varLayout) return;

    // Check if this is a ParameterBlock - uses SubElementRegisterSpace for set number
    if (typeLayout && typeLayout->getKind() == slang::TypeReflection::Kind::ParameterBlock) {
        // For ParameterBlock: set comes from SubElementRegisterSpace
        size_t space = varLayout->getOffset(slang::ParameterCategory::SubElementRegisterSpace);
        if (space != SIZE_MAX) {
            outSet = static_cast<int>(space);
        }

        // Binding comes from the containerVarLayout's DescriptorTableSlot
        slang::VariableLayoutReflection* containerLayout = typeLayout->getContainerVarLayout();
        if (containerLayout) {
            size_t binding = containerLayout->getOffset(slang::ParameterCategory::DescriptorTableSlot);
            if (binding != SIZE_MAX) {
                outBinding = static_cast<int>(binding);
            }
        }

        Log::debug(LOG_TAG, "    ParameterBlock {} -> set={}, binding={}",
            varLayout->getName() ? varLayout->getName() : "unnamed", outSet, outBinding);
        return;
    }

    // Regular case: DescriptorTableSlot category gives us the Vulkan binding/set
    size_t binding = varLayout->getOffset(slang::ParameterCategory::DescriptorTableSlot);
    size_t set = varLayout->getBindingSpace(slang::ParameterCategory::DescriptorTableSlot);

    if (binding != SIZE_MAX) {
        outBinding = static_cast<int>(binding);
        outSet = (set != SIZE_MAX) ? static_cast<int>(set) : 0;
        return;
    }

    // Fallback: use the primary category
    slang::ParameterCategory category = varLayout->getCategory();
    binding = varLayout->getOffset(category);
    set = varLayout->getBindingSpace(category);
    outBinding = (binding != SIZE_MAX) ? static_cast<int>(binding) : 0;
    outSet = (set != SIZE_MAX) ? static_cast<int>(set) : 0;
}

// Get binding type for a type layout
static slang::BindingType getBindingTypeForTypeLayout(slang::TypeLayoutReflection* typeLayout) {
    // Default to a value that will map to VK_DESCRIPTOR_TYPE_MAX_ENUM
    slang::BindingType unknownType = static_cast<slang::BindingType>(0);

    if (!typeLayout) return unknownType;

    auto kind = typeLayout->getKind();
    switch (kind) {
    case slang::TypeReflection::Kind::ConstantBuffer:
        return slang::BindingType::ConstantBuffer;
    case slang::TypeReflection::Kind::ParameterBlock:
        return slang::BindingType::ParameterBlock;
    case slang::TypeReflection::Kind::SamplerState:
        return slang::BindingType::Sampler;
    case slang::TypeReflection::Kind::Resource: {
        // Check if it's a combined texture sampler by looking at binding ranges
        SlangInt bindingRangeCount = typeLayout->getBindingRangeCount();
        if (bindingRangeCount > 0) {
            return typeLayout->getBindingRangeType(0);
        }
        return slang::BindingType::Texture;
    }
    case slang::TypeReflection::Kind::Array: {
        // For arrays, get the element type's binding type
        slang::TypeLayoutReflection* elementLayout = typeLayout->getElementTypeLayout();
        return getBindingTypeForTypeLayout(elementLayout);
    }
    default:
        return unknownType;
    }
}

std::vector<BindingInfo> ShaderReflection::parseBindings(
    slang::ProgramLayout* layout,
    SlangStage stage
) {
    std::vector<BindingInfo> bindings;
    if (!layout) return bindings;

    slang::VariableLayoutReflection* globalVarLayout = layout->getGlobalParamsVarLayout();
    if (!globalVarLayout) return bindings;

    slang::TypeLayoutReflection* globalTypeLayout = globalVarLayout->getTypeLayout();
    if (!globalTypeLayout) return bindings;

    VkShaderStageFlags stageFlags = getVkStageFlags(stage);

    // Iterate through global parameters directly (like the JSON reflection shows)
    SlangInt paramCount = globalTypeLayout->getFieldCount();

    Log::debug(LOG_TAG, "Parsing {} global parameters for stage {}",
        paramCount, shaderStageToString(stageFlags));

    for (SlangInt i = 0; i < paramCount; ++i) {
        slang::VariableLayoutReflection* paramLayout = globalTypeLayout->getFieldByIndex(i);
        if (!paramLayout) continue;

        slang::TypeLayoutReflection* typeLayout = paramLayout->getTypeLayout();
        if (!typeLayout) continue;

        const char* paramName = paramLayout->getName();
        std::string resourceName = paramName ? paramName : "Unnamed";

        Log::debug(LOG_TAG, "  Parameter {}: {} (kind: {})",
            i, resourceName, getTypeKindName(typeLayout->getKind()));

        // Get Vulkan set/binding
        int vulkanSet = 0, vulkanBinding = 0;
        extractVulkanBinding(paramLayout, typeLayout, vulkanSet, vulkanBinding);

        // Get binding type and descriptor count
        slang::BindingType bindingType = getBindingTypeForTypeLayout(typeLayout);
        uint32_t arrayCount = 1;

        // Handle arrays
        slang::TypeLayoutReflection* effectiveTypeLayout = typeLayout;
        if (typeLayout->getKind() == slang::TypeReflection::Kind::Array) {
            arrayCount = static_cast<uint32_t>(typeLayout->getElementCount());
            effectiveTypeLayout = typeLayout->getElementTypeLayout();
        }

        BindingInfo binding;
        binding.resourceName = resourceName;
        binding.vulkanSet = vulkanSet;
        binding.vulkanBinding = vulkanBinding;
        binding.descriptorType = mapBindingTypeToVulkan(bindingType);
        binding.stageFlags = stageFlags;
        binding.isInput = true;
        binding.arrayCount = arrayCount;
        binding.typeKind = getTypeKindName(effectiveTypeLayout->getKind());
        binding.typeName = extractStructTypeName(effectiveTypeLayout);

        // Extract members for buffer types
        auto kind = effectiveTypeLayout->getKind();
        if (kind == slang::TypeReflection::Kind::ParameterBlock ||
            kind == slang::TypeReflection::Kind::ConstantBuffer) {
            slang::TypeLayoutReflection* elementLayout = effectiveTypeLayout->getElementTypeLayout();
            extractBufferMembers(elementLayout, binding.resourceName,
                binding.vulkanSet, binding.vulkanBinding, binding.members);
        }

        Log::debug(LOG_TAG, "    -> set={}, binding={}, type={}, descriptor={}",
            vulkanSet, vulkanBinding, binding.typeKind,
            descriptorTypeToString(binding.descriptorType));

        bindings.push_back(std::move(binding));
    }

    return bindings;
}

// ============================================================================
// Vertex Input Collection
// ============================================================================

std::vector<VertexInputAttribute> ShaderReflection::collectVertexInputs(
    slang::EntryPointReflection* entryPoint
) {
    std::vector<VertexInputAttribute> attributes;
    if (!entryPoint) return attributes;

    SlangInt paramCount = entryPoint->getParameterCount();
    Log::debug(LOG_TAG, "Scanning {} parameters for vertex inputs", paramCount);

    for (SlangInt i = 0; i < paramCount; ++i) {
        slang::VariableLayoutReflection* param = entryPoint->getParameterByIndex(i);
        if (!param) continue;

        if (param->getCategory() != slang::ParameterCategory::VaryingInput) continue;

        slang::TypeLayoutReflection* typeLayout = param->getTypeLayout();
        slang::TypeReflection* type = typeLayout->getType();

        if (type->getKind() != slang::TypeReflection::Kind::Struct) continue;

        SlangInt fieldCount = typeLayout->getFieldCount();
        Log::debug(LOG_TAG, "Found vertex input struct with {} fields", fieldCount);

        uint32_t currentOffset = 0;
        for (SlangInt f = 0; f < fieldCount; ++f) {
            slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(f);
            if (!field) continue;

            VertexInputAttribute attr;
            attr.name = field->getName() ? field->getName() : "unnamed";
            attr.semantic = field->getSemanticName() ? field->getSemanticName() : "";
            attr.typeName = getFullTypeName(field->getTypeLayout()->getType());
            attr.location = static_cast<uint32_t>(
                field->getOffset(slang::ParameterCategory::VaryingInput));
            attr.format = getVkFormatFromTypeName(attr.typeName);
            attr.offset = currentOffset;
            attr.binding = 0;

            currentOffset += getTypeSize(attr.typeName);
            attributes.push_back(attr);

            Log::debug(LOG_TAG, "  Vertex attr: {} (location={}, format={}, offset={})",
                attr.name, attr.location, getVkFormatString(attr.format), attr.offset);
        }
    }

    return attributes;
}

// ============================================================================
// Shader Compilation Pipeline
// ============================================================================

static Slang::ComPtr<slang::ISession> createSlangSession(
    slang::IGlobalSession* globalSession,
    const std::filesystem::path& projectRoot
) {
    slang::SessionDesc sessionDesc = {};
    slang::TargetDesc targetDesc = {};
    targetDesc.format = SLANG_SPIRV;
    targetDesc.profile = globalSession->findProfile("spirv_1_5");

    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;
    sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

    std::array options{
        slang::CompilerOptionEntry{
            slang::CompilerOptionName::EmitSpirvDirectly,
            {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
        },
        slang::CompilerOptionEntry{
            slang::CompilerOptionName::VulkanUseEntryPointName,
            {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
        }
    };

    std::string shaderSearchPath = projectRoot.empty()
        ? "shaders"
        : (projectRoot / "shaders").string();

    const char* searchPaths[] = {shaderSearchPath.c_str()};
    sessionDesc.compilerOptionEntries = options.data();
    sessionDesc.compilerOptionEntryCount = options.size();
    sessionDesc.searchPaths = searchPaths;
    sessionDesc.searchPathCount = 1;

    Slang::ComPtr<slang::ISession> session;
    globalSession->createSession(sessionDesc, session.writeRef());
    return session;
}

static Slang::ComPtr<slang::IModule> loadShaderModule(
    slang::ISession* session,
    const std::filesystem::path& moduleName,
    ShaderParsedResult& result
) {
    Slang::ComPtr<slang::IBlob> diagnosticsBlob;
    auto modulePathStr = moduleName.string();

    slang::IModule* rawModule = session->loadModule(
        modulePathStr.c_str(), diagnosticsBlob.writeRef());

    if (!rawModule) {
        result.errorMessage = getDiagnosticMessage(diagnosticsBlob);
        ShaderReflection::diagnoseIfNeeded(diagnosticsBlob);
        Log::error(LOG_TAG, "Failed to load shader module: {} (syntax error)", moduleName.string());
        return nullptr;
    }

    if (diagnosticsBlob) {
        result.warningMessage = getDiagnosticMessage(diagnosticsBlob);
        ShaderReflection::diagnoseIfNeeded(diagnosticsBlob, false);
    }

    return Slang::ComPtr<slang::IModule>(rawModule);
}

static Slang::ComPtr<slang::IEntryPoint> findEntryPoint(
    slang::IModule* module,
    SlangStage stage
) {
    SlangInt32 count = module->getDefinedEntryPointCount();
    for (SlangInt32 idx = 0; idx < count; ++idx) {
        Slang::ComPtr<slang::IEntryPoint> entry;
        module->getDefinedEntryPoint(idx, entry.writeRef());

        slang::EntryPointReflection* reflection = entry->getLayout()->getEntryPointByIndex(0);
        if (reflection->getStage() == stage) {
            return entry;
        }
    }

    Log::error(LOG_TAG, "No entry point found matching requested shader stage");
    return nullptr;
}

static Slang::ComPtr<slang::IComponentType> linkProgram(
    slang::ISession* session,
    slang::IModule* module,
    slang::IEntryPoint* entryPoint
) {
    std::array<slang::IComponentType*, 2> components = {module, entryPoint};

    Slang::ComPtr<slang::IBlob> diagnosticsBlob;
    Slang::ComPtr<slang::IComponentType> composed;

    if (SLANG_FAILED(session->createCompositeComponentType(
            components.data(), components.size(),
            composed.writeRef(), diagnosticsBlob.writeRef()))) {
        ShaderReflection::diagnoseIfNeeded(diagnosticsBlob);
        return nullptr;
    }

    Slang::ComPtr<slang::IComponentType> linked;
    if (SLANG_FAILED(composed->link(linked.writeRef(), diagnosticsBlob.writeRef()))) {
        ShaderReflection::diagnoseIfNeeded(diagnosticsBlob);
        return nullptr;
    }

    return linked;
}

static std::vector<uint32_t> getCompiledCode(slang::IComponentType* linkedProgram) {
    Slang::ComPtr<slang::IBlob> codeBlob;
    Slang::ComPtr<slang::IBlob> diagnosticsBlob;

    if (SLANG_FAILED(linkedProgram->getEntryPointCode(
            0, 0, codeBlob.writeRef(), diagnosticsBlob.writeRef()))) {
        ShaderReflection::diagnoseIfNeeded(diagnosticsBlob);
        return {};
    }

    if (!codeBlob) {
        Log::error(LOG_TAG, "Failed to generate SPIR-V code");
        return {};
    }

    auto ptr = static_cast<const uint32_t*>(codeBlob->getBufferPointer());
    size_t count = codeBlob->getBufferSize() / sizeof(uint32_t);
    return std::vector<uint32_t>(ptr, ptr + count);
}

ShaderParsedResult ShaderReflection::reflectShader(
    const std::filesystem::path& moduleName,
    SlangStage stage,
    const std::filesystem::path& projectRoot
) {
    ShaderParsedResult result;

    Log::debug(LOG_TAG, "Reflecting shader: {} (stage: {})",
        moduleName.string(), stage == SLANG_STAGE_VERTEX ? "vertex" : "fragment");

    // Create session
    auto session = createSlangSession(globalSession.get(), projectRoot);
    if (!session) {
        result.errorMessage = "Failed to create Slang session";
        return result;
    }

    // Load module
    auto module = loadShaderModule(session.get(), moduleName, result);
    if (!module) return result;

    // Find entry point
    auto entryPoint = findEntryPoint(module.get(), stage);
    if (!entryPoint) return result;

    // Link program
    auto linkedProgram = linkProgram(session.get(), module.get(), entryPoint.get());
    if (!linkedProgram) return result;

    // Get compiled SPIR-V
    result.code = getCompiledCode(linkedProgram.get());
    if (result.code.empty()) return result;

    result.success = true;
    Log::debug(LOG_TAG, "Shader compiled successfully ({} bytes SPIR-V)",
        result.code.size() * sizeof(uint32_t));

    // Get reflection data
    slang::ProgramLayout* programLayout = linkedProgram->getLayout();
    if (!programLayout) return result;

    slang::EntryPointReflection* entryPointLayout = programLayout->getEntryPointByIndex(0);
    if (entryPointLayout && entryPointLayout->getName()) {
        result.entryPointName = entryPointLayout->getName();
    }

    // Collect stage-specific data
    if (stage == SLANG_STAGE_VERTEX) {
        result.vertexAttributes = collectVertexInputs(entryPointLayout);
    } else if (stage == SLANG_STAGE_FRAGMENT) {
        if (auto* results = entryPointLayout->getResultVarLayout()) {
            result.outputs = collectOutputs(results);
        }
    }

    // Collect bindings
    result.bindings = parseBindings(programLayout, stage);

    // Detect special struct types (lights/cameras)
    slang::VariableLayoutReflection* varLayout = programLayout->getGlobalParamsVarLayout();
    if (varLayout) {
        slang::TypeLayoutReflection* typeLayout = varLayout->getTypeLayout();
        if (typeLayout) {
            result.lightStructs = detectStructs(typeLayout, "light");
            result.cameraStructs = detectStructs(typeLayout, "camera");
        }
    }

    printParsedResult(result);
    return result;
}

// ============================================================================
// Debug Output
// ============================================================================

void ShaderReflection::printParsedResult(const ShaderParsedResult& result) {
    Log::debug(LOG_TAG, "========== SHADER REFLECTION SUMMARY ==========");

    if (!result.vertexAttributes.empty()) {
        Log::debug(LOG_TAG, "[Vertex Attributes] ({} total)", result.vertexAttributes.size());
        for (const auto& attr : result.vertexAttributes) {
            Log::debug(LOG_TAG, "  {} | loc={} | bind={} | offset={} | {}",
                attr.name, attr.location, attr.binding, attr.offset,
                getVkFormatString(attr.format));
        }
    }

    if (!result.outputs.empty()) {
        Log::debug(LOG_TAG, "[Shader Outputs] ({} total)", result.outputs.size());
        for (const auto& out : result.outputs) {
            Log::debug(LOG_TAG, "  {} | semantic={} | type={}",
                out.name, out.semantic, out.typeName);
        }
    }

    if (!result.bindings.empty()) {
        // Group bindings by descriptor set for clearer output
        std::map<int, std::vector<const BindingInfo*>> bindingsBySet;
        for (const auto& b : result.bindings) {
            bindingsBySet[b.vulkanSet].push_back(&b);
        }

        Log::debug(LOG_TAG, "[Descriptor Sets] ({} sets, {} bindings total)",
            bindingsBySet.size(), result.bindings.size());

        for (const auto& [setNum, setBindings] : bindingsBySet) {
            Log::debug(LOG_TAG, "  Set {}: {} descriptor ranges", setNum, setBindings.size());

            for (const auto* b : setBindings) {
                std::string arrayStr = b->arrayCount > 1
                    ? "[" + std::to_string(b->arrayCount) + "]" : "";

                Log::debug(LOG_TAG, "    binding={}: {} | type='{}' | descriptor='{}'{}",
                    b->vulkanBinding,
                    b->resourceName,
                    b->typeKind,
                    descriptorTypeToString(b->descriptorType),
                    arrayStr);

                if (!b->typeName.empty()) {
                    Log::debug(LOG_TAG, "      struct type: {}", b->typeName);
                }

                for (const auto& m : b->members) {
                    std::string memberArrayStr = m.arraySize > 0
                        ? "[" + std::to_string(m.arraySize) + "]" : "";
                    Log::debug(LOG_TAG, "        {} : {} (offset: {}{})",
                        m.name, m.typeName, m.offset, memberArrayStr);
                }
            }
        }
    }

    auto printStructs = [](const std::string& label, const std::vector<StructInfo>& list) {
        if (list.empty()) return;
        Log::debug(LOG_TAG, "[{}] ({} total)", label, list.size());
        for (const auto& s : list) {
            Log::debug(LOG_TAG, "  {} | type={} | elements={} | members={}",
                s.instanceName, s.structName,
                s.arraySize > 0 ? std::to_string(s.arraySize) : "1",
                s.members.size());
        }
    };

    printStructs("Light Structs", result.lightStructs);
    printStructs("Camera Structs", result.cameraStructs);

    Log::debug(LOG_TAG, "================================================");
}
