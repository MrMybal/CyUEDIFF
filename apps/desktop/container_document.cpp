#include "container_document.hpp"
#include <algorithm>
#include <map>
namespace cyuediff::desktop {
Json ContainerDocument::report() const {
    Json pairs = Json::array();
    for (std::size_t i = 0; i < members.size(); ++i) {
        const auto &row = members[i];
        pairs.push_back(
            {{"key", row.key},
             {"state", member_states[i]},
             {"beforeId", row.before ? Json(input->entries()[*row.before].id) : Json(nullptr)},
             {"afterId", row.after ? Json(after->input->entries()[*row.after].id) : Json(nullptr)}});
    }
    return {{"schema", "cyuediff.desktop-container/1"},
            {"metadata", metadata},
            {"inspection", inspection},
            {"zen", zen ? zen->to_json() : Json(nullptr)},
            {"after", after ? after->report() : Json(nullptr)},
            {"members", std::move(pairs)},
            {"comparison", comparison},
            {"selectedComparison", selected_comparison}};
}
namespace {
std::shared_ptr<const ContainerDocument> load_single(const ContainerSource &source, const fs::path &codec,
                                                     const OperationControl &control) {
    if (source.path.empty())
        throw Error("desktop-container", "Choose a Pak or IoStore TOC");
    if (!source.max_payload_bytes || source.max_payload_bytes > 256ull * 1024 * 1024)
        throw Error("desktop-container", "Member read budget must be between 1 byte and 256 MiB");
    control.checkpoint("Indexing container");
    ContainerOptions index;
    index.control = control;
    index.max_entries = 100'000;
    index.max_index_bytes = 64ull * 1024 * 1024;
    auto document = std::make_shared<ContainerDocument>();
    document->source = source;
    document->input = std::make_shared<Container>(Container::open(source.path, index));
    document->metadata = document->input->metadata();
    document->protected_files = document->input->files();
    if (!codec.empty())
        document->protected_files.push_back(codec);
    std::optional<Container> global;
    if (!source.script_objects.empty()) {
        global = Container::open(source.script_objects, index);
        document->protected_files.insert(document->protected_files.end(), global->files().begin(),
                                         global->files().end());
    }
    if (source.entry.empty()) {
        control.check();
        return document;
    }
    const auto &entries = document->input->entries();
    std::size_t found = entries.size();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        control.check();
        if (entries[i].id == source.entry || (!entries[i].path.empty() && entries[i].path == source.entry)) {
            if (found != entries.size())
                throw Error("container-entry", "Ambiguous member ID or virtual path");
            found = i;
        }
    }
    if (found == entries.size())
        throw Error("container-entry", "No matching container member");
    if (entries[found].deleted || entries[found].encrypted)
        throw Error("desktop-container", "Deleted or encrypted members cannot be inspected");
    ContainerReadOptions read;
    read.control = control;
    read.max_payload_bytes = source.max_payload_bytes;
    if (!codec.empty())
        read.decoder = container_block_decoder(load_block_decoder(codec));
    auto bytes = document->input->read(found, read);
    const auto digest = sha256_bytes(bytes, control);
    std::string prefix;
    const char *hex = "0123456789abcdef";
    for (std::size_t i = 0; i < std::min<std::size_t>(64, bytes.size()); ++i) {
        if (i)
            prefix += ' ';
        prefix += hex[bytes[i] >> 4];
        prefix += hex[bytes[i] & 15];
    }
    document->inspection = {{"coverage", "decoded-bytes"},  {"entry", entries[found].to_json()},
                            {"decodedBytes", bytes.size()}, {"decodedSha256", digest},
                            {"prefixHex", prefix},          {"prefixLimit", 64}};
    if (document->metadata["kind"] == "iostore" && entries[found].chunk_type == 1 &&
        source.profile != ZenProfile::unspecified) {
        ZenReadOptions zen;
        zen.profile = source.profile;
        zen.control = control;
        zen.max_package_bytes = source.max_payload_bytes;
        zen.max_header_bytes = 16ull * 1024 * 1024;
        zen.max_text_bytes = 16ull * 1024 * 1024;
        zen.max_entries = 100'000;
        if (global) {
            std::size_t scripts = global->entries().size();
            for (std::size_t i = 0; i < global->entries().size(); ++i) {
                control.check();
                if (global->entries()[i].chunk_type == 5 && !global->entries()[i].deleted) {
                    if (scripts != global->entries().size())
                        throw Error("zen-entry", "Ambiguous global script object table");
                    scripts = i;
                }
            }
            if (scripts == global->entries().size())
                throw Error("zen-entry", "No script object chunk in global TOC");
            zen.script_objects =
                std::make_shared<ScriptObjects>(ScriptObjects::parse(global->read(scripts, read), zen));
        }
        document->zen = ZenPackage::parse(bytes, zen);
    }
    control.checkpoint("Document ready");
    return document;
}
} // namespace
std::shared_ptr<const ContainerDocument> load_container_document(const ContainerSource &source,
                                                                 const fs::path &codec,
                                                                 const OperationControl &control) {
    if (source.after_path.empty()) {
        if (source.compare_payloads || !source.after_script_objects.empty() ||
            source.after_profile != ZenProfile::unspecified)
            throw Error("desktop-container", "After container options require an after container");
        return load_single(source, codec, control);
    }
    if (!source.max_comparison_bytes || source.max_comparison_bytes > 1024ull * 1024 * 1024)
        throw Error("desktop-container", "Comparison budget must be between 1 byte and 1 GiB");
    auto left = source;
    left.after_path.clear();
    left.after_script_objects.clear();
    left.after_profile = ZenProfile::unspecified;
    left.compare_payloads = false;
    left.entry.clear();
    auto right = left;
    right.path = source.after_path;
    right.script_objects = source.after_script_objects;
    right.profile = source.after_profile;
    auto document = std::make_shared<ContainerDocument>(*load_single(left, codec, control));
    document->after = load_single(right, codec, control);
    document->members = pair_container_entries(*document->input, *document->after->input, control);
    std::optional<std::size_t> selected;
    for (std::size_t i = 0; i < document->members.size(); ++i) {
        control.check();
        const auto &row = document->members[i];
        document->member_states.push_back(!row.before ? "added" : (!row.after ? "removed" : "unchecked"));
        auto matches = [&](const ContainerDocument &side, const std::optional<std::size_t> &entry) {
            return entry && (side.input->entries()[*entry].id == source.entry ||
                             (!side.input->entries()[*entry].path.empty() &&
                              side.input->entries()[*entry].path == source.entry));
        };
        if (!source.entry.empty() && (row.key == source.entry || matches(*document, row.before) ||
                                      matches(*document->after, row.after))) {
            if (selected)
                throw Error("container-entry", "Ambiguous comparison member ID or virtual path");
            selected = i;
        }
    }
    if (!source.entry.empty() && !selected)
        throw Error("container-entry", "No matching live comparison member");
    if (selected) {
        const auto &pair = document->members[*selected];
        if (pair.before) {
            left.entry = document->input->entries()[*pair.before].id;
            const auto loaded = load_single(left, codec, control);
            document->inspection = loaded->inspection;
            document->zen = loaded->zen;
        }
        if (pair.after) {
            right.entry = document->after->input->entries()[*pair.after].id;
            document->after = load_single(right, codec, control);
        }
        const auto &b = document->inspection;
        const auto &a = document->after->inspection;
        const std::string state =
            !pair.before
                ? "added"
                : (!pair.after ? "removed"
                               : (b.at("decodedSha256") == a.at("decodedSha256") ? "unchanged" : "modified"));
        document->member_states[*selected] = state;
        document->selected_comparison = {
            {"schema", "cyuediff.desktop-container-member-diff/1"},
            {"key", pair.key},
            {"state", state},
            {"coverage", "decoded-bytes"},
            {"externalBulkCompared", false},
            {"zen", document->zen && document->after->zen
                        ? compare_zen_packages(*document->zen, *document->after->zen, control)
                        : Json(nullptr)}};
    }
    if (source.compare_payloads) {
        ContainerReadOptions read;
        read.control = control;
        read.max_payload_bytes = source.max_payload_bytes;
        read.max_comparison_bytes = source.max_comparison_bytes;
        if (!codec.empty())
            read.decoder = container_block_decoder(load_block_decoder(codec));
        document->comparison = compare_containers(*document->input, *document->after->input, read);
        std::map<std::string, std::string> changes;
        for (const auto &change : document->comparison.at("changes"))
            changes.emplace(change.at("key").get<std::string>(), change.at("change").get<std::string>());
        for (std::size_t i = 0; i < document->members.size(); ++i) {
            control.check();
            auto found = changes.find(document->members[i].key);
            document->member_states[i] = found == changes.end() ? "unchanged" : found->second;
        }
    }
    document->source = source;
    document->protected_files.insert(document->protected_files.end(),
                                     document->after->protected_files.begin(),
                                     document->after->protected_files.end());
    control.checkpoint("Document ready");
    return document;
}
} // namespace cyuediff::desktop
