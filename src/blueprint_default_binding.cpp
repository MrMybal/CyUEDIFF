#include "blueprint_default_binding.hpp"
#include <set>
namespace cyuediff::detail {
namespace {
void need(bool yes, const char *reason) {
    if (!yes)
        throw Error("default-context", reason);
}
std::string word(const Json &j) {
    need(j.is_string(), "Expected Blueprint identity text");
    auto v = j.get<std::string>();
    need(!v.empty() && v.size() <= 32768, "Invalid Blueprint identity text");
    for (unsigned char c : v)
        need(c >= 32 && c != 127, "Invalid Blueprint identity control");
    return v;
}
std::string package(const std::string &path) {
    auto dot = path.rfind('.');
    need(path.size() > 1 && path[0] == '/' && dot != std::string::npos && dot + 1 < path.size() &&
             path.find('\\') == std::string::npos && path.find(':') == std::string::npos,
         "Invalid Blueprint class path");
    return path.substr(0, dot);
}
std::string cdo(const std::string &path) {
    auto dot = path.rfind('.');
    return package(path) + ".Default__" + path.substr(dot + 1);
}
} // namespace
Json validate_blueprint_default_binding(const DefaultSnapshot &snapshot, const Json &source,
                                        const Json &environment, const OperationControl &control) {
    const auto &p = snapshot.provenance();
    const auto cls = word(p.at("object").at("class"));
    need(p.at("blueprintClass") == true && !p.at("classGeneratedBy").is_null() &&
             cls.compare(0, 8, "/Script/") != 0,
         "Expected a prepared generated Blueprint class");
    const auto kind = word(source.at("objectKind"));
    need(kind == "instance" || kind == "class-default", "Unsupported Blueprint source object kind");
    const auto &binding = environment.at("blueprintDependencies");
    need(binding.at("format") == "cyuediff.blueprint-default-dependencies/1" &&
             binding.at("complete") == true,
         "Incomplete Blueprint package binding");
    const auto &classes = binding.at("classes"), &packages = binding.at("packages"),
               &files = environment.at("files");
    need(classes.is_array() && classes.size() >= 2 && classes.size() <= 64 && packages.is_object() &&
             !packages.empty() && packages.size() <= 256,
         "Invalid Blueprint dependency inventory");
    std::set<std::string> class_names, reachable, ids;
    std::vector<std::string> pending;
    for (std::size_t i = 0; i < classes.size(); ++i) {
        control.check();
        const auto &row = classes.at(i);
        const auto name = word(row.at("class"));
        need(class_names.insert(name).second, "Repeated Blueprint class ancestor");
        need(row.at("defaultObject").at("path") == cdo(name) && row.at("defaultObject").at("class") == name,
             "Blueprint ancestor default identity differs");
        need(snapshot.schemas().schema(name).super == row.at("super"),
             "Blueprint declaration ancestry differs");
        need(row.at("super") == (i + 1 < classes.size() ? classes.at(i + 1).at("class") : Json("")),
             "Blueprint class hierarchy is not closed");
        need(row.at("blueprint").is_boolean(), "Expected class producer kind");
        if (row.at("blueprint") == true) {
            need(name.compare(0, 8, "/Script/") != 0 && !row.at("generatedBy").is_null() &&
                     package(word(row.at("generatedBy").at("path"))) == package(name),
                 "Blueprint class package/producer differs");
            need(name.size() > 2 && name.substr(name.size() - 2) == "_C",
                 "Unsupported Blueprint generated class name");
            const auto expected =
                package(name) + "." + name.substr(name.rfind('.') + 1, name.size() - name.rfind('.') - 3);
            need(row.at("generatedBy").at("path") == expected, "Blueprint generating asset identity differs");
            pending.push_back(package(name));
        } else {
            need(name.compare(0, 8, "/Script/") == 0 && row.at("generatedBy").is_null(),
                 "Unexpected native Blueprint ancestor");
            const auto module = name.substr(8, name.find('.', 8) - 8);
            need(files.contains("module:" + module) && files.at("module:" + module).at("role") == "module",
                 "Blueprint native ancestor module is unrecorded");
        }
    }
    need(classes.at(0).at("class") == cls && classes.at(0).at("generatedBy") == p.at("classGeneratedBy"),
         "Blueprint capture root differs");
    for (std::size_t n = 0; n < pending.size(); ++n) {
        control.check();
        const auto name = pending[n];
        if (!reachable.insert(name).second)
            continue;
        need(packages.contains(name), "Unrecorded Blueprint imported package");
        const auto &row = packages.at(name);
        const auto id = "package:" + name;
        need(row.at("fileId") == id && files.contains(id) && files.at(id).at("role") == "asset-package" &&
                 files.at(id).at("present") == true,
             "Blueprint package bytes are unrecorded");
        ids.insert(id);
        const auto &companions = row.at("companionIds");
        need(companions.is_array() && companions.size() == 6, "Incomplete Blueprint companion inventory");
        std::size_t index = 0;
        for (const char *ext : {"uexp", "ubulk", "uptnl", "m.ubulk", "o.ubulk", "upayload"}) {
            const auto sibling = id + ":" + ext;
            need(companions.at(index++) == sibling && files.contains(sibling) &&
                     files.at(sibling).at("role") == "asset-companion",
                 "Blueprint companion identity differs");
            ids.insert(sibling);
        }
        const auto &imports = row.at("imports");
        need(imports.is_array() && imports.size() <= 4096, "Invalid Blueprint import inventory");
        std::set<std::string> unique;
        for (const auto &j : imports) {
            control.check();
            const auto imported = word(j);
            need(unique.insert(imported).second, "Repeated Blueprint package import");
            if (imported.compare(0, 8, "/Script/") == 0) {
                need(files.contains("module:" + imported.substr(8)) &&
                         files.at("module:" + imported.substr(8)).at("role") == "module",
                     "Blueprint imported script module is unrecorded");
            } else {
                need(imported.size() > 1 && imported[0] == '/' && imported.find('.') == std::string::npos &&
                         imported.find('\\') == std::string::npos,
                     "Invalid Blueprint package import");
                if (!reachable.count(imported))
                    pending.push_back(imported);
                need(pending.size() <= 65536, "Blueprint import traversal limit exceeded");
            }
        }
    }
    need(reachable.size() == packages.size(), "Unreachable Blueprint package binding");
    for (auto it = files.begin(); it != files.end(); ++it)
        if (it->at("role") == "asset-package" || it->at("role") == "asset-companion")
            need(ids.count(it.key()) != 0, "Unbound Blueprint dependency bytes");
    const auto root_id = "package:" + package(cls);
    need(source.at("blueprintPackageSha256") == files.at(root_id).at("sha256"),
         "Source Blueprint package fingerprint differs");
    const auto &target = classes.at(0).at("defaultObject"), &parent = classes.at(1).at("defaultObject");
    if (kind == "class-default") {
        const auto &f = p.at("factory");
        need(f.at("format") == "cyuediff.blueprint-cdo-factory/1" &&
                 f.at("operation") == "construct-cdo-from-explicit-parent-template" &&
                 f.at("class").at("path") == cls && f.at("targetDefault") == target &&
                 f.at("parentDefault") == parent && p.at("archetype") == parent,
             "Blueprint CDO factory initialization is not bound to its parent");
        const auto seed = word(p.at("object").at("path"));
        need(seed.rfind("/Temp/CyUEDiffFactory_", 0) == 0, "Expected an isolated Blueprint factory seed");
    } else
        need(!p.contains("factory") && p.at("object") == target && p.at("archetype") == parent,
             "Blueprint instance defaults are not the selected real class CDO");
    for (auto it = snapshot.fields().begin(); it != snapshot.fields().end(); ++it)
        if (it->at("owner").get_ref<const std::string &>().compare(0, 8, "/Script/") != 0) {
            const auto guid = it->value("propertyGuid", "");
            need(guid.size() == 32 && guid != std::string(32, '0') &&
                     guid.find_first_not_of("0123456789abcdef") == std::string::npos,
                 "Blueprint field GUID is unavailable");
        }
    return {{"objectKind", kind},
            {"class", cls},
            {"sourceDefault", target.at("path")},
            {"parentDefault", parent.at("path")},
            {"packageSha256", source.at("blueprintPackageSha256")},
            {"packages", packages.size()},
            {"classes", classes.size()}};
}
} // namespace cyuediff::detail
