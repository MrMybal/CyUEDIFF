#pragma once
#include "schema_collection_fixture.hpp"
namespace cyuediff::test {
inline Bytes texture_scripts() {
    Bytes b;
    name_batch(b, {"/Script/Engine", "Texture2D", "Default__Texture2D"});
    b.i(3);
    for (unsigned j = 0; j < 3; ++j) {
        b.put<std::uint32_t>(0x80000000u | j);
        b.i(0);
        b.put(script_root + j);
        b.put(j ? script_root : ~0ull);
        b.put<std::uint64_t>(~0ull);
    }
    return b;
}
struct TextureFixture {
    Bytes package, bulk;
    std::size_t body = 0, bulk_row = 0, format_skip = 0, mip_index = 0, dimensions = 0;
};
inline TextureFixture cooked_texture_fixture(bool external = false, unsigned flags = 0) {
    TextureFixture f;
    auto &b = f.package;
    b.zeros(52);
    name_batch(b, {"None", "/Game/Texture", "Texture", "PF_DXT1"});
    b.put<std::uint64_t>(32);
    f.bulk_row = b.data.size();
    b.put<std::int64_t>(external ? 3 : 0);
    b.put<std::int64_t>(-1);
    b.put<std::int64_t>(8);
    b.i(flags ? flags : external ? 0x10501 : 72);
    b.i(0);
    auto offset = [&](int n) { b.patch(24 + n * 4, static_cast<int>(b.data.size())); };
    offset(0);
    offset(1);
    offset(2);
    const auto exp = b.data.size();
    b.put<std::uint64_t>(0);
    b.put<std::uint64_t>(0);
    b.i(2);
    b.i(0);
    b.put<std::uint64_t>(~0ull);
    b.put(script_class);
    b.put<std::uint64_t>(~0ull);
    b.put(script_cdo);
    b.put<std::uint64_t>(1);
    b.i(3);
    b.i(0);
    offset(3);
    b.i(0);
    b.i(0);
    b.i(0);
    b.i(1);
    offset(4);
    b.i(-1);
    b.zeros(16);
    offset(5);
    offset(6);
    name_batch(b, {});
    f.body = b.data.size();
    b.patch(4, static_cast<int>(f.body));
    b.patch(8, 1);
    b.patch(16, 0x80002200u);
    b.patch(20, 1000);
    b.put<std::uint16_t>(0x0100);
    b.i(0);
    b.put<std::uint8_t>(1);
    b.put<std::uint8_t>(0);
    b.put<std::uint8_t>(1);
    b.put<std::uint8_t>(0);
    b.i(1);
    b.i(1);
    b.i(3);
    b.i(0);
    f.format_skip = b.data.size();
    b.put<std::uint64_t>(0);
    b.zeros(16);
    b.i(4);
    b.i(4);
    b.i(1);
    b.string("PF_DXT1");
    b.i(0);
    b.i(1);
    f.mip_index = b.data.size();
    b.i(0);
    const std::vector<std::uint8_t> color = {0, 248, 224, 7, 0, 0, 0, 0};
    if (external) {
        f.bulk.zeros(3);
        f.bulk.data.insert(f.bulk.data.end(), color.begin(), color.end());
    } else
        b.data.insert(b.data.end(), color.begin(), color.end());
    f.dimensions = b.data.size();
    b.i(4);
    b.i(4);
    b.i(1);
    b.i(0);
    const auto skip = b.data.size() - f.format_skip;
    std::memcpy(b.data.data() + f.format_skip, &skip, 8);
    b.i(0);
    b.i(0);
    const std::uint64_t size = b.data.size() - f.body;
    std::memcpy(b.data.data() + exp + 8, &size, 8);
    return f;
}
inline void texture_container(const fs::path &stem, const TextureFixture &fixture, unsigned bulk_type = 2,
                              bool corrupt = false) {
    Bytes toc;
    toc.zeros(144);
    const std::string magic = "-==--==--==--==-";
    std::copy(magic.begin(), magic.end(), toc.data.begin());
    toc.data[16] = 5;
    const bool bulk = !fixture.bulk.data.empty();
    const unsigned count = bulk ? 2 : 1;
    toc.patch(20, 144);
    toc.patch(24, count);
    const auto block_count =
        (fixture.package.data.size() + 65535) / 65536 + (fixture.bulk.data.size() + 65535) / 65536;
    toc.patch(28, static_cast<int>(block_count));
    toc.patch(32, 12);
    toc.patch(44, 65536);
    toc.patch(52, 1);
    const auto partition = ~0ull;
    std::memcpy(toc.data.data() + 88, &partition, 8);
    for (unsigned i = 0; i < count; ++i) {
        toc.zeros(11);
        toc.put<std::uint8_t>(i ? bulk_type : 1);
    }
    auto big = [&](std::uint64_t value) {
        for (int i = 4; i >= 0; --i)
            toc.put<std::uint8_t>(static_cast<std::uint8_t>(value >> (8 * i)));
    };
    big(0);
    big(fixture.package.data.size());
    if (bulk) {
        big(((fixture.package.data.size() + 65535) / 65536) * 65536);
        big(fixture.bulk.data.size());
    }
    const auto package_bytes = fixture.package.data.size();
    for (unsigned i = 0; i < count; ++i) {
        const auto at = i ? package_bytes : 0;
        const auto bytes = i ? fixture.bulk.data.size() : package_bytes;
        for (std::size_t offset = 0; offset < bytes; offset += 65536) {
            const auto size = std::min<std::size_t>(65536, bytes - offset);
            const auto block_at = at + offset;
            for (unsigned n = 0; n < 5; ++n)
                toc.put<std::uint8_t>(static_cast<std::uint8_t>(block_at >> (8 * n)));
            for (unsigned repeat = 0; repeat < 2; ++repeat)
                for (unsigned n = 0; n < 3; ++n)
                    toc.put<std::uint8_t>(static_cast<std::uint8_t>(size >> (8 * n)));
            toc.put<std::uint8_t>(0);
        }
    }
    io_chunk_meta(toc, fixture.package.data);
    if (bulk)
        io_chunk_meta(toc, fixture.bulk.data);
    auto data = fixture.package;
    data.append(fixture.bulk);
    if (corrupt)
        data.data.back() ^= 1;
    data.save(fs::path(stem.string() + ".ucas"));
    toc.save(fs::path(stem.string() + ".utoc"));
}
inline ZenReadOptions texture_context() {
    ZenReadOptions z;
    z.profile = ZenProfile::unreal_5_3;
    z.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(texture_scripts().data, z));
    auto c = std::make_shared<ZenSchemaContext>();
    c->profile = z.profile;
    c->schemas =
        std::make_shared<UnversionedSchemas>(UnversionedSchemas::parse(collection_schema_fixture().data));
    c->bindings["/Script/Engine.Texture2D"] = {"Collections", 0};
    z.schema_context = c;
    return z;
}
} // namespace cyuediff::test
