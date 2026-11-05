#include "marker_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main(int argc, char **argv) {
    const auto root =
        fs::current_path() /
        (".test-tmp-markers-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int checks = 0;
    try {
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            const auto out = fs::u8path(argv[2]);
            fs::create_directories(out);
            save(out, "animation-before", marker_file_fixture());
            save(out, "animation-after", marker_file_fixture(1));
            save(out, "animation-reordered", marker_file_fixture(0, true));
            save(out, "animation-stripped", marker_file_fixture(0, false, 15, 1));
            save(out, "animation-invalid", marker_file_fixture(0, false, 15, 0, 0, 2));
            save(out, "animation-no-markers", marker_file_fixture(0, false, 1));
            save(out, "animation-curves-after", curve_file_fixture(1, 0, false, 37, 17, 1013, 1));
            return 0;
        }
        fs::create_directories(root);
        auto check = [&](bool v, const char *msg) {
            ++checks;
            if (!v)
                throw std::runtime_error(msg);
        };
        const auto f = marker_file_fixture();
        const auto p = Package::open(save(root, "base", f));
        const auto &e = p.exports[0];
        check(e.native_data["syncMarkers"]["coverage"] == "stored" &&
                  e.native_data["syncMarkers"]["names"] == Json({"Right", "Left", "Left"}),
              "Stored marker names retain order and multiplicity");
        check(e.opaque["suffix"]["bytes"] == 4, "Unknown trailing bytes retained beyond markers");
        for (int v : {0, 1})
            check(Package::open(save(root, "old", marker_file_fixture(0, false, v)))
                          .exports[0]
                          .native_data["syncMarkers"]["coverage"] == "not-stored",
                  "Pre-marker profile is explicit, no invented names");
        for (int v : {2, 15})
            check(Package::open(save(root, "known", marker_file_fixture(0, false, v)))
                          .exports[0]
                          .native_data["syncMarkers"]["coverage"] == "stored",
                  "Verified Anim boundary versions");
        for (int v : {-1, 16})
            check(Package::open(save(root, "future", marker_file_fixture(0, false, v)))
                          .exports[0]
                          .native_data["syncMarkers"]["coverage"] == "opaque",
                  "Missing/future Anim version not guessed");
        auto stripped = Package::open(save(root, "stripped", marker_file_fixture(0, false, 15, 1, 128)));
        check(stripped.exports[0].native_data["syncMarkers"]["coverage"] == "stripped" &&
                  !stripped.exports[0].native_data["syncMarkers"].contains("names") &&
                  stripped.exports[0].native_data["syncMarkers"]["stripFlags"]["class"] == 128,
              "Editor-stripped names absent and class flags retained");
        for (auto invalid : {marker_file_fixture(0, false, 15, 4), marker_file_fixture(0, false, 15, 0, 0, 1),
                             marker_file_fixture(0, false, 15, 0, 0, 2)}) {
            const auto bad = Package::open(save(root, "bad", invalid));
            check(bad.exports[0].native_data["syncMarkers"]["coverage"] == "opaque" &&
                      bad.exports[0].native_data["curveMetadata"]["coverage"] == "stored",
                  "Marker failure only rolls back marker stage");
        }
        ReadOptions off;
        off.native_skeleton_metadata = false;
        check(Package::open(save(root, "off", f), off).exports[0].native_data["syncMarkers"]["coverage"] ==
                  "disabled",
              "Metadata opt-out includes markers");
        auto curve = Package::open(save(root, "curve", curve_file_fixture()));
        const auto start = curve.exports[0].native_end;
        for (auto end = start; end < e.native_end; ++end) {
            auto cut = f;
            cut.script.data.resize(std::size_t(end));
            const auto size = std::int64_t(end);
            std::memcpy(cut.header.data.data() + cut.export_offset + 28, &size, 8);
            const auto bad = Package::open(save(root, "cut", cut));
            check(bad.exports[0].native_data["syncMarkers"]["coverage"] == "opaque" &&
                      bad.exports[0].native_data["curveMetadata"]["coverage"] == "stored",
                  "Every truncated marker byte preserves curves");
        }
        ReadOptions budget;
        budget.max_properties = 19;
        check(Package::open(save(root, "budget", f), budget)
                      .exports[0]
                      .native_data["syncMarkers"]["coverage"] == "opaque",
              "Marker count shares earlier-stage aggregate budget");
        bool stop = false;
        ReadOptions cancel;
        cancel.control.cancelled = [&] { return stop; };
        cancel.control.progress = [&](const OperationProgress &p) {
            if (p.phase == "Reading skeleton sync markers")
                stop = true;
        };
        bool stopped = false;
        try {
            Package::open(save(root, "cancel", f), cancel);
        } catch (const Error &error) {
            stopped = error.code == "cancelled";
        }
        check(stopped, "Marker cancellation propagates");
        fs::remove_all(root);
        std::cout << checks << " native marker checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        fs::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
