#pragma once
#include "hca_header.hpp"

#include <expected>
#include <span>
#include <string>

namespace cricodecs::hca::detail {

[[nodiscard]] std::expected<HcaHeader, std::string> parse_header(std::span<const uint8_t> data);

} // namespace cricodecs::hca::detail
