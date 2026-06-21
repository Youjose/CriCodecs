#include "io_reader.hpp"

#if defined(_WIN32)
#include "io_reader_windows.cpp"
#elif !defined(USE_FALLBACK_READER) && (defined(__unix__) || defined(__APPLE__) || defined(__linux__))
#include "io_reader_posix.cpp"
#else
#include "io_reader_fallback.cpp"
#endif
