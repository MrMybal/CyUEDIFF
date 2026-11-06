#include <cyuediff/mesh_morphs.hpp>
#include <iostream>
using namespace cyuediff;
int main(int argc, char **argv) {
    try {
        MeshGeometry mesh;
        mesh.vertices = {{2, {0, 0, 0}}, {7, {1, 0, 0}}};
        auto morphs = std::make_shared<MeshMorphs>();
        morphs->targets["Smile"].positions = {{2, {0, .5f, 0}}, {7, {0, 0, 0}}};
        mesh.morphs = morphs;
        morphs->validate(mesh);
        if (mesh.to_json()["morphs"]["targets"]["Smile"]["positions"]["2"][1] != .5 ||
            !compare_meshes(mesh, mesh).changes.empty())
            throw Error("consumer-morph", "Installed source morph API check failed");
        if (argc == 1) {
            std::cout << Json{{"verified", true}, {"morphs", morphs->to_json()}}.dump() << '\n';
            return 0;
        }
        if (argc > 4)
            throw Error("consumer-arguments", "Expected ASSET [PAYLOAD_ROOT [CODEC_MODULE]]");
        const auto package = Package::open(fs::u8path(argv[1]));
        if (!package.complete_input)
            throw Error("consumer-input", "Package is incomplete");
        MeshDecodeOptions options;
        options.verify_raw_hash = true;
        if (argc >= 3)
            options.virtualized_payload_root = fs::u8path(argv[2]);
        if (argc == 4)
            options.decoder = load_block_decoder(fs::u8path(argv[3]));
        const auto decoded = decode_mesh(package.mesh_source(), options);
        if (!decoded.morphs)
            throw Error("consumer-morph", "No supported source morph attributes");
        std::cout << Json{{"vertices", decoded.vertices.size()}, {"morphs", decoded.morphs->to_json()}}.dump(
                         2)
                  << '\n';
        return 0;
    } catch (const Error &e) {
        std::cerr << Json{{"error", e.code}, {"message", e.what()}}.dump() << '\n';
        return 2;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
