#include <cyuediff/mesh_skin.hpp>
#include <iostream>
using namespace cyuediff;
int main(int argc, char **argv) {
    try {
        auto skin = std::make_shared<MeshSkin>();
        skin->bones = {{0, -1, "root"}};
        skin->profiles["Default"][0] = {{0, 65535}};
        MeshGeometry before;
        before.vertices = {{0, {0, 0, 0}}};
        before.skin = skin;
        const auto value = before.to_json();
        if (value["skin"]["vertices"]["0"]["Default"]["root"] != 65535 ||
            !compare_meshes(before, before).changes.empty())
            throw Error("consumer-skin", "Installed source skin API check failed");
        if (argc == 1) {
            std::cout << Json{{"verified", true}, {"skin", skin->to_json()}}.dump() << '\n';
            return 0;
        }
        if (argc < 2 || argc > 4)
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
        const auto mesh = decode_mesh(package.mesh_source(), options);
        if (!mesh.skin)
            throw Error("consumer-skin", "No supported skeletal source attributes");
        std::cout << Json{{"vertices", mesh.vertices.size()}, {"skin", mesh.skin->to_json()}}.dump(2) << '\n';
        return 0;
    } catch (const Error &e) {
        std::cerr << Json{{"error", e.code}, {"message", e.what()}}.dump() << '\n';
        return 2;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
