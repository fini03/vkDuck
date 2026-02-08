#include "image_loader.h"
#include "../util/logger.h"
#include <fstream>
#ifdef _WIN32
#include <Windows.h>
#else
extern "C" {
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
}
#endif

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
#include <wuffs-v0.4.c>

namespace fs = std::filesystem;

#ifdef _WIN32
struct MappedFile {
    LPVOID data{nullptr};
    size_t size{0};
    HANDLE hFile{INVALID_HANDLE_VALUE};
    HANDLE hMap{INVALID_HANDLE_VALUE};

    MappedFile(const fs::path& path) {
	auto pathStr{path.string()};
        hFile = CreateFile(
            pathStr.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (hFile == INVALID_HANDLE_VALUE) return;

        LARGE_INTEGER liFileSize{};
        if (!GetFileSizeEx(hFile, &liFileSize)) return;
        if (liFileSize.QuadPart == 0) return;
        size = static_cast<size_t>(liFileSize.QuadPart);

        hMap = CreateFileMapping(
            hFile,
            nullptr,
            PAGE_READONLY,
            0,
            0,
            nullptr
        );
        if (hMap == INVALID_HANDLE_VALUE) return;


        data = MapViewOfFile(
            hMap,
            FILE_MAP_READ,
            0,
            0,
            0
        );
        if (data == nullptr) return;
    }

    ~MappedFile() {
        if (data) UnmapViewOfFile(data);
        if (hMap != INVALID_HANDLE_VALUE) CloseHandle(hMap);
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);

        data = nullptr;
        hMap = INVALID_HANDLE_VALUE;
        hFile = INVALID_HANDLE_VALUE;
    }

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    bool isValid() const { return data != nullptr; }
};

#else

struct MappedFile {
    void* data = nullptr;
    size_t size = 0;
    int fd = -1;

    MappedFile(const fs::path& path) {
        fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) return;

        struct stat st;
        if (fstat(fd, &st) < 0) {
            close(fd);
            fd = -1;
            return;
        }
        size = static_cast<size_t>(st.st_size);

        data = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (data == MAP_FAILED) {
            data = nullptr;
            close(fd);
            fd = -1;
            return;
        }

        // Hint to kernel for sequential read-ahead
        madvise(data, size, MADV_SEQUENTIAL);
    }

    ~MappedFile() {
        if (data) munmap(data, size);
        if (fd >= 0) close(fd);
    }

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    bool isValid() const { return data != nullptr; }
};
#endif


void* imageLoad(
    const std::filesystem::path& path,
    uint32_t& width,
    uint32_t& height
) {
    // Use memory-mapped I/O for zero-copy file access
    MappedFile file(path);
    if (!file.isValid()) {
        Log::error("Model", "Error while mapping texture file: {}", path.string());
        return nullptr;
    }

    wuffs_aux::DecodeImageCallbacks callbacks;
    wuffs_aux::sync_io::MemoryInput input(
        static_cast<const char*>(file.data), file.size
    );

    wuffs_aux::DecodeImageResult result =
        wuffs_aux::DecodeImage(callbacks, input);
    if (!result.error_message.empty()) {
        Log::error(
            "Model", "Error decoding texture: {}", result.error_message
        );
        return nullptr;
    }

    width = result.pixbuf.pixcfg.width();
    height = result.pixbuf.pixcfg.height();
    return result.pixbuf_mem_owner.release();
}

void imageFree(void* pixels) {
    //delete[] reinterpret_cast<uint8_t*>(pixels);
    free(pixels);
}
