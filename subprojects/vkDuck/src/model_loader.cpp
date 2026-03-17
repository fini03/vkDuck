// vim:foldmethod=marker
#include <vkDuck/model_loader.h>

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_INCLUDE_STB
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#include "tiny_gltf.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <future>

// SIMD headers for optimized index conversion
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define USE_NEON 1
#elif defined(__SSE4_1__)
#include <smmintrin.h>
#define USE_SSE 1
#endif

// Internal helper functions {{{
namespace {

namespace fs = std::filesystem;

// SIMD-optimized index conversion for better throughput {{{
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
// }}}

// Texture path resolution {{{
/// Try multiple paths to find the texture (all project-relative)
static fs::path findTexturePath(
    const fs::path& parentPath,
    const fs::path& projectRoot,
    std::string_view uri
) {
    auto texturePath = parentPath / uri;
    if (fs::exists(texturePath))
        return texturePath;

    // Try sibling "images" folder (e.g., data/models/../images/texture.png)
    auto siblingImagesPath = parentPath.parent_path() / "images" / uri;
    if (fs::exists(siblingImagesPath)) {
        return siblingImagesPath;
    }

    if (projectRoot.empty()) {
        return texturePath;
    }

    // Try data/images relative to project root
    auto dataImagesPath = projectRoot / "data" / "images" / uri;
    if (fs::exists(dataImagesPath)) {
        return dataImagesPath;
    }

    return texturePath;
}

/// Batch resolve texture paths in parallel to reduce syscall overhead
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
// }}}

glm::mat4 getNodeTransform(const tinygltf::Node& node) {
    glm::mat4 transform(1.0f);

    if (node.matrix.size() == 16) {
        transform = glm::make_mat4(node.matrix.data());
    } else {
        if (node.translation.size() == 3) {
            transform = glm::translate(transform,
                glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
        }
        if (node.rotation.size() == 4) {
            glm::quat q(
                static_cast<float>(node.rotation[3]),
                static_cast<float>(node.rotation[0]),
                static_cast<float>(node.rotation[1]),
                static_cast<float>(node.rotation[2])
            );
            transform *= glm::mat4_cast(q);
        }
        if (node.scale.size() == 3) {
            transform = glm::scale(transform,
                glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
        }
    }

    return transform;
}

struct TempGeometry {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    int materialIndex;
};

void processNode(
    tinygltf::Model& model,
    int nodeIndex,
    const glm::mat4& parentTransform,
    std::vector<TempGeometry>& geometries
) {
    if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= model.nodes.size())
        return;

    const tinygltf::Node& node = model.nodes[nodeIndex];
    glm::mat4 localTransform = getNodeTransform(node);
    glm::mat4 worldTransform = parentTransform * localTransform;

    if (node.mesh >= 0 && static_cast<size_t>(node.mesh) < model.meshes.size()) {
        const auto& mesh = model.meshes[node.mesh];

        for (const auto& primitive : mesh.primitives) {
            TempGeometry geometry{};
            geometry.materialIndex = primitive.material;

            glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(worldTransform)));

            if (primitive.attributes.find("POSITION") == primitive.attributes.end()) {
                continue;
            }

            const tinygltf::Accessor& posAccessor =
                model.accessors[primitive.attributes.at("POSITION")];

            const tinygltf::Accessor* normalAccessor = nullptr;
            bool hasNormals = primitive.attributes.find("NORMAL") != primitive.attributes.end();
            if (hasNormals) {
                normalAccessor = &model.accessors[primitive.attributes.at("NORMAL")];
            }

            bool hasTexCoords = primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end();
            const tinygltf::Accessor* texCoordAccessor = nullptr;
            if (hasTexCoords) {
                texCoordAccessor = &model.accessors[primitive.attributes.at("TEXCOORD_0")];
            }

            bool hasTangents = primitive.attributes.find("TANGENT") != primitive.attributes.end();
            const tinygltf::Accessor* tangentAccessor = nullptr;
            if (hasTangents) {
                tangentAccessor = &model.accessors[primitive.attributes.at("TANGENT")];
            }

            bool hasColors = primitive.attributes.find("COLOR_0") != primitive.attributes.end();
            const tinygltf::Accessor* colorAccessor = nullptr;
            if (hasColors) {
                colorAccessor = &model.accessors[primitive.attributes.at("COLOR_0")];
            }

            const tinygltf::BufferView& posBufferView = model.bufferViews[posAccessor.bufferView];
            const tinygltf::BufferView* normalBufferView = nullptr;
            const tinygltf::BufferView* texCoordBufferView = nullptr;
            const tinygltf::BufferView* colorBufferView = nullptr;
            const tinygltf::BufferView* tangentBufferView = nullptr;

            if (hasNormals) {
                normalBufferView = &model.bufferViews[normalAccessor->bufferView];
            }
            if (hasTexCoords) {
                texCoordBufferView = &model.bufferViews[texCoordAccessor->bufferView];
            }
            if (hasColors) {
                colorBufferView = &model.bufferViews[colorAccessor->bufferView];
            }
            if (hasTangents) {
                tangentBufferView = &model.bufferViews[tangentAccessor->bufferView];
            }

            const tinygltf::Buffer& posBuffer = model.buffers[posBufferView.buffer];
            const tinygltf::Buffer* normalBuffer = nullptr;
            const tinygltf::Buffer* texCoordBuffer = nullptr;
            const tinygltf::Buffer* colorBuffer = nullptr;
            const tinygltf::Buffer* tangentBuffer = nullptr;

            if (hasNormals) {
                normalBuffer = &model.buffers[normalBufferView->buffer];
            }
            if (hasTexCoords) {
                texCoordBuffer = &model.buffers[texCoordBufferView->buffer];
            }
            if (hasColors) {
                colorBuffer = &model.buffers[colorBufferView->buffer];
            }
            if (hasTangents) {
                tangentBuffer = &model.buffers[tangentBufferView->buffer];
            }

            size_t posStride = posBufferView.byteStride ? posBufferView.byteStride : sizeof(float) * 3;
            size_t normalStride = (hasNormals && normalBufferView->byteStride)
                ? normalBufferView->byteStride : sizeof(float) * 3;
            size_t texCoordStride = (hasTexCoords && texCoordBufferView->byteStride)
                ? texCoordBufferView->byteStride : sizeof(float) * 2;
            // COLOR_0 can be VEC3 or VEC4, default to VEC4 stride
            size_t colorStride = (hasColors && colorBufferView->byteStride)
                ? colorBufferView->byteStride : sizeof(float) * 4;
            size_t tangentStride = (hasTangents && tangentBufferView->byteStride)
                ? tangentBufferView->byteStride : sizeof(float) * 4;

            const uint8_t* posData = &posBuffer.data[posBufferView.byteOffset + posAccessor.byteOffset];
            const uint8_t* normalData = nullptr;
            const uint8_t* texCoordData = nullptr;
            const uint8_t* colorData = nullptr;
            const uint8_t* tangentData = nullptr;

            if (hasNormals) {
                normalData = &normalBuffer->data[normalBufferView->byteOffset + normalAccessor->byteOffset];
            }
            if (hasTexCoords) {
                texCoordData = &texCoordBuffer->data[texCoordBufferView->byteOffset + texCoordAccessor->byteOffset];
            }
            if (hasColors) {
                colorData = &colorBuffer->data[colorBufferView->byteOffset + colorAccessor->byteOffset];
            }
            if (hasTangents) {
                tangentData = &tangentBuffer->data[tangentBufferView->byteOffset + tangentAccessor->byteOffset];
            }

            // Pre-allocate vertices based on accessor count (glTF vertices are already deduplicated)
            const size_t vertexCount = posAccessor.count;
            geometry.vertices.resize(vertexCount);

            // Process all vertices in a single pass - much faster than per-index processing
            for (size_t i = 0; i < vertexCount; ++i) {
                Vertex& vertex = geometry.vertices[i];

                const float* pos = reinterpret_cast<const float*>(posData + i * posStride);
                glm::vec4 worldPos = worldTransform * glm::vec4(pos[0], pos[1], pos[2], 1.0f);
                vertex.pos = glm::vec3(worldPos);

                if (hasNormals) {
                    const float* norm = reinterpret_cast<const float*>(normalData + i * normalStride);
                    glm::vec3 transformedNormal = normalMatrix * glm::vec3(norm[0], norm[1], norm[2]);
                    vertex.normal = glm::normalize(transformedNormal);
                } else {
                    vertex.normal = {0.0f, 0.0f, 1.0f};
                }

                if (hasTexCoords) {
                    const float* tex = reinterpret_cast<const float*>(texCoordData + i * texCoordStride);
                    vertex.texCoord = {tex[0], tex[1]};
                } else {
                    vertex.texCoord = {0.0f, 0.0f};
                }

                if (hasColors) {
                    const float* col = reinterpret_cast<const float*>(colorData + i * colorStride);
                    vertex.color = {col[0], col[1], col[2]};
                } else {
                    vertex.color = {1.0f, 1.0f, 1.0f};  // Default white
                }

                if (hasTangents) {
                    const float* tan = reinterpret_cast<const float*>(tangentData + i * tangentStride);
                    glm::vec3 transformedTangent = normalMatrix * glm::vec3(tan[0], tan[1], tan[2]);
                    vertex.tangent = glm::vec4(glm::normalize(transformedTangent), tan[3]);
                } else {
                    // Default tangent pointing along +X axis with positive handedness
                    vertex.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
                }
            }

            // Copy indices directly - no hash map lookups needed
            if (primitive.indices >= 0) {
                const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
                const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
                const tinygltf::Buffer& indexBuffer = model.buffers[indexBufferView.buffer];
                const uint8_t* indexData = &indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset];

                const size_t indexCount = indexAccessor.count;
                geometry.indices.resize(indexCount);

                // Fast path: copy indices directly based on component type (with SIMD optimization)
                switch (indexAccessor.componentType) {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                    std::memcpy(geometry.indices.data(), indexData, indexCount * sizeof(uint32_t));
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                    const uint16_t* src = reinterpret_cast<const uint16_t*>(indexData);
                    convertIndices16to32(src, geometry.indices.data(), indexCount);
                    break;
                }
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
                    convertIndices8to32(indexData, geometry.indices.data(), indexCount);
                    break;
                }
                default:
                    throw std::runtime_error("Unsupported index component type");
                }
            }

            if (!geometry.vertices.empty()) {
                geometries.push_back(std::move(geometry));
            }
        }
    }

    for (int child : node.children) {
        processNode(model, child, worldTransform, geometries);
    }
}

// Process camera node transforms
void processCameraNode(
    tinygltf::Model& model,
    int nodeIndex,
    const glm::mat4& parentTransform,
    std::vector<GLTFCamera>& cameras
) {
    if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= model.nodes.size())
        return;

    const tinygltf::Node& node = model.nodes[nodeIndex];
    glm::mat4 localTransform = getNodeTransform(node);
    glm::mat4 worldTransform = parentTransform * localTransform;

    // Check if this node has a camera
    if (node.camera >= 0 && static_cast<size_t>(node.camera) < cameras.size()) {
        GLTFCamera& cam = cameras[node.camera];
        cam.transform = worldTransform;
        // Extract position from transform matrix
        cam.position = glm::vec3(worldTransform[3]);
    }

    // Process children
    for (int child : node.children) {
        processCameraNode(model, child, worldTransform, cameras);
    }
}

// Process light node transforms (KHR_lights_punctual)
void processLightNode(
    tinygltf::Model& model,
    int nodeIndex,
    const glm::mat4& parentTransform,
    std::vector<GLTFLight>& lights
) {
    if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= model.nodes.size())
        return;

    const tinygltf::Node& node = model.nodes[nodeIndex];
    glm::mat4 localTransform = getNodeTransform(node);
    glm::mat4 worldTransform = parentTransform * localTransform;

    // Check if this node has a light (via KHR_lights_punctual extension)
    if (node.extensions.count("KHR_lights_punctual")) {
        const auto& ext = node.extensions.at("KHR_lights_punctual");
        if (ext.Has("light")) {
            int lightIndex = ext.Get("light").GetNumberAsInt();
            if (lightIndex >= 0 && static_cast<size_t>(lightIndex) < lights.size()) {
                GLTFLight& light = lights[lightIndex];
                light.transform = worldTransform;
                // Extract position from transform matrix
                light.position = glm::vec3(worldTransform[3]);
                // Extract direction (lights point down -Z in local space)
                light.direction = -glm::normalize(glm::vec3(worldTransform[2]));
            }
        }
    }

    // Process children
    for (int child : node.children) {
        processLightNode(model, child, worldTransform, lights);
    }
}

} // anonymous namespace
// }}}

// Model loading implementation {{{

ModelData loadModel(const std::string& path, const std::string& projectRoot) {
#ifndef NDEBUG
    auto totalStart = std::chrono::high_resolution_clock::now();
#endif

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ret = false;
    if (path.ends_with(".glb")) {
        ret = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    } else {
        ret = loader.LoadASCIIFromFile(&model, &err, &warn, path);
    }

    if (!warn.empty()) {
        std::cerr << "Warning loading model: " << warn << std::endl;
    }

    if (!err.empty() || !ret) {
        throw std::runtime_error("Failed to load model: " + path + " - " + err);
    }

    std::vector<TempGeometry> geometries;

    // Pre-allocate geometry vector to avoid reallocations during loading
    {
        size_t estimatedGeometries = 0;
        for (const auto& mesh : model.meshes) {
            estimatedGeometries += mesh.primitives.size();
        }
        geometries.reserve(estimatedGeometries);
    }

    // Process all scenes
    int defaultScene = model.defaultScene >= 0 ? model.defaultScene : 0;
    for (int sceneIndex : model.scenes[defaultScene].nodes) {
        processNode(model, sceneIndex, glm::mat4(1.0f), geometries);
    }

    // Consolidate all geometries into single buffers
    ModelData result;

    // Extract cameras from GLTF {{{
    for (size_t i = 0; i < model.cameras.size(); ++i) {
        const auto& gltfCam = model.cameras[i];
        GLTFCamera cam;
        cam.name = gltfCam.name.empty() ? "Camera " + std::to_string(i) : gltfCam.name;

        if (gltfCam.type == "perspective") {
            cam.isPerspective = true;
            cam.fov = glm::degrees(static_cast<float>(gltfCam.perspective.yfov));
            cam.aspectRatio = static_cast<float>(gltfCam.perspective.aspectRatio);
            cam.nearPlane = static_cast<float>(gltfCam.perspective.znear);
            cam.farPlane = static_cast<float>(gltfCam.perspective.zfar);
        } else if (gltfCam.type == "orthographic") {
            cam.isPerspective = false;
            cam.xmag = static_cast<float>(gltfCam.orthographic.xmag);
            cam.ymag = static_cast<float>(gltfCam.orthographic.ymag);
            cam.nearPlane = static_cast<float>(gltfCam.orthographic.znear);
            cam.farPlane = static_cast<float>(gltfCam.orthographic.zfar);
        }

        result.cameras.push_back(cam);
    }

    // Find camera transforms from scene nodes
    for (int sceneIndex : model.scenes[defaultScene].nodes) {
        processCameraNode(model, sceneIndex, glm::mat4(1.0f), result.cameras);
    }
    // }}}

    // Extract lights from KHR_lights_punctual extension {{{
    if (model.extensions.count("KHR_lights_punctual")) {
        const auto& lightsExt = model.extensions.at("KHR_lights_punctual");
        if (lightsExt.Has("lights") && lightsExt.Get("lights").IsArray()) {
            const auto& lightsArray = lightsExt.Get("lights");
            for (size_t i = 0; i < lightsArray.ArrayLen(); ++i) {
                const auto& lightObj = lightsArray.Get(static_cast<int>(i));
                GLTFLight light;

                // Name
                if (lightObj.Has("name")) {
                    light.name = lightObj.Get("name").Get<std::string>();
                } else {
                    light.name = "Light " + std::to_string(i);
                }

                // Type
                if (lightObj.Has("type")) {
                    std::string typeStr = lightObj.Get("type").Get<std::string>();
                    if (typeStr == "directional") {
                        light.type = GLTFLightType::Directional;
                    } else if (typeStr == "point") {
                        light.type = GLTFLightType::Point;
                    } else if (typeStr == "spot") {
                        light.type = GLTFLightType::Spot;
                    }
                }

                // Color (default white)
                if (lightObj.Has("color") && lightObj.Get("color").IsArray()) {
                    const auto& colorArr = lightObj.Get("color");
                    if (colorArr.ArrayLen() >= 3) {
                        light.color = glm::vec3(
                            static_cast<float>(colorArr.Get(0).GetNumberAsDouble()),
                            static_cast<float>(colorArr.Get(1).GetNumberAsDouble()),
                            static_cast<float>(colorArr.Get(2).GetNumberAsDouble())
                        );
                    }
                }

                // Intensity (default 1.0)
                if (lightObj.Has("intensity")) {
                    light.intensity = static_cast<float>(lightObj.Get("intensity").GetNumberAsDouble());
                }

                // Range (point/spot only, 0 = infinite)
                if (lightObj.Has("range")) {
                    light.range = static_cast<float>(lightObj.Get("range").GetNumberAsDouble());
                }

                // Spot light cone angles
                if (light.type == GLTFLightType::Spot && lightObj.Has("spot")) {
                    const auto& spotObj = lightObj.Get("spot");
                    if (spotObj.Has("innerConeAngle")) {
                        light.innerConeAngle = static_cast<float>(spotObj.Get("innerConeAngle").GetNumberAsDouble());
                    }
                    if (spotObj.Has("outerConeAngle")) {
                        light.outerConeAngle = static_cast<float>(spotObj.Get("outerConeAngle").GetNumberAsDouble());
                    }
                }

                result.lights.push_back(light);
            }
        }
    }

    // Find light transforms from scene nodes
    for (int sceneIndex : model.scenes[defaultScene].nodes) {
        processLightNode(model, sceneIndex, glm::mat4(1.0f), result.lights);
    }
    // }}}

    // Extract texture paths and material data {{{
    fs::path parentPath = fs::path(path).parent_path();
    fs::path projRoot = projectRoot.empty() ? fs::path() : fs::path(projectRoot);

    // Collect texture URIs for batch resolution
    std::vector<std::pair<size_t, std::string>> texturesToResolve;
    texturesToResolve.reserve(model.textures.size());
    for (const auto& tex : model.textures) {
        if (tex.source >= 0 && static_cast<size_t>(tex.source) < model.images.size()) {
            texturesToResolve.emplace_back(tex.source, model.images[tex.source].uri);
        }
    }

    // Resolve all texture paths in parallel
    auto resolvedPaths = findTexturePathsBatch(
        parentPath, projRoot, texturesToResolve, model.images.size()
    );

    // Build mapping from GLTF image index to our allTexturePaths index
    // This deduplicates textures used by multiple materials
    std::unordered_map<int, int> imageIndexToPathIndex;
    for (size_t i = 0; i < resolvedPaths.size(); ++i) {
        if (!resolvedPaths[i].empty()) {
            imageIndexToPathIndex[static_cast<int>(i)] = static_cast<int>(result.allTexturePaths.size());
            result.allTexturePaths.push_back(resolvedPaths[i].string());
        }
    }

    // Helper to get texture index from GLTF texture info
    auto getTextureIndex = [&](int gltfTextureIndex) -> int {
        if (gltfTextureIndex < 0 || static_cast<size_t>(gltfTextureIndex) >= model.textures.size()) {
            return -1;
        }
        int imageIndex = model.textures[gltfTextureIndex].source;
        auto it = imageIndexToPathIndex.find(imageIndex);
        return (it != imageIndexToPathIndex.end()) ? it->second : -1;
    };

    // Extract all PBR material data
    result.materials.resize(model.materials.size());
    result.texturePaths.resize(model.materials.size()); // Legacy: keep for backwards compatibility
    for (size_t i = 0; i < model.materials.size(); ++i) {
        const auto& mat = model.materials[i];
        MaterialData& matData = result.materials[i];

        // Base color texture
        matData.baseColorTextureIndex = getTextureIndex(mat.pbrMetallicRoughness.baseColorTexture.index);
        matData.baseColorFactor = glm::vec4(
            static_cast<float>(mat.pbrMetallicRoughness.baseColorFactor[0]),
            static_cast<float>(mat.pbrMetallicRoughness.baseColorFactor[1]),
            static_cast<float>(mat.pbrMetallicRoughness.baseColorFactor[2]),
            static_cast<float>(mat.pbrMetallicRoughness.baseColorFactor[3])
        );

        // Metallic-roughness texture
        matData.metallicRoughnessTextureIndex = getTextureIndex(mat.pbrMetallicRoughness.metallicRoughnessTexture.index);
        matData.metallicFactor = static_cast<float>(mat.pbrMetallicRoughness.metallicFactor);
        matData.roughnessFactor = static_cast<float>(mat.pbrMetallicRoughness.roughnessFactor);

        // Emissive texture
        matData.emissiveTextureIndex = getTextureIndex(mat.emissiveTexture.index);
        matData.emissiveFactor = glm::vec3(
            static_cast<float>(mat.emissiveFactor[0]),
            static_cast<float>(mat.emissiveFactor[1]),
            static_cast<float>(mat.emissiveFactor[2])
        );

        // Normal texture
        matData.normalTextureIndex = getTextureIndex(mat.normalTexture.index);

#ifndef NDEBUG
        // Debug: log all PBR material values (only in debug builds)
        std::cout << "Material " << i << " '" << mat.name << "':\n"
            << "  Textures: baseColor=" << mat.pbrMetallicRoughness.baseColorTexture.index
            << ", metRough=" << mat.pbrMetallicRoughness.metallicRoughnessTexture.index
            << ", normal=" << mat.normalTexture.index
            << ", emissive=" << mat.emissiveTexture.index << "\n"
            << "  baseColorFactor: ["
            << mat.pbrMetallicRoughness.baseColorFactor[0] << ", "
            << mat.pbrMetallicRoughness.baseColorFactor[1] << ", "
            << mat.pbrMetallicRoughness.baseColorFactor[2] << ", "
            << mat.pbrMetallicRoughness.baseColorFactor[3] << "]\n"
            << "  metallicFactor: " << mat.pbrMetallicRoughness.metallicFactor << "\n"
            << "  roughnessFactor: " << mat.pbrMetallicRoughness.roughnessFactor << "\n"
            << "  emissiveFactor: ["
            << mat.emissiveFactor[0] << ", "
            << mat.emissiveFactor[1] << ", "
            << mat.emissiveFactor[2] << "]\n"
            << std::endl;
#endif

        // Legacy: store base color path for backwards compatibility
        if (matData.baseColorTextureIndex >= 0) {
            result.texturePaths[i] = result.allTexturePaths[matData.baseColorTextureIndex];
        }
    }
    // }}}

    // Pre-allocate result vectors to avoid reallocations
    {
        size_t totalVerts = 0;
        size_t totalIndices = 0;
        for (const auto& geom : geometries) {
            totalVerts += geom.vertices.size();
            totalIndices += geom.indices.size();
        }

        // Check for overflow before casting to uint32_t for Vulkan
        constexpr size_t maxUint32 = static_cast<size_t>(UINT32_MAX);
        if (totalVerts > maxUint32) {
            throw std::runtime_error("Model has too many vertices (" +
                std::to_string(totalVerts) + ") - exceeds uint32_t maximum");
        }
        if (totalIndices > maxUint32) {
            throw std::runtime_error("Model has too many indices (" +
                std::to_string(totalIndices) + ") - exceeds uint32_t maximum");
        }

        result.vertices.reserve(totalVerts);
        result.indices.reserve(totalIndices);
        result.ranges.reserve(geometries.size());
    }

    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;

    for (const auto& geom : geometries) {
        // Check individual geometry sizes before casting
        if (geom.vertices.size() > UINT32_MAX) {
            throw std::runtime_error("Geometry has too many vertices (" +
                std::to_string(geom.vertices.size()) + ")");
        }
        if (geom.indices.size() > UINT32_MAX) {
            throw std::runtime_error("Geometry has too many indices (" +
                std::to_string(geom.indices.size()) + ")");
        }

        GeometryRange range{};
        range.firstVertex = vertexOffset;
        range.vertexCount = static_cast<uint32_t>(geom.vertices.size());
        range.firstIndex = indexOffset;
        range.indexCount = static_cast<uint32_t>(geom.indices.size());
        range.materialIndex = geom.materialIndex;

        // Check for offset overflow before incrementing
        if (vertexOffset > UINT32_MAX - range.vertexCount) {
            throw std::runtime_error("Vertex offset overflow at geometry with " +
                std::to_string(range.vertexCount) + " vertices (current offset: " +
                std::to_string(vertexOffset) + ")");
        }
        if (indexOffset > UINT32_MAX - range.indexCount) {
            throw std::runtime_error("Index offset overflow at geometry with " +
                std::to_string(range.indexCount) + " indices (current offset: " +
                std::to_string(indexOffset) + ")");
        }

        result.ranges.push_back(range);

        // Add vertices
        result.vertices.insert(result.vertices.end(), geom.vertices.begin(), geom.vertices.end());

        // Add indices as-is (NOT rebased to absolute)
        // The ranges contain firstVertex which tells us where each geometry's vertices start
        // This allows consumers to either:
        // 1. Use the full consolidated buffer with vkCmdDrawIndexed(..., firstVertex=range.firstVertex)
        // 2. Create per-geometry slices where indices remain relative
        result.indices.insert(result.indices.end(), geom.indices.begin(), geom.indices.end());

        vertexOffset += range.vertexCount;
        indexOffset += range.indexCount;
    }

#ifndef NDEBUG
    auto totalEnd = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> totalMs = totalEnd - totalStart;
    std::cout << "Model loaded in " << totalMs.count() << "ms" << std::endl;
#endif

    return result;
}

void loadModelGeometry(
    const ModelData& data,
    uint32_t geometryIndex,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices
) {
    if (geometryIndex >= data.ranges.size()) {
        throw std::runtime_error("Geometry index out of range: " +
            std::to_string(geometryIndex) + " >= " + std::to_string(data.ranges.size()));
    }

    const auto& range = data.ranges[geometryIndex];

    // Extract vertices for this geometry
    outVertices.assign(
        data.vertices.begin() + range.firstVertex,
        data.vertices.begin() + range.firstVertex + range.vertexCount
    );

    // Extract indices for this geometry (they are already relative to each geometry's vertex buffer)
    outIndices.assign(
        data.indices.begin() + range.firstIndex,
        data.indices.begin() + range.firstIndex + range.indexCount
    );
}

std::unordered_map<std::string, ModelData> loadModelsAsync(const std::vector<std::string>& paths) {
#ifndef NDEBUG
    auto totalStart = std::chrono::high_resolution_clock::now();
#endif

    // Launch async tasks for each model
    std::vector<std::future<std::pair<std::string, ModelData>>> futures;
    futures.reserve(paths.size());

    for (const auto& path : paths) {
        futures.push_back(std::async(std::launch::async, [path]() {
            return std::make_pair(path, loadModel(path));
        }));
    }

    // Collect results
    std::unordered_map<std::string, ModelData> results;
    results.reserve(paths.size());

    for (auto& future : futures) {
        auto [path, data] = future.get();
        results[path] = std::move(data);
    }

#ifndef NDEBUG
    auto totalEnd = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> totalMs = totalEnd - totalStart;
    std::cout << "All models loaded in " << totalMs.count() << "ms (async, " << paths.size() << " models)" << std::endl;
#endif

    return results;
}
// }}}
