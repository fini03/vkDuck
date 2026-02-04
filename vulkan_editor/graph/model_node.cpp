#include "model_node.h"
#include "node_graph.h"
#include "vulkan_editor/io/image_loader.h"
#include "vulkan_editor/util/logger.h"
#include <cmath>
#include <cstring>
#include <future>
#include <imgui.h>
#include <imgui_node_editor.h>
#include <set>
#include <thread>
#include <vulkan/vk_enum_string_helper.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define USE_NEON 1
#elif defined(__SSE4_1__)
#include <smmintrin.h>
#define USE_SSE 1
#endif

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_INCLUDE_STB
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#include <tiny_gltf.h>

#include "external/utilities/builders.h"
#include "external/utilities/widgets.h"
#include <imgui.h>

namespace {
constexpr float PADDING_X = 10.0f;
}
namespace ed = ax::NodeEditor;
namespace fs = std::filesystem;

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
const std::vector<const char*> ModelNode::topologyOptions =
    createEnumStringList(
        topologyOptionsEnum, string_VkPrimitiveTopology
    );

// SIMD-optimized index conversion for better throughput
static inline void convertIndices16to32(
    const uint16_t* __restrict src,
    uint32_t* __restrict dst,
    size_t count
) {
#if defined(USE_NEON)
    // ARM NEON: process 8 indices at a time
    size_t simdCount = count / 8;
    for (size_t i = 0; i < simdCount; ++i) {
        uint16x8_t in = vld1q_u16(src + i * 8);
        uint32x4_t lo = vmovl_u16(vget_low_u16(in));
        uint32x4_t hi = vmovl_u16(vget_high_u16(in));
        vst1q_u32(dst + i * 8, lo);
        vst1q_u32(dst + i * 8 + 4, hi);
    }
    for (size_t i = simdCount * 8; i < count; ++i) {
        dst[i] = src[i];
    }
#elif defined(USE_SSE)
    // SSE4.1: process 8 indices at a time
    size_t simdCount = count / 8;
    for (size_t i = 0; i < simdCount; ++i) {
        __m128i in = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i * 8));
        __m128i lo = _mm_cvtepu16_epi32(in);
        __m128i hi = _mm_cvtepu16_epi32(_mm_srli_si128(in, 8));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i * 8), lo);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i * 8 + 4), hi);
    }
    for (size_t i = simdCount * 8; i < count; ++i) {
        dst[i] = src[i];
    }
#else
    // Fallback: compiler will auto-vectorize this simple loop
    for (size_t i = 0; i < count; ++i) {
        dst[i] = src[i];
    }
#endif
}

static inline void convertIndices8to32(
    const uint8_t* __restrict src,
    uint32_t* __restrict dst,
    size_t count
) {
#if defined(USE_NEON)
    // ARM NEON: process 16 indices at a time
    size_t simdCount = count / 16;
    for (size_t i = 0; i < simdCount; ++i) {
        uint8x16_t in = vld1q_u8(src + i * 16);
        uint16x8_t mid_lo = vmovl_u8(vget_low_u8(in));
        uint16x8_t mid_hi = vmovl_u8(vget_high_u8(in));
        vst1q_u32(dst + i * 16, vmovl_u16(vget_low_u16(mid_lo)));
        vst1q_u32(dst + i * 16 + 4, vmovl_u16(vget_high_u16(mid_lo)));
        vst1q_u32(dst + i * 16 + 8, vmovl_u16(vget_low_u16(mid_hi)));
        vst1q_u32(dst + i * 16 + 12, vmovl_u16(vget_high_u16(mid_hi)));
    }
    for (size_t i = simdCount * 16; i < count; ++i) {
        dst[i] = src[i];
    }
#elif defined(USE_SSE)
    // SSE4.1: process 16 indices at a time
    size_t simdCount = count / 16;
    for (size_t i = 0; i < simdCount; ++i) {
        __m128i in = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i * 16));
        __m128i a = _mm_cvtepu8_epi32(in);
        __m128i b = _mm_cvtepu8_epi32(_mm_srli_si128(in, 4));
        __m128i c = _mm_cvtepu8_epi32(_mm_srli_si128(in, 8));
        __m128i d = _mm_cvtepu8_epi32(_mm_srli_si128(in, 12));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i * 16), a);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i * 16 + 4), b);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i * 16 + 8), c);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i * 16 + 12), d);
    }
    for (size_t i = simdCount * 16; i < count; ++i) {
        dst[i] = src[i];
    }
#else
    // Fallback: compiler will auto-vectorize this simple loop
    for (size_t i = 0; i < count; ++i) {
        dst[i] = src[i];
    }
#endif
}

/// Try multiple paths to find the texture (all project-relative)
static fs::path findTexturePath(
    const fs::path& parentPath,
    const fs::path& projectRoot,
    std::string_view uri
) {
    auto texturePath = parentPath / uri;
    if (fs::exists(texturePath))
        return texturePath;

    // Try sibling "images" folder (e.g.,
    // data/models/../images/texture.png)
    auto siblingImagesPath = parentPath.parent_path() / "images" / uri;
    if (std::filesystem::exists(siblingImagesPath)) {
        Log::debug(
            "Model", "Found texture in sibling images folder: {}",
            siblingImagesPath.string()
        );
        return siblingImagesPath;
    }

    if (projectRoot.empty()) {
        Log::warning(
            "Model",
            "Texture not found and no project root provided: {}", uri
        );
        return texturePath;
    }

    // Try data/images relative to project root
    auto dataImagesPath = projectRoot / "data" / "images" / uri;
    if (fs::exists(dataImagesPath)) {
        Log::debug(
            "Model", "Found texture in project data/images: {}",
            dataImagesPath.string()
        );
        return dataImagesPath;
    }

    Log::warning(
        "Model", "Texture not found in any search path: {}", uri
    );
    return texturePath;
}

// Batch resolve texture paths in parallel to reduce syscall overhead
static std::vector<fs::path> findTexturePathsBatch(
    const fs::path& parentPath,
    const fs::path& projectRoot,
    const std::vector<std::pair<size_t, std::string>>& texturesToResolve,
    size_t totalImages
) {
    std::vector<fs::path> resolvedPaths(totalImages);

    if (texturesToResolve.empty()) {
        return resolvedPaths;
    }

    // For small batches, just do it sequentially
    if (texturesToResolve.size() < 4) {
        for (const auto& [index, uri] : texturesToResolve) {
            resolvedPaths[index] = findTexturePath(parentPath, projectRoot, uri);
        }
        return resolvedPaths;
    }

    // Resolve paths in parallel for larger batches
    std::vector<std::future<std::pair<size_t, fs::path>>> futures;
    futures.reserve(texturesToResolve.size());

    for (const auto& [index, uri] : texturesToResolve) {
        futures.push_back(std::async(std::launch::async,
            [&parentPath, &projectRoot, index, uri]() {
                return std::make_pair(index, findTexturePath(parentPath, projectRoot, uri));
            }
        ));
    }

    for (auto& future : futures) {
        auto [index, path] = future.get();
        resolvedPaths[index] = std::move(path);
    }

    return resolvedPaths;
}

// Parallel image loading result
struct DecodedImageResult {
    void* pixels{nullptr};
    uint32_t width{0};
    uint32_t height{0};
    size_t index{0};
    bool success{false};
};

// Load a single image (for use in parallel)
static DecodedImageResult loadSingleImage(const fs::path& path, size_t index) {
    DecodedImageResult result;
    result.index = index;
    result.pixels = imageLoad(path, result.width, result.height);
    result.success = (result.pixels != nullptr);
    return result;
}

// Load multiple images in parallel using std::async
static std::vector<DecodedImageResult> loadImagesParallel(
    const std::vector<std::pair<size_t, fs::path>>& imagesToLoad
) {
    std::vector<std::future<DecodedImageResult>> futures;
    futures.reserve(imagesToLoad.size());

    for (const auto& [index, path] : imagesToLoad) {
        futures.push_back(std::async(std::launch::async, loadSingleImage, path, index));
    }

    std::vector<DecodedImageResult> results;
    results.reserve(futures.size());
    for (auto& future : futures) {
        results.push_back(future.get());
    }
    return results;
}

Image::~Image() {
    if (pixels)
        imageFree(pixels);
}

ModelNode::ModelNode()
    : Node() {
    name = "Model";
    createDefaultPins();
    fileWatcher = std::make_unique<ModelFileWatcher>();
}

ModelNode::ModelNode(int id)
    : Node(id) {
    name = "Model";
    createDefaultPins();
    fileWatcher = std::make_unique<ModelFileWatcher>();
}

ModelNode::~ModelNode() {}

nlohmann::json ModelNode::toJson() const {
    nlohmann::json j;
    j["type"] = "model";
    j["id"] = id;
    j["name"] = name;
    j["position"] = {Node::position.x, Node::position.y};
    j["settings"] = settings.toJson();
    j["selectedCameraIndex"] = selectedCameraIndex;

    // Store all output pins by index
    j["outputPins"] = nlohmann::json::array();
    j["outputPins"].push_back(
        {{"id", modelMatrixPin.id.Get()},
         {"type", static_cast<int>(modelMatrixPin.type)},
         {"label", modelMatrixPin.label}}
    );
    j["outputPins"].push_back(
        {{"id", texturePin.id.Get()},
         {"type", static_cast<int>(texturePin.type)},
         {"label", texturePin.label}}
    );
    j["outputPins"].push_back(
        {{"id", vertexDataPin.id.Get()},
         {"type", static_cast<int>(vertexDataPin.type)},
         {"label", vertexDataPin.label}}
    );
    j["outputPins"].push_back(
        {{"id", cameraPin.id.Get()},
         {"type", static_cast<int>(cameraPin.type)},
         {"label", cameraPin.label}}
    );

    return j;
}

void ModelNode::fromJson(const nlohmann::json& j) {
    name = j.value("name", "Model");
    if (j.contains("position") && j["position"].is_array() &&
        j["position"].size() == 2) {
        Node::position = ImVec2(
            j["position"][0].get<float>(), j["position"][1].get<float>()
        );
    }

    if (j.contains("settings")) {
        settings.fromJson(j["settings"]);
    }

    // Note: Model loading is handled separately in pipeline_state.cpp

    // Store camera selection to restore after model is loaded
    selectedCameraIndex = j.value("selectedCameraIndex", -1);

    // Restore output pins by index (order: modelMatrix, texture,
    // vertexData, cameras)
    if (j.contains("outputPins") && j["outputPins"].is_array()) {
        auto& pins = j["outputPins"];
        if (pins.size() > 0)
            modelMatrixPin.id = ed::PinId(pins[0]["id"].get<int>());
        if (pins.size() > 1)
            texturePin.id = ed::PinId(pins[1]["id"].get<int>());
        if (pins.size() > 2)
            vertexDataPin.id = ed::PinId(pins[2]["id"].get<int>());
        if (pins.size() > 3)
            cameraPin.id = ed::PinId(pins[3]["id"].get<int>());
    }
}

void ModelNode::createDefaultPins() {
    // Model Matrix output
    modelMatrixPin.id = ed::PinId(GetNextGlobalId());
    modelMatrixPin.type = PinType::UniformBuffer;
    modelMatrixPin.label = "Model matrix";

    // Texture output
    texturePin.id = ed::PinId(GetNextGlobalId());
    texturePin.type = PinType::Image;
    texturePin.label = "Image";

    vertexDataPin.id = ed::PinId(GetNextGlobalId());
    vertexDataPin.type = PinType::VertexData;
    vertexDataPin.label = "Vertex data";

    // Camera output (selected GLTF camera as UniformBuffer)
    cameraPin.id = ed::PinId(GetNextGlobalId());
    cameraPin.type = PinType::UniformBuffer;
    cameraPin.label = "Camera";
}

void ModelNode::render(
    ax::NodeEditor::Utilities::BlueprintNodeBuilder& builder,
    const NodeGraph& graph
) const {
    // Calculate node width - only include camera pin if model has
    // cameras
    std::vector<std::string> pinLabels = {
        vertexDataPin.label, modelMatrixPin.label, texturePin.label
    };
    if (!gltfCameras.empty()) {
        pinLabels.push_back(cameraPin.label);
    }
    float nodeWidth = CalculateNodeWidth(name, pinLabels);

    // Violet background for all nodes (semi-transparent)
    ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(180, 115, 0, 80));

    builder.Begin(id);

    // Draw header - orange for model nodes
    builder.Header(ImColor(255, 165, 0));

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
            const_cast<ModelNode*>(this)->isRenaming = true;
        }
    } else {
        // Editable name
        char nameBuffer[128];
        strncpy(nameBuffer, name.c_str(), sizeof(nameBuffer));
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';

        ImGui::SetNextItemWidth(nodeWidth - PADDING_X);
        ImGui::InputText(
            "##NodeName", nameBuffer, sizeof(nameBuffer),
            ImGuiInputTextFlags_AutoSelectAll
        );
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const_cast<ModelNode*>(this)->name = nameBuffer;
            const_cast<ModelNode*>(this)->isRenaming = false;
        }
    }

    ImGui::Spring(1);
    ImGui::Dummy(ImVec2(0, 28));
    ImGui::Spring(0);
    builder.EndHeader();

    // Draw output pins
    DrawOutputPin(
        vertexDataPin.id, vertexDataPin.label,
        static_cast<int>(vertexDataPin.type),
        graph.isPinLinked(vertexDataPin.id), nodeWidth, builder
    );

    DrawOutputPin(
        modelMatrixPin.id, modelMatrixPin.label,
        static_cast<int>(modelMatrixPin.type),
        graph.isPinLinked(modelMatrixPin.id), nodeWidth, builder
    );

    DrawOutputPin(
        texturePin.id, texturePin.label,
        static_cast<int>(texturePin.type), graph.isPinLinked(texturePin.id),
        nodeWidth, builder
    );

    // Only show camera pin if model has GLTF cameras
    if (!gltfCameras.empty()) {
        DrawOutputPin(
            cameraPin.id, cameraPin.label,
            static_cast<int>(cameraPin.type), graph.isPinLinked(cameraPin.id),
            nodeWidth, builder
        );
    }

    builder.End();
    ed::PopStyleColor();
}

VkPrimitiveTopology ModelNode::gltfModeToVulkan(int mode) {
    switch (mode) {
    case 0: // POINTS
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

    case 1: // LINES
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    case 2: // LINE_LOOP (not supported)
        // must be converted to LINE_STRIP + duplicated first vertex
        return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;

    case 3: // LINE_STRIP
        return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;

    case 4: // TRIANGLES
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    case 5: // TRIANGLE_STRIP
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    case 6: // TRIANGLE_FAN
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;

    default:
        // Spec says default is TRIANGLES
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

glm::mat4 ModelNode::getNodeTransform(const tinygltf::Node& node) {
    glm::mat4 transform(1.0f);
    if (node.matrix.size() == 16) {
        transform = glm::make_mat4(node.matrix.data()); //
    } else {
        if (node.translation.size() == 3) {
            transform = glm::translate(
                transform, glm::vec3(
                               node.translation[0], node.translation[1],
                               node.translation[2]
                           )
            ); //
        }
        if (node.rotation.size() == 4) {
            glm::quat q(
                node.rotation[3], node.rotation[0], node.rotation[1],
                node.rotation[2]
            );                              //
            transform *= glm::mat4_cast(q); //
        }
        if (node.scale.size() == 3) {
            transform = glm::scale(
                transform,
                glm::vec3(node.scale[0], node.scale[1], node.scale[2])
            ); //
        }
    }
    return transform;
}

void ModelNode::loadModel(
    const std::filesystem::path& path,
    const std::filesystem::path& projectRoot
) {
    auto totalStart = std::chrono::high_resolution_clock::now();
    Log::info("Model", "Loading model from: {}", path.string());

    // Clear existing data
    geometries.clear();
    materials.clear();
    images.clear();
    modelData.clear();
    gltfCameras.clear();
    selectedCameraIndex = -1;
    defaultTexture = {.path = projectRoot / "data/images/default.png"};

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    Log::debug("Model", "Parsing glTF file...");

    bool ret;
    if (path.extension() == ".glb")
        ret = loader.LoadBinaryFromFile(
            &model, &err, &warn, path.string()
        );
    else
        ret = loader.LoadASCIIFromFile(
            &model, &err, &warn, path.string()
        );

    if (!ret) {
        Log::error("Model", "Failed to load gITF: {}", err);
        return;
    }

    int defaultScene = model.defaultScene > 0 ? model.defaultScene : 0;
    const tinygltf::Scene& scene = model.scenes[defaultScene];

    // Pre-allocate geometry vector to avoid reallocations during loading
    {
        size_t estimatedGeometries = 0;
        for (const auto& mesh : model.meshes) {
            estimatedGeometries += mesh.primitives.size();
        }
        geometries.reserve(estimatedGeometries);
    }

    {
        auto t1 = std::chrono::high_resolution_clock::now();
        for (int nodeIndex : scene.nodes)
            processNode(model, nodeIndex, glm::mat4(1.0f));
        auto t2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> ms_double = t2 - t1;
        Log::debug(
            "Model", "Node processing took {}ms", ms_double.count()
        );
    }

    // Parse cameras from GLTF
    for (size_t i = 0; i < model.cameras.size(); ++i) {
        const auto& gltfCam = model.cameras[i];
        GLTFCamera cam;
        cam.name = gltfCam.name.empty() ? "Camera " + std::to_string(i)
                                        : gltfCam.name;

        if (gltfCam.type == "perspective") {
            cam.isPerspective = true;
            cam.fov = glm::degrees(
                static_cast<float>(gltfCam.perspective.yfov)
            );
            cam.aspectRatio =
                static_cast<float>(gltfCam.perspective.aspectRatio);
            cam.nearPlane =
                static_cast<float>(gltfCam.perspective.znear);
            cam.farPlane = static_cast<float>(gltfCam.perspective.zfar);
        } else if (gltfCam.type == "orthographic") {
            cam.isPerspective = false;
            cam.xmag = static_cast<float>(gltfCam.orthographic.xmag);
            cam.ymag = static_cast<float>(gltfCam.orthographic.ymag);
            cam.nearPlane =
                static_cast<float>(gltfCam.orthographic.znear);
            cam.farPlane =
                static_cast<float>(gltfCam.orthographic.zfar);
        }

        gltfCameras.push_back(cam);
    }

    // Find camera transforms from scene nodes
    for (int nodeIndex : scene.nodes)
        processCameraNode(model, nodeIndex, glm::mat4(1.0f));

    if (!gltfCameras.empty()) {
        Log::info(
            "Model", "Found {} camera(s) in GLTF file",
            gltfCameras.size()
        );
        selectedCameraIndex = 0; // Select first camera by default
        needsCameraApply =
            true; // Trigger auto-apply when camera UI is shown
    }

    // We don't load the images directly here yet, because we only want
    // to load the ones we mark as used (in case we do not support all
    // features of gltfs and so on)
    const auto& parentPath = path.parent_path();
    images.resize(model.images.size());

    // Collect texture URIs for batch resolution
    std::vector<std::pair<size_t, std::string>> texturesToResolve;
    texturesToResolve.reserve(model.textures.size());
    for (const auto& tex : model.textures) {
        assert(tex.source >= 0);
        assert(static_cast<uint32_t>(tex.source) < images.size());
        texturesToResolve.emplace_back(tex.source, model.images[tex.source].uri);
    }

    // Resolve all texture paths in parallel
    auto resolvedPaths = findTexturePathsBatch(
        parentPath, projectRoot, texturesToResolve, images.size()
    );
    for (size_t i = 0; i < images.size(); ++i) {
        images[i].path = std::move(resolvedPaths[i]);
    }

    materials.resize(model.materials.size());
    auto allMaterials = std::views::zip(materials, model.materials);
    for (auto&& [mat, modelMat] : allMaterials) {
        // NOTE: baseColorTexture is per gltf spec in SRGB space
        int tex = modelMat.pbrMetallicRoughness.baseColorTexture.index;
        if (tex < 0)
            continue;

        mat.baseTextureIndex = model.textures[tex].source;
        assert(mat.baseTextureIndex >= 0);
        images[mat.baseTextureIndex].toLoad = true;
    }

    // Load default texture first so we can use it as a fallback
    Log::debug(
        "Model", "Loading default texture {}",
        defaultTexture.path.string()
    );
    defaultTexture.pixels = imageLoad(
        defaultTexture.path, defaultTexture.width, defaultTexture.height
    );
    if (defaultTexture.pixels == nullptr) {
        Log::error(
            "Model", "Failed to load default texture: {}",
            defaultTexture.path.string()
        );
    }

    // Load textures in parallel (async)
    {
        auto t1 = std::chrono::high_resolution_clock::now();

        // Collect images that need to be loaded
        std::vector<std::pair<size_t, fs::path>> imagesToLoad;
        for (size_t i = 0; i < images.size(); ++i) {
            if (images[i].toLoad) {
                imagesToLoad.push_back({i, images[i].path});
            }
        }

        // Load images in parallel
        if (!imagesToLoad.empty()) {
            Log::debug("Model", "Loading {} images in parallel...", imagesToLoad.size());
            auto results = loadImagesParallel(imagesToLoad);

            for (const auto& result : results) {
                images[result.index].pixels = result.pixels;
                images[result.index].width = result.width;
                images[result.index].height = result.height;

                if (!result.success) {
                    Log::warning(
                        "Model",
                        "Failed to load texture: {}, using default texture",
                        images[result.index].path.string()
                    );
                }
            }
        }

        auto t2 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> ms_double = t2 - t1;
        Log::debug(
            "Model", "Image loading took {}ms (parallel)", ms_double.count()
        );
    }

    // Consolidate geometries
    consolidateGeometries();

    Log::info(
        "Model", "Loaded full model: {} primitives", geometries.size()
    );
    Log::info(
        "Model",
        "Consolidated: {} total vertices, {} total indices, {} "
        "geometry ranges",
        modelData.getTotalVertexCount(), modelData.getTotalIndexCount(),
        modelData.getGeometryCount()
    );

    auto totalEnd = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> totalMs = totalEnd - totalStart;
    Log::info("Model", "Total loading time: {:.1f}ms", totalMs.count());

    // Store the model path and project root for potential reload
    currentModelPath = path.string();
    this->projectRoot = projectRoot;

    // Set up file watcher for auto-reload
    if (fileWatcher && fileWatchingEnabled) {
        fileWatcher->setReloadCallback([this](
                                           const std::string& filepath
                                       ) {
            Log::info(
                "Model", "Detected change in model file: {}", filepath
            );
            pendingReload = true;
        });
        fileWatcher->watchFile(path.string());
        fileWatcher->setLoadingState(
            ModelFileWatcher::LoadingState::Loaded
        );
    }
}

void ModelNode::consolidateGeometries() {
    modelData.clear();

    // Pre-calculate total sizes and reserve capacity to avoid reallocations
    {
        size_t totalVerts = 0;
        size_t totalIndices = 0;
        for (const auto& geom : geometries) {
            totalVerts += geom.m_vertices.size();
            totalIndices += geom.m_indices.size();
        }
        modelData.vertices.reserve(totalVerts);
        modelData.indices.reserve(totalIndices);
        modelData.ranges.reserve(geometries.size());
    }

    // Get the default topology from settings
    VkPrimitiveTopology defaultTopology =
        topologyOptionsEnum[settings.topology];

    // Add each geometry to the consolidated data
    for (const auto& geom : geometries) {
        // Use the topology that was set during loading (stored in
        // settings.topology) or you could store per-geometry topology
        // if needed
        modelData.addGeometry(geom, defaultTopology);
    }

    Log::debug(
        "Model",
        "Consolidated {} geometries: {} vertices, {} indices, {} "
        "ranges",
        geometries.size(), modelData.getTotalVertexCount(),
        modelData.getTotalIndexCount(), modelData.getGeometryCount()
    );

    // Print each range for debugging
    for (size_t i = 0; i < modelData.ranges.size(); i++) {
        const auto& range = modelData.ranges[i];
        Log::debug(
            "Model",
            "  Range {}: verts[{}:{}], indices[{}:{}], material={}", i,
            range.firstVertex, range.firstVertex + range.vertexCount,
            range.firstIndex, range.firstIndex + range.indexCount,
            range.materialIndex
        );
    }
}

void ModelNode::processNode(
    tinygltf::Model& model,
    int nodeIndex,
    const glm::mat4& parentTransform
) {
    if (nodeIndex < 0 ||
        static_cast<uint32_t>(nodeIndex) >= model.nodes.size())
        return;

    const tinygltf::Node& node = model.nodes[nodeIndex];
    glm::mat4 localTransform = getNodeTransform(node);
    glm::mat4 worldTransform = parentTransform * localTransform;

    if (node.mesh < 0 ||
        static_cast<uint32_t>(node.mesh) > model.meshes.size()) {
        for (int child : node.children)
            processNode(model, child, worldTransform);
        return;
    }

    const auto& mesh = model.meshes[node.mesh];
    for (const auto& primitive : mesh.primitives) {
        Geometry geometry{};

        // Set the topology from the glTF primitive mode
        int mode = (primitive.mode == -1) ? 4 : primitive.mode;
        VkPrimitiveTopology topology = gltfModeToVulkan(mode);

        // Store the topology - you might want to add this to
        // Geometry struct for now we'll use the global
        // settings.topology
        settings.topology = static_cast<int>(std::distance(
            topologyOptionsEnum.begin(),
            std::find(
                topologyOptionsEnum.begin(), topologyOptionsEnum.end(),
                topology
            )
        ));

        geometry.materialIndex = primitive.material;

        glm::mat3 normalMatrix =
            glm::transpose(glm::inverse(glm::mat3(worldTransform)));

        if (primitive.attributes.find("POSITION") ==
            primitive.attributes.end()) {
            Log::warning(
                "Model",
                "Primitive missing POSITION attribute, skipping"
            );
            continue;
        }

        const tinygltf::Accessor& posAccessor =
            model.accessors[primitive.attributes.at("POSITION")];

        const tinygltf::Accessor* normalAccessor = nullptr;
        bool hasNormals = primitive.attributes.find("NORMAL") !=
                          primitive.attributes.end();
        if (hasNormals) {
            normalAccessor =
                &model.accessors[primitive.attributes.at("NORMAL")];
        }

        bool hasTexCoords = primitive.attributes.find("TEXCOORD_0") !=
                            primitive.attributes.end();
        const tinygltf::Accessor* texCoordAccessor = nullptr;
        if (hasTexCoords) {
            texCoordAccessor =
                &model.accessors[primitive.attributes.at("TEXCOORD_0")];
        }

        const tinygltf::BufferView& posBufferView =
            model.bufferViews[posAccessor.bufferView];
        const tinygltf::BufferView* normalBufferView = nullptr;
        const tinygltf::BufferView* texCoordBufferView = nullptr;

        if (hasNormals) {
            normalBufferView =
                &model.bufferViews[normalAccessor->bufferView];
        }
        if (hasTexCoords) {
            texCoordBufferView =
                &model.bufferViews[texCoordAccessor->bufferView];
        }

        const tinygltf::Buffer& posBuffer =
            model.buffers[posBufferView.buffer];
        const tinygltf::Buffer* normalBuffer = nullptr;
        const tinygltf::Buffer* texCoordBuffer = nullptr;

        if (hasNormals) {
            normalBuffer = &model.buffers[normalBufferView->buffer];
        }
        if (hasTexCoords) {
            texCoordBuffer = &model.buffers[texCoordBufferView->buffer];
        }

        size_t posStride = posBufferView.byteStride
                               ? posBufferView.byteStride
                               : sizeof(float) * 3;
        size_t normalStride =
            (hasNormals && normalBufferView->byteStride)
                ? normalBufferView->byteStride
                : sizeof(float) * 3;
        size_t texCoordStride =
            (hasTexCoords && texCoordBufferView->byteStride)
                ? texCoordBufferView->byteStride
                : sizeof(float) * 2;

        const uint8_t* posData =
            &posBuffer.data
                 [posBufferView.byteOffset + posAccessor.byteOffset];
        const uint8_t* normalData = nullptr;
        const uint8_t* texCoordData = nullptr;

        if (hasNormals) {
            normalData = &normalBuffer->data
                              [normalBufferView->byteOffset +
                               normalAccessor->byteOffset];
        }
        if (hasTexCoords) {
            texCoordData = &texCoordBuffer->data
                                [texCoordBufferView->byteOffset +
                                 texCoordAccessor->byteOffset];
        }

        // Pre-allocate vertices based on accessor count (glTF vertices
        // are already deduplicated)
        const size_t vertexCount = posAccessor.count;
        geometry.m_vertices.resize(vertexCount);

        // Process all vertices in a single pass - much faster than
        // per-index processing
        for (size_t i = 0; i < vertexCount; ++i) {
            Vertex& vertex = geometry.m_vertices[i];

            const float* pos = reinterpret_cast<const float*>(
                posData + i * posStride
            );
            glm::vec4 worldPos =
                worldTransform *
                glm::vec4(pos[0], pos[1], pos[2], 1.0f);
            vertex.pos = glm::vec3(worldPos);

            if (hasNormals) {
                const float* norm = reinterpret_cast<const float*>(
                    normalData + i * normalStride
                );
                glm::vec3 transformedNormal =
                    normalMatrix *
                    glm::vec3(norm[0], norm[1], norm[2]);
                vertex.normal = glm::normalize(transformedNormal);
            } else {
                vertex.normal = {0.0f, 0.0f, 1.0f};
            }

            if (hasTexCoords) {
                const float* tex = reinterpret_cast<const float*>(
                    texCoordData + i * texCoordStride
                );
                vertex.texCoord = {tex[0], tex[1]};
            } else {
                vertex.texCoord = {0.0f, 0.0f};
            }
        }

        // Copy indices directly - no hash map lookups needed
        if (primitive.indices >= 0) {
            const tinygltf::Accessor& indexAccessor =
                model.accessors[primitive.indices];
            const tinygltf::BufferView& indexBufferView =
                model.bufferViews[indexAccessor.bufferView];
            const tinygltf::Buffer& indexBuffer =
                model.buffers[indexBufferView.buffer];
            const uint8_t* indexData = &indexBuffer.data
                                         [indexBufferView.byteOffset +
                                          indexAccessor.byteOffset];

            const size_t indexCount = indexAccessor.count;
            geometry.m_indices.resize(indexCount);

            // Fast path: copy indices directly based on component type
            switch (indexAccessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                std::memcpy(
                    geometry.m_indices.data(), indexData,
                    indexCount * sizeof(uint32_t)
                );
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                const uint16_t* src =
                    reinterpret_cast<const uint16_t*>(indexData);
                convertIndices16to32(src, geometry.m_indices.data(), indexCount);
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
                convertIndices8to32(indexData, geometry.m_indices.data(), indexCount);
                break;
            }
            default:
                throw std::runtime_error(
                    "Unsupported index component type"
                );
            }
        }

        if (!geometry.m_vertices.empty()) {
            geometries.push_back(std::move(geometry));
            Log::debug(
                "Model",
                "Loaded geometry with {} vertices, {} indices, "
                "material {} (node: {})",
                geometries.back().m_vertices.size(),
                geometries.back().m_indices.size(),
                geometries.back().materialIndex, nodeIndex
            );
        }
    }

    for (int child : node.children) {
        processNode(model, child, worldTransform);
    }
}

void ModelNode::processCameraNode(
    tinygltf::Model& model,
    int nodeIndex,
    const glm::mat4& parentTransform
) {
    if (nodeIndex < 0 ||
        static_cast<uint32_t>(nodeIndex) >= model.nodes.size())
        return;

    const tinygltf::Node& node = model.nodes[nodeIndex];
    glm::mat4 localTransform = getNodeTransform(node);
    glm::mat4 worldTransform = parentTransform * localTransform;

    // Check if this node has a camera
    if (node.camera >= 0 &&
        static_cast<size_t>(node.camera) < gltfCameras.size()) {
        GLTFCamera& cam = gltfCameras[node.camera];
        cam.transform = worldTransform;
        // Extract position from transform matrix
        cam.position = glm::vec3(worldTransform[3]);
        Log::debug(
            "Model", "Camera '{}' at position ({}, {}, {})", cam.name,
            cam.position.x, cam.position.y, cam.position.z
        );
    }

    // Process children
    for (int child : node.children) {
        processCameraNode(model, child, worldTransform);
    }
}

void ModelNode::clearPrimitives() {
    for (auto& image : images)
        image.image = {};
    baseTextureArray = {};
    vertexDataArray = {};
    modelMatrixArray = {};
    cameraUboArray = {};
    cameraUbo = nullptr;
}

void ModelNode::createPrimitives(primitives::Store& store) {
    primitives::StoreHandle textureNotFound{};
    auto createNewDefaultTexture = [this, &store]() -> auto {
        auto texture = store.newImage();
        auto& storeImage = store.images[texture.handle];
        storeImage.imageData = defaultTexture.pixels;
        storeImage.imageSize =
            defaultTexture.width * defaultTexture.height * 4;
        storeImage.extentType = ExtentType::Custom;
        storeImage.imageInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
        storeImage.imageInfo.extent.width = defaultTexture.width;
        storeImage.imageInfo.extent.height = defaultTexture.height;
        storeImage.imageInfo.extent.depth = 1;
        storeImage.imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        storeImage.viewInfo.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        return texture;
    };

    for (auto& image : images) {
        // We don't want to create primitives for images we didn't
        // even load in the first place...
        if (!image.toLoad)
            continue;

        // If we didn't successfully load the image, just pass the
        // handle to the default texture instead
        if (image.pixels == nullptr) {
            if (!textureNotFound.isValid())
                textureNotFound = createNewDefaultTexture();
            image.image = textureNotFound;
        }

        image.image = store.newImage();
        auto& storeImage = store.images[image.image.handle];
        storeImage.imageData = image.pixels;
        storeImage.imageSize = image.width * image.height * 4;
        storeImage.extentType = ExtentType::Custom;
        storeImage.imageInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
        storeImage.imageInfo.extent.width = image.width;
        storeImage.imageInfo.extent.height = image.height;
        storeImage.imageInfo.extent.depth = 1;
        storeImage.imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        storeImage.viewInfo.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        // Store original image path for code generation (wuffs loading)
        storeImage.originalImagePath = image.path.string();
    }

    baseTextureArray = store.newArray();
    auto& array = store.arrays[baseTextureArray.handle];
    array.type = primitives::Type::Image;

    array.handles.resize(modelData.ranges.size());
    auto handles = std::views::zip(array.handles, modelData.ranges);
    for (auto&& [handle, range] : handles) {
        assert(range.materialIndex >= 0);
        int img = materials[range.materialIndex].baseTextureIndex;
        if (img < 0) {
            // Only create a default texture if we actually need one
            if (!textureNotFound.isValid())
                textureNotFound = createNewDefaultTexture();

            handle = textureNotFound.handle;
        } else {
            assert(images[img].image.isValid());
            handle = images[img].image.handle;
        }
    }

    // TODO
    vertexDataArray = store.newArray();
    auto& vertexArray = store.arrays[vertexDataArray.handle];
    vertexArray.type = primitives::Type::VertexData;

    vertexArray.handles.resize(modelData.ranges.size());

    for (size_t i = 0; i < modelData.ranges.size(); ++i) {
        const auto& range = modelData.ranges[i];

        // Create a VertexData primitive for this geometry
        primitives::StoreHandle hVertexData = store.newVertexData();
        primitives::VertexData& vertexData =
            store.vertexDatas[hVertexData.handle];

        // Calculate spans for this specific geometry range
        size_t vertexOffset = range.firstVertex * sizeof(Vertex);
        size_t vertexSize = range.vertexCount * sizeof(Vertex);
        size_t indexOffset = range.firstIndex * sizeof(uint32_t);
        size_t indexSize = range.indexCount * sizeof(uint32_t);

        // Set up vertex data span
        uint8_t* vertexDataPtr = reinterpret_cast<uint8_t*>(
            modelData.vertices.data() + range.firstVertex
        );
        vertexData.vertexData =
            std::span<uint8_t>(vertexDataPtr, vertexSize);
        vertexData.vertexDataSize = vertexSize;
        vertexData.vertexCount = range.vertexCount;

        // Set up index data span
        uint32_t* indexDataPtr =
            modelData.indices.data() + range.firstIndex;
        vertexData.indexData =
            std::span<uint32_t>(indexDataPtr, range.indexCount);
        vertexData.indexDataSize = indexSize;
        vertexData.indexCount = range.indexCount;

        // Set up vertex input descriptions
        vertexData.bindingDescription = Vertex::getBindingDescription();
        vertexData.attributeDescriptions =
            Vertex::getAttributeDescriptions();

        // Set up model file path and geometry index for code generation
        vertexData.modelFilePath = settings.modelPath;
        vertexData.geometryIndex = static_cast<uint32_t>(i);

        // Store handle in array
        vertexArray.handles[i] = hVertexData.handle;

        Log::debug(
            "Model",
            "Created VertexData primitive for range {}: {} vertices, "
            "{} indices",
            i, range.vertexCount, range.indexCount
        );
    }

    modelMatrixArray = store.newArray();
    auto& uboArray = store.arrays[modelMatrixArray.handle];
    uboArray.type = primitives::Type::UniformBuffer;
    uboArray.handles.resize(modelData.ranges.size());

    glm::mat4 modelMatrix{1.0};
    settings.modelMatrix = modelMatrix;

    // TODO
    glm::mat3 normalMat3 =
        glm::transpose(glm::inverse(glm::mat3(modelMatrix)));
    glm::mat4 normalMatrix = glm::mat4(normalMat3);

    // Store matrices permanently
    modelMatricesData.clear();
    modelMatricesData.resize(modelData.ranges.size());
    for (auto& matrices : modelMatricesData) {
        matrices.model = modelMatrix;
        matrices.normalMatrix = normalMatrix;
    }

    // Create UBO primitives pointing to persistent storage
    for (size_t i = 0; i < modelData.ranges.size(); ++i) {
        primitives::StoreHandle hUBO = store.newUniformBuffer();
        primitives::UniformBuffer& ubo =
            store.uniformBuffers[hUBO.handle];

        // Point to persistent storage - now includes both model and
        // normal matrix
        ubo.data = std::span<uint8_t>(
            reinterpret_cast<uint8_t*>(&modelMatricesData[i]),
            sizeof(ModelMatrices)
        );

        uboArray.handles[i] = hUBO.handle;

        Log::debug(
            "Model",
            "Created UniformBuffer primitive for range {} with model "
            "and normal matrix",
            i
        );
    }

    // Create camera UBO if we have cameras
    if (!gltfCameras.empty()) {
        // Create UniformBuffer primitive for camera
        primitives::StoreHandle hCameraUbo = store.newUniformBuffer();
        cameraUbo = &store.uniformBuffers[hCameraUbo.handle];

        // Point to our persistent camera data
        // TODO: Use the same struct as for other fixed camcam
        cameraUbo->dataType = primitives::UniformDataType::Camera;
        cameraUbo->data = std::span<uint8_t>(
            reinterpret_cast<uint8_t*>(&cameraData),
            sizeof(ModelCameraData)
        );
        cameraUbo->extraData = &cameraType;

        // Create array with single Camera
        cameraUboArray = store.newArray();
        auto& camArray = store.arrays[cameraUboArray.handle];
        camArray.type = primitives::Type::UniformBuffer;
        camArray.handles = {hCameraUbo.handle};

        // Update camera matrices now that UBO is set up
        updateCameraFromSelection();

        Log::debug(
            "ModelNode",
            "Created camera UBO primitive for selected GLTF camera"
        );
    }
}

void ModelNode::getOutputPrimitives(
    const primitives::Store& store,
    std::vector<std::pair<
        ax::NodeEditor::PinId,
        primitives::StoreHandle>>& outputs
) const {
    outputs.push_back({texturePin.id, baseTextureArray});
    if (vertexDataArray.isValid()) {
        outputs.push_back({vertexDataPin.id, vertexDataArray});
    }
    if (modelMatrixArray.isValid()) {
        outputs.push_back({modelMatrixPin.id, modelMatrixArray});
    }
    if (cameraUboArray.isValid()) {
        outputs.push_back({cameraPin.id, cameraUboArray});
    }
}

void ModelNode::updateCameraFromSelection() {
    // Check if we have a valid camera selected
    if (selectedCameraIndex < 0 ||
        selectedCameraIndex >= static_cast<int>(gltfCameras.size())) {
        // No camera selected - use default identity matrices
        cameraData.view = glm::mat4(1.0f);
        cameraData.invView = glm::mat4(1.0f);
        cameraData.proj = glm::mat4(1.0f);
        // Note: UBO update is handled by Camera::recordCommands() for
        // non-fixed cameras
        return;
    }

    const GLTFCamera& gltfCam = gltfCameras[selectedCameraIndex];

    // Compute view matrix from GLTF camera transform
    // GLTF cameras look down -Z in their local space
    glm::vec3 position = gltfCam.position;
    glm::vec3 forward =
        -glm::normalize(glm::vec3(gltfCam.transform[2]));
    glm::vec3 up = glm::normalize(glm::vec3(gltfCam.transform[1]));
    glm::vec3 target = position + forward;

    cameraData.view = glm::lookAt(position, target, up);
    cameraData.invView = glm::inverse(cameraData.view);

    // Compute projection matrix
    if (gltfCam.isPerspective) {
        float fovRadians = glm::radians(gltfCam.fov);
        float aspect = gltfCam.aspectRatio > 0.0f ? gltfCam.aspectRatio
                                                  : aspectRatio;
        cameraData.proj = glm::perspective(
            fovRadians, aspect, gltfCam.nearPlane, gltfCam.farPlane
        );
    } else {
        // Orthographic projection
        cameraData.proj = glm::ortho(
            -gltfCam.xmag, gltfCam.xmag, -gltfCam.ymag, gltfCam.ymag,
            gltfCam.nearPlane, gltfCam.farPlane
        );
    }

    // Flip Y for Vulkan
    cameraData.proj[1][1] *= -1;

    Log::debug(
        "ModelNode",
        "Updated camera from GLTF '{}' - Pos: ({:.2f}, {:.2f}, {:.2f})",
        gltfCam.name, position.x, position.y, position.z
    );
}

// ============================================================================
// File Watcher Integration
// ============================================================================

void ModelNode::setFileWatchingEnabled(bool enabled) {
    fileWatchingEnabled = enabled;

    if (fileWatcher) {
        if (enabled && !currentModelPath.empty()) {
            // Set up callback and start watching
            fileWatcher->setReloadCallback(
                [this](const std::string& filepath) {
                    Log::info(
                        "Model", "Detected change in model file: {}",
                        filepath
                    );
                    pendingReload = true;
                }
            );
            fileWatcher->watchFile(currentModelPath);
            Log::info(
                "Model", "File watching enabled for: {}",
                currentModelPath
            );
        } else if (!enabled) {
            fileWatcher->stopWatching();
            Log::info("Model", "File watching disabled");
        }
    }
}

bool ModelNode::isFileWatchingEnabled() const {
    return fileWatchingEnabled && fileWatcher &&
           fileWatcher->isWatching();
}

ModelFileWatcher::LoadingState ModelNode::getLoadingState() const {
    if (fileWatcher) {
        return fileWatcher->getLoadingState();
    }
    return ModelFileWatcher::LoadingState::Idle;
}

const std::string& ModelNode::getLastError() const {
    static std::string emptyError;
    if (fileWatcher) {
        return fileWatcher->getLastError();
    }
    return emptyError;
}

void ModelNode::reloadModel() {
    if (currentModelPath.empty()) {
        Log::warning("Model", "Cannot reload: no model path set");
        return;
    }

    Log::info("Model", "Reloading model from: {}", currentModelPath);

    if (fileWatcher) {
        fileWatcher->setLoadingState(
            ModelFileWatcher::LoadingState::Loading
        );
    }

    try {
        // Store current state that should persist across reload
        int savedCameraIndex = selectedCameraIndex;
        bool savedNeedsCameraApply = needsCameraApply;

        // Reload the model
        loadModel(std::filesystem::path(currentModelPath), projectRoot);

        // Restore camera selection if it's still valid
        if (savedCameraIndex >= 0 &&
            savedCameraIndex < static_cast<int>(gltfCameras.size())) {
            selectedCameraIndex = savedCameraIndex;
            needsCameraApply = savedNeedsCameraApply;
        }

        if (fileWatcher) {
            fileWatcher->setLoadingState(
                ModelFileWatcher::LoadingState::Loaded
            );
        }

        Log::info("Model", "Model reloaded successfully");
    } catch (const std::exception& e) {
        Log::error("Model", "Failed to reload model: {}", e.what());
        if (fileWatcher) {
            fileWatcher->setLoadingState(
                ModelFileWatcher::LoadingState::Error
            );
            fileWatcher->setLastError(e.what());
        }
    }

    pendingReload = false;
}