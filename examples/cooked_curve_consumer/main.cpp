#include <cyuediff/cooked_animation.hpp>
#include <cyuediff/schema_context.hpp>
#include <fstream>
#include <iostream>
using namespace cyuediff;
namespace {
std::size_t entry(const Container &c, const std::string &id) {
    std::size_t index = SIZE_MAX;
    for (std::size_t i = 0; i < c.entries().size(); ++i)
        if (c.entries()[i].id == id) {
            if (index != SIZE_MAX)
                throw std::runtime_error("Ambiguous member ID");
            index = i;
        }
    if (index == SIZE_MAX)
        throw std::runtime_error("Member ID not found");
    return index;
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 8) {
            std::cerr << "Usage: cooked_curve_consumer INPUT.utoc ID GLOBAL.utoc CONTEXT.json "
                         "CURVE_SETTINGS_ID TIMES.json OUTPUT.json\n";
            return 2;
        }
        const auto times_path = fs::u8path(argv[6]), target = fs::u8path(argv[7]);
        auto c = Container::open(fs::u8path(argv[1]));
        auto global = Container::open(fs::u8path(argv[3]));
        const auto manifest = ZenSchemaManifest::read(fs::u8path(argv[4]));
        auto protected_files = c.files();
        const auto &gfiles = global.files();
        protected_files.insert(protected_files.end(), gfiles.begin(), gfiles.end());
        protected_files.push_back(manifest.source_path());
        protected_files.push_back(manifest.mapping_path());
        protected_files.push_back(times_path);
        if (fs::exists(target))
            for (const auto &p : protected_files)
                if (fs::equivalent(target, p))
                    throw std::runtime_error("Output cannot replace an input");
        if (fs::file_size(times_path) > 16ull * 1024 * 1024)
            throw std::runtime_error("Time input exceeds read budget");
        std::ifstream f(times_path, std::ios::binary);
        Json times;
        f >> times;
        if (!times.is_array() || times.empty() || times.size() > 4096)
            throw std::runtime_error("Select 1..4096 explicit scalar times");
        ZenReadOptions z;
        z.profile = ZenProfile::unreal_5_3;
        std::size_t script = SIZE_MAX;
        for (std::size_t i = 0; i < global.entries().size(); ++i)
            if (global.entries()[i].chunk_type == 5 && !global.entries()[i].deleted) {
                if (script != SIZE_MAX)
                    throw std::runtime_error("Ambiguous script table");
                script = i;
            }
        if (script == SIZE_MAX)
            throw std::runtime_error("No global script table");
        z.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(global.read(script), z));
        z.schema_context = manifest.load();
        CookedAnimationOptions o;
        o.profile = CookedAnimationProfile::unreal_5_3_windows;
        o.max_curves = 10000;
        o.curve_settings = CookedAnimationDependency{c, entry(c, argv[5]), z, {}};
        const auto a = read_zen_animation(c, entry(c, argv[2]), z, {}, o);
        const auto curves = a.compressed_curves();
        if (!curves && a.curve_information().value("status", std::string{}) != "empty")
            throw std::runtime_error("Verified scalar curves unavailable");
        const auto count = curves ? curves->curve_count() : 0;
        if (count * std::uint64_t(times.size()) > 200000)
            throw std::runtime_error("Example scalar sample output exceeds budget");
        std::uint64_t points = 0;
        if (curves)
            for (std::size_t i = 0; i < count; ++i)
                points +=
                    std::min<std::uint64_t>(2048, curves->curve_metadata(i).at("keys").get<std::uint64_t>());
        if (points > 200000)
            throw std::runtime_error("Example key preview output exceeds budget");
        Json out = {{"animation", a.to_json()}, {"samples", Json::array()}, {"keyPreviews", Json::array()}};
        for (const auto &time : times)
            out["samples"].push_back(a.sample_curves(time.get<double>()));
        if (curves)
            for (std::size_t i = 0; i < count; ++i)
                out["keyPreviews"].push_back(curves->key_preview(i));
        std::ofstream result(target, std::ios::binary);
        if (!result || !(result << out.dump(2) << '\n'))
            throw std::runtime_error("Cannot write result");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
