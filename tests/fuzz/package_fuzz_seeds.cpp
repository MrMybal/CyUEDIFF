// Writes a seed corpus for package_fuzzer from the synthetic test fixtures.
// Usage: cyuediff_package_fuzz_seeds <output directory>
#include "../package_fixture.hpp"
#include <iostream>
using namespace cyuediff;
using cyuediff::test::Bytes;
using cyuediff::test::NameWriter;
namespace {
void write_seed(const fs::path &file, const Bytes &package, const Bytes *uexp = nullptr) {
    Bytes seed;
    seed.put<std::uint32_t>(static_cast<std::uint32_t>(package.data.size()));
    seed.append(package);
    if (uexp)
        seed.append(*uexp);
    seed.save(file);
}
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
// UE 5.4-style complete tag types with arrays of builtin structs, tagged structs, names and objects.
test::Fixture rich() {
    const std::vector<std::string> names = {"Points", "Vector", "Rows", "Id", "Label", "Target", "Weight",
                                            "NameProperty", "ObjectProperty", "FloatProperty", "Labels", "Item"};
    return test::fixture(1012, 3, false, 0, names, [](Bytes &s, const NameWriter &name) {
        Bytes p;
        p.i(2);
        for (int i = 0; i < 6; ++i)
            p.put<double>(i * 0.5);
        tag(s, name, "Points", {"ArrayProperty", {{"StructProperty", {{"Vector", {}}}}}}, p);
        Bytes r;
        r.i(2);
        for (int i = 0; i < 2; ++i) {
            Bytes v;
            v.i(i);
            tag(r, name, "Id", {"IntProperty", {}}, v);
            Bytes l;
            name(l, "Item");
            l.patch(l.data.size() - 4, i + 1);
            tag(r, name, "Label", {"NameProperty", {}}, l);
            Bytes o;
            o.i(-1);
            tag(r, name, "Target", {"ObjectProperty", {}}, o);
            Bytes w;
            w.put<float>(0.25f * static_cast<float>(i));
            tag(r, name, "Weight", {"FloatProperty", {}}, w);
            name(r, "None");
        }
        tag(s, name, "Rows", {"ArrayProperty", {{"StructProperty", {{"ExampleStruct", {}}}}}}, r);
        Bytes t;
        t.i(3);
        for (int i = 0; i < 3; ++i) {
            name(t, "Item");
            t.patch(t.data.size() - 4, i);
        }
        tag(s, name, "Labels", {"ArrayProperty", {{"NameProperty", {}}}}, t);
    });
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) {
        std::cerr << "usage: cyuediff_package_fuzz_seeds <package corpus> [diff corpus]\n";
        return 2;
    }
    const fs::path out = fs::u8path(argv[1]);
    fs::create_directories(out);
    int count = 0;
    for (int version : {0, 1009, 1011, 1012, 1016, 1018}) {
        auto f = test::fixture(version);
        write_seed(out / ("v" + std::to_string(version) + "-split.bin"), f.header, &f.script);
        auto whole = f.header;
        whole.append(f.script);
        write_seed(out / ("v" + std::to_string(version) + ".bin"), whole);
        count += 2;
    }
    auto r = rich();
    write_seed(out / "rich-split.bin", r.header, &r.script);
    auto whole = r.header;
    whole.append(r.script);
    write_seed(out / "rich.bin", whole);
    count += 2;
    std::cout << count << " seeds written to " << out.u8string() << '\n';
    if (argc == 3) {
        // Pairs for package_diff_fuzzer: [u32 n][before package][after package].
        const fs::path diff = fs::u8path(argv[2]);
        fs::create_directories(diff);
        auto pair = [&](const std::string &file, test::Fixture a, test::Fixture b) {
            a.header.append(a.script);
            b.header.append(b.script);
            write_seed(diff / file, a.header, &b.header);
        };
        int pairs = 0;
        for (int version : {0, 1009, 1012, 1018}) {
            pair("v" + std::to_string(version) + "-value.bin", test::fixture(version, 1), test::fixture(version, 2));
            pair("v" + std::to_string(version) + "-reorder.bin", test::fixture(version, 1),
                 test::fixture(version, 1, true));
            pairs += 2;
        }
        pair("rich-self.bin", rich(), rich());
        pair("rich-value.bin", rich(), test::fixture(1012, 4));
        pairs += 2;
        std::cout << pairs << " diff seeds written to " << diff.u8string() << '\n';
    }
    return 0;
}
