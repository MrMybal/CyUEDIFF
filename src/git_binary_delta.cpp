#include <cyuediff/git_binary_delta.hpp>
#include "binary_delta_internal.hpp"
namespace cyuediff {
std::string GitBinaryDelta::status() const {
    if (!before.present && !after.present)
        return "absent";
    if (!before.present)
        return "added";
    if (!after.present)
        return "removed";
    return delta.base_bytes == delta.target_bytes && delta.base_sha256 == delta.target_sha256 ? "identical"
                                                                                              : "changed";
}
Json GitBinaryDelta::summary(const BinaryDeltaOptions &o) const {
    return {{"schema", "cyuediff.git-binary-delta-summary/1"},
            {"status", status()},
            {"before", before.to_json()},
            {"after", after.to_json()},
            {"delta", delta.summary(o)}};
}
Json GitBinaryDelta::to_json(const BinaryDeltaOptions &o) const {
    return {{"schema", "cyuediff.git-binary-delta/1"},
            {"status", status()},
            {"before", before.to_json()},
            {"after", after.to_json()},
            {"delta", delta.to_json(o)}};
}
GitBinaryDelta compare_git_binary_files(const GitRepository &repo, const std::string &before_revision,
                                        const std::string &after_revision, const std::string &before_path,
                                        const std::string &after_path, const BinaryDeltaOptions &o) {
    detail::binary_delta_options(o);
    GitBinaryDelta result;
    result.before = repo.materialize_file(before_revision, before_path, o.max_input_bytes, o.control);
    result.after = repo.materialize_file(after_revision, after_path.empty() ? before_path : after_path,
                                         o.max_input_bytes, o.control);
    const auto a = result.before.present
                       ? read_binary_file(result.before.file_path(), o.max_input_bytes, o.control)
                       : std::vector<std::uint8_t>{};
    const auto b = result.after.present
                       ? read_binary_file(result.after.file_path(), o.max_input_bytes, o.control)
                       : std::vector<std::uint8_t>{};
    result.delta = make_binary_delta(a, b, o);
    auto verify = [](const GitRevision &revision, const std::string &hash) {
        if (revision.present && (revision.files.size() != 1 || revision.files.front().content_sha256 != hash))
            throw Error("git-binary-source", "Materialized binary bytes differ from revision provenance");
    };
    verify(result.before, result.delta.base_sha256);
    verify(result.after, result.delta.target_sha256);
    return result;
}
} // namespace cyuediff
