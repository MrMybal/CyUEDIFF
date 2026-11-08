#include <cyuediff/cooked_animation_view.hpp>
#include "cooked_animation_fixture.hpp"
#include <chrono>
#include <iostream>
#include <thread>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0, frames = 0;
void check(bool b, const char *s) {
    ++checks;
    if (!b)
        throw std::runtime_error(s);
}
void ready(CookedAnimationView &v) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (v.busy() && std::chrono::steady_clock::now() < end) {
        v.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(!v.busy() && v.report()["error"] == "", "Latest asynchronous review is ready");
}
std::shared_ptr<const CookedAnimation> load(const fs::path &r, const AnimationFixture &f, bool bind = true) {
    save_animation(r, f);
    auto z = animation_context();
    CookedAnimationOptions o;
    o.profile = CookedAnimationProfile::unreal_5_3_windows;
    o.bone_settings = CookedAnimationDependency{Container::open(r / "settings.utoc"), 0, z, {}};
    if (bind)
        o.skeleton = CookedAnimationDependency{Container::open(r / "skeleton.utoc"), 0, z, {}};
    return std::make_shared<CookedAnimation>(
        read_zen_animation(Container::open(r / "animation.utoc"), 0, z, {}, o));
}
AnimationFixture large(unsigned count) {
    auto f = cooked_animation_fixture();
    Bytes body;
    body.put<std::uint16_t>(0x0700);
    body.i(-1);
    body.put<std::uint8_t>(0);
    body.i(-2);
    body.i(0);
    for (unsigned i = 0; i < 16; ++i)
        body.put<std::uint8_t>(std::uint8_t(i + 1));
    body.put<std::uint8_t>(1);
    body.put<std::uint8_t>(0);
    body.i(1);
    body.i(100);
    body.i(int(count));
    for (unsigned i = 0; i < count; ++i)
        body.i(int(i));
    body.i(0);
    body.i(int(count * 8 + 12));
    body.i(0);
    for (unsigned i = 0; i < count; ++i) {
        body.i(0);
        body.i(-1);
    }
    body.put<std::uint32_t>(0x11000002);
    body.put<float>(2);
    body.put<float>(10);
    body.string("Instance_7");
    body.string("/Game/CurveSettings.Settings:Codec");
    body.i(0);
    body.i(5);
    for (unsigned v : {2u, 6u, 6u, 6u})
        body.put<std::uint8_t>(std::uint8_t(v));
    body.i(12);
    body.i(int(count * 2));
    body.i(0);
    body.i(2);
    body.i(1);
    f.animation = animation_package({"None", "/Game/Animation", "Animation"}, {{2, 1, 3, ~0ull, 1, body}},
                                    {0x8000000000000000ull, 0x8000000100000001ull}, {0xabc, 0xdef},
                                    {"/Game/Settings", "/Game/Skeleton"});
    std::vector<std::string> names = {"None", "/Game/Skeleton", "Skeleton"};
    for (unsigned i = 0; i < count; ++i)
        names.push_back("Bone" + std::to_string(i));
    Bytes sk;
    sk.put<std::uint16_t>(0x0100);
    sk.i(0);
    sk.i(int(count));
    for (unsigned i = 0; i < count; ++i) {
        sk.i(int(i + 3));
        sk.i(0);
        sk.i(i ? 0 : -1);
    }
    sk.i(int(count));
    for (unsigned i = 0; i < count; ++i)
        for (double v : {0., 0., 0., 1., 0., 0., 0., 1., 1., 1.})
            sk.put(v);
    sk.i(int(count));
    for (unsigned i = 0; i < count; ++i) {
        sk.i(int(i + 3));
        sk.i(0);
        sk.i(int(i));
    }
    sk.i(0);
    for (unsigned i = 0; i < 16; ++i)
        sk.put<std::uint8_t>(std::uint8_t(i + 1));
    f.skeleton = animation_package(names, {{2, 4, 3, ~0ull, 0xdef, sk}});
    return f;
}
void draw(CookedAnimationView &v, float width = 1100, float height = 1000) {
    auto &io = ImGui::GetIO();
    io.DisplaySize = {width, height};
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Cooked animation", nullptr, ImGuiWindowFlags_NoDecoration);
    v.draw();
    ImGui::End();
    ImGui::Render();
    ++frames;
    check(ImGui::GetDrawData()->TotalVtxCount > 0, "Cooked view produces actual UI geometry");
}
} // namespace
int main() {
    const auto r = fs::weakly_canonical(fs::temp_directory_path()) /
                   ("cyuediff-animation-ui-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(r);
        auto f = cooked_animation_fixture();
        auto a = load(r, f);
        float value = 14;
        std::memcpy(f.animation.storage.package.data.data() + f.buffer + 16, &value, 4);
        auto b = load(r, f);
        auto numeric = load(r, f, false);
        auto absent_fixture = cooked_animation_fixture(true, true);
        absent_fixture.animation.storage.bulk.data.clear();
        auto absent = load(r, absent_fixture);
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DeltaTime = 1.f / 60;
        CookedAnimationView::register_translations();
        CookedAnimationView view;
        view.set_animations(a, b, true);
        ready(view);
        check(view.select_track("name:Child"), "Named track selectable");
        view.set_frames(2.5, 2.5, true);
        ready(view);
        auto report = view.report();
        check(report["current"] == true && report["metrics"]["positionDistance"] == 2.5,
              "Fractional frames match Core local metrics");
        check(report["tracks"][0]["storedStatus"] == "changed" &&
                  report["tracks"][0]["frameStatus"] == "changed",
              "Stored and frame statuses are separate");
        check(report["plotPoints"] == Json({257, 257}) && report["storedPoints"] == Json({2, 2}),
              "Bounded sampled traces and actual key markers");
        view.set_frames(0, 0, false);
        ready(view);
        check(view.report()["metrics"]["changed"] == false &&
                  view.report()["tracks"][0]["storedStatus"] == "changed",
              "Equal selected frames do not hide changed stored encoding");
        view.set_changed_only(true);
        check(view.report()["visible"] == 1 && view.next_change(), "Confirmed changed encoding navigation");
        ready(view);
        view.set_frames(2.5, 2.5, true);
        view.set_key(1);
        view.set_key(1, true);
        ready(view);
        check(view.report()["before"]["selectedStoredKey"]["value"]["value"][0] == 10,
              "Focused original key inspection");
        view.set_key(20);
        ready(view);
        check(view.report()["before"]["selectedStoredKey"]["ready"] == false,
              "Out-of-range stored key is not clamped");
        auto state = view.view_state();
        state["properties"]["filter"] = "positions";
        state["properties"]["selected"] = "/currentFrame/positions/0";
        view.restore_view_state(state);
        ready(view);
        check(view.view_state() == state, "Fractional frames, keys, filter and focused property retained");
        CookedAnimationView restored;
        restored.set_animations(a, b, true);
        restored.restore_view_state(state);
        ready(restored);
        check(restored.view_state() == state, "State restores after asynchronous preparation");
        auto invalid = state;
        invalid["frame"] = -1;
        bool refused = false;
        try {
            restored.restore_view_state(invalid);
        } catch (const Error &) {
            refused = true;
        }
        check(refused && restored.view_state() == state, "Invalid restore is atomic");
        for (unsigned i = 0; i < 100; ++i)
            view.set_frames(double(i % 9) * .5, double((i + 1) % 9) * .5, true);
        view.set_frames(1.25, 3.5, true);
        ready(view);
        check(view.report()["preparedFrame"] == 1.25 && view.report()["preparedAfterFrame"] == 3.5,
              "Superseded worker generations never publish stale frames");
        view.set_frames(99, 0, true);
        ready(view);
        check(view.report()["tracks"][0]["frameStatus"] == "unavailable" &&
                  view.report()["plotPoints"][0] == 257,
              "Unknown frame does not erase valid channel overview");
        CookedAnimationView unknown;
        unknown.set_animations(a, numeric, true);
        ready(unknown);
        check(unknown.report()["tracks"][0]["frameKnown"] == false &&
                  unknown.report()["comparison"]["changedFrames"] == 0,
              "Unknown identities are not deletions");
        CookedAnimationView missing;
        missing.set_animations(a, absent, true);
        ready(missing);
        check(missing.report()["comparison"]["status"] == "incomplete" &&
                  missing.report()["comparison"]["changedFrames"] == 0,
              "Missing payload is not a confirmed removal");
        CookedAnimationView single;
        single.set_animations(a);
        ready(single);
        check(single.report()["before"]["currentFrame"]["positions"][0] == 2,
              "Single animation is inspectable");
        CookedAnimationView cleared;
        cleared.set_animations(a, b, true);
        cleared.set_animations(nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        cleared.poll();
        check(!cleared.busy() && cleared.report()["current"] == false, "Unbinding cancels old publications");
        view.set_frames(2.5, 2.5, true);
        view.set_key(0);
        view.set_filter("");
        view.set_changed_only(false);
        ready(view);
        for (const char *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"})
            for (const char *locale : {"en", "fr"}) {
                cyui::Config config;
                config.themeKey = theme;
                cyui::apply(config);
                cyui::loadFonts();
                unsigned char *pixels;
                int w, h;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
                cyui::setLocale(locale);
                for (float width : {1100.f, 550.f}) {
                    draw(view, width);
                    draw(unknown, width);
                    draw(missing, width);
                    draw(single, width);
                }
            }
        check(cyui::tr("Sampled compressed channel") != "Sampled compressed channel",
              "French native view labels are registered");
        auto huge = load(r, large(5000));
        CookedAnimationView clipped;
        clipped.set_animations(huge);
        ready(clipped);
        clipped.restore_view_state({{"selected", "name:Bone4999"}});
        check(clipped.view_state()["selected"] == "name:Bone4999",
              "Large track table restores final selection");
        ready(clipped);
        for (unsigned i = 0; i < 3; ++i)
            draw(clipped, 800, 650);
        check(clipped.report()["renderedRows"].get<unsigned>() < 100 &&
                  clipped.report()["selectedVisible"] == true,
              "Large track list is clipped and selection scrolls into view");
        ImGui::DestroyContext();
        const auto absolute = fs::weakly_canonical(r);
        check(absolute.parent_path() == fs::weakly_canonical(fs::temp_directory_path()),
              "Cleanup stays inside fixture parent");
        fs::remove_all(absolute);
        std::cout << checks << " cooked animation view checks; " << frames << " UI frames\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "check " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
