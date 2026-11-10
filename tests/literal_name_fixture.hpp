#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff::test {
// Authoring contract for these original hand-built fixtures: NameProperty/EnumProperty
// strings are literal plain names with internal number zero. This is not a wire decoder.
inline Package literal_name_fixture(Package p) {
    Json names = Json::object();
    auto token = [](const std::string &s) {
        std::string out;
        for (char c : s)
            out += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
        return out;
    };
    std::function<void(const Json &, const std::string &)> visit;
    visit = [&](const Json &v, const std::string &path) {
        if (v.is_object() && v.contains("type") && v.contains("value")) {
            const auto &type = v.at("type");
            const auto kind = type.is_string() ? type.get<std::string>() : type.value("name", "");
            if ((kind == "NameProperty" || kind == "EnumProperty") && v.at("value").is_string())
                names[path + "/value"] = {
                    {"base", v.at("value")}, {"number", 0}, {"source", "original-fixture-literal"}};
        }
        if (v.is_object())
            for (auto i = v.begin(); i != v.end(); ++i)
                visit(*i, path + "/" + token(i.key()));
        else if (v.is_array())
            for (std::size_t i = 0; i < v.size(); ++i)
                visit(v[i], path + "/" + std::to_string(i));
    };
    for (const auto &e : p.exports)
        visit(e.properties, "/objects/" + token(e.path) + "/properties");
    p.name_values = {{"schema", "cyuediff.name-values/1"},
                     {"root", "semantic-snapshot"},
                     {"status", "complete"},
                     {"names", std::move(names)}};
    return p;
}
} // namespace cyuediff::test
