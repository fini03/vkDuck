// vim:foldmethod=marker
#include <vkDuck/image_loader.h>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <future>
#include <chrono>
#include <iostream>

#define WUFFS_IMPLEMENTATION
#define WUFFS_CONFIG__STATIC_FUNCTIONS
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__AUX__BASE
#define WUFFS_CONFIG__MODULE__AUX__IMAGE
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
#define WUFFS_CONFIG__MODULE__ZLIB
#define WUFFS_CONFIG__MODULE__PNG
#include "wuffs-v0.4.c"

namespace fs = std::filesystem;

void* imageLoad(
    const std::filesystem::path& path,
    uint32_t& width,
    uint32_t& height
) {
    // Read the entire file into memory
    std::vector<char> data;
    auto file_size = fs::file_size(path);
    data.resize(file_size);
    std::ifstream fin(path, std::ios::binary);
    fin.read(data.data(), file_size);
    if (!fin) {
        return nullptr;
    }

    // Decode using wuffs
    wuffs_aux::DecodeImageCallbacks callbacks;
    wuffs_aux::sync_io::MemoryInput input(data.data(), file_size);

    wuffs_aux::DecodeImageResult result =
        wuffs_aux::DecodeImage(callbacks, input);
    if (!result.error_message.empty()) {
        return nullptr;
    }

    width = result.pixbuf.pixcfg.width();
    height = result.pixbuf.pixcfg.height();
    return result.pixbuf_mem_owner.release();
}

void imageFree(void* pixels) {
    free(pixels);
}

std::unordered_map<std::string, LoadedImage> loadImagesAsync(const std::vector<std::string>& paths) {
    auto totalStart = std::chrono::high_resolution_clock::now();

    // Launch async tasks for each image
    std::vector<std::future<std::pair<std::string, LoadedImage>>> futures;
    futures.reserve(paths.size());

    for (const auto& path : paths) {
        futures.push_back(std::async(std::launch::async, [path]() {
            LoadedImage result;
            result.pixels = imageLoad(path, result.width, result.height);
            result.valid = (result.pixels != nullptr);
            return std::make_pair(path, result);
        }));
    }

    // Collect results
    std::unordered_map<std::string, LoadedImage> results;
    results.reserve(paths.size());

    for (auto& future : futures) {
        auto [path, image] = future.get();
        results[path] = image;
    }

    auto totalEnd = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> totalMs = totalEnd - totalStart;
    std::cout << "All images loaded in " << totalMs.count() << "ms (async, " << paths.size() << " images)" << std::endl;

    return results;
}
