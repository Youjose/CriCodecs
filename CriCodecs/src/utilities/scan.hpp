#pragma once
/**
 * @file scan.hpp
 * @brief Small portable wrappers for C formatted input.
 *
 * Keeps MSVC's secure CRT spelling out of format modules while preserving
 * normal libc calls on GCC/Clang.
 */

#include <cstdio>

namespace cricodecs::util {

template <typename... Args>
int sscanf(const char* buffer, const char* format, Args... args) noexcept {
#if defined(_MSC_VER)
    return ::sscanf_s(buffer, format, args...);
#else
    return std::sscanf(buffer, format, args...);
#endif
}

} // namespace cricodecs::util
