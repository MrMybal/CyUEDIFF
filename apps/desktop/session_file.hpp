#pragma once
#include "workspace_file.hpp"
namespace cyuediff::desktop {
struct SessionRead {
    WorkspaceFile value;
    bool loaded = false, writable = false;
    std::string diagnostic;
};
// Deterministic sibling of an explicit/default preferences file; no path comes from serialized preferences.
fs::path session_path_for_preferences(const fs::path &);
// Optional automatic sessions use the same bounded data-only contract as explicit workspaces.
// Missing files do not create directories. Malformed/future files remain read-only.
SessionRead read_session(const fs::path &);
void save_session(const fs::path &, const WorkspaceFile &);
} // namespace cyuediff::desktop
