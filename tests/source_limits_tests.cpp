#include "package_fixture.hpp"
#include <cyuediff/source_limits.hpp>
#include <cyuediff/cache.hpp>
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool ok, const char *why) {
    ++checks;
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const Error &) {
        ++checks;
        return;
    }
    throw std::runtime_error("Expected rejected source limits");
}
} // namespace
int main(int argc, char **argv) {
    const auto root =
        fs::current_path() / (".test-tmp-source-limits-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try {
        auto file = save(root, "budget", fixture(1012));
        SourceReadLimits defaults;
        ReadOptions o;
        defaults.apply(o);
        auto p = Package::open(file, o);
        const auto report = p.to_json();
        check(!report.contains("sourceReadLimits"), "Default reports preserve their existing shape");
        SourceReadLimits raised{2'000'000, 64 * 1024 * 1024};
        raised.apply(o);
        auto q = Package::open(file, o);
        check(q.to_json()["sourceReadLimits"] == raised.to_json(),
              "Explicit effective limits accompany the package report");
        check(q.semantic_snapshot() == p.semantic_snapshot() && compare(p, q).status() == "identical",
              "Read policy provenance does not fabricate asset differences");
        check(o.max_string_bytes == ReadOptions{}.max_string_bytes && o.max_depth == ReadOptions{}.max_depth,
              "Work/byte policy does not silently change unrelated limits");
        for (auto invalid : std::vector<SourceReadLimits>{
                 {0, 1}, {4'000'001, 1}, {1, 0}, {1, SourceReadLimits::maximum_bytes + 1}}) {
            rejects([&] { invalid.apply(o); });
            rejects([&] { invalid.to_json(); });
        }
        SourceReadLimits edge{SourceReadLimits::maximum_work, SourceReadLimits::maximum_bytes};
        edge.apply(o);
        check(o.max_properties == 4'000'000 && o.max_property_bytes == 256 * 1024 * 1024,
              "Finite public policy accepts both inclusive endpoints");
        PackageCache cache;
        auto ordinary = cache.open(file);
        ReadOptions low;
        SourceReadLimits{1, 32 * 1024 * 1024}.apply(low);
        auto constrained = cache.open(file, low);
        check(constrained != ordinary && constrained->exports[0].properties.empty() &&
                  !ordinary->exports[0].properties.empty(),
              "A constrained read cannot reuse a permissive cached package");
        check(constrained->source_read_limit_failed &&
                  constrained->to_json()["sourceReadLimitFailed"] == true,
              "Read limit/count guard failures are explicit rather than decoded absence");
        auto unavailable = compare(*ordinary, *constrained);
        check(unavailable.status() == "unverified" && unavailable.changes.empty() &&
                  unavailable.binary_comparison_available && !unavailable.binary_changed,
              "Mixed budgets cannot publish invented removals when file bytes are identical");
        check(cache.open(file, low) == constrained && cache.open(file) == ordinary,
              "Changing and restoring source work policies retains separate immutable cache entries");
        auto changed_file = save(root, "changed", fixture(1012, 9));
        auto changed_limited = Package::open(changed_file, low);
        auto binary_only = compare(*ordinary, changed_limited);
        check(binary_only.status() == "unverified" && binary_only.binary_comparison_available &&
                  binary_only.binary_changed && binary_only.changes.empty(),
              "Guard failures preserve changed binary facts without inventing typed changes");
        ReadOptions small;
        SourceReadLimits{100000, 1}.apply(small);
        auto tiny = cache.open(file, small);
        check(tiny != ordinary && tiny->exports[0].properties.empty(),
              "Property byte policy also participates in cache identity");
        ReadOptions cancelled = low;
        cancelled.control.cancelled = [] { return true; };
        rejects([&] { cache.open(file, cancelled); });
        if (argc == 3 && std::string(argv[1]) == "--emit-fixtures") {
            auto out = fs::u8path(argv[2]);
            fs::create_directories(out);
            save(out, "budget", fixture(1012));
        }
        auto final = fs::weakly_canonical(root);
        if (final.parent_path() != fs::weakly_canonical(fs::current_path()) ||
            final.filename().u8string().rfind(".test-tmp-source-limits-", 0) != 0)
            throw std::runtime_error("Cleanup escaped test root");
        fs::remove_all(final);
        std::cout << checks << " source read limit checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << checks << ": " << e.what() << "\nFixtures: " << root << '\n';
        return 1;
    }
}
