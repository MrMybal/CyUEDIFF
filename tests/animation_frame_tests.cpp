#include <limits>
#include "animation_source_model.hpp"
#include "animation_file_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main(int argc, char **argv) {
    const auto root =
        fs::current_path() / (".test-tmp-animation-frame-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int checks = 0;
    try {
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            const auto out = fs::u8path(argv[2]);
            fs::create_directories(out);
            for (int v : {1004, 1013})
                for (int change = 0; change < 18; ++change)
                    save(out, "animation-" + std::to_string(change) + "-" + std::to_string(v),
                         animation_model_file_fixture(change, v));
            return 0;
        }
        fs::create_directories(root);
        auto check = [&](bool ok, const char *msg) {
            ++checks;
            if (!ok)
                throw std::runtime_error(msg);
        };
        auto p = animation_source_model();
        const auto f = animation_frame(p, 1);
        check(f["complete"] && f["timeSeconds"] == 1. / 30., "Exact stored rational source time");
        check(f["tracks"]["name:root"]["positions"] == Json({10., 1., 0.}) &&
                  f["tracks"]["name:root"]["rotations"] == Json({0., 0., 0., 1.}),
              "Indexed and constant source channels");
        auto unknown = p;
        unknown.exports[0].properties["FrameRate"]["value"].erase("Denominator");
        check(animation_frame(unknown, 1)["complete"] && !animation_frame(unknown, 1).contains("timeSeconds"),
              "Frame inspection independent of omitted timing defaults");
        for (int bad = 0; bad < 3; ++bad) {
            auto x = p;
            auto &raw =
                x.exports[0].properties["BoneAnimationTracks"]["value"][0]["InternalTrackData"]["value"];
            if (bad == 0)
                raw["scales"] = Json::array();
            if (bad == 1)
                raw["positions"].erase(2);
            if (bad == 2)
                raw["rotations"][0][3] = 2.;
            const auto out = animation_frame(x, 1);
            check(!out["complete"].get<bool>() && !out["tracks"]["name:root"]["ready"].get<bool>(),
                  "Unknown or invalid local source transform remains unavailable");
        }
        for (auto frame : {3u, UINT32_MAX}) {
            bool no = false;
            try {
                animation_frame(p, frame);
            } catch (const Error &e) {
                no = e.code == "animation-frame";
            }
            check(no, "Out-of-range frame never clamped");
        }
        for (int v : {1004, 1013}) {
            const auto before = Package::open(save(root, "base", animation_model_file_fixture(0, v)));
            const auto after = Package::open(save(root, "after", animation_model_file_fixture(1, v)));
            const auto ordered = Package::open(save(root, "order", animation_model_file_fixture(2, v)));
            check(before.exports[0].class_name == "/Script/Engine.AnimDataModel",
                  "Actual class import and tagged arrays");
            check(animation_snapshot(before)["complete"], "Stored model fixture fully covered");
            check(compare_animation_models(before, after).changes.size() == 1, "One encoded key change");
            check(compare_animation_models(before, ordered).status() == "identical",
                  "Encoded track storage order irrelevant");
            check(animation_frame(before, 2)["tracks"]["name:root"]["positions"] == Json({20., 20., 20.}),
                  "Last encoded frame");
            for (int n = 6; n < 18; ++n) {
                const auto other =
                    Package::open(save(root, "frame-variant", animation_model_file_fixture(n, v)));
                const auto d = compare_animation_frames(before, other, 1, 1);
                const auto &pair = d["tracks"]["name:arm~/track"];
                if (n == 6)
                    check(d["status"] == "identical", "Encoded antipodal quaternion orientation");
                if (n == 7)
                    check(std::abs(pair["metrics"]["rotationDeltaDegrees"].get<double>() - 90.) < 1e-5,
                          "Encoded quarter-turn");
                if (n == 8)
                    check(pair["metrics"]["scaleMaxDelta"] == 2., "Encoded signed scale");
                if (n == 9)
                    check(d["changedTracks"] == 2, "Encoded known empty track table");
                if (n == 10 || n == 12 || n == 13)
                    check(d["status"] == "incomplete", "Encoded unavailable table, quaternion or identity");
                if (n == 11)
                    check(pair["status"] == "removed", "Encoded track removal");
                if (n == 14)
                    check(d["status"] == "identical" && d["beforeTimeSeconds"] != d["afterTimeSeconds"],
                          "Local value comparison independent of explicit timing");
                if (n == 15)
                    check(pair["metrics"]["positionDistance"] == .25,
                          "Encoded sub-unit translation difference");
                if (n == 16)
                    check(pair["metrics"]["rotationDeltaDegrees"] == 180., "Encoded half-turn");
                if (n == 17)
                    check(std::abs(compare_animation_frames(
                                       other, other, 0,
                                       1)["tracks"]["name:arm~/track"]["metrics"]["rotationDeltaDegrees"]
                                       .get<double>() -
                                   2.) < 1e-5,
                          "Encoded shortest wrapped orientation difference");
            }
        }
        fs::remove_all(root);
        std::cout << checks << " animation frame/file checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
