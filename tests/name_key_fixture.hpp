#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff::test {
// Expected wire contract, independent of the production encoder and journal.
inline std::string test_name_key_id(const std::string &base, std::uint32_t number = 0) {
    const auto display = base + (number ? "_" + std::to_string(number - 1) : "");
    return "ue-name/1:" + Json::array({display, Json{{"", {{"base", base}, {"number", number}}}}}).dump();
}
inline std::string test_key_token(const std::string &id) {
    std::string out;
    for (const char c : id)
        out += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
    return out;
}
} // namespace cyuediff::test
