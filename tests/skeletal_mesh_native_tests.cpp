#include "skeletal_mesh_fixture.hpp"
#include <cyuediff/cache.hpp>
#include <chrono>
#include <cmath>
#include <limits>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main(int argc, char **argv) {
    auto root =
        fs::current_path() /
        (".test-tmp-skeletal-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int checks = 0;
    try {
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            auto out = fs::u8path(argv[2]);
            fs::create_directories(out);
            save(out, "mesh-before", skeletal_mesh_fixture());
            save(out, "mesh-after", skeletal_mesh_fixture(2));
            save(out, "mesh-reordered", skeletal_mesh_fixture(0, 0, true, true));
            save(out, "mesh-invalid", skeletal_mesh_fixture(0, 3));
            return 0;
        }
        fs::create_directories(root);
        auto check = [&](bool ok, const char *msg) {
            ++checks;
            if (!ok)
                throw std::runtime_error(msg);
        };
        auto f = skeletal_mesh_fixture();
        const auto path = save(root, "mesh", f);
        const auto p = Package::open(path);
        const auto &e = p.exports[0];
        check(e.native_coverage == "reference-skeleton", "Skeletal mesh prefix read");
        check(e.native_data["mesh"]["importedBounds"]["boxExtent"] == Json({50., 50., 50.}), "Stored bounds");
        check(e.native_data["mesh"]["materials"][0]["importedSlotName"] == "ImportedBody",
              "Imported material slot");
        check(e.native_data["mesh"]["materials"][0]["uvChannels"]["localDensities"] == Json({1., 2., 3., 4.}),
              "UV densities");
        check(e.native_data["bones"].size() == 3 && e.native_data["localPoses"][1]["translation"][0] == 10.,
              "Shared raw bone reader");
        check(e.opaque["suffix"]["bytes"] == 4, "Source model tail remains opaque");
        check(Package::open(save(root, "guid", skeletal_mesh_fixture(0, 0, true, false, true)))
                      .exports[0]
                      .native_data["objectGuid"] == "00000001000000020000000300000004",
              "Optional UObject guid");
        const auto noimport = Package::open(save(root, "no-import", skeletal_mesh_fixture(0, 0, false)));
        check(!noimport.exports[0].native_data["mesh"]["materials"][0].contains("importedSlotName"),
              "Absent imported slot retained");
        for (int bad = 1; bad <= 3; ++bad)
            check(Package::open(save(root, "bad", skeletal_mesh_fixture(0, bad)))
                      .exports[0]
                      .native_data.empty(),
                  "Invalid native prefix atomic fallback");
        for (auto profile :
             {std::array<int, 3>{-1, 4, 45}, std::array<int, 3>{7, 4, 45}, std::array<int, 3>{41, 4, 45},
              std::array<int, 3>{40, 2, 45}, std::array<int, 3>{40, 5, 45}, std::array<int, 3>{40, 4, 9},
              std::array<int, 3>{40, 4, 46}}) {
            check(Package::open(save(root, "profile",
                                     skeletal_mesh_fixture(0, 0, true, false, false, profile[0], profile[1],
                                                           profile[2])))
                      .exports[0]
                      .native_data.empty(),
                  "Custom versions required and bounded");
        }
        check(
            Package::open(save(root, "old", skeletal_mesh_fixture(0, 0, true, false, false, 8, 3, 10, 1004)))
                    .exports[0]
                    .native_coverage == "reference-skeleton",
            "Explicit oldest supported profile");
        for (std::size_t end = std::size_t(e.native_start); end < e.native_end; ++end) {
            auto cut = f;
            cut.script.data.resize(end);
            const auto size = std::int64_t(end);
            std::memcpy(cut.header.data.data() + cut.export_offset + 28, &size, 8);
            check(Package::open(save(root, "cut", cut)).exports[0].native_data.empty(),
                  "Truncated prefix never partially published");
        }
        auto corrupt_bounds = f;
        const auto bad_double = std::numeric_limits<double>::quiet_NaN();
        std::memcpy(corrupt_bounds.script.data.data() + e.native_start + 6, &bad_double, 8);
        check(Package::open(save(root, "nonfinite-bounds", corrupt_bounds)).exports[0].native_data.empty(),
              "Nonfinite imported bounds stay opaque");
        auto corrupt_uv = f;
        const auto bad_float = std::numeric_limits<float>::infinity();
        std::memcpy(corrupt_uv.script.data.data() + e.native_start + 98, &bad_float, 4);
        check(Package::open(save(root, "nonfinite-uv", corrupt_uv)).exports[0].native_data.empty(),
              "Nonfinite UV density stays opaque");
        auto licensee = f;
        licensee.header.patch(20, 1);
        bool licensee_rejected=false;
        try{Package::open(save(root,"licensee",licensee));}catch(const Error&error){licensee_rejected=error.code=="licensee-version";}
        check(licensee_rejected,"Licensee package requires a dedicated backend");
        ReadOptions disabled;
        disabled.native_skeletons = false;
        PackageCache cache;
        check(cache.open(path)->exports[0].native_data.size() &&
                  cache.open(path, disabled)->exports[0].native_data.empty(),
              "Cache preserves decoder opt-out");
        ReadOptions budget;
        budget.max_properties = 9;
        check(Package::open(path, budget).exports[0].native_data.empty(),
              "Materials share bone aggregate work budget");
        bool stop = false;
        ReadOptions cancel;
        cancel.control.cancelled = [&] { return stop; };
        cancel.control.progress = [&](const OperationProgress &progress) {
            if (progress.phase == "Reading skeletal mesh materials")
                stop = true;
        };
        bool stopped = false;
        try {
            Package::open(path, cancel);
        } catch (const Error &error) {
            stopped = error.code == "cancelled";
        }
        check(stopped, "Skeletal material cancellation propagates");
        fs::remove_all(root);
        std::cout << checks << " skeletal mesh native checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
