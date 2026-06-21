#include "io_writer.hpp"

#if defined(_WIN32)
#include "io_writer_windows.cpp"
#elif defined(__unix__) || defined(__APPLE__) || defined(__linux__)
#include "io_writer_posix.cpp"
#else
#include "io_writer_fallback.cpp"
#endif
