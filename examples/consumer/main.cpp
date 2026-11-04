#include <cyuediff/core.hpp>
#include <iostream>
int main(int argc, char **argv) {
    if (argc < 2 || argc > 4)
        return 2;
    try {
        auto package = cyuediff::Package::open(std::filesystem::u8path(argv[1]));
        if (argc >= 3 && std::filesystem::u8path(argv[2]).extension() == ".obj") {
            cyuediff::MeshDecodeOptions options;
            if (argc == 4)
                options.decoder = cyuediff::load_block_decoder(std::filesystem::u8path(argv[3]));
            auto mesh = cyuediff::decode_mesh(package.mesh_source(), options);
            cyuediff::write_obj(std::filesystem::u8path(argv[2]), mesh);
        } else if (argc >= 3) {
            if (package.textures.size() != 1)
                throw std::runtime_error("Choose a package with one Texture2D source");
            cyuediff::TextureDecodeOptions options;
            if (argc == 4)
                options.decoder = cyuediff::load_block_decoder(std::filesystem::u8path(argv[3]));
            auto image = cyuediff::decode_texture(package.textures.front(), 0, options);
            cyuediff::write_png(std::filesystem::u8path(argv[2]), image);
        }
        std::cout << package.to_json().dump(2) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
