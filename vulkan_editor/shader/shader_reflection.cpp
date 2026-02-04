// shader_reflection.cpp
#include "shader_reflection.h"
#include "../util/logger.h"
#include "../graph/node.h"
#include <algorithm>
#include <array>
#include <set>
#include <sstream>

using namespace Slang;
using namespace ShaderTypes;

ComPtr<slang::IGlobalSession> ShaderReflection::initializeSlang() {
    if (globalSession)
        return globalSession;
    createGlobalSession(globalSession.writeRef());
    return globalSession;
}

void ShaderReflection::resetSession() {
    // Clear the global session to force fresh shader parsing
    // This is needed for hot reload to pick up changes
    globalSession = nullptr;
    Log::debug("ShaderReflection", "Session reset for hot reload");
}

void ShaderReflection::diagnoseIfNeeded(slang::IBlob* diagnosticsBlob, bool isError) {
    if (diagnosticsBlob) {
        if (isError) {
            Log::error(
                "ShaderReflection", "Shader compilation error:\n{}",
                (const char*)diagnosticsBlob->getBufferPointer()
            );
        } else {
            Log::warning(
                "ShaderReflection", "Shader compilation warning:\n{}",
                (const char*)diagnosticsBlob->getBufferPointer()
            );
        }
    }
}

static std::string getDiagnosticMessage(slang::IBlob* diagnosticsBlob) {
    if (diagnosticsBlob) {
        return std::string(
            (const char*)diagnosticsBlob->getBufferPointer()
        );
    }
    return "";
}

const char*
ShaderReflection::getTypeKindName(slang::TypeReflection::Kind kind) {
    using Kind = slang::TypeReflection::Kind;
    switch (kind) {
    case Kind::None:
        return "None";
    case Kind::Struct:
        return "Struct";
    case Kind::Array:
        return "Array";
    case Kind::Matrix:
        return "Matrix";
    case Kind::Vector:
        return "Vector";
    case Kind::Scalar:
        return "Scalar";
    case Kind::ConstantBuffer:
        return "ConstantBuffer";
    case Kind::Resource:
        return "Resource";
    case Kind::SamplerState:
        return "SamplerState";
    case Kind::TextureBuffer:
        return "TextureBuffer";
    case Kind::ShaderStorageBuffer:
        return "ShaderStorageBuffer";
    case Kind::ParameterBlock:
        return "ParameterBlock";
    case Kind::GenericTypeParameter:
        return "GenericTypeParameter";
    case Kind::Interface:
        return "Interface";
    case Kind::OutputStream:
        return "OutputStream";
    case Kind::Specialized:
        return "Specialized";
    case Kind::Feedback:
        return "Feedback";
    case Kind::Pointer:
        return "Pointer";
    case Kind::DynamicResource:
        return "DynamicResource";
    default:
        return "Unknown";
    }
}

std::string ShaderReflection::getVkFormatString(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R32G32_SFLOAT:
        return "R32G32_SFLOAT (float2)";
    case VK_FORMAT_R32G32B32_SFLOAT:
        return "R32G32B32_SFLOAT (float3)";
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return "R32G32B32A32_SFLOAT (float4)";
    case VK_FORMAT_R32_SFLOAT:
        return "R32_SFLOAT (float)";
    default:
        return "UNDEFINED / UNKNOWN";
    }
}

VkFormat
ShaderReflection::getVkFormatFromTypeName(const std::string& typeName) {
    if (typeName == "float3")
        return VK_FORMAT_R32G32B32_SFLOAT;
    if (typeName == "float2")
        return VK_FORMAT_R32G32_SFLOAT;
    if (typeName == "float4")
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    if (typeName == "float")
        return VK_FORMAT_R32_SFLOAT;
    return VK_FORMAT_UNDEFINED;
}

uint32_t ShaderReflection::getTypeSize(const std::string& typeName) {
    if (typeName == "float3")
        return sizeof(float) * 3;
    if (typeName == "float2")
        return sizeof(float) * 2;
    if (typeName == "float4")
        return sizeof(float) * 4;
    if (typeName == "float")
        return sizeof(float) * 1;
    return 0;
}

std::string
ShaderReflection::getFullTypeName(slang::TypeReflection* type) {
    std::string base;
    auto scalar = type->getScalarType();

    switch (scalar) {
    case slang::TypeReflection::ScalarType::Float32:
        base = "float";
        break;
    case slang::TypeReflection::ScalarType::Int32:
        base = "int";
        break;
    case slang::TypeReflection::ScalarType::UInt32:
        base = "uint";
        break;
    case slang::TypeReflection::ScalarType::Bool:
        base = "bool";
        break;
    default:
        base = "unknown";
        break;
    }

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

// Helper to create MemberInfo with proper array handling
MemberInfo ShaderReflection::extractMemberInfo(slang::VariableReflection* member) {
    MemberInfo memberInfo;
    memberInfo.name = member->getName() ? member->getName() : "unnamed";

    slang::TypeReflection* type = member->getType();
    if (type->getKind() == slang::TypeReflection::Kind::Array) {
        // For arrays, get element type and set array size
        slang::TypeReflection* elementType = type->getElementType();
        memberInfo.typeName = getFullTypeName(elementType);
        memberInfo.arraySize = static_cast<int>(type->getElementCount());
        memberInfo.typeKind = getTypeKindName(elementType->getKind());
    } else {
        memberInfo.typeName = getFullTypeName(type);
        memberInfo.arraySize = 0;
        memberInfo.typeKind = getTypeKindName(type->getKind());
    }

    return memberInfo;
}

VkShaderStageFlags ShaderReflection::getVkStageFlags(SlangStage stage) {
    switch (stage) {
    case SLANG_STAGE_VERTEX:
        return VK_SHADER_STAGE_VERTEX_BIT;
    case SLANG_STAGE_FRAGMENT:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    case SLANG_STAGE_COMPUTE:
        return VK_SHADER_STAGE_COMPUTE_BIT;
    default:
        return 0;
    }
}

std::vector<OutputInfo> ShaderReflection::collectOutputs(
    slang::VariableLayoutReflection* varLayout
) {
    std::vector<OutputInfo> outputs;
    if (!varLayout)
        return outputs;

    slang::TypeLayoutReflection* typeLayout =
        varLayout->getTypeLayout();
    if (!typeLayout)
        return outputs;

    SlangInt fieldCount = typeLayout->getFieldCount();
    for (SlangInt i = 0; i < fieldCount; ++i) {
        slang::VariableLayoutReflection* field =
            typeLayout->getFieldByIndex(i);
        if (!field)
            continue;

        OutputInfo out;
        out.name = field->getName() ? field->getName() : "";
        out.semantic =
            field->getSemanticName() ? field->getSemanticName() : "";
        out.typeName = field->getTypeLayout()->getName()
                           ? field->getTypeLayout()->getName()
                           : "";
        outputs.push_back(std::move(out));
    }
    return outputs;
}

// Helper to extract a struct definition from a TypeReflection
static StructInfo extractStructInfo(
    slang::TypeReflection* structType,
    const std::string& instanceName,
    int arraySize
) {
    StructInfo info;
    info.structName = structType->getName() ? structType->getName() : "";
    info.instanceName = instanceName;
    info.arraySize = arraySize;

    SlangInt memberCount = structType->getFieldCount();
    for (SlangInt m = 0; m < memberCount; ++m) {
        slang::VariableReflection* member = structType->getFieldByIndex(m);
        if (!member) continue;
        info.members.push_back(ShaderReflection::extractMemberInfo(member));
    }
    return info;
}

// Debug helper to dump struct layout from TypeLayoutReflection
static void dumpStructLayout(
    slang::TypeLayoutReflection* typeLayout,
    const std::string& structName,
    int indentLevel = 0
) {
    if (!typeLayout) return;

    std::string indent(indentLevel * 2, ' ');
    size_t totalSize = typeLayout->getSize();
    size_t alignment = typeLayout->getAlignment();

    Log::debug("ShaderReflection", "{}=== STRUCT LAYOUT: {} ===", indent, structName);
    Log::debug("ShaderReflection", "{}  Total Size: {} bytes", indent, totalSize);
    Log::debug("ShaderReflection", "{}  Alignment: {} bytes", indent, alignment);
    Log::debug("ShaderReflection", "{}  Fields:", indent);

    SlangInt fieldCount = typeLayout->getFieldCount();
    for (SlangInt i = 0; i < fieldCount; ++i) {
        slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);
        if (!field) continue;

        const char* fieldName = field->getName();
        size_t fieldOffset = field->getOffset();

        slang::TypeLayoutReflection* fieldTypeLayout = field->getTypeLayout();
        if (!fieldTypeLayout) continue;

        size_t fieldSize = fieldTypeLayout->getSize();
        size_t fieldAlignment = fieldTypeLayout->getAlignment();
        slang::TypeReflection* fieldType = fieldTypeLayout->getType();
        const char* typeName = fieldType ? fieldType->getName() : nullptr;

        std::string typeStr;
        if (typeName) {
            typeStr = typeName;
        } else if (fieldType) {
            typeStr = ShaderReflection::getFullTypeName(fieldType);
        } else {
            typeStr = "unknown";
        }

        // Check for arrays
        if (fieldType && fieldType->getKind() == slang::TypeReflection::Kind::Array) {
            size_t elementCount = fieldType->getElementCount();
            size_t stride = fieldTypeLayout->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
            slang::TypeReflection* elementType = fieldType->getElementType();
            std::string elementTypeName = elementType ? ShaderReflection::getFullTypeName(elementType) : "unknown";

            Log::debug("ShaderReflection",
                "{}    [{}] {} : {}[{}]  (offset: {}, size: {}, stride: {}, align: {})",
                indent, i, fieldName ? fieldName : "unnamed",
                elementTypeName, elementCount,
                fieldOffset, fieldSize, stride, fieldAlignment);

            // If array element is a struct, dump its layout too
            if (elementType && elementType->getKind() == slang::TypeReflection::Kind::Struct) {
                slang::TypeLayoutReflection* elementLayout = fieldTypeLayout->getElementTypeLayout();
                if (elementLayout) {
                    dumpStructLayout(elementLayout, elementTypeName, indentLevel + 2);
                }
            }
        } else {
            Log::debug("ShaderReflection",
                "{}    [{}] {} : {}  (offset: {}, size: {}, align: {})",
                indent, i, fieldName ? fieldName : "unnamed",
                typeStr, fieldOffset, fieldSize, fieldAlignment);

            // If field is a struct, recursively dump its layout
            if (fieldType && fieldType->getKind() == slang::TypeReflection::Kind::Struct) {
                dumpStructLayout(fieldTypeLayout, typeStr, indentLevel + 2);
            }
        }
    }

    Log::debug("ShaderReflection", "{}=== END {} ===", indent, structName);
}

// Helper to extract nested struct types from a struct's members (e.g., Light from LightsUBO)
static std::vector<StructInfo> extractNestedStructTypes(
    slang::TypeReflection* parentStruct,
    std::set<std::string>& seenStructs
) {
    std::vector<StructInfo> nested;
    if (!parentStruct) return nested;

    SlangInt memberCount = parentStruct->getFieldCount();
    for (SlangInt m = 0; m < memberCount; ++m) {
        slang::VariableReflection* member = parentStruct->getFieldByIndex(m);
        if (!member) continue;

        slang::TypeReflection* memberType = member->getType();
        if (!memberType) continue;

        slang::TypeReflection* structType = nullptr;
        int nestedArraySize = 0;

        // Check if member is an array of structs
        if (memberType->getKind() == slang::TypeReflection::Kind::Array) {
            slang::TypeReflection* elementType = memberType->getElementType();
            if (elementType && elementType->getKind() == slang::TypeReflection::Kind::Struct) {
                structType = elementType;
                nestedArraySize = 0; // The nested type itself is not an array
            }
        }
        // Check if member is a direct struct
        else if (memberType->getKind() == slang::TypeReflection::Kind::Struct) {
            structType = memberType;
            nestedArraySize = 0;
        }

        if (structType) {
            const char* name = structType->getName();
            std::string structName = name ? name : "";

            // Skip if already seen or if it's a primitive/built-in type
            if (!structName.empty() && seenStructs.find(structName) == seenStructs.end()) {
                seenStructs.insert(structName);

                // Extract this nested struct - use member name as instanceName
                std::string memberName = member->getName() ? member->getName() : structName;
                StructInfo info = extractStructInfo(structType, memberName, nestedArraySize);
                nested.push_back(info);

                // Recursively check for further nested structs
                auto deeper = extractNestedStructTypes(structType, seenStructs);
                nested.insert(nested.end(), deeper.begin(), deeper.end());
            }
        }
    }
    return nested;
}

std::vector<StructInfo> ShaderReflection::detectStructs(
    slang::TypeLayoutReflection* typeLayout,
    const std::string& structFilter
) {
    std::vector<StructInfo> structs;
    if (!typeLayout)
        return structs;

    std::string filterLower = structFilter;
    std::transform(
        filterLower.begin(), filterLower.end(), filterLower.begin(),
        ::tolower
    );

    std::set<std::string> seenStructs; // Track structs we've already added
    SlangInt fieldCount = typeLayout->getFieldCount();

    for (SlangInt i = 0; i < fieldCount; ++i) {
        slang::VariableLayoutReflection* field =
            typeLayout->getFieldByIndex(i);
        if (!field)
            continue;

        slang::TypeLayoutReflection* fieldTypeLayout =
            field->getTypeLayout();
        slang::TypeReflection* fieldType = fieldTypeLayout->getType();
        const char* fieldName = field->getName();
        auto fieldKind = fieldType->getKind();

        bool foundMatch = false;
        StructInfo structInfo;
        slang::TypeReflection* matchedStructType = nullptr;

        // Check if field is an array of structs
        if (fieldKind == slang::TypeReflection::Kind::Array) {
            slang::TypeReflection* elementType =
                fieldType->getElementType();
            slang::TypeReflection* structType = nullptr;
            slang::TypeLayoutReflection* structTypeLayout = nullptr;
            int arraySize =
                static_cast<int>(fieldType->getElementCount());

            // Direct array of structs: Light lights[N]
            if (elementType->getKind() ==
                slang::TypeReflection::Kind::Struct) {
                structType = elementType;
                structTypeLayout = fieldTypeLayout->getElementTypeLayout();
            }
            // Array of ConstantBuffers: ConstantBuffer<Light> lights[N]
            else if (elementType->getKind() ==
                     slang::TypeReflection::Kind::ConstantBuffer) {
                slang::TypeReflection* cbElementType =
                    elementType->getElementType();
                if (cbElementType &&
                    cbElementType->getKind() ==
                        slang::TypeReflection::Kind::Struct) {
                    structType = cbElementType;
                    // For ConstantBuffer<T>, get the element's element type layout
                    auto cbLayout = fieldTypeLayout->getElementTypeLayout();
                    if (cbLayout) {
                        structTypeLayout = cbLayout->getElementTypeLayout();
                    }
                }
            }

            if (structType) {
                const char* structName = structType->getName();
                std::string structNameLower =
                    structName ? structName : "";
                std::transform(
                    structNameLower.begin(), structNameLower.end(),
                    structNameLower.begin(), ::tolower
                );

                if (structNameLower.find(filterLower) !=
                    std::string::npos) {
                    foundMatch = true;
                    matchedStructType = structType;
                    structInfo.structName =
                        structName ? structName : "";
                    structInfo.instanceName =
                        fieldName ? fieldName : "unnamed";
                    structInfo.arraySize = arraySize;

                    // Dump struct layout for debugging
                    if (structTypeLayout) {
                        dumpStructLayout(structTypeLayout, structInfo.structName);
                    }

                    // Extract members
                    SlangInt memberCount = structType->getFieldCount();
                    for (SlangInt m = 0; m < memberCount; ++m) {
                        slang::VariableReflection* member =
                            structType->getFieldByIndex(m);
                        if (!member)
                            continue;

                        structInfo.members.push_back(extractMemberInfo(member));
                    }
                }
            }
        }
        // Check if field is a single struct instance
        else if (fieldKind == slang::TypeReflection::Kind::Struct) {
            const char* structName = fieldType->getName();
            std::string structNameLower = structName ? structName : "";
            std::transform(
                structNameLower.begin(), structNameLower.end(),
                structNameLower.begin(), ::tolower
            );

            if (structNameLower.find(filterLower) !=
                std::string::npos) {
                foundMatch = true;
                matchedStructType = fieldType;
                structInfo.structName = structName ? structName : "";
                structInfo.instanceName =
                    fieldName ? fieldName : "unnamed";
                structInfo.arraySize = 0;

                // Dump struct layout for debugging
                dumpStructLayout(fieldTypeLayout, structInfo.structName);

                // Extract members
                SlangInt memberCount = fieldType->getFieldCount();
                for (SlangInt m = 0; m < memberCount; ++m) {
                    slang::VariableReflection* member =
                        fieldType->getFieldByIndex(m);
                    if (!member)
                        continue;

                    structInfo.members.push_back(extractMemberInfo(member));
                }
            }
        }

        if (foundMatch) {
            seenStructs.insert(structInfo.structName);

            // Extract nested struct types FIRST (e.g., Light from LightsUBO)
            // They need to be defined before the parent struct
            if (matchedStructType) {
                auto nestedStructs = extractNestedStructTypes(matchedStructType, seenStructs);
                structs.insert(structs.end(), nestedStructs.begin(), nestedStructs.end());
            }

            // Then add the parent struct
            structs.push_back(std::move(structInfo));
        }

        // Check single ConstantBuffer<Struct> or ParameterBlock<Struct>
        if (!foundMatch &&
            (fieldKind == slang::TypeReflection::Kind::ParameterBlock ||
             fieldKind == slang::TypeReflection::Kind::ConstantBuffer)) {

            slang::TypeReflection* elementType =
                fieldType->getElementType();
            slang::TypeLayoutReflection* elementTypeLayout =
                fieldTypeLayout->getElementTypeLayout();

            if (elementType &&
                elementType->getKind() ==
                    slang::TypeReflection::Kind::Struct) {
                const char* structName = elementType->getName();
                std::string structNameLower =
                    structName ? structName : "";
                std::transform(
                    structNameLower.begin(), structNameLower.end(),
                    structNameLower.begin(), ::tolower
                );

                if (structNameLower.find(filterLower) !=
                    std::string::npos) {
                    foundMatch = true;
                    structInfo.structName =
                        structName ? structName : "";
                    structInfo.instanceName =
                        fieldName ? fieldName : "unnamed";
                    structInfo.arraySize = 0;

                    // Dump struct layout for debugging
                    if (elementTypeLayout) {
                        dumpStructLayout(elementTypeLayout, structInfo.structName);
                    }

                    // Extract members
                    SlangInt memberCount = elementType->getFieldCount();
                    for (SlangInt m = 0; m < memberCount; ++m) {
                        slang::VariableReflection* member =
                            elementType->getFieldByIndex(m);
                        if (!member)
                            continue;

                        structInfo.members.push_back(extractMemberInfo(member));
                    }

                    // Track this struct as seen
                    seenStructs.insert(structInfo.structName);

                    // Extract nested struct types FIRST (e.g., Light from LightsUBO)
                    // They need to be defined before the parent struct
                    auto nestedStructs = extractNestedStructTypes(elementType, seenStructs);
                    structs.insert(structs.end(), nestedStructs.begin(), nestedStructs.end());

                    // Then add the parent struct
                    structs.push_back(std::move(structInfo));
                }
            }
        }

        // Recursive check for wrapped types
        if (!foundMatch &&
            (fieldKind == slang::TypeReflection::Kind::ParameterBlock ||
             fieldKind == slang::TypeReflection::Kind::ConstantBuffer ||
             fieldKind == slang::TypeReflection::Kind::Struct)) {

            slang::TypeLayoutReflection* layoutToRecurse = nullptr;

            if (fieldKind ==
                    slang::TypeReflection::Kind::ParameterBlock ||
                fieldKind ==
                    slang::TypeReflection::Kind::ConstantBuffer) {
                layoutToRecurse =
                    fieldTypeLayout->getElementTypeLayout();
            } else {
                layoutToRecurse = fieldTypeLayout;
            }

            if (layoutToRecurse) {
                auto nested =
                    detectStructs(layoutToRecurse, structFilter);
                structs.insert(
                    structs.end(), nested.begin(), nested.end()
                );
            }
        }
    }

    return structs;
}

std::vector<BindingInfo> ShaderReflection::parseBindings(
    slang::ProgramLayout* layout,
    SlangStage stage
) {
    std::vector<BindingInfo> bindings;
    if (!layout)
        return bindings;

    slang::VariableLayoutReflection* globalVarLayout =
        layout->getGlobalParamsVarLayout();
    if (!globalVarLayout)
        return bindings;

    slang::TypeLayoutReflection* globalTypeLayout =
        globalVarLayout->getTypeLayout();
    if (!globalTypeLayout)
        return bindings;

    VkShaderStageFlags stageFlags = getVkStageFlags(stage);
    SlangInt bindingRangeCount =
        globalTypeLayout->getBindingRangeCount();

    for (SlangInt i = 0; i < bindingRangeCount; ++i) {
        slang::VariableReflection* leafVariable =
            globalTypeLayout->getBindingRangeLeafVariable(i);
        if (!leafVariable)
            continue;

        slang::TypeLayoutReflection* leafTypeLayout =
            globalTypeLayout->getBindingRangeLeafTypeLayout(i);
        if (!leafTypeLayout)
            continue;

        // Initialize with defaults
        int vulkanSet = 0;
        int vulkanBinding = 0;

        // We find it by matching the leaf variable name to the fields
        // in the global scope.
        // bool foundSpecificLayout = false;
        SlangInt fieldCount = globalTypeLayout->getFieldCount();

        for (SlangInt f = 0; f < fieldCount; ++f) {
            slang::VariableLayoutReflection* fieldLayout =
                globalTypeLayout->getFieldByIndex(f);

            // Match by name (robust enough for global uniforms)
            if (fieldLayout && fieldLayout->getName() &&
                leafVariable->getName() &&
                strcmp(
                    fieldLayout->getName(), leafVariable->getName()
                ) == 0) {

                // Use the category from the screenshot of the slang
                // video but how we gonna get the category itself then?
                slang::ParameterCategory category =
                    fieldLayout->getCategory();

                // Query the specific field for its Set (Space) and
                // Binding (Offset)
                vulkanSet = (int)fieldLayout->getBindingSpace(category);
                vulkanBinding = (int)fieldLayout->getOffset(category);

                // foundSpecificLayout = true;
                break;
            }
        }

        // Fallback to the logical range API if we couldn't match the
        // field (e.g. nested arrays)
        // if (!foundSpecificLayout) {
        //    std::cout << "Actually we should never get there?" <<
        //    std::endl;
        /*SlangInt slangSetIndex =
            globalTypeLayout->getBindingRangeDescriptorSetIndex(i);
        SlangInt slangRangeIndex =
            globalTypeLayout->getBindingRangeFirstDescriptorRangeIndex(
                i
            );

        vulkanSet = (int)globalTypeLayout->getDescriptorSetSpaceOffset(
            slangSetIndex
        );
        vulkanBinding =
            (int)globalTypeLayout
                ->getDescriptorSetDescriptorRangeIndexOffset(
                    slangSetIndex, slangRangeIndex
                );
        //}*/

        // 4. Extract Type Information
        std::string structTypeName;
        slang::TypeReflection::Kind kind = leafTypeLayout->getKind();

        if (kind == slang::TypeReflection::Kind::ParameterBlock ||
            kind == slang::TypeReflection::Kind::ConstantBuffer) {
            slang::TypeLayoutReflection* elementTypeLayout =
                leafTypeLayout->getElementTypeLayout();
            if (elementTypeLayout &&
                elementTypeLayout->getType()->getName()) {
                structTypeName =
                    elementTypeLayout->getType()->getName();
            }
        } else {
            structTypeName = leafTypeLayout->getName()
                                 ? leafTypeLayout->getName()
                                 : "";
        }

        // 5. Map to Vulkan descriptor type
        slang::BindingType bindingType =
            globalTypeLayout->getBindingRangeType(i);
        VkDescriptorType vkDescriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;

        switch (bindingType) {
        case slang::BindingType::ParameterBlock:
        case slang::BindingType::ConstantBuffer:
            vkDescriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            break;
        case slang::BindingType::CombinedTextureSampler:
        case slang::BindingType::Texture:
            vkDescriptorType =
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            break;
        case slang::BindingType::Sampler:
            vkDescriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            break;
        default:
            break;
        }

        // 6. Populate BindingInfo
        BindingInfo binding;
        binding.resourceName = leafVariable->getName()
                                   ? leafVariable->getName()
                                   : "Unnamed";
        binding.vulkanSet = vulkanSet;
        binding.vulkanBinding = vulkanBinding;
        binding.typeKind = getTypeKindName(kind);
        binding.typeName = structTypeName;
        binding.descriptorType = vkDescriptorType;
        binding.stageFlags = stageFlags;
        binding.isInput = true;

        // Get array count for descriptor arrays (e.g., lights[6])
        binding.arrayCount = static_cast<uint32_t>(
            globalTypeLayout->getBindingRangeBindingCount(i));

        // Extract members
        if (kind == slang::TypeReflection::Kind::ParameterBlock ||
            kind == slang::TypeReflection::Kind::ConstantBuffer) {

            slang::TypeLayoutReflection* elementTypeLayout =
                leafTypeLayout->getElementTypeLayout();
            if (elementTypeLayout) {
                // Dump the buffer layout for debugging
                Log::debug("ShaderReflection",
                    "=== UNIFORM BUFFER LAYOUT: {} (binding {}.{}) ===",
                    binding.resourceName, vulkanSet, vulkanBinding);
                Log::debug("ShaderReflection",
                    "  Buffer Size: {} bytes, Alignment: {} bytes",
                    elementTypeLayout->getSize(),
                    elementTypeLayout->getAlignment());

                SlangInt memberCount =
                    elementTypeLayout->getFieldCount();
                for (SlangInt m = 0; m < memberCount; ++m) {
                    slang::VariableLayoutReflection* memberVar =
                        elementTypeLayout->getFieldByIndex(m);
                    if (!memberVar)
                        continue;

                    MemberInfo memberInfo;
                    memberInfo.name = memberVar->getName()
                                          ? memberVar->getName()
                                          : "unnamed";
                    memberInfo.offset =
                        static_cast<int>(memberVar->getOffset());

                    slang::TypeLayoutReflection* memberTypeLayout = memberVar->getTypeLayout();
                    size_t memberSize = memberTypeLayout ? memberTypeLayout->getSize() : 0;
                    size_t memberAlign = memberTypeLayout ? memberTypeLayout->getAlignment() : 0;

                    // Handle arrays properly - separate base type from array size
                    slang::TypeReflection* memberType = memberVar->getTypeLayout()->getType();
                    if (memberType->getKind() == slang::TypeReflection::Kind::Array) {
                        slang::TypeReflection* elementType = memberType->getElementType();
                        memberInfo.typeName = getFullTypeName(elementType);
                        memberInfo.arraySize = static_cast<int>(memberType->getElementCount());
                        memberInfo.typeKind = getTypeKindName(elementType->getKind());

                        size_t stride = memberTypeLayout->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
                        Log::debug("ShaderReflection",
                            "  [{}] {} : {}[{}]  offset: {}, size: {}, stride: {}, align: {}",
                            m, memberInfo.name, memberInfo.typeName, memberInfo.arraySize,
                            memberInfo.offset, memberSize, stride, memberAlign);

                        // If array element is a struct, dump its layout
                        if (elementType->getKind() == slang::TypeReflection::Kind::Struct) {
                            slang::TypeLayoutReflection* elemLayout = memberTypeLayout->getElementTypeLayout();
                            if (elemLayout) {
                                dumpStructLayout(elemLayout, memberInfo.typeName, 2);
                            }
                        }
                    } else {
                        memberInfo.typeName = getFullTypeName(memberType);
                        memberInfo.arraySize = 0;
                        memberInfo.typeKind = getTypeKindName(memberType->getKind());

                        Log::debug("ShaderReflection",
                            "  [{}] {} : {}  offset: {}, size: {}, align: {}",
                            m, memberInfo.name, memberInfo.typeName,
                            memberInfo.offset, memberSize, memberAlign);

                        // If field is a struct, dump its layout
                        if (memberType->getKind() == slang::TypeReflection::Kind::Struct) {
                            dumpStructLayout(memberTypeLayout, memberInfo.typeName, 2);
                        }
                    }
                    binding.members.push_back(std::move(memberInfo));
                }
                Log::debug("ShaderReflection", "=== END {} ===", binding.resourceName);
            }
        }

        bindings.push_back(std::move(binding));
    }

    return bindings;
}

std::vector<VertexInputAttribute> ShaderReflection::collectVertexInputs(
    slang::EntryPointReflection* entryPoint
) {
    std::vector<VertexInputAttribute> attributes;
    if (!entryPoint)
        return attributes;

    // Iterate through parameters in the function signature
    SlangInt paramCount = entryPoint->getParameterCount();
    for (SlangInt i = 0; i < paramCount; ++i) {
        slang::VariableLayoutReflection* param =
            entryPoint->getParameterByIndex(i);
        if (!param)
            continue;

        // We want to only process the VaryingInput
        if (param->getCategory() !=
            slang::ParameterCategory::VaryingInput) {
            continue;
        }

        slang::TypeLayoutReflection* typeLayout =
            param->getTypeLayout();
        slang::TypeReflection* type = typeLayout->getType();

        Log::debug("ShaderReflection", "Type Kind of struct is:");
        if (type->getKind() == slang::TypeReflection::Kind::Struct) {
            Log::debug("ShaderReflection", "We have a struct here");
            SlangInt fieldCount = typeLayout->getFieldCount();
            Log::debug("ShaderReflection", "Count is: {}", fieldCount);
            uint32_t currentOffset = 0;

            for (SlangInt f = 0; f < fieldCount; ++f) {
                slang::VariableLayoutReflection* field =
                    typeLayout->getFieldByIndex(f);

                VertexInputAttribute attr;
                attr.name =
                    field->getName() ? field->getName() : "unnamed";
                attr.semantic = field->getSemanticName()
                                    ? field->getSemanticName()
                                    : "";
                attr.typeName =
                    getFullTypeName(field->getTypeLayout()->getType());

                // location index
                attr.location = (uint32_t)field->getOffset(
                    slang::ParameterCategory::VaryingInput
                );

                // map types to Vulkan formats
                attr.format = getVkFormatFromTypeName(attr.typeName);

                // Calculate memory offset
                attr.offset = currentOffset;
                attr.binding = 0;

                // Increment the offset for the next member based on
                // type size
                currentOffset += getTypeSize(attr.typeName);

                attributes.push_back(attr);
            }
        }
    }
    return attributes;
}

ShaderParsedResult ShaderReflection::reflectShader(
    const std::filesystem::path& moduleName,
    SlangStage stage,
    const std::filesystem::path& projectRoot
) {
    ShaderParsedResult result;

    slang::SessionDesc sessionDesc = {};
    slang::TargetDesc targetDesc = {};
    targetDesc.format = SLANG_SPIRV;
    targetDesc.profile = globalSession->findProfile("spirv_1_5");

    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;
    sessionDesc.defaultMatrixLayoutMode =
        SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

    std::array options{
        slang::CompilerOptionEntry{slang::CompilerOptionName::EmitSpirvDirectly,
          {slang::CompilerOptionValueKind::Int, 1, 0, nullptr,
           nullptr}},
        slang::CompilerOptionEntry{slang::CompilerOptionName::VulkanUseEntryPointName,
          {slang::CompilerOptionValueKind::Int, 1, 0, nullptr,
           nullptr}}
    };

    // Compute shader search path from project root
    std::string shaderSearchPath;
    if (!projectRoot.empty()) {
        shaderSearchPath = (projectRoot / "shaders").string();
    } else {
        shaderSearchPath = "shaders";
    }
    const char* searchPaths[] = {shaderSearchPath.c_str()};
    sessionDesc.compilerOptionEntries = options.data();
    sessionDesc.compilerOptionEntryCount = options.size();
    sessionDesc.searchPaths = searchPaths;
    sessionDesc.searchPathCount = 1;

    Slang::ComPtr<slang::ISession> session;
    globalSession->createSession(sessionDesc, session.writeRef());

    // Load module
    Slang::ComPtr<slang::IModule> slangModule;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        slangModule = session->loadModule(
            moduleName.c_str(), diagnosticsBlob.writeRef()
        );
        if (!slangModule) {
            if (diagnosticsBlob) {
                result.errorMessage = getDiagnosticMessage(diagnosticsBlob);
            }
            diagnoseIfNeeded(diagnosticsBlob);
            Log::error(
                "ShaderReflection",
                "Failed to load shader module: {} - Syntax error in shader",
                moduleName.string()
            );
            return result;
        }
        // Module loaded successfully but may have warnings (e.g. unused variables)
        if (diagnosticsBlob) {
            result.warningMessage = getDiagnosticMessage(diagnosticsBlob);
        }
        diagnoseIfNeeded(diagnosticsBlob, false);
    }

    // Find entry point
    Slang::ComPtr<slang::IEntryPoint> entryPoint;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        SlangInt32 count = slangModule->getDefinedEntryPointCount();
        for (SlangInt32 idx = 0; idx < count; idx++) {
            Slang::ComPtr<slang::IEntryPoint> entry;
            slangModule->getDefinedEntryPoint(idx, entry.writeRef());
            slang::EntryPointReflection* entryPointReflection =
                entry->getLayout()->getEntryPointByIndex(0);

            if (entryPointReflection->getStage() == stage) {
                entryPoint = entry;
                break;
            }
        }

        diagnoseIfNeeded(diagnosticsBlob);
        if (!entryPoint) {
            Log::error(
                "ShaderReflection",
                "Could not find entryPoint matching shader stage"
            );
            return result;
        }
    }

    // Combine module + entry point
    std::array<slang::IComponentType*, 2> componentTypes = {
        slangModule.get(), entryPoint.get()
    };

    Slang::ComPtr<slang::IComponentType> composedProgram;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        if (SLANG_FAILED(session->createCompositeComponentType(
                componentTypes.data(), componentTypes.size(),
                composedProgram.writeRef(), diagnosticsBlob.writeRef()
            ))) {
            diagnoseIfNeeded(diagnosticsBlob);
            return result;
        }
    }

    // Link program
    Slang::ComPtr<slang::IComponentType> linkedProgram;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        if (SLANG_FAILED(composedProgram->link(
                linkedProgram.writeRef(), diagnosticsBlob.writeRef()
            ))) {
            diagnoseIfNeeded(diagnosticsBlob);
            return result;
        }
    }

    // Get Entry Point Code
    Slang::ComPtr<slang::IBlob> codeBlob;
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        if (SLANG_FAILED(linkedProgram->getEntryPointCode(
                0, 0, codeBlob.writeRef(), diagnosticsBlob.writeRef()
            ))) {
            diagnoseIfNeeded(diagnosticsBlob);
            return result;
        }
    }

    if (!codeBlob) {
        Log::error(
            "ShaderReflection",
            "Failed to generate code for shader: {}",
            moduleName.string()
        );
        return result;
    }

    auto ptr = (const uint32_t*)(codeBlob->getBufferPointer());
    size_t count = codeBlob->getBufferSize() / sizeof(uint32_t);
    result.code = std::vector<uint32_t>(ptr, ptr + count);

    // Mark as successful compilation
    result.success = true;

    // Get reflection layout
    slang::ProgramLayout* programLayout = linkedProgram->getLayout();
    if (!programLayout)
        return result;

    slang::EntryPointReflection* entryPointLayout =
        programLayout->getEntryPointByIndex(0);

    // Store entry point name
    if (entryPointLayout) {
        const char* entryName = entryPointLayout->getName();
        if (entryName) {
            result.entryPointName = entryName;
        }
    }

    // Collect vertex attrib inputs
    if (stage == SLANG_STAGE_VERTEX) {
        Log::debug("ShaderReflection", "Collecting vertex inputs");
        result.vertexAttributes = collectVertexInputs(entryPointLayout);
    }

    // Collect outputs
    if (stage == SLANG_STAGE_FRAGMENT) {
        if (auto* results = entryPointLayout->getResultVarLayout()) {
            result.outputs = collectOutputs(results);
        }
    }

    // Collect bindings
    result.bindings = parseBindings(programLayout, stage);

    // Detect lights and cameras
    slang::VariableLayoutReflection* varLayout =
        programLayout->getGlobalParamsVarLayout();
    if (varLayout) {
        slang::TypeLayoutReflection* typeLayout =
            varLayout->getTypeLayout();
        if (typeLayout) {
            result.lightStructs = detectStructs(typeLayout, "light");
            result.cameraStructs = detectStructs(typeLayout, "camera");
        }
    }

    printParsedResult(result);
    return result;
}

void ShaderReflection::printParsedResult(
    const ShaderParsedResult& result
) {
    Log::debug(
        "ShaderReflection",
        "========== SHADER REFLECTION RESULTS =========="
    );

    // 1. Vertex Attributes Table
    if (!result.vertexAttributes.empty()) {
        Log::debug("ShaderReflection", "[Vertex Attributes]");
        for (const auto& attr : result.vertexAttributes) {
            Log::debug(
                "ShaderReflection",
                "  {} | Loc: {} | Bind: {} | Offset: {} | {}",
                attr.name, attr.location, attr.binding, attr.offset,
                getVkFormatString(attr.format)
            );
        }
    }

    // 2. Shader Outputs Table
    if (!result.outputs.empty()) {
        Log::debug("ShaderReflection", "[Shader Outputs]");
        for (const auto& out : result.outputs) {
            Log::debug(
                "ShaderReflection", "  {} | Semantic: {} | Type: {}",
                out.name, out.semantic, out.typeName
            );
        }
    }

    // 3. Resource Bindings Table
    if (!result.bindings.empty()) {
        Log::debug("ShaderReflection", "[Resource Bindings]");
        for (const auto& b : result.bindings) {
            Log::debug(
                "ShaderReflection",
                "  {} | Set: {} | Bind: {} | Kind: {} | Type: {}",
                b.resourceName, b.vulkanSet, b.vulkanBinding,
                b.typeKind, b.typeName
            );

            // Optional: Print nested members if it's a buffer
            for (const auto& m : b.members) {
                Log::debug(
                    "ShaderReflection", "    -> {} | Offset: {} | {}",
                    m.name, m.offset, m.typeName
                );
            }
        }
    }

    // 4. Detected Objects (Lights/Cameras)
    auto printStructTable = [](const std::string& label,
                               const std::vector<StructInfo>& list) {
        if (list.empty())
            return;
        Log::debug("ShaderReflection", "[{}]", label);
        for (const auto& item : list) {
            Log::debug(
                "ShaderReflection",
                "  {} | Type: {} | Elements: {} | Members: {}",
                item.instanceName, item.structName,
                (item.arraySize > 0 ? std::to_string(item.arraySize)
                                    : "1"),
                item.members.size()
            );
        }
    };

    printStructTable("Detected Light Structs", result.lightStructs);
    printStructTable("Detected Camera Structs", result.cameraStructs);

    Log::debug(
        "ShaderReflection",
        "================================================"
    );
}

std::string
ShaderReflection::descriptorTypeToString(VkDescriptorType type) {
    switch (type) {
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        return "VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER";
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return "VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER";
    case VK_DESCRIPTOR_TYPE_SAMPLER:
        return "VK_DESCRIPTOR_TYPE_SAMPLER";
    default:
        return "VK_DESCRIPTOR_TYPE_UNKNOWN";
    }
}

std::string
ShaderReflection::shaderStageToString(VkShaderStageFlags flags) {
    if (flags ==
        (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)) {
        return "VK_SHADER_STAGE_VERTEX_BIT | "
               "VK_SHADER_STAGE_FRAGMENT_BIT";
    }
    switch (flags) {
    case VK_SHADER_STAGE_VERTEX_BIT:
        return "VK_SHADER_STAGE_VERTEX_BIT";
    case VK_SHADER_STAGE_FRAGMENT_BIT:
        return "VK_SHADER_STAGE_FRAGMENT_BIT";
    case VK_SHADER_STAGE_COMPUTE_BIT:
        return "VK_SHADER_STAGE_COMPUTE_BIT";
    default:
        return "VK_SHADER_STAGE_UNKNOWN";
    }
}