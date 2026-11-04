#pragma once
#include <cyuediff/git.hpp>
namespace cyuediff::detail {
GitProjectContext build_git_project(std::string commit, std::string project,
                                    std::vector<GitProjectDocument> documents, const GitProjectOptions &,
                                    bool discovery_complete, std::vector<std::string> diagnostics);
std::vector<std::string> plugin_directories(const Json &, const std::string &project_path, bool &complete,
                                            std::vector<std::string> &diagnostics);
} // namespace cyuediff::detail
