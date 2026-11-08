#include "nanite_geometry_fixture.hpp"
#include <iostream>
#include <fstream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
unsigned checks = 0;
void check(bool b, const char *m) {
    ++checks;
    if (!b)
        throw std::runtime_error(m);
}
template <class F> void refused(F f) {
    try {
        f();
    } catch (const Error &) {
        ++checks;
        return;
    }
    throw std::runtime_error("Invalid Nanite data accepted");
}
} // namespace
int main() {
    try {
        NaniteGeometryOptions o;
        o.profile = NaniteProfile::unreal_5_3_windows;
        auto s = nanite_source();
        auto m = decode_nanite_geometry(s, o);
        check(m.page_count() == 1 && m.cluster_count(0) == 1, "Original page and cluster decoded");
        check(m.position(0, 0, 0) == std::array<float, 3>{-2, -2, 0} &&
                  m.position(0, 0, 2) == std::array<float, 3>{-2, 0, 0},
              "Quantized absolute positions decoded");
        check(m.triangle(0, 0, 0).vertices == std::array<std::uint32_t, 3>{0, 1, 2} &&
                  m.triangle(0, 0, 0).material == 5,
              "Oriented topology and material retained");
        check(m.catalog()["fullLeafTriangles"] == 1 &&
                  m.cluster_metadata(0, 0)["storedAttributesDecoded"] == false,
              "Explicit full-leaf and opaque attribute coverage");
        s.pages[0].bytes.clear();
        check(m.position(0, 0, 1)[0] == 0, "Owned immutable geometry survives source mutation");
        s = nanite_source();
        auto local = s;
        local.pages[0] = nanite_page(2);
        auto l = decode_nanite_geometry(local, o);
        check(l.position(0, 0, 0) == l.position(0, 0, 1),
              "Local reference indexes coded vertices rather than original vertices");
        auto external = s;
        external.pages.push_back(nanite_page(1));
        auto e = decode_nanite_geometry(external, o);
        check(e.position(1, 0, 2) == m.position(0, 0, 2),
              "Relative parent references use fully reconstructed original vertex indices");
        auto reversed = external;
        reversed.pages.push_back(nanite_page(1));
        reversed.pages[1].dependencies = {2};
        auto r = decode_nanite_geometry(reversed, o);
        check(r.position(1, 0, 0) == m.position(0, 0, 0), "Out-of-order dependencies decoded topologically");
        for (std::size_t n = 0; n < s.pages[0].bytes.size(); ++n) {
            auto prefix = s;
            prefix.pages[0].bytes.resize(n);
            refused([&] { decode_nanite_geometry(prefix, o); });
        }
        for (auto offset :
             {0u, 2u, 8u + 4u, 8u + 20u, 8u + 24u, 8u + 28u, 8u + 32u, 8u + 36u, 8u + 40u, 8u + 44u, 8u + 48u,
              8u + 52u, 8u + 64u, 8u + 80u, 8u + 84u, 8u + 108u, 8u + 160u, 8u + 164u}) {
            auto invalid = s;
            nanite_patch(invalid.pages[0].bytes, offset, 0xffffffffu);
            refused([&] { decode_nanite_geometry(invalid, o); });
        }
        auto corrupt = s;
        nanite_patch(corrupt.pages[0].bytes, 8 + 240, 1);
        refused([&] { decode_nanite_geometry(corrupt, o); });
        corrupt = external;
        corrupt.pages[1].dependencies = {1};
        refused([&] { decode_nanite_geometry(corrupt, o); });
        corrupt = reversed;
        corrupt.pages[2].dependencies = {1};
        refused([&] { decode_nanite_geometry(corrupt, o); });
        corrupt = external;
        corrupt.pages[1].dependencies = {0, 0};
        refused([&] { decode_nanite_geometry(corrupt, o); });
        corrupt = external;
        corrupt.pages[1].flags = 0;
        refused([&] { decode_nanite_geometry(corrupt, o); });
        corrupt = external;
        nanite_patch(corrupt.pages[1].bytes, 8 + 240, 512);
        refused([&] { decode_nanite_geometry(corrupt, o); });
        auto limited = o;
        limited.max_read_bytes = 1;
        refused([&] { decode_nanite_geometry(s, limited); });
        limited = o;
        limited.max_output_bytes = 1;
        refused([&] { decode_nanite_geometry(s, limited); });
        limited = o;
        limited.max_vertices = 2;
        refused([&] { decode_nanite_geometry(s, limited); });
        limited = o;
        limited.max_triangles = 0;
        refused([&] { decode_nanite_geometry(s, limited); });
        limited = o;
        limited.max_clusters = 0;
        refused([&] { decode_nanite_geometry(s, limited); });
        limited = o;
        limited.max_pages = 0;
        refused([&] { decode_nanite_geometry(s, limited); });
        limited = o;
        limited.profile = NaniteProfile::unspecified;
        refused([&] { decode_nanite_geometry(s, limited); });
        limited = o;
        limited.control.cancelled = [] { return true; };
        refused([&] { decode_nanite_geometry(s, limited); });
        OperationControl cancelled;
        cancelled.cancelled = [] { return true; };
        refused([&] { m.position(0, 0, 0, cancelled); });
        refused([&] { m.catalog(cancelled); });
        refused([&] { m.position(0, 0, 3); });
        refused([&] { m.triangle(0, 0, 1); });
        refused([&] { m.cluster_metadata(0, 1); });
        auto chain = external;
        for (unsigned i = 2; i < 384; ++i) {
            chain.pages.push_back(nanite_page(1));
            chain.pages[i - 1].dependencies = {i};
        }
        chain.pages.back().dependencies = {0};
        auto graph = decode_nanite_geometry(chain, o);
        check(graph.position(1, 0, 2) == m.position(0, 0, 2) && graph.page_count() == 384,
              "Long reversed graph uses bounded iterative ordering");
        std::ifstream reference_file(CYUEDIFF_NANITE_REFERENCE, std::ios::binary);
        Json reference;
        reference_file >> reference;
        check(reference.at("schema") == "cyuediff.nanite-strip-reference/1",
              "Actual numerical reference schema");
        for (const auto &row : reference.at("cases")) {
            auto original = nanite_source();
            original.pages[0].bytes = row.at("buffer").get<std::vector<std::uint8_t>>();
            const auto decoded = decode_nanite_geometry(original, o);
            const auto meta = decoded.cluster_metadata(0, 0);
            check(meta.at("triangles") == row.at("actualTriangles").size(),
                  "Actual Engine strip reference triangle count");
            for (std::size_t i = 0; i < row.at("actualTriangles").size(); ++i) {
                check(Json(decoded.triangle(0, 0, i).vertices) == row.at("actualTriangles").at(i),
                      "Original strip topology matches actual Engine decoder");
                check(decoded.triangle(0, 0, i).material == row.at("expectedMaterials").at(i),
                      "Original inline/slow material partitions preserved");
            }
            for (std::size_t i = 0; i < meta.at("vertices").get<std::size_t>(); ++i)
                check(decoded.position(0, 0, i) ==
                          std::array<float, 3>{float(i % 16) - 8, float(i / 16) - 8, -2},
                      "Original scalar grid positions independent of strip coding");
        }
        std::cout << checks << " Nanite geometry checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "check " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
