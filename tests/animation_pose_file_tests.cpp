#include <cyuediff/animation_pose.hpp>
#include "animation_file_fixture.hpp"
#include "animation_pose_file_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main(int argc, char **argv) {
    const auto temp =
        fs::current_path() / (".test-tmp-animation-pose-files-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int checks = 0;
    try {
        auto write = [](const fs::path &out) {
            fs::create_directories(out);
            for (int v : {1004, 1013})
                for (int n = 0; n < 18; ++n)
                    save(out, "animation-" + std::to_string(n) + "-" + std::to_string(v),
                         animation_model_file_fixture(n, v));
            for (int n = 0; n < 5; ++n)
                save(out, "skeleton-" + std::to_string(n), animation_skeleton_file_fixture(n));
        };
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            write(fs::u8path(argv[2]));
            return 0;
        }
        write(temp);
        auto check = [&](bool ok, const char *why) {
            ++checks;
            if (!ok)
                throw std::runtime_error(why);
        };
        const auto s = Package::open(temp / "skeleton-0.uasset");
        for (int v : {1004, 1013}) {
            auto a = Package::open(temp / ("animation-0-" + std::to_string(v) + ".uasset"));
            auto p = animation_pose(a, s, 1).to_json();
            check(p["complete"] && p["joints"]["arm~/track"]["position"] == Json({20., 20., 20.}),
                  "Independent binary source keys and Skeleton hierarchy");
            for (int n = 0; n < 18; ++n) {
                auto b = Package::open(
                    temp / ("animation-" + std::to_string(n) + "-" + std::to_string(v) + ".uasset"));
                auto pose = animation_pose(b, s, 1);
                check(pose.display_pose.complete == (n == 0 || n == 1 || n == 2 || n == 4 || n == 6 ||
                                                     n == 7 || n == 8 || n == 14 || n == 15 || n == 16 ||
                                                     n == 17),
                      "Variant availability");
            }
            auto r = Package::open(temp / "skeleton-4.uasset");
            check(animation_pose(a, r, 1).to_json()["joints"] == p["joints"],
                  "Stored rest transforms never alter source pose");
        }
        fs::remove_all(temp);
        std::cout << checks << " animation pose file checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(temp);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
