#include "session_file.hpp"
namespace cyuediff::desktop {
fs::path session_path_for_preferences(const fs::path &preferences) {
    if (preferences.empty())
        return {};
    auto result = preferences;
    result.replace_filename(preferences.stem().native() + fs::path(".session.json").native());
    return result;
}
SessionRead read_session(const fs::path &path) {
    SessionRead result;
    try {
        if (path.empty() || path.extension() != ".json")
            throw Error("desktop-session", "Session requires a .json path");
        if (!fs::exists(path)) {
            if (fs::is_symlink(fs::symlink_status(path)))
                throw Error("desktop-session", "Session cannot be a symbolic link");
            result.writable = true;
            return result;
        }
        result.value = read_workspace(path);
        result.loaded = result.writable = true;
    } catch (const std::exception &e) {
        result.diagnostic = e.what();
    }
    return result;
}
void save_session(const fs::path &path, const WorkspaceFile &value) {
    if (!read_session(path).writable)
        throw Error("desktop-session", "Existing session cannot be replaced");
    save_workspace(path, value);
}
} // namespace cyuediff::desktop
