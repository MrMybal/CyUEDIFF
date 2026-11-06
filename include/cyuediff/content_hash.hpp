#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
// Lowercase 256-bit BLAKE3; Unreal's FIoHash uses its first 20 bytes (40 hex digits).
std::string blake3_bytes(const std::vector<std::uint8_t> &, const OperationControl &control = {});
std::string blake3_file(const fs::path &, const OperationControl &control = {});
} // namespace cyuediff
