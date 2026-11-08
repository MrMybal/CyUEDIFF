#include <cyuediff/core.hpp>
#include <iostream>
namespace {
int run(const std::vector<cyuediff::fs::path> &paths) {
    using namespace cyuediff;
    try {
        if (paths.empty() || paths.size() > 2) {
            std::cerr << "Usage: mesh_source_consumer ASSET [EXPLICIT_CODEC_MODULE]\n";
            return 2;
        }
        const auto package = Package::open(paths[0]);
        const auto &source = package.mesh_source();
        MeshDecodeOptions options;
        options.verify_raw_hash = true;
        if (paths.size() == 2)
            options.decoder = load_block_decoder(paths[1]);
        const auto geometry = decode_mesh(source, options);
        std::cout << Json{{"schema", "cyuediff.mesh-source-example/1"},
                          {"rawHashVerified", true},
                          {"object", source.object_path},
                          {"contentId", source.payload.content_id},
                          {"geometry", geometry.to_json()}}
                         .dump()
                  << '\n';
        return 0;
    } catch (const Error &error) {
        std::cerr << Json{{"error", error.code}, {"message", error.what()}}.dump() << '\n';
        return 2;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<cyuediff::fs::path> paths;
    for (int i = 1; i < argc; ++i)
        paths.emplace_back(argv[i]);
    return run(paths);
}
#else
int main(int argc, char **argv) {
    std::vector<cyuediff::fs::path> paths;
    for (int i = 1; i < argc; ++i)
        paths.push_back(cyuediff::fs::u8path(argv[i]));
    return run(paths);
}
#endif
