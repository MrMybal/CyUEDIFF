#include <cyuediff/compressed_curves.hpp>
#include <fstream>
#include <iostream>
using namespace cyuediff;
namespace {
std::vector<std::uint8_t> bytes(const fs::path &path, std::uint64_t maximum) {
    const auto n = fs::file_size(path);
    if (n > maximum)
        throw std::runtime_error("Example input exceeds read budget");
    std::vector<std::uint8_t> b(static_cast<std::size_t>(n));
    std::ifstream f(path, std::ios::binary);
    if (!f || (!b.empty() && !f.read(reinterpret_cast<char *>(b.data()), b.size())))
        throw std::runtime_error("Cannot read input");
    return b;
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 5) {
            std::cerr << "Usage: compressed_curve_consumer rich|indexable|uniform INPUT.json BUFFER.bin "
                         "OUTPUT.json\n";
            return 2;
        }
        CompressedCurveSource s;
        const std::string codec = argv[1];
        s.codec = codec == "rich"        ? CompressedCurveCodec::rich_curve
                  : codec == "indexable" ? CompressedCurveCodec::uniform_indexable
                  : codec == "uniform"   ? CompressedCurveCodec::uniformly_sampled
                                         : CompressedCurveCodec::unspecified;
        if (s.codec == CompressedCurveCodec::unspecified)
            throw std::runtime_error("Select an explicit supported codec");
        const auto meta_path = fs::u8path(argv[2]), buffer_path = fs::u8path(argv[3]),
                   target = fs::u8path(argv[4]);
        if (fs::exists(target) && (fs::equivalent(target, meta_path) || fs::equivalent(target, buffer_path)))
            throw std::runtime_error("Output cannot replace an input");
        const auto meta_bytes = bytes(meta_path, 16ull * 1024 * 1024);
        const auto meta = Json::parse(meta_bytes.begin(), meta_bytes.end());
        if (meta.at("schema") != "cyuediff.compressed-curve-input/1")
            throw std::runtime_error("Unsupported example metadata");
        s.names = meta.at("names").get<std::vector<std::string>>();
        s.buffer = bytes(buffer_path, 128ull * 1024 * 1024);
        const auto &times = meta.at("times");
        if (!times.is_array() || times.empty() || times.size() > 4096 ||
            std::uint64_t(times.size()) * s.names.size() > 200000)
            throw std::runtime_error("Example sample count exceeds budget");
        CompressedCurveOptions options;
        options.profile = CompressedCurveProfile::unreal_5_3_windows;
        const auto a = decode_compressed_curves(s, options);
        auto catalog = a.catalog();
        std::uint64_t points = 0;
        for (const auto &c : catalog.at("curves"))
            points += std::min<std::uint64_t>(2048, c.at("keys").get<std::uint64_t>());
        if (points > 200000)
            throw std::runtime_error("Example key preview exceeds budget");
        Json out = {
            {"metadata", std::move(catalog)}, {"samples", Json::array()}, {"keyPreviews", Json::array()}};
        for (const auto &time : times)
            out["samples"].push_back(a.sample(time.get<double>()));
        for (std::size_t i = 0; i < a.curve_count(); ++i)
            out["keyPreviews"].push_back(a.key_preview(i));
        std::ofstream f(target, std::ios::binary);
        if (!f || !(f << out.dump(2) << '\n'))
            throw std::runtime_error("Cannot write result");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
