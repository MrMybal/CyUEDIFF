#include <cyuediff/content_hash.hpp>
#include <cyuediff/virtualized.hpp>
#include <iostream>
using namespace cyuediff;
int main(int argc, char **argv) {
    try {
        if (blake3_bytes({}) != "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262")
            throw Error("consumer-hash", "BLAKE3 startup vector failed");
        if (argc == 1) {
            std::cout << "Verified installed Core BLAKE3; pass ASSET PAYLOAD_ROOT [CODEC_MODULE]\n";
            return 0;
        }
        if (argc != 3 && argc != 4)
            throw Error("consumer-arguments", "Expected ASSET PAYLOAD_ROOT [CODEC_MODULE]");
        auto package = Package::open(fs::u8path(argv[1]));
        if (!package.complete_input)
            throw Error("consumer-input", "Package is incomplete");
        BulkDecodeOptions options;
        options.virtualized_payload_root = fs::u8path(argv[2]);
        options.verify_raw_hash = true;
        if (argc == 4)
            options.decoder = load_block_decoder(fs::u8path(argv[3]));
        Json payloads = Json::array();
        auto read = [&](const BulkPayload &source) {
            if (source.storage == "empty")
                return;
            const auto bytes = decode_bulk_payload(source, options);
            payloads.push_back({{"id", source.content_id},
                                {"storage", source.storage},
                                {"rawBytes", bytes.size()},
                                {"blake3", blake3_bytes(bytes)}});
        };
        for (const auto &texture : package.textures)
            read(texture.payload);
        for (const auto &mesh : package.mesh_sources)
            read(mesh.payload);
        if (payloads.empty())
            throw Error("consumer-input", "No supported editor payloads in this package");
        std::cout << Json{{"schema", "cyuediff.payload-consumer/1"}, {"payloads", payloads}}.dump(2) << '\n';
        return 0;
    } catch (const Error &e) {
        std::cerr << Json{{"error", e.code}, {"message", e.what()}}.dump() << '\n';
        return 2;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
