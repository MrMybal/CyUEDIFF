#include "../apps/desktop/preferences.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::desktop;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
void write(const fs::path &path, const std::string &text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
    if (!file)
        throw std::runtime_error("fixture write");
}
std::string read(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
} // namespace
int main() {
    auto root =
        fs::current_path() / (".test-tmp-preferences-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        auto path = root / fs::u8path("préférences.json");
        auto missing = read_preferences(path);
        check(!missing.loaded && missing.writable && missing.diagnostic.empty() && !fs::exists(path),
              "Missing preferences do not create a file");
        check(missing.value.theme == "mid-grey" && missing.value.locale == "en" && missing.value.previews &&
                  !missing.value.include_engine,
              "English defaults");
        DesktopPreferences value{"violet", "fr", false, true};
        save_preferences(path, value);
        auto loaded = read_preferences(path);
        check(loaded.loaded && loaded.writable && loaded.value.to_json() == value.to_json(),
              "Unicode path round trip");
        value.theme = "navy";
        value.locale = "en";
        save_preferences(path, value);
        check(read_preferences(path).value.to_json() == value.to_json(), "Atomic replacement");
        for (const auto &theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"}) {
            value.theme = theme;
            save_preferences(path, value);
            check(read_preferences(path).value.theme == theme, "Every built-in theme persists");
        }
        const auto valid = value.to_json();
        auto invalid = [&](const std::string &text) {
            write(path, text);
            const auto failed = read_preferences(path);
            check(!failed.loaded && !failed.writable && !failed.diagnostic.empty() &&
                      failed.value.locale == "en",
                  "Invalid preferences use defaults with a diagnostic");
            bool rejected = false;
            try {
                save_preferences(path, value);
            } catch (const Error &e) {
                rejected = e.code == "desktop-preferences";
            }
            check(rejected && read(path) == text, "Invalid or unrelated file is preserved");
        };
        invalid("{");
        invalid("");
        invalid("[]");
        invalid("null");
        auto malformed = valid;
        malformed["schema"] = "cyuediff.preferences/2";
        invalid(malformed.dump());
        malformed = valid;
        malformed["theme"] = "missing";
        invalid(malformed.dump());
        malformed = valid;
        malformed["locale"] = "de";
        invalid(malformed.dump());
        malformed = valid;
        malformed["previews"] = 1;
        invalid(malformed.dump());
        malformed = valid;
        malformed.erase("includeEngine");
        invalid(malformed.dump());
        malformed = valid;
        malformed["extra"] = true;
        invalid(malformed.dump());
        invalid("{\"theme\":\"navy\"," + valid.dump().substr(1));
        invalid(std::string(65537, ' '));
        invalid(std::string(18, '[') + "0" + std::string(18, ']'));
        auto wrong = root / "unrelated.txt";
        write(wrong, valid.dump());
        check(!read_preferences(wrong).writable, "Unexpected extension is read-only");
        auto directory = root / "directory.json";
        fs::create_directory(directory);
        check(!read_preferences(directory).writable, "Directory cannot be overwritten");
        // Invalid values must not create parent folders or replace an existing valid file.
        write(path, valid.dump());
        auto wrong_value = value;
        wrong_value.locale = "de";
        bool rejected = false;
        try {
            save_preferences(root / "absent" / "prefs.json", wrong_value);
        } catch (const Error &) {
            rejected = true;
        }
        check(rejected && !fs::exists(root / "absent") && read(path) == valid.dump(),
              "Invalid output is rejected before filesystem changes");
        bool clean = true;
        for (const auto &entry : fs::directory_iterator(root))
            if (entry.path().filename().u8string().find(".tmp-") != std::string::npos)
                clean = false;
        check(clean, "No abandoned atomic temporary files");
        fs::remove_all(root);
        std::cout << checks << " preferences checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
}
