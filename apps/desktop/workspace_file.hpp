#pragma once
#include "context.hpp"
#include <cyuediff/view_state.hpp>
namespace cyuediff::desktop {
// Explicit data-only file. Codecs, decoded assets, callbacks and closed history are not serialized.
struct SavedDocument {
    Settings settings;
    std::string view = "properties", cooked_view;
    int container_tab = -1;
    Json view_state = Json::object();
};
struct WorkspaceFile {
    std::vector<SavedDocument> documents{SavedDocument{}};
    std::size_t active = 0;
    Json to_json() const;
    static WorkspaceFile from_json(const Json &);
};
WorkspaceFile read_workspace(const fs::path &);
// Saves absolute local filesystem paths; Git paths and mounted package names keep their meaning.
// Existing unrelated/future/malformed files are never replaced. Atomic Windows replacement.
void save_workspace(const fs::path &, const WorkspaceFile &);
} // namespace cyuediff::desktop
