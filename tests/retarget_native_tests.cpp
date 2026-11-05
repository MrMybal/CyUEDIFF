#include "retarget_fixture.hpp"
#include <cyuediff/cache.hpp>
#include "../src/skeleton_native.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main(int argc, char **argv) {
    const auto root =
        fs::current_path() /
        (".test-tmp-retarget-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int checks = 0;
    try {
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            auto out = fs::u8path(argv[2]);
            fs::create_directories(out);
            save(out, "retarget-before", retarget_file_fixture());
            save(out, "retarget-after", retarget_file_fixture(2));
            save(out, "retarget-reordered", retarget_file_fixture(0, 0, 17, 1013, true));
            save(out, "retarget-mismatch", retarget_file_fixture(0, 0, 17, 1013, false, true));
            save(out, "retarget-invalid", retarget_file_fixture(0, 1));
            return 0;
        }
        fs::create_directories(root);
        auto check = [&](bool ok, const char *msg) {
            ++checks;
            if (!ok)
                throw std::runtime_error(msg);
        };
        const auto file = save(root, "skeleton", retarget_file_fixture());
        const auto p = Package::open(file);
        const auto &e = p.exports[0];
        const auto &r = e.native_data.at("retargeting");
        check(r["coverage"] == "stored" && r["sources"].size() == 2, "Stored retarget source map");
        check(r["sources"]["A~/pose"]["poseName"] == "DisplayPose",
              "Map key and pose label retained separately");
        check(r["sources"]["A~/pose"]["localPoses"][1]["translation"][0] == 15., "Stored source transforms");
        check(r["sources"]["A~/pose"]["sourceReferenceMesh"]["path"] == "/Game/Source.Source:Component",
              "Modern soft reference is retained without loading");
        check(r["skeletonGuid"] == "12345678000000020000000300000004", "Distinct skeleton GUID");
        check(e.opaque["suffix"]["bytes"] == 4, "Unparsed SmartNames/marker bytes stay opaque");
        auto old = Package::open(save(root, "old", retarget_file_fixture(0, 0, 17, 1004)));
        check(old.exports[0].native_data["retargeting"]["sources"]["B"]["sourceReferenceMesh"]["path"] ==
                  "/Game/Source.Source:Component",
              "UE5.0 soft path layout");
        auto hard = Package::open(save(root, "hard", retarget_file_fixture(0, 0, 14)));
        check(hard.exports[0].native_data["retargeting"]["sources"]["B"]["sourceReferenceMesh"] ==
                  Json({{"kind", "object"}, {"path", "None"}}),
              "Legacy stored object reference");
        auto bad = Package::open(save(root, "bad", retarget_file_fixture(0, 1)));
        check(bad.exports[0].native_coverage == "reference-skeleton" &&
                  bad.exports[0].native_data["retargeting"]["coverage"] == "opaque",
              "Malformed details preserve checked base bones");
        check(bad.exports[0].opaque["suffix"]["bytes"].get<std::uint64_t>() > 4,
              "Entire invalid details remain fingerprinted");
        for (int version : {-1, 18}) {
            const auto unknown = Package::open(save(root, "version", retarget_file_fixture(0, 0, version)));
            check(unknown.exports[0].native_data["retargeting"]["coverage"] == "opaque",
                  "Missing and future custom versions not guessed");
        }
        const auto invalid_ref = Package::open(save(root, "invalid-ref", retarget_file_fixture(0, 2, 14)));
        check(invalid_ref.exports[0].native_data["retargeting"]["coverage"] == "opaque",
              "Invalid hard reference cannot consume native details");
        const auto empty = Package::open(
            save(root, "empty", retarget_file_fixture(0, 0, -1, 1013, false, false, false, true)));
        check(empty.exports[0].native_data["retargeting"]["coverage"] == "stored" &&
                  empty.exports[0].native_data["retargeting"]["sources"].empty(),
              "Known empty table does not require an unused source-reference version");
        ReadOptions disabled;
        disabled.native_skeleton_retargeting = false;
        PackageCache cache;
        const auto prefix = cache.open(file, disabled);
        check(prefix->exports[0].native_end < e.native_end &&
                  cache.open(file)->exports[0].native_end == e.native_end,
              "Decoder opt-out has separate cache identity and opaque boundary");
        auto f = retarget_file_fixture();
        for (std::size_t end = std::size_t(prefix->exports[0].native_end); end < e.native_end; ++end) {
            auto cut = f;
            cut.script.data.resize(end);
            const auto size = std::int64_t(end);
            std::memcpy(cut.header.data.data() + cut.export_offset + 28, &size, 8);
            auto truncated = Package::open(save(root, "cut", cut));
            check(truncated.exports[0].native_coverage == "reference-skeleton" &&
                      truncated.exports[0].native_data["retargeting"]["coverage"] == "opaque",
                  "Every truncated detail suffix preserves only checked base bones");
        }
        { // Filtered name/reference omission is a bounded prefix fixture, not a claimed full filtered
          // package.
            auto filtered = retarget_file_fixture(0, 0, 17, 1013, false, false, true);
            filtered.script.save(root / "filtered.bin");
            ReadOptions options;
            detail::Archive archive(root / "filtered.bin", options);
            archive.seek(e.native_start);
            auto context = p;
            context.summary.flags |= 0x80000000u;
            const auto result = detail::decode_reference_skeleton(archive, context, archive.size(), options);
            check(result.data["retargeting"]["coverage"] == "stored" &&
                      !result.data["retargeting"]["sources"]["B"].contains("sourceReferenceMesh"),
                  "Filtered source references remain absent");
        }
        ReadOptions budget;
        budget.max_properties = 10;
        check(Package::open(file, budget).exports[0].native_data["retargeting"]["coverage"] == "opaque",
              "Raw bones and retarget arrays share aggregate work budget");
        bool stop = false;
        ReadOptions cancel;
        cancel.control.cancelled = [&] { return stop; };
        cancel.control.progress = [&](const OperationProgress &progress) {
            if (progress.phase == "Reading skeleton retarget sources")
                stop = true;
        };
        bool stopped = false;
        try {
            Package::open(file, cancel);
        } catch (const Error &error) {
            stopped = error.code == "cancelled";
        }
        check(stopped, "Retarget stage cancellation propagates");
        fs::remove_all(root);
        std::cout << checks << " retarget native checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
