#include <cyuediff/cooked_animation_view.hpp>
#include "cooked_animation_fixture.hpp"
#include <chrono>
#include <iostream>
#include <thread>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
unsigned checks = 0, frames = 0;
void check(bool b, const char *s) {
    ++checks;
    if (!b)
        throw std::runtime_error(s);
}
template <class F> void refused(F f) {
    try {
        f();
    } catch (const Error &) {
        ++checks;
        return;
    }
    throw std::runtime_error("Invalid scalar state accepted");
}
void ready(CookedCurveView &v) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (v.busy() && std::chrono::steady_clock::now() < end) {
        v.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(!v.busy() && v.report()["error"] == "", "Latest scalar worker result ready");
}
std::shared_ptr<const CookedAnimation> load(const fs::path &r, const AnimationCurveFixture &f,
                                            bool bind = true) {
    save_animation_curves(r, f);
    auto z = animation_context(true);
    CookedAnimationOptions o;
    o.profile = CookedAnimationProfile::unreal_5_3_windows;
    if (bind)
        o.curve_settings = CookedAnimationDependency{Container::open(r / "curve-settings.utoc"), 0, z, {}};
    return std::make_shared<CookedAnimation>(
        read_zen_animation(Container::open(r / "animation.utoc"), 0, z, {}, o));
}
void draw(CookedCurveView &v, float width = 1100) {
    auto &io = ImGui::GetIO();
    io.DisplaySize = {width, 950};
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Scalar curves", nullptr, ImGuiWindowFlags_NoDecoration);
    v.draw();
    ImGui::End();
    ImGui::Render();
    ++frames;
    check(ImGui::GetDrawData()->TotalVtxCount > 0, "Scalar widget produces actual CyUI geometry");
}
} // namespace
int main() {
    const auto root =
        fs::temp_directory_path() /
        ("cyuediff-curve-ui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        auto f = cooked_animation_curve_fixture(0, true);
        auto a = load(root, f);
        auto b = load(root, cooked_animation_curve_fixture(1, true));
        auto unknown = load(root, f, false);
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DeltaTime = 1.f / 60;
        CookedAnimationView::register_translations();
        CookedCurveView view;
        view.set_animations(a, b, true);
        view.set_times(.25, .75, true);
        view.set_key(1);
        view.set_range(-.25, 1.25);
        ready(view);
        auto j = view.report();
        check(j["comparison"]["complete"] == true && j["comparison"]["encodingChanges"] == 1 &&
                  j["comparison"]["valueChanges"] == 1,
              "Native scalar encoding and independent values compared");
        check(j["before"]["scalarSample"]["value"] == 2 && j["after"]["scalarSample"]["value"] == 4,
              "Independent scalar seconds match expected values");
        check(j["before"]["selectedStoredKey"]["value"]["index"] == 1 &&
                  j["before"]["selectedStoredKey"]["value"]["value"] == 5,
              "Exact original scalar key selected");
        check(j["plotPoints"] == Json({257, 257}) && j["storedPoints"] == Json({2, 2}),
              "Bounded traces and original key markers");
        view.set_times(0, 0, true);
        ready(view);
        check(view.report()["curves"][0]["encodingStatus"] == "changed" &&
                  view.report()["curves"][0]["valueStatus"] == "identical",
              "Encoding change does not imply selected scalar change");
        view.set_animations(a, a, true);
        view.set_times(0, .5, true);
        view.set_tolerance(2);
        ready(view);
        check(view.report()["comparison"]["status"] == "identical",
              "Scalar tolerance applied without changing stored status");
        view.set_tolerance(0);
        ready(view);
        check(view.next_change(), "Known scalar changes navigable");
        ready(view);
        view.set_filter("does-not-exist");
        check(view.report()["visible"] == 0, "Scalar search filters list");
        view.set_filter("");
        view.set_changed_only(true);
        check(view.report()["visible"] == 1, "Changed curve filter keeps known differences");
        view.set_key(999);
        ready(view);
        check(view.report()["before"]["selectedStoredKey"]["ready"] == false,
              "Missing exact key is not clamped");
        const auto state = view.view_state();
        CookedCurveView restored;
        restored.set_animations(a, a, true);
        restored.restore_view_state(state);
        ready(restored);
        check(restored.view_state() == state, "Scalar presentation round trips");
        auto invalid = state;
        invalid["rangeEnd"] = invalid["rangeStart"];
        refused([&] { restored.restore_view_state(invalid); });
        check(restored.view_state() == state, "Rejected scalar restore is atomic");
        refused([&] { view.set_times(std::numeric_limits<double>::infinity(), 0, true); });
        refused([&] { view.set_range(2, 1); });
        refused([&] { view.set_tolerance(-1); });
        for (unsigned i = 0; i < 150; ++i)
            view.set_times(double(i % 10) / 10, double((i + 1) % 10) / 10, true);
        view.set_times(.125, .875, true);
        ready(view);
        check(view.report()["preparedTime"] == .125 && view.report()["preparedAfterTime"] == .875,
              "Superseded curve generations cannot publish as current");
        CookedCurveView missing;
        missing.set_animations(a, unknown, true);
        ready(missing);
        check(missing.report()["comparison"]["status"] == "incomplete" &&
                  missing.report()["curves"][0]["encodingStatus"] == "unavailable" &&
                  missing.report()["curves"][0]["valueStatus"] == "unavailable",
              "Unknown settings are not removals");
        CookedCurveView lost;
        lost.set_animations(a, nullptr, true);
        ready(lost);
        check(lost.report()["comparison"]["status"] == "incomplete" &&
                  lost.report()["curves"][0]["valueStatus"] == "unavailable",
              "Missing side is not a known removal");
        CookedCurveView single;
        single.set_animations(b);
        ready(single);
        check(single.report()["comparison"]["status"] == "inspection" &&
                  single.report()["before"]["scalarSample"]["value"] == 1,
              "Curve-only single asset needs no bone decoder");
        CookedCurveView cleared;
        cleared.set_animations(a, b, true);
        cleared.set_animations(nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        cleared.poll();
        check(!cleared.busy() && cleared.report()["current"] == false,
              "Clearing cancels pending scalar review");
        for (const char *theme : {"mid-grey", "light-grey", "amber-night", "violet", "navy"})
            for (const char *locale : {"en", "fr"}) {
                cyui::Config config;
                config.themeKey = theme;
                cyui::apply(config);
                cyui::loadFonts();
                unsigned char *p;
                int w, h;
                io.Fonts->GetTexDataAsRGBA32(&p, &w, &h);
                cyui::setLocale(locale);
                for (float width : {1100.f, 550.f}) {
                    draw(view, width);
                    draw(missing, width);
                    draw(single, width);
                }
            }
        check(cyui::tr("Scalar options") != "Scalar options", "French scalar labels registered");
        auto huge = load(root, cooked_animation_curve_fixture(1, true, 5000));
        CookedCurveView clipped;
        clipped.set_animations(huge);
        clipped.restore_view_state({{"selected", "name:Curve4999"}});
        ready(clipped);
        for (unsigned i = 0; i < 3; ++i)
            draw(clipped, 700);
        check(clipped.report()["renderedRows"].get<unsigned>() < 100 &&
                  clipped.report()["selectedVisible"] == true,
              "Large scalar list clipped and exact selected row visible");
        CookedCurveView gaps;
        gaps.set_animations(b);
        gaps.set_range(0, std::numeric_limits<float>::max());
        ready(gaps);
        check(gaps.report()["unavailablePlotPoints"][0] == 256,
              "Unsafe native scalar sample domains remain explicit plot gaps");
        auto excessive = load(root, cooked_animation_curve_fixture(1, true, 10001));
        CookedCurveView bounded;
        bounded.set_animations(excessive);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (bounded.busy() && std::chrono::steady_clock::now() < deadline) {
            bounded.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(!bounded.busy() && bounded.report()["current"] == false &&
                  bounded.report()["error"].get<std::string>().find("ui-curve-limit") != std::string::npos,
              "Excessive curve tables fail within explicit review budget");
        CookedAnimationView parent;
        parent.set_animations(a, b, true);
        parent.set_scalar_mode(true);
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (parent.busy() && std::chrono::steady_clock::now() < end) {
            parent.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(!parent.busy() && parent.report()["curves"]["current"] == true,
              "Parent includes scalar worker polling and busy state");
        check(parent.view_state()["scalarMode"] == true && parent.view_state().contains("curves"),
              "Parent preserves scalar mode and nested controls");
        ImGui::DestroyContext();
        const auto absolute = fs::weakly_canonical(root);
        check(absolute.parent_path() == fs::weakly_canonical(fs::temp_directory_path()),
              "Fixture cleanup remains in temp parent");
        fs::remove_all(absolute);
        std::cout << checks << " cooked scalar view checks; " << frames << " UI frames\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "check " << checks << ": " << e.what() << " fixtures " << root << '\n';
        return 1;
    }
}
