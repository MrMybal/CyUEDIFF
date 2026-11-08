#include <cyuediff/cooked_animation.hpp>
#include <cyuediff/schema_context.hpp>
#include <fstream>
#include <iostream>
using namespace cyuediff;
namespace {
std::size_t entry(const Container &c, const std::string &id) {
    std::size_t found = SIZE_MAX;
    for (std::size_t i = 0; i < c.entries().size(); ++i)
        if (c.entries()[i].id == id) {
            if (found != SIZE_MAX)
                throw std::runtime_error("Ambiguous entry");
            found = i;
        }
    if (found == SIZE_MAX)
        throw std::runtime_error("Entry not found");
    return found;
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 8) {
            std::cerr << "Usage: cooked_animation_consumer INPUT.utoc ID GLOBAL.utoc CONTEXT.json "
                         "SETTINGS_ID SKELETON_ID OUTPUT.json\n";
            return 2;
        }
        auto c = Container::open(fs::u8path(argv[1]));
        auto global = Container::open(fs::u8path(argv[3]));
        ZenReadOptions z;
        z.profile = ZenProfile::unreal_5_3;
        std::size_t script = SIZE_MAX;
        for (std::size_t i = 0; i < global.entries().size(); ++i)
            if (global.entries()[i].chunk_type == 5) {
                if (script != SIZE_MAX)
                    throw std::runtime_error("Ambiguous script table");
                script = i;
            }
        if (script == SIZE_MAX)
            throw std::runtime_error("Missing script table");
        z.script_objects = std::make_shared<ScriptObjects>(ScriptObjects::parse(global.read(script), z));
        const auto manifest = ZenSchemaManifest::read(fs::u8path(argv[4]));
        z.schema_context = manifest.load();
        CookedAnimationOptions o;
        o.profile = CookedAnimationProfile::unreal_5_3_windows;
        o.interpolation = CompressedAnimationInterpolation::linear;
        o.bone_settings = CookedAnimationDependency{c, entry(c, argv[5]), z, {}};
        o.skeleton = CookedAnimationDependency{c, entry(c, argv[6]), z, {}};
        const auto a = read_zen_animation(c, entry(c, argv[2]), z, {}, o);
        if (!a.decoded() || !a.skeleton_bound() || !a.frame_count())
            throw std::runtime_error("Animation or verified Skeleton is unavailable");
        auto info = a.to_json();
        const auto frames = *a.frame_count();
        const auto tracks = info.at("trackToSkeleton").size();
        if (frames > 10000 || std::uint64_t(2 * frames - 1) * tracks > 200000)
            throw std::runtime_error("Example sample output exceeds budget");
        Json out = {{"animation", std::move(info)}, {"samples", Json::array()}};
        for (std::uint32_t i = 0; i < 2 * frames - 1; ++i)
            out["samples"].push_back(a.sample(i * .5));
        const auto target = fs::u8path(argv[7]);
        std::vector<fs::path> protected_paths = c.files();
        const auto &global_paths = global.files();
        protected_paths.insert(protected_paths.end(), global_paths.begin(), global_paths.end());
        protected_paths.push_back(manifest.source_path());
        protected_paths.push_back(manifest.mapping_path());
        if (fs::exists(target))
            for (const auto &source : protected_paths)
                if (fs::equivalent(target, source))
                    throw std::runtime_error("Output cannot replace an input");
        std::ofstream f(target, std::ios::binary);
        if (!f || !(f << out.dump(2) << '\n'))
            throw std::runtime_error("Cannot write result");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
