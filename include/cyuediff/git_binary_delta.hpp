#pragma once
#include <cyuediff/binary_delta.hpp>
#include <cyuediff/git.hpp>
namespace cyuediff {
struct GitBinaryDelta {
    // Retain materialized revisions and immutable commit/path/content identities for host inspection.
    GitRevision before, after;
    BinaryDelta delta;
    std::string status() const;
    Json summary(const BinaryDeltaOptions & = {}) const;
    Json to_json(const BinaryDeltaOptions & = {}) const;
};
// Missing sides are explicit and use an empty byte base/target for reconstruction.
// Both missing is status "absent", distinct from two present empty files.
GitBinaryDelta compare_git_binary_files(const GitRepository &, const std::string &before_revision,
                                        const std::string &after_revision, const std::string &before_path,
                                        const std::string &after_path = "", const BinaryDeltaOptions & = {});
} // namespace cyuediff
