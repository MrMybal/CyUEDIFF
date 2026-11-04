#include "skeleton_fixture.hpp"
#include <cyuediff/cache.hpp>
#include "../src/skeleton_native.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
int main(int argc, char **argv) {
    int checks = 0;
    auto root =
        fs::current_path() /
        (".test-tmp-skeleton-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        if (argc == 3 && std::string(argv[1]) == "--write-fixtures") {
            auto out = fs::u8path(argv[2]);
            fs::create_directories(out);
            save(out, "before", skeleton_file_fixture());
            save(out, "after", skeleton_file_fixture(false, false, 1));
            save(out, "reordered", skeleton_file_fixture(false, true));

            save(out, "invalid", skeleton_file_fixture(false, false, 0, 2));
            save(out, "unsupported", fixture(1013));
            return 0;
        }
        fs::create_directories(root);
        auto check = [&](bool ok, const char *message) {
            ++checks;
            if (!ok)
                throw std::runtime_error(message);
        };
        auto path = save(root, "skeleton", skeleton_file_fixture());
        auto p = Package::open(path);
        const auto &body = p.exports[0];
        check(body.native_coverage == "reference-skeleton", "Native hierarchy prefix read");
        check(body.native_data.at("bones").size() == 3, "Three stored bone infos");
        check(body.native_data.at("localPoses")[1]["translation"][0] == 10., "Stored local pose doubles");
        check(body.native_data.at("nameToIndex")["tip"] == 2, "Stored map checked and retained");
        check(body.opaque.at("suffix").at("bytes") == 4, "Unparsed native tail remains opaque");
        {
            auto filtered_fixture = skeleton_file_fixture(true);
            const auto filtered_path = root / "filtered-native.bin";
            filtered_fixture.script.save(filtered_path);
            ReadOptions read_options;
            detail::Archive filtered_archive(filtered_path, read_options);
            filtered_archive.seek(body.native_start);
            auto filtered_package = p;
            filtered_package.summary.flags |= 0x80000000u;
            auto filtered = detail::decode_reference_skeleton(filtered_archive, filtered_package,
                                                              filtered_archive.size(), read_options);
            check(!filtered.data["bones"][0].contains("exportName"),
                  "Filtered editor export names remain absent");
        }
        for (int kind = 1; kind <= 4; ++kind) {
            auto bad = Package::open(save(root, "bad", skeleton_file_fixture(false, false, 0, kind)));
            check(bad.exports[0].native_coverage == "opaque" && bad.exports[0].native_data.empty() &&
                      bad.exports[0].opaque.contains("suffix"),
                  "Malformed native data falls back atomically");
        }
        auto unsupported_fixture = skeleton_file_fixture();
        unsupported_fixture.header.patch(16, 1012);
        auto unsupported = Package::open(save(root, "unsupported-profile", unsupported_fixture));
        check(unsupported.exports[0].native_coverage == "opaque", "Uncertified file profile stays opaque");
        ReadOptions disabled;
        disabled.native_skeletons = false;
        check(Package::open(path, disabled).exports[0].native_data.empty(),
              "Native skeleton read can be disabled");
        PackageCache cache;
        check(cache.open(path)->exports[0].native_coverage == "reference-skeleton" &&
                  cache.open(path, disabled)->exports[0].native_data.empty(),
              "Cache separates skeleton decoder profiles");
        auto fixture = skeleton_file_fixture();
        const auto prefix_start = std::size_t(body.native_start);
        for (std::size_t end = prefix_start; end < body.native_end; ++end) {
            auto cut = fixture;
            cut.script.data.resize(end);
            const auto size = std::int64_t(end);
            std::memcpy(cut.header.data.data() + cut.export_offset + 28, &size, 8);
            auto truncated = Package::open(save(root, "cut", cut));
            check(truncated.exports[0].native_data.empty(), "Every truncated native prefix remains opaque");
        }
        ReadOptions budget;
        budget.max_properties = 8;
        check(Package::open(path, budget).exports[0].native_coverage == "opaque",
              "Native aggregate work budget");
        bool cancel = false;
        ReadOptions cancelled;
        cancelled.control.cancelled = [&] { return cancel; };
        cancelled.control.progress = [&](const OperationProgress &progress) {
            if (progress.phase == "Reading reference skeleton")
                cancel = true;
        };
        bool stopped = false;
        try {
            Package::open(path, cancelled);
        } catch (const Error &e) {
            stopped = e.code == "cancelled";
        }
        check(stopped, "Native skeleton cancellation propagates");
        fs::remove_all(root);
        std::cout << checks << " skeleton native checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
