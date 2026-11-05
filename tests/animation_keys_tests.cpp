#include <limits>
#include "animation_file_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    const auto root =
        fs::current_path() / (".test-tmp-animation-keys-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int checks = 0;
    try {
        fs::create_directories(root);
        auto check = [&](bool ok, const char *message) {
            ++checks;
            if (!ok)
                throw std::runtime_error(message);
        };
        auto read = [&](int invalid = 0, int version = 1013, int release = 39,
                        const ReadOptions &options = ReadOptions{}) {
            return Package::open(save(root, "track", animation_key_fixture(invalid, version, release)),
                                 options);
        };
        for (int profile : {1004, 1013})
            for (int release : {39, 51}) {
                auto p = read(0, profile, release);
                const auto &v = p.exports[0].properties.at("Track").at("value");
                check(v["kind"] == "raw-animation-keys" && v["positions"][1] == Json({10.f, 11.f, 12.f}),
                      "Float position keys");
                check(v["rotations"][0] == Json({0.f, 0.f, 0.f, 1.f}) &&
                          v["scales"][0] == Json({1.f, 1.f, 1.f}),
                      "Quaternion and scale keys");
                check(p.exports[0].properties.contains("Value"), "Neighboring properties retained");
            }
        for (int invalid : {1, 2, 3, 4, 5})
            check(read(invalid).exports[0].properties["Track"].contains("opaque"),
                  "Malformed key arrays are atomic opaque values");
        for (int version : {1005, 1012})
            check(read(0, version).exports[0].properties["Track"]["opaque"]["reason"] == "animation-layout",
                  "Unknown package profile");
        check(read(0, 1013, 52).exports[0].properties["Track"].contains("opaque"), "Unknown custom version");
        for (int release : {-1, 38})
            check(read(0, 1013, release).exports[0].properties["Track"].contains("opaque"),
                  "Legacy branch never guessed as bulk keys");
        const auto quaternion = fixture(
            1004, 1, false, 0, {"Rotation", "Quat4f"},
            [](Bytes &b, const NameWriter &name) {
                name(b, "Rotation");
                name(b, "StructProperty");
                b.i(16);
                b.i(0);
                name(b, "Quat4f");
                b.zeros(16);
                b.put<std::uint8_t>(0);
                for (float value : {0.f, 0.f, 0.f, 1.f})
                    b.put<float>(value);
            },
            -1, 38);
        check(Package::open(save(root, "legacy-quaternion", quaternion))
                      .exports[0]
                      .properties["Rotation"]["value"] == Json({0., 0., 0., 1.}),
              "Legacy reflected quaternion remains float32 in LWC packages");
        ReadOptions small;
        small.max_properties = 15;
        check(read(0, 1013, 39, small).exports[0].properties["Track"]["opaque"]["reason"] == "limit",
              "Aggregate scalar work bounded");
        bool stop = false;
        ReadOptions cancel;
        cancel.control.cancelled = [&] { return stop; };
        cancel.control.progress = [&](const OperationProgress &p) {
            if (p.phase == "Reading raw animation keys")
                stop = true;
        };
        bool stopped = false;
        try {
            read(0, 1013, 39, cancel);
        } catch (const Error &e) {
            stopped = e.code == "cancelled";
        }
        check(stopped, "Cancellation propagates through property rollback");
        fs::remove_all(root);
        std::cout << checks << " raw animation key checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
