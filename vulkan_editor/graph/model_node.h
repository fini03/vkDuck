#pragma once
#include "../io/model_watcher.h"
#include "node.h"
#include "pin_registry.h"
#include "vulkan/vulkan.h"
#include "vulkan_editor/io/serialization.h"
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

// Use shared types from vkDuck library
#include <vkDuck/model_loader.h>

using namespace ShaderTypes;

// Editor-specific types (renamed to avoid conflicts with vkDuck types)
struct EditorImage {
    std::filesystem::path path{};
    void* pixels{nullptr};
    bool toLoad{false};

    uint32_t width;
    uint32_t height;

    primitives::StoreHandle image{};

    ~EditorImage();
};

struct EditorMaterial {
    int baseTextureIndex{-1};
};

// Editor's GeometryRange includes topology field (vkDuck's doesn't)
struct EditorGeometryRange {
    uint32_t firstVertex;
    uint32_t vertexCount;
    uint32_t firstIndex;
    uint32_t indexCount;
    int materialIndex;
    VkPrimitiveTopology topology;
};

struct ConsolidatedModelData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<EditorGeometryRange> ranges;

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexBufferAllocation = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VmaAllocation indexBufferAllocation = VK_NULL_HANDLE;

    void clear() {
        vertices.clear();
        indices.clear();
        ranges.clear();
        vertexBuffer = VK_NULL_HANDLE;
        vertexBufferAllocation = VK_NULL_HANDLE;
        indexBuffer = VK_NULL_HANDLE;
        indexBufferAllocation = VK_NULL_HANDLE;
    }

    size_t getTotalVertexCount() const {
        return vertices.size();
    }
    size_t getTotalIndexCount() const {
        return indices.size();
    }
    size_t getGeometryCount() const {
        return ranges.size();
    }
};

struct ModelMatrices {
    alignas(16) glm::mat4 model{1.0f};
    alignas(16) glm::mat4 normalMatrix{1.0f};
};

struct ModelCameraData {
    alignas(16) glm::mat4 view{1.0f};
    alignas(16) glm::mat4 invView{1.0f};
    alignas(16) glm::mat4 proj{1.0f};
};

// GLTFCamera is now provided by vkDuck/model_loader.h

struct ModelSettings : public ISerializable {
    char modelPath[256] = "";
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
    int topology = 0;
    bool primitiveRestart = false;
    glm::mat4 modelMatrix = glm::mat4(1.0f);

    nlohmann::json toJson() const override {
        nlohmann::json j;
        j["modelPath"] = modelPath;
        j["position"] = {position.x, position.y, position.z};
        j["rotation"] = {rotation.x, rotation.y, rotation.z};
        j["scale"] = {scale.x, scale.y, scale.z};
        j["topology"] = topology;
        j["primitiveRestart"] = primitiveRestart;
        return j;
    }

    void fromJson(const nlohmann::json& j) override {
        copyString("modelPath", j, modelPath, sizeof(modelPath));

        topology = j.value("topology", 0);
        primitiveRestart = j.value("primitiveRestart", false);
    }

private:
    static void copyString(
        const std::string& key,
        const nlohmann::json& j,
        char* dest,
        size_t size
    ) {
        std::string val = j.value(key, "");
        std::strncpy(dest, val.c_str(), size - 1);
        dest[size - 1] = '\0';
    }
};

/**
 * @class ModelNode
 * @brief Loads and manages 3D models (glTF/OBJ) for rendering in the pipeline.
 *
 * Handles model loading, texture management, transform matrices, and optional
 * embedded camera extraction from glTF files. Supports file watching for auto-reload.
 */
class ModelNode : public Node, public ISerializable {
public:
    constexpr static std::array<VkPrimitiveTopology, 6>
        topologyOptionsEnum{
            VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
            VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
            VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN
        };

    ModelNode();
    ModelNode(int id);
    ~ModelNode() override;

    void render(
        ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
        const NodeGraph& graph
    ) const override;

    nlohmann::json toJson() const override;
    void fromJson(const nlohmann::json& j) override;

    // Pin registration (new system)
    void registerPins(PinRegistry& registry) override;
    bool usesPinRegistry() const override { return usesRegistry; }

    void loadModel(const std::filesystem::path& path, const std::filesystem::path& projectRoot = "");

    static const std::vector<const char*> topologyOptions;
    ModelSettings settings;

    // Legacy pins (kept for backwards compatibility)
    Pin modelMatrixPin;
    Pin texturePin;
    Pin vertexDataPin;
    Pin cameraPin;

    // New registry handles
    PinHandle modelMatrixPinHandle = INVALID_PIN_HANDLE;
    PinHandle texturePinHandle = INVALID_PIN_HANDLE;
    PinHandle vertexDataPinHandle = INVALID_PIN_HANDLE;
    PinHandle cameraPinHandle = INVALID_PIN_HANDLE;

    std::vector<EditorMaterial> materials;
    std::vector<EditorImage> images;

    std::vector<GLTFCamera> gltfCameras;
    int selectedCameraIndex{-1};
    bool needsCameraApply{false};
    ModelCameraData cameraData;
    float aspectRatio{16.0f / 9.0f};

    void updateCameraFromSelection();

    void setFileWatchingEnabled(bool enabled);
    bool isFileWatchingEnabled() const;
    ModelFileWatcher::LoadingState getLoadingState() const;
    const std::string& getLastError() const;
    bool needsReload() const { return pendingReload; }
    void clearReloadFlag() { pendingReload = false; }
    void reloadModel();

    std::filesystem::path projectRoot;

    ConsolidatedModelData modelData;
    void clearPrimitives() override;
    void createPrimitives(primitives::Store& store) override;
    virtual void getOutputPrimitives(
        const primitives::Store& store,
        std::vector<std::pair<
            ax::NodeEditor::PinId,
            primitives::StoreHandle>>& outputs
    ) const override;

private:
    void createDefaultPins();
    bool usesRegistry = false;

    EditorImage defaultTexture{};

    primitives::StoreHandle baseTextureArray{};
    primitives::StoreHandle vertexDataArray{};
    primitives::StoreHandle modelMatrixArray{};
    primitives::StoreHandle cameraUboArray{};
    primitives::UniformBuffer* cameraUbo{nullptr};
    primitives::CameraType cameraType{primitives::CameraType::Fixed};

    std::vector<ModelMatrices> modelMatricesData;

    std::unique_ptr<ModelFileWatcher> fileWatcher;
    bool fileWatchingEnabled{true};
    bool pendingReload{false};
    std::string currentModelPath;
};