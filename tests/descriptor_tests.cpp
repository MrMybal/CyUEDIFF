#include "../src/descriptor.hpp"
#include <iostream>
using namespace cyuediff;
int main() {
    int checks = 0;
    auto check = [&](bool value) {
        ++checks;
        if (!value)
            throw std::runtime_error("Descriptor result mismatch");
    };
    auto reject = [&](const std::string &value) {
        bool failed = false;
        try {
            detail::parse_descriptor(value);
        } catch (const std::exception &) {
            failed = true;
        }
        check(failed);
    };
    try {
        check(detail::parse_descriptor(R"({"FileVersion":3})")["FileVersion"] == 3);
        check(detail::parse_descriptor("// header\n{\"x\":1}// EOF")["x"] == 1);
        check(detail::parse_descriptor(R"({"Modules":[{"Name":"Module"},]})")["Modules"].size() == 1);
        check(detail::parse_descriptor(R"({"x":[1,/* comma before close */]})")["x"] == Json::array({1}));
        check(detail::parse_descriptor(R"({"x":[[1,],{"y":[true,false,null,]},]})")["x"][1]["y"].size() == 3);
        check(detail::parse_descriptor(R"({"x":"literal // /* ,]", "y":[], "z":["a\\b\"c",]})")["x"] ==
              "literal // /* ,]");
        check(detail::parse_descriptor(std::string("\xef\xbb\xbf") + R"({"x":"été"})")["x"] == "été");
        reject(R"({"x":1,})");
        reject(R"({"x":[,]})");
        reject(R"({"x":[1,,]})");
        reject(R"({"x":[1,/* unclosed})");
        reject(R"({"x":"unclosed})");
        reject(R"({"x":[1,]} garbage)");
        reject(R"({"x":[1,}})");
        reject(std::string(129, '[') + "0" + std::string(129, ']'));
        std::cout << checks << " descriptor checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
