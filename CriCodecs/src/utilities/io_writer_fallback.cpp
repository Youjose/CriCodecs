/**
 * @file io_writer_fallback.cpp
 * @brief Portable fallback file writer implementation.
 *
 * Project-local fallback path for platforms without the POSIX/Win32 writers.
 * Implemented by Youjose.
 */

#include "io_writer.hpp"

// Fallback writer using std::ofstream
#if !defined(_WIN32) && !defined(__unix__) && !defined(__APPLE__) && !defined(__linux__)

#include <cstring>
#include <fstream>

namespace cricodecs::io {

namespace detail {
struct fallback_writer_state {
    explicit fallback_writer_state(const std::filesystem::path& path)
        : file(path, std::ios::binary | std::ios::out) {}

    std::ofstream file;
};
} // namespace detail

writer::~writer() { static_cast<void>(close()); }

writer::writer(writer&& other) noexcept
    : m_fallback(std::move(other.m_fallback)),
      m_buffer(std::move(other.m_buffer)),
      m_buffer_pos(other.m_buffer_pos),
      m_total_written(other.m_total_written),
      m_write_failed(other.m_write_failed) {
    other.m_buffer_pos = 0;
    other.m_total_written = 0;
    other.m_write_failed = false;
}

writer& writer::operator=(writer&& other) noexcept {
    if (this != &other) {
        static_cast<void>(close());
        m_fallback = std::move(other.m_fallback);
        m_buffer = std::move(other.m_buffer);
        m_buffer_pos = other.m_buffer_pos;
        m_total_written = other.m_total_written;
        m_write_failed = other.m_write_failed;
        other.m_buffer_pos = 0;
        other.m_total_written = 0;
        other.m_write_failed = false;
    }
    return *this;
}

std::expected<void, const char*> writer::open(const std::filesystem::path& path, size_t buffer_size) {
    if (auto result = close(); !result) {
        return result;
    }

    m_fallback = std::make_unique<detail::fallback_writer_state>(path);
    if (!m_fallback->file.is_open()) {
        m_fallback.reset();
        return std::unexpected("I/O writer failed: could not create file");
    }

    m_buffer.resize(buffer_size);
    m_buffer_pos = 0;
    m_total_written = 0;
    m_write_failed = false;
    return {};
}

std::expected<void, const char*> writer::close() noexcept {
    if (!m_fallback) return {};
    if (m_write_failed) {
        m_fallback->file.close();
        m_fallback.reset();
        m_buffer.clear();
        m_buffer_pos = 0;
        m_write_failed = false;
        return std::unexpected("I/O writer failed: write failed");
    }

    if (m_buffer_pos > 0) {
        auto res = flush_buffer();
        if (!res) {
            m_fallback->file.close();
            m_fallback.reset();
            return res;
        }
    }

    m_fallback->file.close();
    if (m_fallback->file.fail()) {
        m_fallback.reset();
        return std::unexpected("I/O writer failed: could not close file");
    }

    m_fallback.reset();
    m_buffer.clear();
    m_buffer_pos = 0;
    return {};
}

bool writer::is_open() const noexcept {
    return m_fallback && m_fallback->file.is_open();
}

std::expected<void, const char*> writer::write(const void* data, size_t size) {
    if (!m_fallback) return std::unexpected("I/O writer failed: file is not open");
    write_to_buffer(static_cast<const uint8_t*>(data), size);
    if (m_write_failed) return std::unexpected("I/O writer failed: write failed");
    return {};
}

std::expected<void, const char*> writer::write(std::span<const uint8_t> data) {
    return write(data.data(), data.size());
}

std::expected<void, const char*> writer::flush() {
    if (!m_fallback) return std::unexpected("I/O writer failed: file is not open");
    if (m_write_failed) return std::unexpected("I/O writer failed: write failed");
    return flush_buffer();
}

void writer::write_to_buffer(const uint8_t* data, size_t size) noexcept {
    while (size > 0) {
        size_t space = m_buffer.size() - m_buffer_pos;
        size_t to_copy = size < space ? size : space;
        std::memcpy(m_buffer.data() + m_buffer_pos, data, to_copy);
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

    m_fallback->file.write(reinterpret_cast<const char*>(m_buffer.data()), m_buffer_pos);

    if (m_fallback->file.fail()) {
        m_write_failed = true;
        return std::unexpected("I/O writer failed: write failed");
    }

    m_total_written += m_buffer_pos;
    m_buffer_pos = 0;
    return {};
}

void writer::write_zeros(size_t count) noexcept {
    if (count == 0 || !m_fallback || m_buffer.empty() || m_write_failed) {
        return;
    }

    const auto write_direct = [&](const uint8_t* source, size_t remaining) noexcept {
        m_fallback->file.write(reinterpret_cast<const char*>(source), static_cast<std::streamsize>(remaining));
        if (m_fallback->file.fail()) {
            return false;
        }
        m_total_written += remaining;
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
    }

    static const uint8_t zeros[DEFAULT_BUFFER_SIZE] = {};
    while (count >= m_buffer.size()) {
        const size_t chunk = count < sizeof(zeros) ? count : sizeof(zeros);
        if (!write_direct(zeros, chunk)) {
            m_write_failed = true;
            return;
        }
        count -= chunk;
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

#endif // Fallback
