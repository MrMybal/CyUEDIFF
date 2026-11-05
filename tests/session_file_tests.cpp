#include "../apps/desktop/session_file.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::desktop;
int main() {
    int checks = 0;
    const auto root =
        fs::current_path() /
        (".test-tmp-session-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto check = [&](bool good) {
        ++checks;
        if (!good)
            throw std::runtime_error("Session file check failed");
    };
    auto write = [&](const fs::path &p, const std::string &s) {
        std::ofstream f(p, std::ios::binary);
        f << s;
    };
    auto bytes = [&](const fs::path &p) {
        std::ifstream f(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), {});
    };
    try {
        const auto path = root / fs::u8path("préférences.session.json");
        check(session_path_for_preferences({}).empty());
        check(session_path_for_preferences(root / fs::u8path("préférences.json")) == path);
        auto missing = read_session(path);
        check(!missing.loaded && missing.writable && missing.diagnostic.empty() && !fs::exists(root));
        WorkspaceFile value;
        value.documents.front().settings.before = "before.uasset";
        value.documents.front().settings.codec = "not-serialized.dll";
        value.documents.front().view = "animations";
        value.documents.front().settings.pose_enabled = true;
        value.documents.front().settings.pose_before_skeleton = "Rig.uasset";
        SavedDocument git;
        git.settings.source = 1;
        git.settings.repository = "repo";
        git.settings.before = "Content/A.uasset";
        git.settings.before_revision = "pinned-before";
        git.settings.after_revision = "pinned-after";
        SavedDocument container;
        container.settings.source = 2;
        container.settings.container_path = "source.utoc";
        container.settings.container_schema = "schema.json";
        container.settings.cooked_profile = 1;
        value.documents.push_back(git);
        value.documents.push_back(container);
        value.active = 2;
        save_session(path, value);
        const auto loaded = read_session(path);
        check(loaded.loaded && loaded.writable && loaded.value.documents.size() == 3 &&
              loaded.value.active == 2);
        check(loaded.value.documents[0].settings.before == (fs::current_path() / "before.uasset").u8string());
        check(loaded.value.documents[0].settings.pose_before_skeleton ==
              (fs::current_path() / "Rig.uasset").u8string());
        check(loaded.value.documents[0].settings.codec.empty() &&
              bytes(path).find("not-serialized") == std::string::npos);
        check(loaded.value.documents[1].settings.before == "Content/A.uasset" &&
              loaded.value.documents[1].settings.after_revision == "pinned-after");
        check(loaded.value.documents[2].settings.container_schema ==
              (fs::current_path() / "schema.json").u8string());
        auto updated = loaded.value;
        updated.active = 0;
        save_session(path, updated);
        check(read_session(path).value.active == 0);
        const auto valid = bytes(path);
        for (const auto &text : {std::string("{"), std::string("[]"), std::string("{\"schema\":\"future\"}"),
                                 std::string(1024 * 1024 + 1, ' ')}) {
            write(path, text);
            const auto bad = read_session(path);
            check(!bad.loaded && !bad.writable && !bad.diagnostic.empty());
            bool rejected = false;
            try {
                save_session(path, updated);
            } catch (const Error &) {
                rejected = true;
            }
            check(rejected && bytes(path) == text);
        }
        write(path, valid);
        auto invalid = value;
        invalid.documents.resize(9);
        bool rejected = false;
        try {
            save_session(path, invalid);
        } catch (const Error &) {
            rejected = true;
        }
        check(rejected && bytes(path) == valid);
        check(!read_session(root / "wrong.txt").writable);
        fs::create_directory(root / "directory.json");
        check(!read_session(root / "directory.json").writable);
        for (const auto &entry : fs::directory_iterator(root))
            check(entry.path().filename().u8string().find(".tmp-") == std::string::npos);
        fs::remove_all(root);
        std::cout << checks << " session file checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        std::error_code ec;
        fs::remove_all(root, ec);
        return 1;
    }
}
