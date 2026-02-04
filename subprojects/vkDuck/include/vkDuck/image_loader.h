// vim:foldmethod=marker
#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>
#include <string>
#include <unordered_map>

// Loaded image data
struct LoadedImage {
    void* pixels = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    bool valid = false;
};

// Load an image file (PNG, etc.) using wuffs
// Returns pixel data in BGRA format, or nullptr on failure
// Caller must call imageFree() on the returned pointer
void* imageLoad(
    const std::filesystem::path& path,
    uint32_t& width,
    uint32_t& height
);

// Load multiple images in parallel
// Returns a map of path -> LoadedImage
std::unordered_map<std::string, LoadedImage> loadImagesAsync(const std::vector<std::string>& paths);

// Free image data returned by imageLoad
void imageFree(void* pixels);
