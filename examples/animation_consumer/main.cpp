#include <cyuediff/animation.hpp>
#include <iostream>
int main(int argc, char **argv) {
    using namespace cyuediff;
    try {
        if (argc < 2 || argc > 3)
            throw Error("arguments", "Expected one or two loose asset paths");
        const auto before = Package::open(fs::u8path(argv[1])),
                   after = argc == 3 ? Package::open(fs::u8path(argv[2])) : before;
        std::cout << Json{{"snapshot", animation_snapshot(before)},
                          {"frame", animation_frame(before, 0)},
                          {"comparison", compare_animation_models(before, after).to_json()}}
                         .dump(2)
                  << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
