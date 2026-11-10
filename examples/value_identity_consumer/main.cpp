#include <cyuediff/core.hpp>
#include <iostream>
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw cyuediff::Error("argument", "Usage: value_identity_consumer source.uasset");
        cyuediff::ReadOptions options;
        options.value_identities.names = true;
        const auto package = cyuediff::Package::open(cyuediff::fs::u8path(argv[1]), options);
        std::cout << cyuediff::Json{{"snapshot", package.semantic_snapshot()},
                                    {"valueIdentities", package.value_identities}}
                         .dump(2)
                  << '\n';
        return package.value_identities.at("status") == "complete" ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
