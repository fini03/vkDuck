#pragma once
#include <filesystem>

void* imageLoad(
    const std::filesystem::path& path,
    uint32_t& width,
    uint32_t& height
);

void imageFree(void* pixels);
