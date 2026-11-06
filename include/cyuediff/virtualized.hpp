#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff {
// Explicit filesystem backend: root/aa/bb/cc/<remaining 34 lowercase hex digits>.upayload.
// Rejects invalid/zero FIoHash IDs. This only constructs a relative path; no file is opened.
fs::path virtualized_payload_relative_path(const std::string &content_id);
// Resolves an existing regular file inside the explicitly selected root; rejects escaping links.
fs::path virtualized_payload_file(const fs::path &root, const std::string &content_id,
                                  const OperationControl &control = {});
} // namespace cyuediff
