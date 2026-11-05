#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff::desktop {
// Resolve existing ancestors only: MSVC weakly_canonical may fail on absent companions.
inline fs::path resolved_output_path(const fs::path &path) {
    auto existing = fs::absolute(path);
    fs::path suffix;
    while (!fs::exists(existing)) {
        const auto parent = existing.parent_path();
        if (parent.empty() || parent == existing)
            throw Error("desktop-output", "Cannot resolve output ancestor");
        suffix = suffix.empty() ? existing.filename() : existing.filename() / suffix;
        existing = parent;
    }
    const auto resolved = fs::canonical(existing);
    return (suffix.empty() ? resolved : resolved / suffix).lexically_normal();
}
} // namespace cyuediff::desktop
