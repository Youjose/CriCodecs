/**
 * @file io_reader_fallback.cpp
 * @brief Portable fallback file reader implementation.
 *
 * Project-local fallback path for platforms without the mmap/Win32 readers.
 * Implemented by Youjose.
 */

#include "io_reader.hpp"

// Fallback: reads entire file into malloc'd buffer (like fast_io's allocation_file_loader)
// Used on platforms without mmap, or when USE_FALLBACK_READER is defined.
#if defined(USE_FALLBACK_READER) || (!defined(_WIN32) && !defined(__unix__) && !defined(__APPLE__) && !defined(__linux__))

#include <cstdio>
#include <cstdlib>

namespace cricodecs::io {

namespace detail {
struct fallback_reader_state {
    uint8_t* allocated_data = nullptr;
    size_t allocated_size = 0;

    ~fallback_reader_state() {
        if (allocated_data) {
            std::free(allocated_data);
            allocated_data = nullptr;
        }
    }
};
} // namespace detail

reader::reader() noexcept : m_fallback(nullptr) {}

reader::~reader() noexcept { close(); }

reader::reader(reader&& other) noexcept
    : m_fallback(std::move(other.m_fallback)),
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
        m_fallback = std::move(other.m_fallback);
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
    if (is_open()) return std::unexpected("I/O reader failed: already open");
    return open_file_impl(path);
}

std::expected<void, const char*> reader::open(const uint8_t* data, size_t size) noexcept {
    if (is_open()) return std::unexpected("I/O reader failed: already open");
    if (data == nullptr && size > 0) return std::unexpected("I/O reader failed: invalid memory buffer");
    m_data_ptr = data;
    m_data_size = size;
    m_cursor = 0;
    m_owns_mapping = false;
    m_has_external_source = true;
    return {};
}

void reader::close() noexcept {
    m_fallback.reset();
    m_data_ptr = nullptr;
    m_data_size = 0;
    m_cursor = 0;
    m_owns_mapping = false;
    m_has_external_source = false;
}

bool reader::is_open() const noexcept {
    return m_has_external_source || m_data_ptr != nullptr;
}

std::span<const uint8_t> reader::data() const noexcept {
    return {m_data_ptr, m_data_size};
}

size_t reader::size() const noexcept {
    return m_data_size;
}

std::expected<void, const char*> reader::open_file_impl(const std::filesystem::path& path) noexcept {
#if defined(_WIN32)
    std::FILE* fp = _wfopen(path.c_str(), L"rb");
#else
    std::FILE* fp = std::fopen(path.c_str(), "rb");
#endif
    if (!fp) {
        return std::unexpected("I/O reader failed: could not open file");
    }

    if (std::fseek(fp, 0, SEEK_END) != 0) {
        std::fclose(fp);
        return std::unexpected("I/O reader failed: could not seek file");
    }

    long size = std::ftell(fp);
    if (size < 0) {
        std::fclose(fp);
        return std::unexpected("I/O reader failed: could not get file size");
    }

    m_data_size = static_cast<size_t>(size);
    if (m_data_size == 0) {
        std::fclose(fp);
        m_data_ptr = nullptr;
        m_owns_mapping = false;
        m_cursor = 0;
        m_has_external_source = false;
        return {};
    }

    std::rewind(fp);

    m_fallback = std::make_unique<detail::fallback_reader_state>();
    m_fallback->allocated_data = static_cast<uint8_t*>(std::malloc(m_data_size));
    if (!m_fallback->allocated_data) {
        std::fclose(fp);
        m_fallback.reset();
        return std::unexpected("I/O reader failed: could not allocate memory");
    }

    size_t total_read = 0;
    while (total_read < m_data_size) {
        size_t chunk = std::fread(
            m_fallback->allocated_data + total_read,
            1,
            m_data_size - total_read,
            fp
        );
        if (chunk == 0) {
            std::fclose(fp);
            m_fallback.reset();
            return std::unexpected("I/O reader failed: could not read file");
        }
        total_read += chunk;
    }

    std::fclose(fp);
    m_fallback->allocated_size = m_data_size;
    m_data_ptr = m_fallback->allocated_data;
    m_owns_mapping = true;
    m_cursor = 0;
    m_has_external_source = false;
    return {};
}

} // namespace cricodecs::io

#endif // Fallback
