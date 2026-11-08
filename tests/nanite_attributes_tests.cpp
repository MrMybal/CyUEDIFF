#include <cyuediff/nanite_geometry.hpp>
#include <fstream>
#include <iostream>
#include <cmath>
using namespace cyuediff;
namespace {
unsigned checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void refused(F run) {
    try {
        run();
    } catch (const Error &) {
        ++checks;
        return;
    }
    throw std::runtime_error("Invalid Nanite attribute request accepted");
}
NaniteGeometrySource source(const Json &row) {
    NaniteGeometrySource out;
    out.root_pages = row.at("rootPages");
    for (const auto &p : row.at("pages")) {
        NanitePageSource page;
        page.bytes = p.at("bytes").get<std::vector<std::uint8_t>>();
        page.dependencies = p.at("dependencies").get<std::vector<std::uint32_t>>();
        page.flags = p.at("flags");
        out.pages.push_back(std::move(page));
    }
    return out;
}
void patch(std::vector<std::uint8_t> &data, std::size_t offset, std::uint32_t n) {
    for (unsigned k = 0; k < 4; ++k)
        data.at(offset + k) = std::uint8_t(n >> (8 * k));
}
void near(const Json &a, const Json &b) {
    if (b.is_null()) {
        check(a.is_null(), "Absent/zero-bit channel remains unavailable");
        return;
    }
    check(a.size() == b.size(), "Attribute query cardinality");
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].is_array())
            near(a[i], b[i]);
        else
            check(std::abs(a[i].get<double>() - b[i].get<double>()) < 3e-6,
                  "Actual native shader attribute value");
    }
}
} // namespace
int main() {
    try {
        std::ifstream file(CYUEDIFF_NANITE_ATTRIBUTES_REFERENCE);
        Json reference;
        file >> reference;
        check(reference.at("schema") == "cyuediff.nanite-attributes-numeric-reference/1",
              "Original actual-shader fixture schema");
        NaniteGeometryOptions options;
        options.profile = NaniteProfile::unreal_5_3_windows;
        options.attributes = true;
        for (const auto &row : reference.at("cases")) {
            auto s = source(row);
            auto model = decode_nanite_geometry(s, options);
            check(model.attributes_decoded(), "Explicit attribute request retained");
            for (std::size_t p = 0; p < s.pages.size(); ++p) {
                check(model.cluster_metadata(p, 0).at("storedAttributesDecoded") == true,
                      "Cluster attribute coverage");
                for (unsigned v = 0; v < 3; ++v) {
                    const auto actual = model.attributes(p, 0, v).to_json();
                    const auto &expected = row.at("expected")[p][v];
                    for (const char *key : {"packedNormal", "packedTangent", "color", "packedUVs"})
                        check(actual.at(key) == expected.at(key),
                              "Actual native GPU transcode packed attribute identity");
                    for (const char *key : {"normal", "tangent", "uvs"})
                        near(actual.at(key), expected.at(key));
                }
            }
            const auto before = model.attributes(0, 0, 0).to_json();
            s.pages.clear();
            check(model.attributes(0, 0, 0).to_json() == before,
                  "Immutable attribute values survive source mutation");
        }
        auto s = source(reference.at("cases")[4]);
        auto disabled = options;
        disabled.attributes = false;
        auto opaque = decode_nanite_geometry(s, disabled);
        check(!opaque.attributes_decoded(), "Default explicit opaque attribute contract preserved");
        refused([&] { opaque.attributes(0, 0, 0); });
        auto bounded = options;
        bounded.max_output_bytes = opaque.accounted_output_bytes();
        refused([&] { decode_nanite_geometry(s, bounded); });
        auto model = decode_nanite_geometry(s, options);
        refused([&] { model.attributes(0, 0, 3); });
        refused([&] { model.attributes(1, 0, 0); });
        OperationControl cancelled;
        cancelled.cancelled = [] { return true; };
        refused([&] { model.attributes(0, 0, 0, cancelled); });
        for (std::size_t n = 0; n < s.pages[0].bytes.size(); ++n) {
            auto truncated = s;
            truncated.pages[0].bytes.resize(n);
            refused([&] { decode_nanite_geometry(truncated, options); });
        }
        for (const auto &change :
             std::vector<std::pair<unsigned, std::uint32_t>>{{8 + 80 + 12, 0x9999},
                                                             {8 + 80 + 28, 0xff000000},
                                                             {8 + 80 + 84, 128u | (3u << 26)},
                                                             {8 + 192 + 24, 0x80000000u},
                                                             {8 + 80 + 8, 0xffffffffu}}) {
            auto corrupt = s;
            patch(corrupt.pages[0].bytes, change.first, change.second);
            refused([&] { decode_nanite_geometry(corrupt, options); });
        }
        for (const auto &row : reference.at("cases"))
            if (row.at("name") == "parent-scale-0-1") {
                auto chain = source(row);
                const auto expected = decode_nanite_geometry(chain, options).attributes(1, 0, 2).to_json();
                const auto child = chain.pages[1];
                while (chain.pages.size() < 384)
                    chain.pages.push_back(child);
                for (std::size_t p = 1; p < chain.pages.size(); ++p)
                    chain.pages[p].dependencies = {
                        static_cast<std::uint32_t>(p + 1 < chain.pages.size() ? p + 1 : 0)};
                auto long_model = decode_nanite_geometry(chain, options);
                check(long_model.attributes(1, 0, 2).to_json() == expected,
                      "Long reversed parent graph preserves fully transcoded attributes");
            }
        std::cout << checks << " Nanite attribute/shader reference checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "check " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
