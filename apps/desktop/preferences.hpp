#pragma once
#include <cyuediff/core.hpp>

namespace cyuediff::desktop {
struct DesktopPreferences {
    std::string theme = "mid-grey", locale = "en";
    bool previews = true, include_engine = false;
    Json to_json() const;
    static DesktopPreferences from_json(const Json &value);
};
struct PreferencesRead {
    DesktopPreferences value;
    bool loaded = false, writable = false;
    std::string diagnostic;
};
// Missing files use defaults. Invalid/future files remain read-only and retain their bytes.
fs::path default_preferences_path();
PreferencesRead read_preferences(const fs::path &path);
void save_preferences(const fs::path &path, const DesktopPreferences &value);
} // namespace cyuediff::desktop
