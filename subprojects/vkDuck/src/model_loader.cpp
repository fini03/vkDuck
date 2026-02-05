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

            const tinygltf::BufferView& posBufferView = model.bufferViews[posAccessor.bufferView];
            const tinygltf::BufferView* normalBufferView = nullptr;
            const tinygltf::BufferView* texCoordBufferView = nullptr;

            if (hasNormals) {
                normalBufferView = &model.bufferViews[normalAccessor->bufferView];
            }
            if (hasTexCoords) {
                texCoordBufferView = &model.bufferViews[texCoordAccessor->bufferView];
            }

            const tinygltf::Buffer& posBuffer = model.buffers[posBufferView.buffer];
            const tinygltf::Buffer* normalBuffer = nullptr;
            const tinygltf::Buffer* texCoordBuffer = nullptr;

            if (hasNormals) {
                normalBuffer = &model.buffers[normalBufferView->buffer];
            }
            if (hasTexCoords) {
                texCoordBuffer = &model.buffers[texCoordBufferView->buffer];
            }

            size_t posStride = posBufferView.byteStride ? posBufferView.byteStride : sizeof(float) * 3;
            size_t normalStride = (hasNormals && normalBufferView->byteStride)
                ? normalBufferView->byteStride : sizeof(float) * 3;
            size_t texCoordStride = (hasTexCoords && texCoordBufferView->byteStride)
                ? texCoordBufferView->byteStride : sizeof(float) * 2;

            const uint8_t* posData = &posBuffer.data[posBufferView.byteOffset + posAccessor.byteOffset];
            const uint8_t* normalData = nullptr;
            const uint8_t* texCoordData = nullptr;

            if (hasNormals) {
                normalData = &normalBuffer->data[normalBufferView->byteOffset + normalAccessor->byteOffset];
            }
            if (hasTexCoords) {
                texCoordData = &texCoordBuffer->data[texCoordBufferView->byteOffset + texCoordAccessor->byteOffset];
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

} // anonymous namespace
// }}}

// Model loading implementation {{{

ModelData loadModel(const std::string& path, const std::string& projectRoot) {
    auto totalStart = std::chrono::high_resolution_clock::now();

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

    // Extract texture paths {{{
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

    // Store resolved texture paths (one per material)
    result.texturePaths.resize(model.materials.size());
    for (size_t i = 0; i < model.materials.size(); ++i) {
        const auto& mat = model.materials[i];
        int texIndex = mat.pbrMetallicRoughness.baseColorTexture.index;
        if (texIndex >= 0 && static_cast<size_t>(texIndex) < model.textures.size()) {
            int imageIndex = model.textures[texIndex].source;
            if (imageIndex >= 0 && static_cast<size_t>(imageIndex) < resolvedPaths.size()) {
                result.texturePaths[i] = resolvedPaths[imageIndex].string();
            }
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
        result.vertices.reserve(totalVerts);
        result.indices.reserve(totalIndices);
        result.ranges.reserve(geometries.size());
    }

    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;

    for (const auto& geom : geometries) {
        GeometryRange range{};
        range.firstVertex = vertexOffset;
        range.vertexCount = static_cast<uint32_t>(geom.vertices.size());
        range.firstIndex = indexOffset;
        range.indexCount = static_cast<uint32_t>(geom.indices.size());
        range.materialIndex = geom.materialIndex;

        result.ranges.push_back(range);

        // Add vertices
        result.vertices.insert(result.vertices.end(), geom.vertices.begin(), geom.vertices.end());

        // Add indices (offset by current vertex count)
        for (uint32_t idx : geom.indices) {
            result.indices.push_back(idx + vertexOffset);
        }

        vertexOffset += range.vertexCount;
        indexOffset += range.indexCount;
    }

    auto totalEnd = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> totalMs = totalEnd - totalStart;
    std::cout << "Model loaded in " << totalMs.count() << "ms" << std::endl;

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

    // Extract and rebase indices for this geometry
    outIndices.clear();
    outIndices.reserve(range.indexCount);
    for (uint32_t i = 0; i < range.indexCount; ++i) {
        // Indices in the consolidated buffer point to absolute positions,
        // we need to rebase them to be relative to this geometry's vertex offset
        uint32_t absoluteIndex = data.indices[range.firstIndex + i];
        uint32_t relativeIndex = absoluteIndex - range.firstVertex;
        outIndices.push_back(relativeIndex);
    }
}

std::unordered_map<std::string, ModelData> loadModelsAsync(const std::vector<std::string>& paths) {
    auto totalStart = std::chrono::high_resolution_clock::now();

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

    auto totalEnd = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> totalMs = totalEnd - totalStart;
    std::cout << "All models loaded in " << totalMs.count() << "ms (async, " << paths.size() << " models)" << std::endl;

    return results;
}
// }}}
