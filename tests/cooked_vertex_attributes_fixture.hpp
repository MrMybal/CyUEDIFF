#pragma once
#include "cooked_skeletal_fixture.hpp"
namespace cyuediff::test {
struct AttributeSpec {
    std::uint32_t name_index = 2, name_number = 0;
    std::string streamed_name = "Texture";
    unsigned components = 1, pixel = 13;
    std::vector<std::uint8_t> bytes;
};
inline AttributeSpec attribute_spec(unsigned pixel = 13, unsigned components = 1, bool ones = false) {
    AttributeSpec s;
    s.pixel = pixel;
    s.components = components;
    Bytes b;
    for (unsigned i = 0; i < 3 * components; ++i) {
        if (pixel == 13)
            b.put(ones ? 1.f : float(int(i % 17) - 8) * .125f);
        else if (pixel == 21)
            b.put<std::uint16_t>(ones ? 0x3c00
                                      : std::vector<std::uint16_t>{0x3800, 0xbc00, 1, 0x8000}[i % 4]);
        else
            b.put<std::uint8_t>(ones ? 255 : std::vector<unsigned>{0, 127, 255}[i % 3]);
    }
    s.bytes = std::move(b.data);
    return s;
}
struct AttributeFixture {
    SkeletalFixture base;
    std::vector<std::size_t> names, headers, data;
};
inline AttributeFixture cooked_attributes_fixture(const std::vector<AttributeSpec> &specs,
                                                  bool external = false, bool utf16 = false) {
    AttributeFixture f;
    f.base = external ? cooked_skeletal_streaming_fixture() : cooked_skeletal_fixture();
    Bytes table;
    table.i(static_cast<int>(specs.size()));
    std::uint64_t name_bytes = 0;
    const auto at = f.base.attributes;
    for (const auto &s : specs) {
        f.names.push_back(at + table.data.size());
        if (external) {
            table.i(static_cast<int>(s.streamed_name.size() + 1) * (utf16 ? -1 : 1));
            for (unsigned char c : s.streamed_name)
                if (utf16)
                    table.put<std::uint16_t>(c);
                else
                    table.put(c);
            if (utf16)
                table.put<std::uint16_t>(0);
            else
                table.put<std::uint8_t>(0);
        } else {
            table.put(s.name_index);
            table.put(s.name_number);
        }
        name_bytes += at + table.data.size() - f.names.back();
        f.headers.push_back(at + table.data.size());
        table.i(s.components);
        table.i(s.pixel);
        const auto width = s.pixel == 13 ? 4 : s.pixel == 21 ? 2 : 1;
        table.i(width);
        table.i(width * s.components);
        table.i(1);
        table.i(static_cast<int>(s.bytes.size()));
        f.data.push_back(at + table.data.size());
        table.data.insert(table.data.end(), s.bytes.begin(), s.bytes.end());
    }
    auto &bytes = external ? f.base.storage.bulk.data : f.base.storage.package.data;
    bytes.erase(bytes.begin() + at, bytes.begin() + at + 4);
    bytes.insert(bytes.begin() + at, table.data.begin(), table.data.end());
    const auto delta = table.data.size() - 4;
    auto &package = f.base.storage.package;
    std::uint32_t stored;
    std::memcpy(&stored, package.data.data() + f.base.size, 4);
    package.patch(static_cast<int>(f.base.size), static_cast<int>(stored + delta - name_bytes));
    if (external) {
        std::int64_t bulk_bytes;
        std::memcpy(&bulk_bytes, package.data.data() + f.base.storage.bulk_row + 16, 8);
        bulk_bytes += delta;
        std::memcpy(package.data.data() + f.base.storage.bulk_row + 16, &bulk_bytes, 8);
    } else
        f.base.inline_count += delta;
    skeletal_export_size(f.base);
    return f;
}
} // namespace cyuediff::test
