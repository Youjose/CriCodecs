/**
 * @file io_writer_posix.cpp
 * @brief POSIX file writer backend.
 *
 * Project-local write backend for Linux/macOS-style platforms. Implemented by
 * Youjose.
 */

#include "io_writer.hpp"

#if !defined(_WIN32) && (defined(__unix__) || defined(__APPLE__) || defined(__linux__))

#include <array>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

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

    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return std::unexpected("I/O writer failed: could not create file");
    }

    m_handle = reinterpret_cast<void*>(static_cast<intptr_t>(fd));
    m_buffer.resize(buffer_size);
    m_buffer_pos = 0;
    m_total_written = 0;
    m_buffer_zeroed = true;
    m_write_failed = false;
    return {};
}

std::expected<void, const char*> writer::close() noexcept {
    if (!m_handle) return {};

    int fd = static_cast<int>(reinterpret_cast<intptr_t>(m_handle));
    if (m_write_failed) {
        ::close(fd);
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
            ::close(fd);
            m_handle = nullptr;
            return res;
        }
    }

    if (::close(fd) != 0) {
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
    const auto write_direct = [&](const uint8_t* source, size_t remaining) noexcept {
        int fd = static_cast<int>(reinterpret_cast<intptr_t>(m_handle));
        size_t total_written = 0;
        while (total_written < remaining) {
            const ssize_t written = ::write(
                fd,
                source + total_written,
                remaining - total_written
            );
            if (written <= 0) {
                return false;
            }
            total_written += static_cast<size_t>(written);
        }
        m_total_written += total_written;
        return true;
    };

    if (size == 0 || m_handle == nullptr || m_write_failed) {
        return;
    }
    if (m_buffer.empty()) {
        if (!write_direct(data, size)) {
            m_write_failed = true;
        }
        return;
    }
    if (size >= m_buffer.size()) {
        if (m_buffer_pos > 0 && !flush_buffer()) {
            return;
        }
        if (!write_direct(data, size)) {
            m_write_failed = true;
        }
        return;
    }

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

    int fd = static_cast<int>(reinterpret_cast<intptr_t>(m_handle));
    size_t total_written = 0;
    while (total_written < m_buffer_pos) {
        const ssize_t written = ::write(
            fd,
            m_buffer.data() + total_written,
            m_buffer_pos - total_written
        );
        if (written <= 0) {
            m_write_failed = true;
            return std::unexpected("I/O writer failed: write failed");
        }
        total_written += static_cast<size_t>(written);
    }

    m_total_written += total_written;
    m_buffer_pos = 0;
    return {};
}

void writer::write_zeros(size_t count) noexcept {
    if (count == 0 || m_handle == nullptr || m_write_failed) {
        return;
    }
    if (m_buffer.empty()) {
        std::array<uint8_t, 4096> zeros = {};
        while (count > 0) {
            const size_t chunk = count < sizeof(zeros) ? count : sizeof(zeros);
            write_to_buffer(zeros.data(), chunk);
            count -= chunk;
        }
        return;
    }

    const auto write_direct = [&](const uint8_t* source, size_t remaining) noexcept {
        int fd = static_cast<int>(reinterpret_cast<intptr_t>(m_handle));
        size_t total_written = 0;
        while (total_written < remaining) {
            const ssize_t written = ::write(
                fd,
                source + total_written,
                remaining - total_written
            );
            if (written <= 0) {
                return false;
            }
            total_written += static_cast<size_t>(written);
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

#endif // POSIX
