#pragma once
#include "git_process.hpp"
namespace cyuediff::detail {
GitHistory parse_history(const std::string &, const std::string &head, const GitHistoryOptions &,
                         const OperationControl & = {});
GitHistory read_history(const GitOptions &, const fs::path &, const std::string &head,
                        const GitHistoryOptions &, GitWorkspace &);
} // namespace cyuediff::detail
