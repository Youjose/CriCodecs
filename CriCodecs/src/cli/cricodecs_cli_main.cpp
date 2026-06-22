#include "cli.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(argc > 0 ? static_cast<size_t>(argc - 1) : 0u);
    for (int index = 1; index < argc; ++index) {
        args.emplace_back(argv[index]);
    }
    return cricodecs::cli::run(args, std::cout, std::cerr);
}
