#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
const char *narrow_string_encoding_name(NarrowStringEncoding);
NarrowStringEncoding parse_narrow_string_encoding(const std::string &);
// Explicit deterministic conversion; invalid sequences/undefined CP1252 bytes fail.
std::string decode_narrow_string(const std::string &, NarrowStringEncoding, std::uint64_t max_output_bytes,
                                 const OperationControl & = {});
} // namespace cyuediff
