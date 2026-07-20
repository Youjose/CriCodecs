/**
 * @file io_writer_windows.cpp
 * @brief Win32 file writer backend.
 *
 * Project-local write backend for Windows builds. Implemented by Youjose.
 */

#include "io_writer.hpp"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstring>
#include <limits>

namespace cricodecs::io {

writer::~writer() { static_cast<void>(close()); }

writer::writer(writer&& other) noexcept
    : m_handle(other.m_handle),
      m_buffer(std::move(other.m_buffer)),
      m_buffer_pos(other.m_buffer_pos),
      m_total_written(other.m_total_written),
      m_buffer_zeroed(other.m_buffer_zeroed),
      m_write_failed(other.m_write_failed) {
    other.m_handle = nullptr;
    other.m_buffer_pos = 0;
    other.m_total_written = 0;
    other.m_buffer_zeroed = false;
    other.m_write_failed = false;
}

writer& writer::operator=(writer&& other) noexcept {
    if (this != &other) {
        static_cast<void>(close());
        m_handle = other.m_handle;
        m_buffer = std::move(other.m_buffer);
        m_buffer_pos = other.m_buffer_pos;
        m_total_written = other.m_total_written;
        m_buffer_zeroed = other.m_buffer_zeroed;
        m_write_failed = other.m_write_failed;
        other.m_handle = nullptr;
        other.m_buffer_pos = 0;
        other.m_total_written = 0;
        other.m_buffer_zeroed = false;
        other.m_write_failed = false;
    }
    return *this;
}

std::expected<void, const char*> writer::open(const std::filesystem::path& path, size_t buffer_size) {
    if (auto result = close(); !result) {
        return result;
    }

    HANDLE h = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr
    );

    if (h == INVALID_HANDLE_VALUE) {
        return std::unexpected("I/O writer failed: could not create file");
    }

    m_handle = h;
    m_buffer.resize(buffer_size);
    m_buffer_pos = 0;
    m_total_written = 0;
    m_buffer_zeroed = true;
    m_write_failed = false;
    return {};
}

std::expected<void, const char*> writer::close() noexcept {
    if (!m_handle) return {};
    if (m_write_failed) {
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
        m_buffer.clear();
        m_buffer_pos = 0;
        m_buffer_zeroed = false;
        m_write_failed = false;
        return std::unexpected("I/O writer failed: write failed");
    }

    if (m_buffer_pos > 0) {
        auto res = flush_buffer();
        if (!res) {
            CloseHandle(static_cast<HANDLE>(m_handle));
            m_handle = nullptr;
            return res;
        }
    }

    if (!CloseHandle(static_cast<HANDLE>(m_handle))) {
        m_handle = nullptr;
        return std::unexpected("I/O writer failed: could not close file");
    }

    m_handle = nullptr;
    m_buffer.clear();
    m_buffer_pos = 0;
    m_buffer_zeroed = false;
    return {};
}

bool writer::is_open() const noexcept { return m_handle != nullptr; }

std::expected<void, const char*> writer::write(const void* data, size_t size) {
    if (!m_handle) return std::unexpected("I/O writer failed: file is not open");
    write_to_buffer(static_cast<const uint8_t*>(data), size);
    if (m_write_failed) return std::unexpected("I/O writer failed: write failed");
    return {};
}

std::expected<void, const char*> writer::write(std::span<const uint8_t> data) {
    return write(data.data(), data.size());
}

std::expected<void, const char*> writer::flush() {
    if (!m_handle) return std::unexpected("I/O writer failed: file is not open");
    if (m_write_failed) return std::unexpected("I/O writer failed: write failed");
    return flush_buffer();
}

void writer::write_to_buffer(const uint8_t* data, size_t size) noexcept {
    while (size > 0) {
        size_t space = m_buffer.size() - m_buffer_pos;
        size_t to_copy = size < space ? size : space;
        std::memcpy(m_buffer.data() + m_buffer_pos, data, to_copy);
        m_buffer_zeroed = false;
        m_buffer_pos += to_copy;
        data += to_copy;
        size -= to_copy;
        if (m_buffer_pos >= m_buffer.size()) {
            if (!flush_buffer()) {
                return;
            }
        }
    }
}

std::expected<void, const char*> writer::flush_buffer() {
    if (m_write_failed) return std::unexpected("I/O writer failed: write failed");
    if (m_buffer_pos == 0) return {};
    size_t total_written = 0;
    while (total_written < m_buffer_pos) {
        const size_t remaining = m_buffer_pos - total_written;
        const DWORD chunk_size = static_cast<DWORD>(
            remaining > std::numeric_limits<DWORD>::max()
                ? std::numeric_limits<DWORD>::max()
                : remaining
        );
        DWORD written = 0;
        if (!WriteFile(
                static_cast<HANDLE>(m_handle),
                m_buffer.data() + total_written,
                chunk_size,
                &written,
                nullptr
            ) || written == 0) {
            m_write_failed = true;
            return std::unexpected("I/O writer failed: write failed");
        }
        total_written += written;
    }
    m_total_written += total_written;
    m_buffer_pos = 0;
    return {};
}

void writer::write_zeros(size_t count) noexcept {
    if (count == 0 || m_handle == nullptr || m_buffer.empty() || m_write_failed) {
        return;
    }

    const auto write_direct = [&](const uint8_t* source, size_t remaining) noexcept {
        size_t total_written = 0;
        while (total_written < remaining) {
            const size_t pending = remaining - total_written;
            const DWORD chunk_size = static_cast<DWORD>(
                pending > std::numeric_limits<DWORD>::max()
                    ? std::numeric_limits<DWORD>::max()
                    : pending
            );
            DWORD written = 0;
            if (!WriteFile(
                    static_cast<HANDLE>(m_handle),
                    source + total_written,
                    chunk_size,
                    &written,
                    nullptr
                ) || written == 0) {
                return false;
            }
            total_written += written;
        }
        m_total_written += total_written;
        return true;
    };

    if (m_buffer_pos > 0) {
        const size_t space = m_buffer.size() - m_buffer_pos;
        const size_t chunk = count < space ? count : space;
        std::memset(m_buffer.data() + m_buffer_pos, 0, chunk);
        m_buffer_pos += chunk;
        count -= chunk;
        if (m_buffer_pos >= m_buffer.size()) {
            if (!flush_buffer()) {
                return;
            }
        }
        if (count == 0) {
            return;
        }
    }

    if (!m_buffer_zeroed) {
        std::memset(m_buffer.data(), 0, m_buffer.size());
        m_buffer_zeroed = true;
    }
    while (count >= m_buffer.size()) {
        if (!write_direct(m_buffer.data(), m_buffer.size())) {
            m_write_failed = true;
            return;
        }
        count -= m_buffer.size();
    }

    while (count > 0) {
        const size_t space = m_buffer.size() - m_buffer_pos;
        const size_t chunk = count < space ? count : space;
        std::memset(m_buffer.data() + m_buffer_pos, 0, chunk);
        m_buffer_pos += chunk;
        count -= chunk;
    }
}

} // namespace cricodecs::io

#endif // _WIN32
