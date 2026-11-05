#include <cyuediff/binary_layout.hpp>
#include "native_property_fixture.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool value, const char *message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f, const char *code = nullptr) {
    try { f(); } catch (const Error &e) { check(!code || e.code == code, e.what()); return; }
    throw std::runtime_error("Expected a rejected layout");
}
struct Input : DecoderInput {
    std::vector<std::uint8_t> data;
    std::size_t at = 0;
    explicit Input(std::vector<std::uint8_t> value) : data(std::move(value)) {}
    std::uint64_t position() const override { return at; }
    std::uint64_t remaining() const override { return data.size() - at; }
    void read(void *out, std::size_t size) override {
        if (size > remaining()) throw Error("bounds", "Test input truncated");
        if (size) std::memcpy(out, data.data() + at, size);
        at += size;
    }
    void skip(std::uint64_t size) override { if (size > remaining()) throw Error("bounds", "Test skip"); at += static_cast<std::size_t>(size); }
    std::uint32_t count(std::uint32_t) override { return get<std::uint32_t>(); }
    std::string string() override { throw Error("test", "String must use bounded decoder"); }
    std::string name() override { skip(8); return "TestName"; }
    std::string reference() override { skip(4); return "/Script/Test.Object"; }
    std::string guid() override { skip(16); return "00000001000000020000000300000004"; }
};
Json scalar(const char *type) { return {{"type", type}}; }
Json field(const char *name, Json type) { return {{"name", name}, {"value", std::move(type)}}; }
Json catalog(Json type) {
    return {{"format", "cyuediff.binary-layouts/1"}, {"layouts", {{"Record", std::move(type)}}}};
}
Json record() {
    return {{"type", "record"}, {"fields", Json::array({field("number", scalar("i32")),
        field("label", scalar("string")), field("samples", {{"type", "array"}, {"count", "i32"}, {"element", scalar("f32")}}),
        field("enabled", scalar("bool32")), field("extra", {{"type", "optional"}, {"presence", "bool8"}, {"value", scalar("i64")}})})}};
}
Bytes payload(bool changed = false) {
    Bytes b; b.i(changed ? 44 : 42); b.string("plugin"); b.i(2); b.put<float>(1.25f); b.put<float>(-2.5f);
    b.i(1); b.put<std::uint8_t>(1); b.put<std::int64_t>(INT64_MIN); return b;
}
Json bound_catalog() {
    auto j = catalog(record());
    j["decoders"] = Json::array({{{"id", "test.record/1"}, {"kind", "struct"}, {"target", "PluginRecord"},
        {"profile", {{"ue4", {522, 522}}, {"ue5", {0, 1018}}}}, {"layout", "Record"}}});
    return j;
}
}
int main(int argc, char **argv) {
    auto root = fs::current_path() / (".test-tmp-layouts-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        const auto document = bound_catalog();
        auto layouts = BinaryLayouts::parse(document);
        check(layouts.contains("Record") && !layouts.contains("Missing") && layouts.sha256().size() == 64, "Catalog identity");
        check(BinaryLayouts::parse_text(document.dump(2)).sha256() == layouts.sha256(), "Canonical layout identity");
        Input input(payload().data);
        const auto value = layouts.decode(input, "Record");
        check(value["number"] == 42 && value["label"] == "plugin" && value["samples"] == Json::array({1.25, -2.5}) &&
              value["enabled"] == true && value["extra"]["value"] == INT64_MIN && input.remaining() == 0, "Typed nested wire decode");
        rejects([&] { Input i({}); layouts.decode(i, "Missing"); }, "layout-missing");
        for (std::size_t size = 0; size < payload().data.size(); ++size) {
            auto data = payload().data; data.resize(size);
            rejects([&] { Input i(data); layouts.decode(i, "Record"); });
        }
        for (const auto &type : {"u8", "i8", "u16", "i16", "u32", "i32", "u64", "i64", "f32", "f64", "bool8", "bool32"}) {
            auto c = BinaryLayouts::parse(catalog(scalar(type))); Input i(std::vector<std::uint8_t>(16, 0));
            auto v = c.decode(i, "Record"); check(v == 0 || v == false, "Scalar zero");
        }
        for (const auto &type : {"name", "object", "guid"}) {
            auto c = BinaryLayouts::parse(catalog(scalar(type))); Input i(std::vector<std::uint8_t>(32, 0));
            check(c.decode(i, "Record").is_string(), "Resolved input values");
        }
        for (const auto &type : {"f32", "f64", "bool8", "bool32"}) {
            Bytes bad; if (std::string(type) == "f32") bad.put<float>(std::numeric_limits<float>::infinity());
            else if (std::string(type) == "f64") bad.put<double>(std::numeric_limits<double>::quiet_NaN());
            else if (std::string(type) == "bool8") bad.put<std::uint8_t>(2); else bad.i(2);
            auto c = BinaryLayouts::parse(catalog(scalar(type)));
            rejects([&] { Input i(bad.data); c.decode(i, "Record"); }, "layout-value");
        }
        auto bytes = BinaryLayouts::parse(catalog({{"type", "bytes"}, {"count", "i32"}}));
        Input raw({3,0,0,0,0,0xab,0xff}); check(bytes.decode(raw,"Record") == "00abff", "Bounded byte value");
        auto ref = catalog({{"type", "ref"}, {"layout", "Other"}}); ref["layouts"]["Other"] = scalar("u16");
        Input ri({0x34,0x12}); check(BinaryLayouts::parse(ref).decode(ri,"Record") == 0x1234, "Named layout references");
        ref["layouts"]["Other"] = {{"type","ref"},{"layout","Record"}};
        rejects([&]{ BinaryLayouts::parse(ref); }, "layout-format");
        ref["layouts"].erase("Other"); rejects([&]{ BinaryLayouts::parse(ref); }, "layout-format");
        rejects([&]{ BinaryLayouts::parse_text("{\"format\":0,\"format\":1}"); }, "layout-format");
        for (const auto &type : {Json{{"type","unknown"}}, Json{{"type","i32"},{"extra",true}},
              Json{{"type","array"},{"count",-1},{"element",scalar("i32")}},
              Json{{"type","bytes"},{"count",UINT64_MAX}}, Json{{"type","optional"},{"presence","bool64"},{"value",scalar("u8")}},
              Json{{"type","record"},{"fields",Json::array({field("x",scalar("u8")),field("x",scalar("u8"))})}}})
            rejects([&]{ BinaryLayouts::parse(catalog(type)); }, "layout-format");
        auto limits = BinaryLayoutOptions{}; limits.max_nodes=1;
        rejects([&]{ BinaryLayouts::parse(document,limits); }, "layout-limit");
        limits={}; limits.max_text_bytes=3; rejects([&]{ BinaryLayouts::parse(document,limits); }, "layout-limit");
        limits={}; limits.max_bytes=3; rejects([&]{ BinaryLayouts::parse_text(document.dump(),limits); }, "layout-limit");
        BinaryLayoutReadOptions read; read.max_values=2;
        rejects([&]{ Input i(payload().data); layouts.decode(i,"Record",read); }, "layout-limit");
        read={}; read.max_read_bytes=3;
        Input bi(payload().data); rejects([&]{ layouts.decode(bi,"Record",read); }, "layout-limit");
        check(bi.position()==0,"Byte budget checked before reading");
        read={}; read.max_text_bytes=4;
        rejects([&]{ Input i(payload().data); layouts.decode(i,"Record",read); }, "layout-limit");
        read={}; read.max_depth=1;
        rejects([&]{ Input i(payload().data); layouts.decode(i,"Record",read); }, "layout-limit");
        read={}; read.control.cancelled=[] {return true;};
        rejects([&]{ Input i(payload().data); layouts.decode(i,"Record",read); }, "cancelled");
        read={}; unsigned work=0; std::uint64_t text=0;
        read.charge=[&](std::uint32_t w,std::uint64_t n){work+=w;text+=n;};
        Input charged(payload().data); layouts.decode(charged,"Record",read);
        check(work>=9 && text>=6,"Enclosing budgets charged");
        auto string_layout=BinaryLayouts::parse(catalog(scalar("string")));
        Bytes wide; wide.i(-4); wide.put<std::uint16_t>('A'); wide.put<std::uint16_t>(0xd83d); wide.put<std::uint16_t>(0xde00); wide.put<std::uint16_t>(0);
        Input wi(wide.data); check(string_layout.decode(wi,"Record")=="A\xf0\x9f\x98\x80","UTF-16 surrogate decode");
        read={}; read.max_read_bytes=5; Input si(payload().data);
        rejects([&]{string_layout.decode(si,"Record",read);},"layout-limit");
        check(si.position()==4,"String payload rejected before allocation/read");
        for(int version : {0,1009,1012,1013,1018}) {
            auto path=save(root,"native"+std::to_string(version),native_property_fixture(payload(),"PluginRecord",version));
            ReadOptions options; options.decoders=layouts.decoder_registry();
            auto p=Package::open(path,options);
            check(p.exports[0].properties["Native"]["value"]["data"]["number"]==42 &&
                  p.exports[0].properties["AfterNative"]["value"]==987,"Real package tag path uses declarative registry");
            auto truncated=payload(); truncated.data.pop_back();
            auto bad=save(root,"truncated"+std::to_string(version),native_property_fixture(truncated,"PluginRecord",version));
            auto q=Package::open(bad,options);
            check(q.exports[0].properties["Native"]["value"].is_null() && q.exports[0].properties["AfterNative"]["value"]==987,
                  "Malformed layout preserves following property");
        }
        if(argc==3 && std::string(argv[1])=="--emit-fixtures") {
            const auto out=fs::u8path(argv[2]); fs::create_directories(out);
            std::ofstream(out/"layouts.json")<<document.dump(2);
            for (const auto &name : {"before","after","bad"}) {
                auto data=payload(std::string(name)=="after"); if(std::string(name)=="bad")data.data.pop_back();
                save(out,name,native_property_fixture(data,"PluginRecord",1013));
            }
        }
        fs::remove_all(root);
        std::cout<<checks<<" binary layout checks passed\n"; return 0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<"\nFixtures: "<<root<<'\n'; return 1; }
}
