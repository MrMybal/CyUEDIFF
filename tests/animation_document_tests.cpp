#include "../apps/desktop/document.hpp"
#include "animation_file_fixture.hpp"
#include "animation_pose_file_fixture.hpp"
#include <chrono>
#include <future>
#include <fstream>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::desktop;
using namespace cyuediff::test;
int main() {
    int checks = 0;
    const auto root =
        fs::current_path() / (".test-tmp-animation-document-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        auto check = [&](bool ok, const char *why) {
            ++checks;
            if (!ok)
                throw std::runtime_error(why);
        };
        const auto a = save(root, "Before", animation_model_file_fixture()),
                   b = save(root, "After", animation_model_file_fixture(1)),
                   s = save(root, "Rig", animation_skeleton_file_fixture());
        const auto hash = sha256_file(s);
        LoadRequest request;
        request.before = a.u8string();
        request.after = b.u8string();
        request.previews = false;
        request.codec = root / "not-loaded.dll";
        request.animation_pose = AnimationPoseRequest{s.u8string(), s.u8string(), {1}, {1}};
        request.cache = std::make_shared<PackageCache>();
        bool composed = false;
        request.control.progress = [&](const OperationProgress &p) {
            if (p.phase == "Composing source animation hierarchy")
                composed = true;
        };
        auto document = load_document(request);
        auto report = document->report().at("animationPose");
        check(composed && document->before_skeleton == document->after_skeleton,
              "Worker prepares both sides through shared package cache");
        check(report["complete"] && report["comparison"]["changedJoints"] == 1 &&
                  report["before"]["joints"]["arm~/track"]["position"] == Json({20., 20., 20.}),
              "Explicit independent source pose comparison");
        check(!document->request.control.progress && !document->request.cache,
              "Published context retains data only");
        check(document->protected_files.size() == 14 &&
                  std::find(document->protected_files.begin(), document->protected_files.end(), s) !=
                      document->protected_files.end(),
              "Both Skeleton sources and companions protected");
        check(request.cache->stats().hits >= 1, "Repeated Skeleton input reuses verified cache entry");
        auto other = request;
        other.animation_pose->after.frame = 2;
        document = load_document(other);
        check(document->after_animation_pose->frame == 2 &&
                  document->animation_pose_comparison["changedJoints"] == 2,
              "Independent integer frames");
        other = request;
        other.animation_pose->after_skeleton = (root / "Missing.uasset").u8string();
        document = load_document(other);
        report = document->report()["animationPose"];
        check(document->before && document->after && document->before_animation_pose->display_pose.complete &&
                  !document->after_animation_pose->display_pose.complete,
              "Missing external source retains package inspection and known side");
        check(report["status"] == "incomplete" &&
                  report["comparison"]["joints"]["arm~/track"]["kind"] == "unavailable",
              "Failed requested side cannot invent addition or removal");
        other = request;
        other.animation_pose->before.animation_object = "missing";
        document = load_document(other);
        check(!document->animation_pose_diagnostics.empty() &&
                  document->after_animation_pose->display_pose.complete,
              "Invalid object selector stays explicit per side");
        other = request;
        other.animation_pose->before.frame = 3;
        check(!load_document(other)->before_animation_pose->display_pose.complete,
              "Out of range frame is unavailable rather than clamped");
        other = request;
        other.animation_pose->before_skeleton.clear();
        check(!load_document(other)->animation_pose_diagnostics.empty(), "Empty Skeleton is not inferred");
        other = request;
        other.after.clear();
        document = load_document(other);
        check(document->before_animation_pose && !document->after_animation_pose &&
                  document->animation_pose_comparison.is_null(),
              "Single-source inspection ignores inactive after choices");
        other = request;
        other.animation_pose.reset();
        check(load_document(other)->report()["animationPose"]["status"] == "disabled",
              "Default loads do not evaluate source hierarchy");
        fs::create_directories(root / "One/Content");
        fs::create_directories(root / "Two/Content");
        for (const char *name : {"One", "Two"}) {
            std::ofstream f(root / name / "Test.uproject");
            f << "{\"FileVersion\":3}";
        }
        fs::copy_file(a, root / "One/Content/Clip.uasset");
        fs::copy_file(b, root / "Two/Content/Clip.uasset");
        fs::copy_file(s, root / "One/Content/Rig.uasset");
        fs::copy_file(s, root / "Two/Content/Rig.uasset");
        other = request;
        other.project = root / "One/Test.uproject";
        other.after_project = root / "Two/Test.uproject";
        other.before = other.after = "/Game/Clip";
        other.animation_pose->before_skeleton = other.animation_pose->after_skeleton = "/Game/Rig";
        document = load_document(other);
        check(document->before_skeleton->source != document->after_skeleton->source &&
                  document->animation_pose_comparison["changedJoints"] == 1,
              "Per-side mounted project contexts resolve Skeleton inputs");
        bool cancelled = false;
        other = request;
        bool cancel = false;
        other.control.progress = [&](const OperationProgress &p) {
            if (p.phase == "Opening explicit animation Skeleton")
                cancel = true;
        };
        other.control.cancelled = [&] { return cancel; };
        try {
            load_document(other);
        } catch (const Error &e) {
            cancelled = e.code == "cancelled";
        }
        check(cancelled, "Pose-stage cancellation propagates instead of becoming preview failure");
        DocumentSession session;
        session.request(request);
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (session.busy() && std::chrono::steady_clock::now() < until) {
            session.poll();
            std::this_thread::yield();
        }
        check(!session.busy() && session.document() &&
                  session.document()->before_animation_pose->display_pose.complete,
              "Actual document queue publishes prepared source pose");
        check(sha256_file(s) == hash, "External source bytes unchanged");
        fs::remove_all(root);
        std::cout << checks << " animation document checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
