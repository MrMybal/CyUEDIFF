#pragma once
#include <cyuediff/binary_layout.hpp>
#include <cyuediff/string_encoding.hpp>
namespace cyuediff::detail {
// Merkle identities avoid expanding shared references and exclude catalogue-local layout names.
// The validated topological order resolves each referenced layout before its parents.
struct DefinitionIdentity {
    const BinaryLayoutReadOptions &options;
    NarrowStringEncoding encoding;
    std::map<std::string, std::string> layouts;
    std::string hash(const Json &value) const {
        options.control.check();
        const auto text = value.dump();
        return sha256_bytes(std::vector<std::uint8_t>(text.begin(), text.end()), options.control);
    }
    std::string node(const Json &value) const {
        options.control.check();
        const auto type = value.at("type").get<std::string>();
        if (type == "ref")
            return layouts.at(value.at("layout").get<std::string>());
        Json normalized = {{"type", type}};
        if (type == "record") {
            normalized["fields"] = Json::array();
            for (const auto &field : value.at("fields"))
                normalized["fields"].push_back(
                    {{"name", field.at("name")}, {"valueSha256", node(field.at("value"))}});
        } else if (type == "array" || type == "bytes") {
            normalized["count"] = value.at("count");
            if (type == "array")
                normalized["elementSha256"] = node(value.at("element"));
        } else if (type == "optional") {
            normalized["presence"] = value.at("presence");
            normalized["valueSha256"] = node(value.at("value"));
        } else if (type == "string") {
            const auto selected = value.contains("encoding")
                                      ? parse_narrow_string_encoding(value.at("encoding").get<std::string>())
                                      : encoding;
            normalized["encoding"] = narrow_string_encoding_name(selected);
        } else if (type == "object")
            normalized["referenceIdentity"] =
                options.reference ? "host:" + options.reference_identity : "package-string/1";
        return hash(normalized);
    }
    std::string binding(const AssetDecoder &entry, const std::string &name) const {
        return hash({{"schema", "cyuediff.binary-layout-definition/2"},
                     {"interpreter", "cyuediff.binary-layout-reader/1"},
                     {"kind", entry.kind == DecoderKind::struct_value ? "struct" : "native-export"},
                     {"target", entry.target},
                     {"prefix", entry.prefix == DecoderPrefix::none ? "none" : "tagged-properties"},
                     {"programSha256", layouts.at(name)}});
    }
};
} // namespace cyuediff::detail
