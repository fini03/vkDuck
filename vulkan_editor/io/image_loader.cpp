#include "image_loader.h"
#include "../util/logger.h"
#include <fstream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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

// Memory-mapped file for zero-copy I/O
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

class Wuffs_LoadRaw_Callbacks : public wuffs_aux::DecodeImageCallbacks {
public:
    Wuffs_LoadRaw_Callbacks()
        : m_surface(nullptr) {}

    ~Wuffs_LoadRaw_Callbacks() {
        if (m_surface) {
            delete[] m_surface;
            m_surface = nullptr;
        }
    }

    uint8_t* TakeSurface(
        uint32_t& width,
        uint32_t& height
    ) {
        if (!m_surface) {
            return nullptr;
        }

        auto ret = m_surface;
        m_surface = nullptr;
        return ret;
    }

private:
    wuffs_base__pixel_format SelectPixfmt(
        const wuffs_base__image_config& image_config
    ) override {
        return wuffs_base__make_pixel_format(
            WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL
        );
    }

    AllocPixbufResult AllocPixbuf(
        const wuffs_base__image_config& image_config,
        bool allow_uninitialized_memory
    ) override {
        if (m_surface) {
            delete[] m_surface;
            m_surface = nullptr;
        }

        w = image_config.pixcfg.width();
        h = image_config.pixcfg.height();
        if ((w > 0xFFFFFF) || (h > 0xFFFFFF)) {
            return AllocPixbufResult(
                "Wuffs_Load_RW_Callbacks: image is too large"
            );
        }

        m_surface = new uint8_t[w * h * 4];
        if (!m_surface) {
            return AllocPixbufResult(
                "Wuffs_Load_RW_Callbacks: Surface alloc failed"
            );
        }

        wuffs_base__pixel_buffer pixbuf;
        wuffs_base__status status = pixbuf.set_interleaved(
            &image_config.pixcfg,
            wuffs_base__make_table_u8(
                static_cast<uint8_t*>(m_surface), w * 4, h, w * 4
            ),
            wuffs_base__empty_slice_u8()
        );
        if (!status.is_ok()) {
            delete[] m_surface;
            m_surface = nullptr;
            return AllocPixbufResult(status.message());
        }
        return AllocPixbufResult(
            wuffs_aux::MemOwner(NULL, &free), pixbuf
        );
    }

    uint8_t* m_surface;
    uint32_t w;
    uint32_t h;
};

class Wuffs_LoadRaw_Input : public wuffs_aux::sync_io::Input {
public:
    Wuffs_LoadRaw_Input(const fs::path& texturePath)
        : fin{texturePath,
              std::ios::binary} {}

private:
    std::string CopyIn(wuffs_aux::IOBuffer* dst) override {
        if (!fin) {
            return "Wuffs_Load_RW_Input: File not open";
        } else if (!dst) {
            return "Wuffs_Load_RW_Input: NULL IOBuffer";
        } else if (dst->meta.closed) {
            return "Wuffs_Load_RW_Input: end of file";
        }
        dst->compact();
        if (dst->writer_length() == 0) {
            return "Wuffs_Load_RW_Input: full IOBuffer";
        }
        fin.read(
            reinterpret_cast<char*>(dst->writer_pointer()),
            dst->writer_length()
        );
        dst->meta.wi += fin.gcount();
        return std::string();
    }

    std::ifstream fin;
};

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
