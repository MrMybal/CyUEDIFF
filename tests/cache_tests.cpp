#include <cyuediff/cache.hpp>
#include "package_fixture.hpp"
#include <chrono>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main() {
    auto root =
        fs::current_path() /
        (".test-tmp-cache-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        PackageCache cache;
        auto file = save(root, "A", fixture(1012));
        auto before = cache.open(file);
        check(cache.open(file) == before && cache.stats().hits == 1,
              "Identical sources reuse immutable package");
        auto timestamp = fs::last_write_time(file);
        auto size = fs::file_size(file);
        save(root, "A", fixture(1012, 7));
        fs::last_write_time(file, timestamp);
        check(fs::file_size(file) == size, "Fixture preserves byte size");
        auto after = cache.open(file);
        check(after != before && after->exports[0].properties["Value"]["value"] == 7,
              "Same size and timestamp cannot hide changed bytes");
        check(before->exports[0].properties["Value"]["value"] == 1, "Previous snapshot remains immutable");
        auto companion = file;
        companion.replace_extension(".upayload");
        std::ofstream(companion) << "x";
        auto with_companion = cache.open(file);
        check(with_companion != after, "New companion invalidates cache");
        fs::remove(companion);
        check(cache.open(file) != with_companion, "Removed companion invalidates cache");
        ReadOptions metadata;
        metadata.properties = false;
        check(cache.open(file, metadata)->exports[0].coverage == "opaque",
              "Read profiles do not share incompatible entries");
        ReadOptions no_reflection;
        no_reflection.native_reflection = false;
        auto without_reflection = cache.open(file, no_reflection);
        check(cache.open(file) != without_reflection && cache.open(file, no_reflection) == without_reflection,
              "Native field-definition policy participates in cache identity");
        OperationControl cancel;
        cancel.cancelled = [] { return true; };
        ReadOptions cancelled;
        cancelled.control = cancel;
        try {
            cache.open(file, cancelled);
            throw std::runtime_error("Expected cancellation");
        } catch (const Error &e) {
            check(e.code == "cancelled", "Cache lookup respects cancellation");
        }
        PackageCache single({1, 1024 * 1024});
        auto first = single.open(file);
        auto other = save(root, "B", fixture(1012));
        single.open(other);
        check(single.stats().entries == 1 && single.open(file) != first,
              "LRU entry bound evicts old package");
        PackageCache tiny({1, 1});
        tiny.open(file);
        check(tiny.stats().entries == 0 && tiny.stats().misses == 1, "Large source bypasses retention");
        cache.clear();
        check(cache.stats().entries == 0 && cache.stats().source_bytes == 0, "Clear releases retention");
        fs::remove_all(root);
        std::cout << checks << " package cache checks passed\n";
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
