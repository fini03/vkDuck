#pragma once
#include "../gpu/primitives.h"
#include "../shader/shader_types.h"
#include <ostream>
#include <string>
#include <functional>

/// Convert a model file path to a valid C++ variable name for the loaded ModelData
inline std::string modelPathToVarName(const std::string& path) {
    return "loadedModel_" + std::to_string(std::hash<std::string>{}(path));
}

/// Generates code for primitives using their assigned names.
/// Names are set in Store::new*() methods and can be overridden
/// in createPrimitives() by setting primitive.name directly.
///
/// Code generation for individual primitives is now handled by
/// the GenerateNode interface methods on each primitive class.
class PrimitiveGenerator {
public:
    /// Generate creation code for all primitives in the store.
    /// Call store.validateUniqueNames() before this to ensure no duplicates.
    void generateAll(
        const primitives::Store& store,
        std::ostream& out
    ) const;

    /// Generate record commands for all primitives in the store.
    /// Call store.validateUniqueNames() before this to ensure no duplicates.
    void generateAllRecordCommands(
        const primitives::Store& store,
        std::ostream& out
    ) const;

    /// Generate destruction/cleanup code for all primitives in the store.
    /// Resources are destroyed in reverse order of creation.
    void generateAllDestroy(
        const primitives::Store& store,
        std::ostream& out
    ) const;

    /// Generate C++ struct definition from shader-parsed StructInfo.
    /// Converts shader types (float4, mat4x4, etc.) to C++ types (glm::vec4, glm::mat4).
    /// Handles std140 alignment with alignas() directives.
    void generateStructDefinition(
        const ShaderTypes::StructInfo& structInfo,
        std::ostream& out
    ) const;

    /// Generate all struct definitions from a ShaderParsedResult.
    /// Generates Camera, Light, and custom structs.
    void generateAllStructs(
        const ShaderTypes::ShaderParsedResult& parsed,
        std::ostream& out
    ) const;

    /// Generate variable definitions for all primitives (for cpp file).
    /// Example: VkImage image_0 = VK_NULL_HANDLE;
    void generateDefinitions(
        const primitives::Store& store,
        std::ostream& out
    ) const;

private:
    /// Convert shader type name to C++ type (e.g., "float4" -> "glm::vec4")
    std::string shaderTypeToCpp(const std::string& typeName) const;

    /// Get the alignment requirement for a shader type (for std140 layout)
    int getTypeAlignment(const std::string& typeName) const;

    /// Get the size of a shader type in bytes
    int getTypeSize(const std::string& typeName) const;
};
