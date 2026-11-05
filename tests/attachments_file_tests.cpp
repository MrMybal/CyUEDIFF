#include "attachments_file_fixture.hpp"
#include <cyuediff/attachments.hpp>
#include <cyuediff/skeleton_pose.hpp>
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main(int argc, char **argv) {
    const auto root =
        fs::current_path() / (".test-tmp-attachment-files-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int checks = 0;
    try {
        auto check = [&](bool v, const char *msg) {
            ++checks;
            if (!v)
                throw std::runtime_error(msg);
        };
        auto before = virtual_bones_model(), after = before, reordered = before, missing = before,
             cycle = before, empty = before;
        after.exports[1].properties["RelativeLocation"]["value"][0] = 4.;
        after.exports[0].properties["VirtualBones"]["value"][0]["TargetBoneName"]["value"] = "tip";
        std::swap(reordered.exports[1], reordered.exports[2]);
        std::swap(reordered.exports[0].properties["VirtualBones"]["value"][0],
                  reordered.exports[0].properties["VirtualBones"]["value"][1]);
        missing.exports[1].properties.erase("RelativeLocation");
        cycle.exports[0].properties["VirtualBones"]["value"][0]["SourceBoneName"]["value"] = "VB_tip";
        empty.exports[0].properties["Sockets"]["value"] = Json::array();
        empty.exports[0].properties["VirtualBones"]["value"] = Json::array();
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            const auto out = fs::u8path(argv[2]);
            fs::create_directories(out);
            for (int version : {1004, 1013})
                for (auto pair :
                     {std::make_pair("before", &before), std::make_pair("after", &after),
                      std::make_pair("reordered", &reordered), std::make_pair("missing", &missing),
                      std::make_pair("cycle", &cycle), std::make_pair("empty", &empty)})
                    save(out, std::string("attachments-") + pair.first + "-" + std::to_string(version),
                         attachments_file_fixture(*pair.second, version));
            return 0;
        }
        fs::create_directories(root);
        for (int version : {1004, 1013}) {
            const auto p = Package::open(save(root, "before", attachments_file_fixture(before, version)));
            const auto &e = p.exports[0];
            check(p.exports.size() == 3 && e.class_name == "/Script/Engine.Skeleton" &&
                      p.exports[1].class_name == "/Script/Engine.SkeletalMeshSocket",
                  "Three independent export/class entries");
            const auto a = attachment_snapshot(p);
            check(a["complete"] && a["sockets"]["Rig.Grip"]["outer"] == "Rig",
                  "Complete file-based socket/virtual projection");
            check(a["sockets"]["Rig.Grip"]["rotation"] == Json({0., 90., 0.}) &&
                      a["sockets"]["Rig.Grip"]["forceAlwaysAnimated"],
                  "Stored vector/rotator/bool property layouts");
            check(a["owners"]["Rig"]["socketReferences"][0]["path"] == "Rig.Grip",
                  "Serialized object indexes resolve to exact paths");
            auto b = Package::open(save(root, "after", attachments_file_fixture(after, version)));
            check(compare_attachments(p, b).changes.size() == 2,
                  "File-based named socket and virtual differences");
            auto r = Package::open(save(root, "reordered", attachments_file_fixture(reordered, version)));
            check(compare_attachments(p, r).status() == "identical",
                  "Export-index remapping and unique virtual reorder retain identity");
            auto c = Package::open(save(root, "cycle", attachments_file_fixture(cycle, version)));
            check(compare_attachments(c, c).status() == "incomplete", "Cyclic fixture remains incomplete");
            ReferencePoseOptions selected;
            selected.retarget_source = "A~/pose";
            check(reference_pose(p, "Rig", selected).to_json()["joints"]["arm"]["position"][0] == 15.,
                  "Selected retarget source survives independent native fixture");
        }
        fs::remove_all(root);
        std::cout << checks << " attachment file checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
