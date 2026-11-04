#include "container_document.hpp"
#include <algorithm>
namespace cyuediff::desktop {
Json ContainerDocument::report() const {
    return {{"schema", "cyuediff.desktop-container/1"},
            {"metadata", metadata},
            {"inspection", inspection},
            {"zen", zen ? zen->to_json() : Json(nullptr)}};
}
std::shared_ptr<const ContainerDocument> load_container_document(const ContainerSource &source,
                                                                 const fs::path &codec,
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
} // namespace cyuediff::desktop
