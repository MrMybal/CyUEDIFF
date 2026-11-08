#include <cyuediff/nanite_geometry.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
using namespace cyuediff;
namespace {
std::uint32_t number(const Json &j, std::uint32_t maximum) {
    if (!j.is_number_integer() || j < 0 || j > maximum)
        throw std::runtime_error("Invalid bounded manifest integer");
    return j.get<std::uint32_t>();
}
std::vector<std::uint8_t> read(const fs::path &p, std::uint64_t maximum) {
    const auto n = fs::file_size(p);
    if (n > maximum)
        throw std::runtime_error("Input exceeds example read budget");
    std::vector<std::uint8_t> b(static_cast<std::size_t>(n));
    std::ifstream f(p, std::ios::binary);
    if (!f.read(reinterpret_cast<char *>(b.data()), static_cast<std::streamsize>(n)))
        throw std::runtime_error("Cannot read input");
    return b;
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 3) {
            std::cerr << "Usage: nanite_geometry_consumer PAGES.json OUTPUT.json\n";
            return 2;
        }
        const auto manifest_path = fs::absolute(fs::u8path(argv[1])),
                   target = fs::absolute(fs::u8path(argv[2]));
        const auto bytes = read(manifest_path, 8ull * 1024 * 1024);
        const auto manifest = Json::parse(bytes);
        if (!manifest.is_object() || manifest.size() != 3 ||
            manifest.at("schema") != "cyuediff.nanite-pages/1" || !manifest.at("pages").is_array() ||
            manifest.at("pages").empty() || manifest.at("pages").size() > 4096)
            throw std::runtime_error("Invalid explicit page manifest");
        std::vector<fs::path> protected_files{manifest_path};
        NaniteGeometrySource source;
        source.root_pages =
            number(manifest.at("rootPages"), static_cast<std::uint32_t>(manifest.at("pages").size()));
        std::uint64_t total = 0;
        for (const auto &row : manifest.at("pages")) {
            if (!row.is_object() || row.size() != 3 || !row.at("dependencies").is_array() ||
                row.at("dependencies").size() > 4096)
                throw std::runtime_error("Invalid explicit page row");
            const auto path = fs::u8path(row.at("file").get<std::string>());
            if (!path.is_absolute())
                throw std::runtime_error("Use absolute stored page paths");
            protected_files.push_back(path);
            NanitePageSource p;
            p.flags = static_cast<std::uint8_t>(number(row.at("flags"), 1));
            for (const auto &dep : row.at("dependencies"))
                p.dependencies.push_back(number(dep, 4095));
            p.bytes = read(path, std::min<std::uint64_t>(262144 + 6152, 512ull * 1024 * 1024 - total));
            total += p.bytes.size();
            if (total > 512ull * 1024 * 1024)
                throw std::runtime_error("Page inputs exceed aggregate example budget");
            source.pages.push_back(std::move(p));
        }
        if (fs::exists(target))
            for (const auto &p : protected_files)
                if (fs::equivalent(target, p))
                    throw std::runtime_error("Output cannot replace an input");
        NaniteGeometryOptions options;
        options.profile = NaniteProfile::unreal_5_3_windows;
        options.max_vertices = 200000;
        options.max_triangles = 200000;
        const auto model = decode_nanite_geometry(source, options);
        source.pages.clear();
        Json out = {{"catalog", model.catalog()}, {"pages", Json::array()}};
        for (std::size_t pi = 0; pi < model.page_count(); ++pi) {
            Json page = Json::array();
            for (std::size_t ci = 0; ci < model.cluster_count(pi); ++ci) {
                auto meta = model.cluster_metadata(pi, ci);
                Json c = {{"metadata", meta},
                          {"positions", Json::array()},
                          {"quantizedPositions", Json::array()},
                          {"triangles", Json::array()}};
                for (std::size_t vi = 0; vi < meta.at("vertices").get<std::size_t>(); ++vi) {
                    c["positions"].push_back(model.position(pi, ci, vi));
                    c["quantizedPositions"].push_back(model.quantized_position(pi, ci, vi));
                }
                for (std::size_t ti = 0; ti < meta.at("triangles").get<std::size_t>(); ++ti) {
                    const auto t = model.triangle(pi, ci, ti);
                    c["triangles"].push_back({{"vertices", t.vertices}, {"material", t.material}});
                }
                page.push_back(std::move(c));
            }
            out["pages"].push_back(std::move(page));
        }
        std::ofstream f(target, std::ios::binary);
        if (!f || !(f << out.dump(2) << '\n'))
            throw std::runtime_error("Cannot write result");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
