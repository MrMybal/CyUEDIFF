#include "animation_model.hpp"
#include <iostream>
#include <limits>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    int checks = 0;
    try {
        auto check = [&](bool ok, const char *msg) {
            ++checks;
            if (!ok)
                throw std::runtime_error(msg);
        };
        auto p = animation_model();
        const auto original = p.exports[0].properties;
        const auto a = animation_snapshot(p);
        check(a["complete"] && a["models"]["Clip.Model"]["tracks"].size() == 2, "Named tracks");
        auto reorder = p;
        std::swap(reorder.exports[0].properties["BoneAnimationTracks"]["value"][0],
                  reorder.exports[0].properties["BoneAnimationTracks"]["value"][1]);
        check(compare_animation_models(p, reorder).status() == "identical", "Track storage order irrelevant");
        auto changed = p;
        changed.exports[0]
            .properties["BoneAnimationTracks"]["value"][1]["InternalTrackData"]["value"]["positions"][1][0] =
            11.;
        auto diff = compare_animation_models(p, changed);
        check(diff.status() == "changed" && diff.changes.size() == 1 &&
                  diff.changes[0]["path"] == "/tracks/name:arm~0~1track/positions/1/0",
              "Typed escaped named key difference");
        auto renamed = p;
        renamed.exports[0].path = renamed.exports[0].name = "Other.Model";
        check(compare_animation_models(p, renamed).status() == "identical",
              "Selected model paths are comparison context");
        auto tagged = p;
        for (auto &t : tagged.exports[0].properties["BoneAnimationTracks"]["value"]) {
            auto &v = t["InternalTrackData"]["value"];
            Json raw = Json::object();
            for (auto pair : {std::make_pair("PosKeys", "positions"), std::make_pair("RotKeys", "rotations"),
                              std::make_pair("ScaleKeys", "scales")})
                raw[pair.first] = animation_tag("ArrayProperty", v[pair.second], "StructProperty");
            v = raw;
        }
        check(compare_animation_models(p, tagged).status() == "identical",
              "Legacy reflected and native bulk normalize equally");
        for (int bad = 0; bad < 5; ++bad) {
            auto x = p;
            auto &v = x.exports[0].properties;
            if (bad == 0)
                v.erase("BoneAnimationTracks");
            if (bad == 1)
                v["BoneAnimationTracks"]["value"][1]["Name"]["value"] = "root";
            if (bad == 2)
                v["BoneAnimationTracks"]["value"][0]["InternalTrackData"]["value"]["positions"][0] = {1, 2};
            if (bad == 3)
                v["FrameRate"]["value"].erase("Denominator");
            if (bad == 4)
                v["NumberOfKeys"]["value"] = -1;
            check(compare_animation_models(x, x).status() == "incomplete",
                  "Unknown/ambiguous data cannot establish equality");
        }
        auto empty = p;
        empty.exports[0].properties["BoneAnimationTracks"]["value"] = Json::array();
        check(animation_snapshot(empty)["models"]["Clip.Model"]["tracksCoverage"] == "stored",
              "Explicit empty track table");
        auto opaque = p;
        opaque.exports[0].properties["Other"] = {{"opaque", {{"reason", "unimplemented"}}}};
        check(compare_animation_models(p, opaque).status() == "identical",
              "Scope independent of unrelated properties");
        auto absent = p;
        absent.complete_input = false;
        check(compare_animation_models(absent, absent).status() == "incomplete",
              "Missing input remains incomplete");
        auto nonfinite = p;
        nonfinite.exports[0]
            .properties["BoneAnimationTracks"]["value"][0]["InternalTrackData"]["value"]["positions"][0][0] =
            std::numeric_limits<double>::infinity();
        bool refused = false;
        try {
            animation_snapshot(nonfinite);
        } catch (const Error &e) {
            refused = e.code == "animation-value";
        }
        check(refused, "Non-finite host values rejected");
        auto duplicate = p;
        duplicate.exports.push_back(duplicate.exports[0]);
        refused = false;
        try {
            animation_snapshot(duplicate);
        } catch (const Error &e) {
            refused = e.code == "animation-identity";
        }
        check(refused, "Duplicate model identity rejected");
        for (int limit = 0; limit < 6; ++limit) {
            AnimationOptions o;
            if (limit == 0)
                o.max_tracks = 1;
            if (limit == 1)
                o.max_keys = 1;
            if (limit == 2)
                o.max_nodes = 2;
            if (limit == 3)
                o.max_text_bytes = 2;
            if (limit == 4)
                o.max_depth = 0;
            if (limit == 5)
                o.max_models = 0;
            refused = false;
            try {
                animation_snapshot(p, o);
            } catch (const Error &e) {
                refused = e.code == "limit";
            }
            check(refused, "Projection resource bounds");
        }
        AnimationOptions o;
        o.control.cancelled = [] { return true; };
        refused = false;
        try {
            compare_animation_models(p, p, "", "", o);
        } catch (const Error &e) {
            refused = e.code == "cancelled";
        }
        check(refused, "Comparison cancellation");
        check(p.exports[0].properties == original, "Host input unchanged");
        std::cout << checks << " animation model checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
