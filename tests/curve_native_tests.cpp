#include "curve_fixture.hpp"
#include <cyuediff/cache.hpp>
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main(int argc, char **argv) {
    const auto root =
        fs::current_path() /
        (".test-tmp-curves-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int checks = 0;
    try {
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            auto out = fs::u8path(argv[2]);
            fs::create_directories(out);
            save(out, "curves-before", curve_file_fixture());
            save(out, "curves-after", curve_file_fixture(1));
            save(out, "curves-reordered", curve_file_fixture(0, 0, true));
            save(out, "curves-invalid", curve_file_fixture(0, 3));
            return 0;
        }
        fs::create_directories(root);
        auto check = [&](bool v, const char *msg) {
            ++checks;
            if (!v)
                throw std::runtime_error(msg);
        };
        const auto file = save(root, "base", curve_file_fixture());
        const auto p = Package::open(file);
        const auto &e = p.exports[0];
        const auto &table = e.native_data.at("curveMetadata");
        check(table["coverage"] == "stored" && table["containers"].size() == 2, "Modern curve containers");
        const auto &c = table["containers"]["AnimCurveMappingName"]["Smile~/curve"];
        check(c["material"] == true && c["morphTarget"] == false && c["maxLOD"] == 2,
              "Stored type flags and byte LOD");
        check(c["linkedBones"] == Json({"arm", "None"}),
              "Bone links are exact stored names, not resolved defaults");
        check(e.opaque["suffix"]["bytes"] == 4, "Tail boundary follows all checked curve containers");
        check(Package::open(save(root, "reorder", curve_file_fixture(0, 0, true))).exports[0].native_data ==
                  e.native_data,
              "Container and curve map ordering irrelevant");
        for (int v : {1004, 1013})
            check(Package::open(save(root, "profile", curve_file_fixture(0, 0, false, 37, 17, v)))
                          .exports[0]
                          .native_data["curveMetadata"]["coverage"] == "stored",
                  "Explicit file profiles");
        for (int v : {10, 11})
            check(!Package::open(save(root, "lod", curve_file_fixture(0, 0, false, 37, v)))
                       .exports[0]
                       .native_data["curveMetadata"]["containers"]["AnimCurveMappingName"]["Weight"]
                       .contains("maxLOD"),
                  "Unstored LOD remains absent");
        for (int v : {-1, 14, 38})
            check(Package::open(save(root, "version", curve_file_fixture(0, 0, false, v)))
                          .exports[0]
                          .native_data["curveMetadata"]["coverage"] == "opaque",
                  "Unknown Framework branch is opaque");
        for (int v : {9, 18})
            check(Package::open(save(root, "version", curve_file_fixture(0, 0, false, 37, v)))
                          .exports[0]
                          .native_data["curveMetadata"]["coverage"] == "opaque",
                  "Unknown AnimPhys branch is opaque");
        for (int invalid : {1, 2, 3, 4}) {
            const auto bad = Package::open(save(root, "bad", curve_file_fixture(0, invalid)));
            check(bad.exports[0].native_data["curveMetadata"]["coverage"] == "opaque" &&
                      bad.exports[0].native_data["retargeting"]["coverage"] == "stored" &&
                      bad.exports[0].native_data["bones"].size() == 3,
                  "Malformed curve suffix preserves earlier stages atomically");
        }
        ReadOptions off;
        off.native_skeleton_metadata = false;
        PackageCache cache;
        const auto base = cache.open(file, off);
        check(base->exports[0].native_data["curveMetadata"]["coverage"] == "disabled" &&
                  cache.open(file)->exports[0].native_end == e.native_end,
              "Metadata opt-out has separate cache profile");
        auto f = curve_file_fixture();
        for (std::size_t end = std::size_t(base->exports[0].native_end); end < e.native_end; ++end) {
            auto cut = f;
            cut.script.data.resize(end);
            const auto size = std::int64_t(end);
            std::memcpy(cut.header.data.data() + cut.export_offset + 28, &size, 8);
            const auto bad = Package::open(save(root, "cut", cut));
            check(bad.exports[0].native_data["retargeting"]["coverage"] == "stored" &&
                      bad.exports[0].native_data["curveMetadata"]["coverage"] == "opaque",
                  "Every truncated curve byte rolls back only curve suffix");
        }
        ReadOptions budget;
        budget.max_properties = 11;
        check(Package::open(file, budget).exports[0].native_data["curveMetadata"]["coverage"] == "opaque",
              "All native stages share aggregate work budget");
        bool stop = false;
        ReadOptions cancel;
        cancel.control.cancelled = [&] { return stop; };
        cancel.control.progress = [&](const OperationProgress &p) {
            if (p.phase == "Reading skeleton curve metadata")
                stop = true;
        };
        bool stopped = false;
        try {
            Package::open(file, cancel);
        } catch (const Error &error) {
            stopped = error.code == "cancelled";
        }
        check(stopped, "Curve cancellation propagates");
        fs::remove_all(root);
        std::cout << checks << " native curve checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        fs::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
