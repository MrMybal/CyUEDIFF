#include "rich_curve_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    int checks = 0;
    const auto root =
        fs::current_path() /
        (".test-tmp-rich-key-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        auto check = [&](bool ok, const char *msg) {
            ++checks;
            if (!ok)
                throw std::runtime_error(msg);
        };
        auto read = [&](int invalid = 0, int version = 1013, const ReadOptions &o = ReadOptions{}) {
            return Package::open(save(root, "key", rich_key_fixture(invalid, version)), o);
        };
        for (int profile : {0, 1004, 1013}) {
            const auto p = read(0, profile);
            const auto &v = p.exports[0].properties["Key"]["value"];
            check(v["kind"] == "rich-curve-key" && v["time"] == 1.25f && v["value"] == 4.5f,
                  "Stored finite time/value");
            check(v["interpMode"] == 2 && v["tangentMode"] == 1 && v["tangentWeightMode"] == 0 &&
                      v["arriveTangent"] == 2.f && v["leaveTangent"] == -1.f,
                  "Packed enums and six float components");
        }
        for (int invalid : {1, 2, 3})
            check(read(invalid).exports[0].properties["Key"].contains("opaque"),
                  "Bad size or non-finite key atomically opaque");
        for (int invalid : {4, 5})
            check(read(invalid).exports[0].properties["Key"]["value"].is_object(),
                  "Raw unknown enum bytes retained without engine repair");
        for (int version : {1005, 1012})
            check(read(0, version).exports[0].properties["Key"]["opaque"]["reason"] == "curve-layout",
                  "Unknown profile opaque");
        ReadOptions small;
        small.max_properties = 16;
        check(read(0, 1013, small).exports[0].properties["SecondKey"]["opaque"]["reason"] == "limit",
              "Aggregate key work bounded");
        bool stop = false;
        ReadOptions cancel;
        cancel.control.cancelled = [&] { return stop; };
        cancel.control.progress = [&](const OperationProgress &p) {
            if (p.phase == "Reading rich curve keys")
                stop = true;
        };
        bool refused = false;
        try {
            read(0, 1013, cancel);
        } catch (const Error &e) {
            refused = e.code == "cancelled";
        }
        check(refused, "Cancellation propagates");
        fs::remove_all(root);
        std::cout << checks << " rich curve native checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
