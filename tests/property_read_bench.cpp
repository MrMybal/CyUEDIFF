// Property reading benchmark on a synthetic UE 5.4-style package (complete tag types).
// Not a CTest test: run it manually and compare timings before/after a change. The printed
// snapshot SHA-256 must stay identical, proving that an optimization did not change results.
#include "package_fixture.hpp"
#include "../src/source_availability.hpp"
#include <algorithm>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <time.h>
#endif
#include <chrono>
#include <cstdlib>
#include <iostream>
using namespace cyuediff;
using cyuediff::test::Bytes;
using cyuediff::test::NameWriter;
namespace {
struct TypeNode {
    std::string name;
    std::vector<TypeNode> children;
};
void write_type(Bytes &b, const NameWriter &name, const TypeNode &type) {
    name(b, type.name);
    b.i(static_cast<int>(type.children.size()));
    for (const auto &child : type.children)
        write_type(b, name, child);
}
void tag(Bytes &b, const NameWriter &name, const std::string &property, const TypeNode &type, const Bytes &payload) {
    name(b, property);
    write_type(b, name, type);
    b.i(static_cast<int>(payload.data.size()));
    b.put<std::uint8_t>(0);
    b.append(payload);
}
TypeNode array_of(TypeNode element) {
    return {"ArrayProperty", {std::move(element)}};
}
TypeNode struct_of(const std::string &name) {
    return {"StructProperty", {{name, {}}}};
}
void numbered_name(Bytes &b, const NameWriter &name, const std::string &base, int number) {
    name(b, base);
    b.patch(b.data.size() - 4, number);
}
fs::path build(const fs::path &root, const std::string &file, int points, int rows, int numbers, int labels,
               int seed) {
    const std::vector<std::string> names = {"Points", "Vector", "Rows",   "Id",          "Label",  "Target",
                                            "Weight", "NameProperty", "ObjectProperty", "FloatProperty",
                                            "Numbers", "Labels", "Item"};
    auto f = cyuediff::test::fixture(
        1012, 1, false, 0, names, [&](Bytes &s, const NameWriter &name) {
            Bytes p;
            p.i(points);
            for (int i = 0; i < points; ++i)
                for (int c = 0; c < 3; ++c)
                    p.put<double>(i * 0.5 + c + seed);
            tag(s, name, "Points", array_of(struct_of("Vector")), p);
            Bytes r;
            r.i(rows);
            for (int i = 0; i < rows; ++i) {
                Bytes v;
                v.i(i + seed);
                tag(r, name, "Id", {"IntProperty", {}}, v);
                Bytes l;
                numbered_name(l, name, "Item", i % 100 + 1);
                tag(r, name, "Label", {"NameProperty", {}}, l);
                Bytes o;
                o.i(-1);
                tag(r, name, "Target", {"ObjectProperty", {}}, o);
                Bytes w;
                w.put<float>(static_cast<float>(i) * 0.25f);
                tag(r, name, "Weight", {"FloatProperty", {}}, w);
                name(r, "None");
            }
            tag(s, name, "Rows", array_of(struct_of("ExampleStruct")), r);
            Bytes n;
            n.i(numbers);
            for (int i = 0; i < numbers; ++i)
                n.i(i * 3 + seed);
            tag(s, name, "Numbers", array_of({"IntProperty", {}}), n);
            Bytes t;
            t.i(labels);
            for (int i = 0; i < labels; ++i)
                numbered_name(t, name, "Item", i % 1000 + 1);
            tag(s, name, "Labels", array_of({"NameProperty", {}}), t);
        });
    return cyuediff::test::save(root, file, std::move(f));
}
// Thread CPU work, far less sensitive to other processes than wall time. On Windows the unit is
// millions of reference cycles; elsewhere CPU milliseconds. Compare runs on the same machine only.
double thread_work() {
#ifdef _WIN32
    ULONG64 cycles = 0;
    QueryThreadCycleTime(GetCurrentThread(), &cycles);
    return static_cast<double>(cycles) / 1e6;
#else
    timespec t{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
#endif
}
template <class F> double best_ms(int runs, F &&f) {
    double best = 1e300;
    for (int i = 0; i < runs; ++i) {
        const auto start = thread_work();
        f();
        best = std::min(best, thread_work() - start);
    }
    return best;
}
} // namespace
int main(int argc, char **argv) {
    const int runs = argc > 1 ? std::max(1, std::atoi(argv[1])) : 5;
    const auto root = fs::current_path() / (".test-tmp-property-bench-" +
                                            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try {
        ReadOptions options;
        options.max_properties = 4'000'000;
        options.max_property_bytes = 256ull * 1024 * 1024;
        options.name_values.max_names = 4'000'000;
        options.name_values.max_text_bytes = 512ull * 1024 * 1024;
        if (std::getenv("CYUEDIFF_BENCH_NO_NAMES"))
            options.name_values.max_names = 0; // Measures typed-name capture overhead only.
        // Per-kind reading cost: one array kind per package.
        const struct {
            const char *label;
            int points, rows, numbers, labels;
        } kinds[] = {{"Vector[100k]", 100000, 0, 0, 0},
                     {"tagged struct[20k]", 0, 20000, 0, 0},
                     {"int[100k]", 0, 0, 100000, 0},
                     {"name[100k]", 0, 0, 0, 100000}};
        for (const auto &k : kinds) {
            const auto file = build(root, k.label[0] == 'V' ? "k1" : k.label[0] == 't' ? "k2" : k.label[0] == 'i' ? "k3" : "k4",
                                    k.points, k.rows, k.numbers, k.labels, 0);
            std::cout << "open " << k.label << "  " << best_ms(runs, [&] { (void)Package::open(file, options); })
                      << " Mcycles\n";
        }
        const auto before = build(root, "before", 100000, 20000, 100000, 100000, 0);
        const auto similar = build(root, "similar", 100000, 20000, 100000, 100001, 0);
        const auto after = build(root, "after", 100000, 20000, 100000, 100000, 1);
        Package a, b, c;
        const auto open_ms = best_ms(runs, [&] { a = Package::open(before, options); });
        b = Package::open(after, options);
        c = Package::open(similar, options);
        if (!a.complete_input || a.exports.empty() || a.exports[0].coverage != "properties")
            throw std::runtime_error("Benchmark package was not fully decoded: " + a.exports[0].coverage);
        std::string snapshot;
        const auto snapshot_ms = best_ms(runs, [&] { snapshot = a.semantic_snapshot().dump(); });
        std::size_t changes = 0, similar_changes = 0;
        const auto compare_ms = best_ms(runs, [&] { changes = compare(a, b).changes.size(); });
        const auto similar_ms = best_ms(runs, [&] { similar_changes = compare(a, c).changes.size(); });
        const auto coverage_ms = best_ms(runs, [&] { (void)detail::source_coverage(a, c); });
        std::cout << "package bytes      " << fs::file_size(before) << '\n'
                  << "open (best of " << runs << ")  " << open_ms << " Mcycles\n"
                  << "semantic snapshot  " << snapshot_ms << " Mcycles\n"
                  << "compare, 1 change  " << similar_ms << " Mcycles (" << similar_changes << " changes)\n"
                  << "  of which coverage " << coverage_ms << " Mcycles\n"
                  << "compare, all       " << compare_ms << " Mcycles (" << changes << " changes)\n"
                  << "snapshot sha256    "
                  << sha256_bytes(std::vector<std::uint8_t>(snapshot.begin(), snapshot.end())) << '\n';
    } catch (const std::exception &e) {
        std::cerr << "Benchmark failure: " << e.what() << '\n';
        return 1;
    }
    std::error_code ignored;
    fs::remove_all(root, ignored);
    return 0;
}
