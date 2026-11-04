#pragma once
#include <cyuediff/git.hpp>
namespace cyuediff::detail {
struct GitWorkspace {
    fs::path directory;
    explicit GitWorkspace(const fs::path &temporary_root);
    ~GitWorkspace();
    GitWorkspace(const GitWorkspace &) = delete;
    GitWorkspace &operator=(const GitWorkspace &) = delete;
};
// Direct process execution, binary output to disk, bounded diagnostics; never a shell.
int git_process(const GitOptions &, const fs::path &repository, const std::vector<std::string> &arguments,
                const fs::path &output, const fs::path &errors, std::uint64_t output_limit);
std::string git_text(const GitOptions &, const fs::path &repository,
                     const std::vector<std::string> &arguments, GitWorkspace &,
                     bool missing_config_ok = false);
} // namespace cyuediff::detail
