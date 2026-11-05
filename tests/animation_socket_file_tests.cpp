#include <cyuediff/animation_socket_pose.hpp>
#include "animation_file_fixture.hpp"
#include "attachments_file_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
Package rig_model(int variant) {
    auto p = attachments_model();
    if (variant == 1)
        p.exports[1].properties["RelativeLocation"]["value"][0] = 4.;
    if (variant == 2)
        p.exports[1].properties.erase("RelativeLocation");
    if (variant == 3)
        p.exports[0].properties.erase("Sockets");
    if (variant == 4)
        p.exports[0].properties["Sockets"]["value"] = Json::array();
    if (variant == 5)
        p.exports[1].properties["BoneName"]["value"] = "root";
    if (variant == 6)
        p.exports[1].properties["BoneName"]["value"] = "VB_tip";
    if (variant == 7)
        p.exports[0].properties["Sockets"]["value"] = {"Rig.Muzzle"};
    if (variant == 8) {
        p.exports[1].properties.erase("RelativeRotation");
        p.exports[1].properties.erase("RelativeScale");
    }
    if (variant == 9)
        p.exports[0].properties["Sockets"] = attachment_property("invalid", "NameProperty");
    return p;
}
int main(int argc, char **argv) {
    const auto root =
        fs::current_path() / (".test-tmp-animation-socket-files-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int checks = 0;
    try {
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            const auto out = fs::u8path(argv[2]);
            fs::create_directories(out);
            for (int version : {1004, 1013}) {
                for (int variant = 0; variant < 18; ++variant)
                    save(out, "socket-animation-" + std::to_string(variant) + "-" + std::to_string(version),
                         animation_model_file_fixture(variant, version, "arm"));
                for (int variant = 0; variant < 10; ++variant)
                    save(out, "socket-rig-" + std::to_string(variant) + "-" + std::to_string(version),
                         attachments_file_fixture(rig_model(variant), version));
            }
            return 0;
        }
        fs::create_directories(root);
        auto check = [&](bool v, const char *why) {
            ++checks;
            if (!v)
                throw std::runtime_error(why);
        };
        for (int version : {1004, 1013}) {
            const auto file = save(root, "Animation", animation_model_file_fixture(0, version, "arm"));
            const auto rig = save(root, "Rig", attachments_file_fixture(rig_model(0), version));
            const auto hash = sha256_file(rig);
            const auto a = Package::open(file), s = Package::open(rig);
            const auto source = animation_pose(a, s, 1);
            const auto pose = animation_socket_pose(source, s);
            const auto json = pose.to_json();
            check(!source.display_pose.complete && pose.display_pose.complete,
                  "Unanimated unrelated tip does not invalidate encoded arm sockets");
            check(json["sockets"]["Rig.Grip"]["position"] == Json({21., 22., 23.}),
                  "Encoded properties and source keys yield independent expected offset");
            auto after = animation_socket_pose(a, s, {2});
            auto first = animation_socket_pose(a, s, {0});
            check(compare_animation_socket_poses(first, after)["sockets"]["Rig.Grip"]["delta"] ==
                      Json({40., 40., 40.}),
                  "Encoded temporal source displacement");
            for (int variant = 1; variant < 10; ++variant) {
                auto p =
                    Package::open(save(root, "Other", attachments_file_fixture(rig_model(variant), version)));
                const auto other = animation_socket_pose(source, p);
                const auto diff = compare_animation_socket_poses(pose, other);
                if (variant == 1)
                    check(diff["sockets"]["Rig.Grip"]["distance"] == 3., "Encoded socket location change");
                if (variant == 2 || variant == 6)
                    check(!other.display_pose.joints[0].ready && other.display_pose.joints[1].ready,
                          "Encoded missing offset/parent remains per socket");
                if (variant == 3 || variant == 9)
                    check(!other.membership_complete && diff["sockets"]["Rig.Grip"]["kind"] == "unavailable",
                          "Omitted or malformed encoded membership remains unknown");
                if (variant == 4)
                    check(other.display_pose.complete && diff["changedSockets"] == 2,
                          "Encoded empty membership certifies removals");
                if (variant == 5)
                    check(diff["sockets"]["Rig.Grip"]["parentChanged"], "Encoded parent edit");
                if (variant == 7)
                    check(other.display_pose.joints.size() == 1 &&
                              other.display_pose.joints[0].name == "Rig.Muzzle",
                          "Unlisted contained socket excluded");
                if (variant == 8)
                    check(other.to_json()["sockets"] == json["sockets"],
                          "Encoded omitted unused socket orientation does not invent a default");
            }
            for (int variant : {7, 8, 12}) {
                const auto b = Package::open(
                    save(root, "Variant", animation_model_file_fixture(variant, version, "arm")));
                const auto result = animation_socket_pose(b, s, {1}).to_json();
                if (variant == 7)
                    check(std::abs(result["sockets"]["Rig.Grip"]["position"][0].get<double>() - 18.) < 1e-5,
                          "Encoded source rotation transforms offset");
                if (variant == 8)
                    check(result["sockets"]["Rig.Grip"]["position"] == Json({19., 22., 23.}),
                          "Encoded source signed scale transforms offset");
                if (variant == 12)
                    check(!result["complete"].get<bool>(),
                          "Encoded nonunit source quaternion remains unavailable");
            }
            check(sha256_file(rig) == hash, "Encoded caller Skeleton unchanged");
        }
        fs::remove_all(root);
        std::cout << checks << " encoded animation socket checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
