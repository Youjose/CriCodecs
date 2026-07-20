/**
 * @file io_reader_posix.cpp
 * @brief POSIX mmap-backed file reader.
 *
 * Project-local file loading backend for Linux/macOS-style platforms.
 * Implemented by Youjose.
 */

#include "io_reader.hpp"

#if !defined(_WIN32) && !defined(USE_FALLBACK_READER) && (defined(__unix__) || defined(__APPLE__) || defined(__linux__))

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

namespace cricodecs::io {

namespace detail {
struct posix_reader_descriptor {
    int fd = -1;
    size_t mapped_size = 0;
};
} // namespace detail

reader::reader() noexcept : m_descriptor(nullptr) {}

reader::~reader() noexcept { close(); }

reader::reader(reader&& other) noexcept
    : m_descriptor(std::move(other.m_descriptor)),
      m_data_ptr(other.m_data_ptr),
      m_data_size(other.m_data_size),
      m_cursor(other.m_cursor),
      m_owns_mapping(other.m_owns_mapping),
      m_has_external_source(other.m_has_external_source) {
    other.m_data_ptr = nullptr;
    other.m_data_size = 0;
    other.m_cursor = 0;
    other.m_owns_mapping = false;
    other.m_has_external_source = false;
}

reader& reader::operator=(reader&& other) noexcept {
    if (this != &other) {
        close();
        m_descriptor = std::move(other.m_descriptor);
        m_data_ptr = other.m_data_ptr;
        m_data_size = other.m_data_size;
        m_cursor = other.m_cursor;
        m_owns_mapping = other.m_owns_mapping;
        m_has_external_source = other.m_has_external_source;
        other.m_data_ptr = nullptr;
        other.m_data_size = 0;
        other.m_cursor = 0;
        other.m_owns_mapping = false;
        other.m_has_external_source = false;
    }
    return *this;
}

std::expected<void, const char*> reader::open(const std::filesystem::path& path) noexcept {
    return open(path, access_pattern::sequential);
}

std::expected<void, const char*> reader::open(const std::filesystem::path& path, access_pattern pattern) noexcept {
    if (is_open()) return std::unexpected("I/O reader failed: already open");
    return open_file_impl(path, pattern);
}

std::expected<void, const char*> reader::open(const uint8_t* data, size_t size) noexcept {
    if (is_open()) return std::unexpected("I/O reader failed: already open");
    if (data == nullptr && size > 0) return std::unexpected("I/O reader failed: invalid memory buffer");
    m_data_ptr = data;
    m_data_size = size;
    m_cursor = 0;
    m_pattern = access_pattern::normal;
    m_owns_mapping = false;
    m_has_external_source = true;
    return {};
}

void reader::close() noexcept {
    if (m_owns_mapping && m_data_ptr && m_data_size > 0) {
        munmap(const_cast<uint8_t*>(m_data_ptr), m_data_size);
    }
    if (m_descriptor && m_descriptor->fd >= 0) {
        ::close(m_descriptor->fd);
        m_descriptor->fd = -1;
    }
    m_data_ptr = nullptr;
    m_data_size = 0;
    m_cursor = 0;
    m_pattern = access_pattern::normal;
    m_owns_mapping = false;
    m_has_external_source = false;
}

bool reader::is_open() const noexcept {
    return m_has_external_source || m_data_ptr != nullptr || (m_descriptor && m_descriptor->fd >= 0);
}

std::span<const uint8_t> reader::data() const noexcept {
    return {m_data_ptr, m_data_size};
}

size_t reader::size() const noexcept {
    return m_data_size;
}

std::expected<void, const char*> reader::open_file_impl(const std::filesystem::path& path, access_pattern pattern) noexcept {
    m_descriptor = std::make_unique<detail::posix_reader_descriptor>();

    m_descriptor->fd = ::open(path.c_str(), O_RDONLY);
    if (m_descriptor->fd < 0) {
        m_descriptor.reset();
        return std::unexpected("I/O reader failed: could not open file");
    }

    struct stat file_stats;
    if (fstat(m_descriptor->fd, &file_stats) < 0) {
        ::close(m_descriptor->fd);
        m_descriptor.reset();
        return std::unexpected("I/O reader failed: could not stat file");
    }

    m_data_size = static_cast<size_t>(file_stats.st_size);
    if (m_data_size == 0) {
        m_data_ptr = nullptr;
        m_pattern = pattern;
        m_owns_mapping = false;
        m_cursor = 0;
        m_has_external_source = false;
        return {};
    }

    void* mapped = mmap(nullptr, m_data_size, PROT_READ, MAP_PRIVATE, m_descriptor->fd, 0);
    if (mapped == MAP_FAILED) {
        ::close(m_descriptor->fd);
        m_descriptor.reset();
        return std::unexpected("I/O reader failed: could not memory-map file");
    }

#if defined(POSIX_FADV_SEQUENTIAL) && defined(POSIX_FADV_RANDOM)
    if (pattern == access_pattern::random) {
        posix_fadvise(m_descriptor->fd, 0, 0, POSIX_FADV_RANDOM);
    } else if (pattern == access_pattern::sequential) {
        posix_fadvise(m_descriptor->fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    }
#endif
#if defined(MADV_SEQUENTIAL) && defined(MADV_RANDOM)
    if (pattern == access_pattern::random) {
        madvise(mapped, m_data_size, MADV_RANDOM);
    } else if (pattern == access_pattern::sequential) {
        madvise(mapped, m_data_size, MADV_SEQUENTIAL);
    }
#endif

    m_data_ptr = static_cast<const uint8_t*>(mapped);
    m_descriptor->mapped_size = m_data_size;
    m_pattern = pattern;
    m_owns_mapping = true;
    m_cursor = 0;
    m_has_external_source = false;
    return {};
}

} // namespace cricodecs::io

#endif // POSIX
