#include "transform_curve_fixture.hpp"
#include <cyuediff/curves.hpp>
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main(int argc, char **argv) {
    int checks = 0;
    const auto root =
        fs::current_path() / (".test-tmp-transform-curves-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            auto path = fs::u8path(argv[2]);
            fs::create_directories(path);
            for (int profile : {0, 1004, 1013})
                for (int n = 0; n < 17; ++n)
                    save(path, "transform-" + std::to_string(n) + "-" + std::to_string(profile),
                         transform_curve_file_fixture(n, profile));
            return 0;
        }
        fs::create_directories(root);
        auto check = [&](bool ok, const char *why) {
            ++checks;
            if (!ok)
                throw std::runtime_error(why);
        };
        CurveOptions o;
        o.animation_family = AnimationCurveFamily::Transform;
        for (int profile : {0, 1004, 1013}) {
            auto a = Package::open(save(root, "base", transform_curve_file_fixture(0, profile)));
            check(curve_snapshot(a, o)["complete"] &&
                      curve_snapshot(a, o)["owners"]["TestAsset"]["lanes"].size() == 18,
                  "Encoded typed transform arrays and indexed FloatCurves");
            check(evaluate_curve_components(a, 1., "", "name:Root~/control", o)["values"]["rotation.y"] == 2.,
                  "Encoded component evaluation");
            for (int n = 1; n < 17; ++n) {
                auto b = Package::open(save(root, "case", transform_curve_file_fixture(n, profile)));
                auto diff = compare_curves(a, b, "", "", o);
                if (n == 1)
                    check(diff.status() == "changed" && diff.changes.size() == 1,
                          "Encoded changed stable key");
                if (n == 2)
                    check(diff.status() == "identical", "Encoded reordered groups");
                if (n == 3 || n == 4 || n == 5 || n == 7 || n == 8 || n == 16)
                    check(compare_curves(b, b, "", "", o).status() == "incomplete",
                          "Encoded missing fields and names");
                if (n == 6)
                    check(curve_snapshot(b, o)["complete"], "Encoded explicit empty table");
                if (n == 9)
                    check(evaluate_curve(b, 1., "", "name:Root~/control/translation.x", o)["ready"],
                          "Encoded weighted transform component");
                if (n == 10 || n == 14)
                    check(!evaluate_curve(b, 1., "", "name:Root~/control/translation.x", o)["ready"]
                               .get<bool>(),
                          "Encoded unknown or negative weights");
                if (n == 11)
                    check(evaluate_curve_components(b, 100., "", "name:Hand", o)["ready"],
                          "Encoded explicit defaults");
                if (n == 12 || n == 13)
                    check(!evaluate_curve_components(b, 1., "", "name:Hand", o)["ready"].get<bool>(),
                          "Encoded empty or sentinel defaults unavailable");
                if (n == 15)
                    check(evaluate_curve_components(b, -1., "", "name:Hand", o)["ready"],
                          "Encoded explicit extrapolation");
            }
        }
        fs::remove_all(root);
        std::cout << checks << " transform curve file checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
