/**
 * @file io_reader_windows.cpp
 * @brief Win32 file-mapping reader backend.
 *
 * Project-local file loading backend for Windows builds. Implemented by
 * Youjose.
 */

#include "io_reader.hpp"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace cricodecs::io {

namespace detail {
struct win32_reader_handles {
    HANDLE file_handle = INVALID_HANDLE_VALUE;
    HANDLE mapping_handle = NULL;
};
} // namespace detail

reader::reader() noexcept : m_handles(nullptr) {}

reader::~reader() noexcept { close(); }

reader::reader(reader&& other) noexcept
    : m_handles(std::move(other.m_handles)),
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
        m_handles = std::move(other.m_handles);
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
    if (m_owns_mapping && m_data_ptr) {
        UnmapViewOfFile(m_data_ptr);
    }
    if (m_handles) {
        if (m_handles->mapping_handle != NULL) {
            CloseHandle(m_handles->mapping_handle);
            m_handles->mapping_handle = NULL;
        }
        if (m_handles->file_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handles->file_handle);
            m_handles->file_handle = INVALID_HANDLE_VALUE;
        }
    }
    m_data_ptr = nullptr;
    m_data_size = 0;
    m_cursor = 0;
    m_pattern = access_pattern::normal;
    m_owns_mapping = false;
    m_has_external_source = false;
}

bool reader::is_open() const noexcept {
    return m_has_external_source || m_data_ptr != nullptr
        || (m_handles && m_handles->file_handle != INVALID_HANDLE_VALUE);
}

std::span<const uint8_t> reader::data() const noexcept {
    return {m_data_ptr, m_data_size};
}

size_t reader::size() const noexcept {
    return m_data_size;
}

std::expected<void, const char*> reader::open_file_impl(const std::filesystem::path& path, access_pattern pattern) noexcept {
    m_handles = std::make_unique<detail::win32_reader_handles>();
    DWORD flags = FILE_ATTRIBUTE_NORMAL;
    if (pattern == access_pattern::random) {
        flags |= FILE_FLAG_RANDOM_ACCESS;
    } else if (pattern == access_pattern::sequential) {
        flags |= FILE_FLAG_SEQUENTIAL_SCAN;
    }

    m_handles->file_handle = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        flags,
        NULL
    );

    if (m_handles->file_handle == INVALID_HANDLE_VALUE) {
        m_handles.reset();
        return std::unexpected("I/O reader failed: could not open file");
    }

    LARGE_INTEGER file_size_li;
    if (!GetFileSizeEx(m_handles->file_handle, &file_size_li)) {
        CloseHandle(m_handles->file_handle);
        m_handles.reset();
        return std::unexpected("I/O reader failed: could not get file size");
    }

    m_data_size = static_cast<size_t>(file_size_li.QuadPart);
    if (m_data_size == 0) {
        m_data_ptr = nullptr;
        m_pattern = pattern;
        m_owns_mapping = false;
        m_cursor = 0;
        m_has_external_source = false;
        return {};
    }

    m_handles->mapping_handle = CreateFileMappingW(
        m_handles->file_handle, NULL, PAGE_READONLY, 0, 0, NULL
    );

    if (m_handles->mapping_handle == NULL) {
        CloseHandle(m_handles->file_handle);
        m_handles.reset();
        return std::unexpected("I/O reader failed: could not create file mapping");
    }

    const void* mapped_ptr = MapViewOfFile(
        m_handles->mapping_handle, FILE_MAP_READ, 0, 0, 0
    );

    if (mapped_ptr == NULL) {
        CloseHandle(m_handles->mapping_handle);
        CloseHandle(m_handles->file_handle);
        m_handles.reset();
        return std::unexpected("I/O reader failed: could not map view of file");
    }

    m_data_ptr = static_cast<const uint8_t*>(mapped_ptr);
    m_pattern = pattern;
    m_owns_mapping = true;
    m_cursor = 0;
    m_has_external_source = false;
    return {};
}

} // namespace cricodecs::io

#endif // _WIN32
