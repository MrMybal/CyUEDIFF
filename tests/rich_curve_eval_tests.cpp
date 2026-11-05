#include "rich_curve_model.hpp"
#include "rich_curve_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main(int argc, char **argv) {
    int checks = 0;
    const auto root =
        fs::current_path() / (".test-tmp-rich-eval-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            fs::create_directories(fs::u8path(argv[2]));
            for (int v : {0, 1004, 1013})
                for (int change = 0; change < 19; ++change)
                    save(fs::u8path(argv[2]), "curve-" + std::to_string(change) + "-" + std::to_string(v),
                         rich_curve_file_fixture(change, v));
            return 0;
        }
        fs::create_directories(root);
        auto check = [&](bool ok, const char *msg) {
            ++checks;
            if (!ok)
                throw std::runtime_error(msg);
        };
        auto p = rich_model();
        check(evaluate_curve(p, .5)["value"] == 5., "Linear source segment");
        check(evaluate_curve(p, 1.)["method"] == "stored-key" && evaluate_curve(p, 1.)["value"] == 10.,
              "Exact source key");
        check(!evaluate_curve(p, -.1)["ready"].get<bool>() && !evaluate_curve(p, 3.)["ready"].get<bool>(),
              "No inferred extrapolation");
        auto &a = p.exports[0].properties["FloatCurve"]["value"]["Keys"]["value"][0];
        a["interpMode"] = 1;
        check(evaluate_curve(p, .5)["value"] == 0., "Stored constant segment");
        a["interpMode"] = 2;
        a["leaveTangent"] = 4.;
        check(evaluate_curve(p, .5)["value"] == 5.5, "Unweighted cubic Hermite uses stored slopes");
        a["tangentWeightMode"] = 2;
        check(evaluate_curve(p, .5)["ready"] && evaluate_curve(p, 0.)["ready"],
              "Stored weighted segment and exact key usable");
        a["tangentWeightMode"] = 1;
        check(evaluate_curve(p, .5)["ready"], "Inactive arrive weight does not affect outgoing segment");
        a["interpMode"] = 255;
        check(!evaluate_curve(p, .5)["ready"].get<bool>(), "Unknown interpolation never guessed");
        bool no = false;
        try {
            evaluate_curve(p, std::numeric_limits<double>::infinity());
        } catch (const Error &e) {
            no = e.code == "curve-time";
        }
        check(no, "Non-finite time refused");
        auto missing_input = rich_model();
        missing_input.complete_input = false;
        check(!evaluate_curve(missing_input, .5)["ready"].get<bool>(),
              "Missing input never establishes evaluated value");
        auto overflow = rich_model();
        auto &over_keys = overflow.exports[0].properties["FloatCurve"]["value"]["Keys"]["value"];
        over_keys = Json::array({rich_key(-std::numeric_limits<double>::max(), 0.),
                                 rich_key(std::numeric_limits<double>::max(), 1.)});
        check(!evaluate_curve(overflow, 0.)["ready"].get<bool>(),
              "Time arithmetic overflow stays unavailable");
        CurveOptions cancelled;
        cancelled.control.cancelled = [] { return true; };
        no = false;
        try {
            evaluate_curve(rich_model(), .5, "", "", cancelled);
        } catch (const Error &e) {
            no = e.code == "cancelled";
        }
        check(no, "Evaluation cancellation");
        for (int v : {0, 1004, 1013}) {
            auto before = Package::open(save(root, "base", rich_curve_file_fixture(0, v)));
            auto after = Package::open(save(root, "after", rich_curve_file_fixture(1, v)));
            check(curve_snapshot(before)["complete"] && evaluate_curve(before, .5)["value"] == 5.,
                  "Encoded profile source evaluation");
            check(compare_curves(before, after).changes.size() == 1, "Encoded changed key");
            const auto cubic = Package::open(save(root, "cubic", rich_curve_file_fixture(8, v)));
            check(evaluate_curve(cubic, .5)["value"] == 4.625, "Encoded cubic slopes");
            const auto weighted = Package::open(save(root, "weighted", rich_curve_file_fixture(4, v)));
            check(evaluate_curve(weighted, .5)["ready"].get<bool>(), "Encoded weighted segment evaluated");
            for (int setting = 9; setting <= 13; ++setting) {
                const auto explicit_mode =
                    Package::open(save(root, "extrapolation", rich_curve_file_fixture(setting, v)));
                const double expected[] = {20., 25., 5., 25., 15.};
                check(evaluate_curve(explicit_mode, 2.5)["value"] == expected[setting - 9],
                      "Encoded explicit extrapolation mode");
            }
            const auto stored_default = Package::open(save(root, "default", rich_curve_file_fixture(14, v)));
            check(evaluate_curve(stored_default, 100.)["value"] == 7.,
                  "Encoded explicit empty-curve default");
        }
        fs::remove_all(root);
        std::cout << checks << " rich curve evaluation/file checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
