#include "../apps/desktop/document.hpp"
#include "animation_file_fixture.hpp"
#include "attachments_file_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::desktop;
using namespace cyuediff::test;
int main() {
    const auto root =
        fs::current_path() / (".test-tmp-socket-document-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int checks = 0;
    try {
        fs::create_directories(root);
        auto check = [&](bool v, const char *why) {
            ++checks;
            if (!v)
                throw std::runtime_error(why);
        };
        auto a = save(root, "Clip", animation_model_file_fixture(0, 1013, "arm"));
        auto s = save(root, "Rig", attachments_file_fixture(attachments_model()));
        LoadRequest request;
        request.before = a.u8string();
        request.after = a.u8string();
        request.previews = false;
        request.codec = root / "not-loaded.dll";
        request.animation_pose = AnimationPoseRequest{s.u8string(), s.u8string(), {1}, {2}};
        auto d = load_document(request);
        auto report = d->report()["animationPose"]["socketOrigins"];
        check(!d->before_animation_pose->display_pose.complete && report["complete"],
              "Socket scope stays usable with unrelated missing tip source");
        check(report["before"]["sockets"]["Rig.Grip"]["position"] == Json({21., 22., 23.}) &&
                  report["comparison"]["changedSockets"] == 2,
              "Worker computes sockets before publication without previews/codec");
        auto missing = request;
        missing.animation_pose->after_skeleton = (root / "Missing.uasset").u8string();
        d = load_document(missing);
        report = d->report()["animationPose"]["socketOrigins"];
        check(d->before && d->after && report["before"]["complete"] && !report["complete"],
              "Binding failure preserves package and known socket side");
        check(report["comparison"]["sockets"]["Rig.Grip"]["kind"] == "unavailable",
              "Failed binding cannot invent socket removal");
        auto unknown = attachments_model();
        unknown.exports[0].properties.erase("Sockets");
        auto u = save(root, "Unknown", attachments_file_fixture(unknown));
        missing = request;
        missing.animation_pose->after_skeleton = u.u8string();
        d = load_document(missing);
        check(d->animation_socket_comparison["sockets"]["Rig.Grip"]["kind"] == "unavailable",
              "Unavailable membership remains distinct from empty");
        missing = request;
        missing.after.clear();
        d = load_document(missing);
        check(d->before_animation_sockets && !d->after_animation_sockets &&
                  d->animation_socket_comparison.is_null(),
              "Single local source does not invent opposite input");
        missing = request;
        missing.animation_pose.reset();
        check(!load_document(missing)->report()["animationPose"]["socketOrigins"]["requested"],
              "Default loads do not evaluate source sockets");
        bool stop = false, cancelled = false;
        request.control.cancelled = [&] { return stop; };
        request.control.progress = [&](const OperationProgress &p) {
            if (p.phase == "Composing source animation sockets")
                stop = true;
        };
        try {
            load_document(request);
        } catch (const Error &e) {
            cancelled = e.code == "cancelled";
        }
        check(cancelled, "Socket composition cancellation reaches document worker");
        fs::remove_all(root);
        std::cout << checks << " source socket document checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
