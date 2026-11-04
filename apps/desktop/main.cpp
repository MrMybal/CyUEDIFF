#include "document.hpp"
#include "browser.hpp"
#include "preferences.hpp"
#include "container_view.hpp"
#ifdef CYUEDIFF_HAS_GIT
#include "history_view.hpp"
#endif
#include <cyuediff/catalog_view.hpp>
#include <cyuediff/asset_view.hpp>
#include "platform_win32.hpp"
#include <shellapi.h>
#include <imgui_impl_win32.h>
#include <cwctype>
#include <fstream>
#include <iostream>
#include <chrono>
#include <algorithm>

using namespace cyuediff;
using namespace cyuediff::desktop;
namespace {
struct Settings {
    std::string before, after, project, engine, codec, repository, lfs_objects, after_project, after_engine;
    std::string before_revision = "HEAD~1", after_revision = "HEAD";
    int source = 0, cooked_profile = 0;
    std::string container_path, container_entry, script_objects;
    bool previews = true, discover_engine = true, include_engine = false;
    bool ready() const {
        if (source == 2)
            return !container_path.empty();
        return !before.empty() &&
               (source == 0 || (!repository.empty() && !before_revision.empty() && !after_revision.empty()));
    }
    bool browse_ready() const {
        if (source == 2)
            return false;
        return source == 0 ? !project.empty()
                           : (!repository.empty() && !before_revision.empty() && !after_revision.empty());
    }
    BrowseRequest browse_request() const {
        return {fs::u8path(project), fs::u8path(engine), include_engine, request().git};
    }
    LoadRequest request() const {
        if (source == 2) {
            LoadRequest result;
            ContainerSource input;
            input.path = fs::u8path(container_path);
            input.script_objects = fs::u8path(script_objects);
            input.entry = container_entry;
            input.profile = cooked_profile == 1
                                ? ZenProfile::unreal_5_3
                                : (cooked_profile == 2 ? ZenProfile::unreal_5_5 : ZenProfile::unspecified);
            result.container = std::move(input);
            result.codec = fs::u8path(codec);
            result.previews = false;
            return result;
        }
        LoadRequest result{before,  after, fs::u8path(project), fs::u8path(engine), fs::u8path(codec),
                           previews};
        result.after_project = fs::u8path(after_project);
        result.after_engine_root = fs::u8path(after_engine);
        if (source == 1)
            result.git = GitSource{fs::u8path(repository), fs::u8path(lfs_objects), before_revision,
                                   after_revision, discover_engine};
        return result;
    }
};
struct Options {
    Settings settings;
    bool smoke = false, help = false, browse = false;
    fs::path capture, report, preferences;
    bool no_preferences = false, save_preferences = false, preference_write = false;
    bool theme_override = false, locale_override = false, previews_override = false, engine_override = false;
    std::string preferences_diagnostic;
    std::string browse_filter, browse_select, container_filter;
    bool container_parameters = false;
    bool history = false, history_parameters = false, history_compare = false;
    std::string history_revision = "HEAD", history_before, history_after, history_filter;
    std::string theme = "mid-grey", locale = "en", view = "properties";
};
AssetTab tab(const std::string &name) {
    if (name == "properties")
        return AssetTab::properties;
    if (name == "blueprint")
        return AssetTab::blueprint;
    if (name == "materials")
        return AssetTab::materials;
    if (name == "textures")
        return AssetTab::textures;
    if (name == "meshes")
        return AssetTab::meshes;
    if (name == "comparison")
        return AssetTab::comparison;
    throw Error("desktop-option", "Unknown view: " + name);
}
Options parse(const std::vector<std::string> &args) {
    Options options;
    bool git_options = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto &argument = args[i];
        auto value = [&]() {
            if (++i >= args.size())
                throw Error("desktop-option", "Missing value for " + argument);
            return args[i];
        };
        if (argument == "--smoke")
            options.smoke = true;
        else if (argument == "--container") {
            options.settings.container_path = value();
            options.settings.source = 2;
        } else if (argument == "--container-entry" || argument == "--container-filter" ||
                   argument == "--script-objects") {
            auto text = value();
            options.container_parameters = true;
            if (argument == "--container-entry")
                options.settings.container_entry = text;
            else if (argument == "--script-objects")
                options.settings.script_objects = text;
            else
                options.container_filter = text;
        } else if (argument == "--cooked-profile") {
            const auto profile = value();
            options.container_parameters = true;
            if (profile == "ue5.3")
                options.settings.cooked_profile = 1;
            else if (profile == "ue5.5")
                options.settings.cooked_profile = 2;
            else
                throw Error("desktop-option", "Supported cooked profiles are ue5.3 and ue5.5");
        } else if (argument == "--history")
            options.history = true;
        else if (argument == "--history-compare") {
            options.history_compare = true;
            options.history_parameters = true;
        } else if (argument == "--history-revision" || argument == "--history-before" ||
                   argument == "--history-after" || argument == "--history-filter") {
            auto text = value();
            if (argument == "--history-revision")
                options.history_revision = text;
            else if (argument == "--history-before")
                options.history_before = text;
            else if (argument == "--history-after")
                options.history_after = text;
            else
                options.history_filter = text;
            options.history_parameters = true;
        } else if (argument == "--browse")
            options.browse = true;
        else if (argument == "--browse-filter")
            options.browse_filter = value();
        else if (argument == "--browse-select")
            options.browse_select = value();
        else if (argument == "--include-engine" || argument == "--exclude-engine") {
            options.settings.include_engine = argument == "--include-engine";
            options.engine_override = true;
        } else if (argument == "--help" || argument == "-h")
            options.help = true;
        else if (argument == "--before")
            options.settings.before = value();
        else if (argument == "--after")
            options.settings.after = value();
        else if (argument == "--repo") {
            options.settings.repository = value();
            options.settings.source = 1;
        } else if (argument == "--before-revision") {
            options.settings.before_revision = value();
            git_options = true;
        } else if (argument == "--after-revision") {
            options.settings.after_revision = value();
            git_options = true;
        } else if (argument == "--lfs-objects") {
            options.settings.lfs_objects = value();
            git_options = true;
        } else if (argument == "--no-engine") {
            options.settings.discover_engine = false;
            git_options = true;
        } else if (argument == "--project" || argument == "--before-project")
            options.settings.project = value();
        else if (argument == "--engine-root" || argument == "--before-engine-root")
            options.settings.engine = value();
        else if (argument == "--after-project")
            options.settings.after_project = value();
        else if (argument == "--after-engine-root")
            options.settings.after_engine = value();
        else if (argument == "--codec")
            options.settings.codec = value();
        else if (argument == "--no-previews" || argument == "--previews") {
            options.settings.previews = argument == "--previews";
            options.previews_override = true;
        } else if (argument == "--preferences")
            options.preferences = fs::u8path(value());
        else if (argument == "--no-preferences")
            options.no_preferences = true;
        else if (argument == "--save-preferences")
            options.save_preferences = true;
        else if (argument == "--capture")
            options.capture = fs::u8path(value());
        else if (argument == "--report")
            options.report = fs::u8path(value());
        else if (argument == "--theme") {
            options.theme = value();
            options.theme_override = true;
        } else if (argument == "--locale") {
            options.locale = value();
            options.locale_override = true;
        } else if (argument == "--view")
            options.view = value();
        else if (argument.rfind("--", 0) == 0)
            throw Error("desktop-option", "Unknown option: " + argument);
        else if (options.settings.before.empty())
            options.settings.before = argument;
        else if (options.settings.after.empty())
            options.settings.after = argument;
        else
            throw Error("desktop-option", "Too many package paths");
    }
    if (options.no_preferences && (!options.preferences.empty() || options.save_preferences))
        throw Error("desktop-option", "--no-preferences conflicts with preference file options");
    if (!options.preferences.empty() && options.preferences.extension() != ".json")
        throw Error("desktop-option", "Preferences require a .json path");
    if (options.save_preferences && (!options.smoke || options.preferences.empty()))
        throw Error("desktop-option", "--save-preferences requires --smoke and --preferences");
    if (options.history_parameters && !options.history)
        throw Error("desktop-option", "History options require --history");
    if (options.history &&
        (options.settings.source != 1 || options.settings.repository.empty() || options.browse))
        throw Error("desktop-option", "--history requires --repo and conflicts with --browse");
    if ((!options.history_before.empty() || !options.history_after.empty() || options.history_compare) &&
        !options.smoke)
        throw Error("desktop-option", "Automatic history selection requires --smoke");
    if (options.history_compare && (options.history_before.empty() || options.history_after.empty()))
        throw Error("desktop-option", "--history-compare requires two history commit IDs");
    if ((options.container_parameters || !options.settings.container_path.empty()) &&
        options.settings.source != 2)
        throw Error("desktop-option", "Container options require --container");
    if (options.settings.source == 2 &&
        (options.settings.container_path.empty() || !options.settings.before.empty() ||
         !options.settings.after.empty() || !options.settings.repository.empty() ||
         !options.settings.project.empty() || !options.settings.engine.empty() ||
         !options.settings.after_project.empty() || !options.settings.after_engine.empty() ||
         options.browse || options.history))
        throw Error("desktop-option", "Choose a container without loose, project or Git source options");
    if (options.settings.source == 2 && options.view != "properties")
        throw Error("desktop-option", "Asset view options do not apply to container browsing");
    tab(options.view);
    if ((!options.browse_filter.empty() || options.settings.include_engine ||
         !options.browse_select.empty()) &&
        !options.browse)
        throw Error("desktop-option", "Catalogue options require --browse");
    if (!options.browse_select.empty() && !options.smoke)
        throw Error("desktop-option", "--browse-select requires --smoke");
    if (git_options && options.settings.source != 1)
        throw Error("desktop-option", "Git options require --repo");
#ifndef CYUEDIFF_HAS_GIT
    if (options.settings.source == 1)
        throw Error("desktop-option", "This desktop build does not include the Git adapter");
#endif
    if (options.settings.source == 1 &&
        (options.settings.repository.empty() || options.settings.before_revision.empty() ||
         options.settings.after_revision.empty()))
        throw Error("desktop-option", "Choose a repository and two Git revisions");
    if (!options.settings.after.empty() && options.settings.before.empty())
        throw Error("desktop-option", "An after package requires a before package");
    if ((!options.capture.empty() || !options.report.empty()) && !options.smoke)
        throw Error("desktop-option", "Capture and report require --smoke");
    if (!options.capture.empty() && options.capture.extension() != ".png")
        throw Error("desktop-option", "Capture requires a .png path");
    if (!options.report.empty() && options.report.extension() != ".json")
        throw Error("desktop-option", "Report requires a .json path");
    return options;
}
void register_translations() {
#ifdef CYUEDIFF_HAS_GIT
    HistoryView::register_translations();
#endif
    ContainerView::register_translations();
    AssetView::register_translations();
    CatalogView::register_translations();
    cyui::registerTranslations(
        "fr",
        {{"CyUEDiff preferences", "Préférences CyUEDiff"},
         {"Preferences could not be loaded.", "Les préférences n'ont pas pu être chargées."},
         {"Cancelled", "Annulé"},
         {"Cancel loading", "Annuler le chargement"},
         {"Opening document", "Ouverture du document"},
         {"Reading file", "Lecture du fichier"},
         {"Reading exports", "Lecture des exports"},
         {"Reading bulk data", "Lecture des blocs de données"},
         {"Decoding bulk data", "Décodage des blocs de données"},
         {"Decoding image", "Décodage de l'image"},
         {"Converting image", "Conversion de l'image"},
         {"Decoding mesh", "Décodage du mesh"},
         {"Reading Git objects", "Lecture des objets Git"},
         {"Comparing previews", "Comparaison des aperçus"},
         {"Document ready", "Document prêt"},
         {"Browse content...", "Parcourir les contenus..."},
         {"Browse content (Ctrl+B)", "Parcourir les contenus (Ctrl+B)"},
         {"Content browser", "Navigateur de contenus"},
         {"Scanning content...", "Analyse des contenus..."},
         {"Refresh catalogue", "Actualiser le catalogue"},
         {"Include engine content", "Inclure les contenus du moteur"},
         {"Catalogue is partial. See scan diagnostics.",
          "Le catalogue est partiel. Consultez les diagnostics."},
         {"Scan diagnostics", "Diagnostics du parcours"},
         {"Choose a project to browse its content.", "Choisissez un projet pour parcourir ses contenus."},
         {"Local files", "Fichiers locaux"},
         {"After project", "Projet après"},
         {"After engine", "Moteur après"},
         {"Empty after context inherits the before context.",
          "Le contexte après vide reprend celui d'avant."},
         {"Decoding HDR source", "Décodage de la source HDR"},
         {"Tone mapping HDR preview", "Conversion de l'aperçu HDR"},
         {"Comparing HDR samples", "Comparaison des échantillons HDR"},
         {"Hashing data", "Calcul des empreintes"},
         {"Copying local LFS data", "Copie des données LFS locales"},
         {"Comparing packages", "Comparaison des packages"},
         {"Checking package cache", "Vérification du cache"},
         {"Git revisions", "Révisions Git"},
         {"Repository", "Dépôt"},
         {"Before ref", "Rév. avant"},
         {"After ref", "Rév. après"},
         {"Before path", "Chemin avant"},
         {"After path", "Chemin après"},
         {"Same path when empty", "Même chemin si vide"},
         {"Use paths relative to the repository, or /Game/... with a historical project.",
          "Utilisez des chemins relatifs au dépôt, ou /Game/... avec un projet historique."},
         {"Historical project (optional)", "Projet historique (facultatif)"},
         {"Project path", "Chemin projet"},
         {"Discover engine", "Détecter le moteur"},
         {"LFS cache (optional)", "Cache LFS (facultatif)"},
         {"LFS objects", "Objets LFS"},
         {"Swap revisions", "Inverser les révisions"},
         {"Added", "Ajouté"},
         {"Removed", "Supprimé"},
         {"Project context changes", "Changements du contexte projet"},
         {"Context changes: %zu", "Changements de contexte : %zu"},
         {"Choose a repository, two revisions and an asset path, then load them.",
          "Choisissez un dépôt, deux révisions et un chemin d’asset, puis chargez-les."},
         {"File", "Fichier"},
         {"Open before...", "Ouvrir avant..."},
         {"Open after...", "Ouvrir après..."},
         {"Choose project", "Choisir le projet"},
         {"Choose package", "Choisir un package"},
         {"Unreal project (*.uproject)", "Projet Unreal (*.uproject)"},
         {"Unreal package (*.uasset;*.umap)", "Package Unreal (*.uasset;*.umap)"},
         {"Exit", "Quitter"},
         {"View", "Affichage"},
         {"Theme", "Thème"},
         {"Compare packages", "Comparer des packages"},
         {"Project file", "Fichier projet"},
         {"Engine root", "Racine moteur"},
         {"Read only", "Lecture seule"},
         {"Project (optional)", "Projet (facultatif)"},
         {"Engine root (optional)", "Racine moteur (facultative)"},
         {"Load previews", "Charger les aperçus"},
         {"Load", "Charger"},
         {"Swap files", "Inverser les fichiers"},
         {"Loading...", "Chargement..."},
         {"Ready", "Prêt"},
         {"Load failed", "Échec du chargement"},
         {"Changed", "Modifié"},
         {"Identical", "Identique"},
         {"Binary changes", "Changements binaires"},
         {"Incomplete input", "Entrée incomplète"},
         {"Unverified", "Non vérifié"},
         {"Diagnostics", "Diagnostics"},
         {"No package loaded", "Aucun package chargé"},
         {"Choose a package on the left, then load it. Add a second package to compare versions.",
          "Choisissez un package à gauche, puis chargez-le. Ajoutez un second package pour comparer "
          "les versions."},
         {"Open before (Ctrl+O)", "Ouvrir avant (Ctrl+O)"},
         {"Open after (Ctrl+Shift+O)", "Ouvrir après (Ctrl+Maj+O)"},
         {"Reload (Ctrl+R)", "Recharger (Ctrl+R)"},
         {"%zu exported objects", "%zu objets exportés"},
         {"Preview diagnostics", "Diagnostics des aperçus"},
         {"Additional diagnostics are available.", "Des diagnostics supplémentaires sont disponibles."},
         {"The previous document remains displayed.", "Le document précédent reste affiché."}});
}
int resize_text(ImGuiInputTextCallbackData *data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto &text = *static_cast<std::string *>(data->UserData);
        text.resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = text.data();
    }
    return 0;
}
void field(const char *id, std::string &text) {
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText(id, text.data(), text.capacity() + 1, ImGuiInputTextFlags_CallbackResize, resize_text,
                     &text);
    cyui::tooltip(text.c_str());
}
bool same_path(const fs::path &a, const fs::path &b) {
    std::error_code error;
    if (fs::exists(a) && fs::exists(b) && fs::equivalent(a, b, error) && !error)
        return true;
    auto left = fs::weakly_canonical(fs::absolute(a)).native(),
         right = fs::weakly_canonical(fs::absolute(b)).native();
    std::transform(left.begin(), left.end(), left.begin(), [](wchar_t c) { return std::towlower(c); });
    std::transform(right.begin(), right.end(), right.begin(), [](wchar_t c) { return std::towlower(c); });
    return left == right;
}
void protect_outputs(const Options &options, const std::shared_ptr<const Document> &document,
                     const std::shared_ptr<const BrowseDocument> &browser = {},
                     const fs::path &history_storage = {}) {
    std::vector<fs::path> outputs;
    for (const auto &path : {options.capture, options.report})
        if (!path.empty()) {
            if (!options.preferences.empty() && same_path(path, options.preferences))
                throw Error("desktop-output", "Output overlaps preferences input");
            outputs.push_back(path);
        }
    if (options.preference_write && !options.preferences.empty())
        outputs.push_back(options.preferences);
    if (outputs.empty())
        return;
    std::vector<fs::path> inputs;
    if (options.settings.source == 2) {
        for (const auto &path : {options.settings.container_path, options.settings.script_objects}) {
            if (path.empty())
                continue;
            ContainerOptions limits;
            limits.max_entries = 100'000;
            limits.max_index_bytes = 64ull * 1024 * 1024;
            auto input = Container::open(fs::u8path(path), limits);
            inputs.insert(inputs.end(), input.files().begin(), input.files().end());
        }
    }
    auto package = [&](const fs::path &path) {
        if (path.empty())
            return;
        inputs.push_back(path);
        for (auto extension : {".uexp", ".ubulk", ".uptnl", ".m.ubulk", ".o.ubulk", ".upayload"}) {
            auto companion = path;
            companion.replace_extension(extension);
            inputs.push_back(companion);
        }
    };
    std::vector<fs::path> directories;
    if (!history_storage.empty())
        directories.push_back(history_storage);
    if (options.settings.source == 1) {
#ifdef CYUEDIFF_HAS_GIT
        GitOptions git_options;
        git_options.lfs_objects = fs::u8path(options.settings.lfs_objects);
        auto repository = GitRepository::open(fs::u8path(options.settings.repository), git_options);
        directories.push_back(repository.common_directory());
        directories.push_back(repository.lfs_objects());
        inputs.push_back(repository.root() / ".git");
        for (const auto &path : {options.settings.before, options.settings.after})
            if (!path.empty() && path.front() != '/')
                package(repository.root() / fs::u8path(path));
        for (const auto &project : {options.settings.project, options.settings.after_project})
            if (!project.empty())
                inputs.push_back(repository.root() / fs::u8path(project));
#endif
    } else {
        auto request = options.settings.request();
        for (bool after : {false, true}) {
            const auto &path = after ? request.after : request.before;
            if (path.empty())
                continue;
            auto project = after && !request.after_project.empty() ? request.after_project : request.project;
            auto engine =
                after && !request.after_engine_root.empty() ? request.after_engine_root : request.engine_root;
            if (!project.empty() && path.front() == '/')
                package(ProjectContext::open(project, engine).resolve(path));
            else
                package(fs::u8path(path));
        }
    }
    if (document) {
        if (document->before)
            package(document->before->source);
        inputs.insert(inputs.end(), document->protected_files.begin(), document->protected_files.end());
        directories.insert(directories.end(), document->protected_directories.begin(),
                           document->protected_directories.end());
        if (document->after)
            package(document->after->source);
    }
    if (browser) {
        for (const auto &file : browser->protected_files)
            package(file);
        directories.insert(directories.end(), browser->protected_directories.begin(),
                           browser->protected_directories.end());
    }
    for (auto path : {options.settings.project, options.settings.after_project, options.settings.codec})
        if (!path.empty())
            inputs.push_back(fs::u8path(path));
    for (const auto &engine : {options.settings.engine, options.settings.after_engine})
        if (!engine.empty())
            inputs.push_back(fs::u8path(engine) / "Engine/Build/Build.version");
    for (const auto &output : outputs)
        if (!output.empty())
            for (const auto &input : inputs)
                if (same_path(input, output))
                    throw Error("desktop-output", "Output overlaps an input");
    auto normalized = [](const fs::path &path) {
        auto name = fs::weakly_canonical(fs::absolute(path)).make_preferred().native();
        std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return std::towlower(c); });
        return name;
    };
    for (const auto &output : outputs)
        if (!output.empty()) {
            auto name = normalized(output);
            for (const auto &directory : directories) {
                auto prefix = normalized(directory);
                if (!prefix.empty() && prefix.back() != L'\\')
                    prefix += L'\\';
                if (same_path(output, directory) || name.rfind(prefix, 0) == 0)
                    throw Error("desktop-output",
                                "Output overlaps source content, Git storage or engine context");
            }
        }
    for (std::size_t i = 0; i < outputs.size(); ++i)
        for (std::size_t j = i + 1; j < outputs.size(); ++j)
            if (same_path(outputs[i], outputs[j]))
                throw Error("desktop-output", "Outputs overlap");
}
enum class Action {
    none,
    cancel_load,
    open_before,
    open_after,
    project,
    load,
    swap,
    browse,
    browser_open,
    browser_before,
    browser_after,
    history,
    history_event,
    open_container,
    container_index,
    container_inspect,
    container_object
};
class Shell {
    DocumentSession session_;
    BrowserSession browser_;
#ifdef CYUEDIFF_HAS_GIT
    HistorySession history_;
    HistoryView history_view_;
    std::shared_ptr<const HistoryDocument> historied_;
    bool history_open_ = false, auto_history_compare_ = false, history_compare_request_ = false;
    std::string history_tip_ = "HEAD", history_before_, history_after_, history_chosen_before_,
                history_chosen_after_;
    HistoryAction history_action_ = HistoryAction::none;
#endif
    std::shared_ptr<const BrowseDocument> browsed_;
    CatalogView catalog_view_;
    bool browser_open_ = false, close_browser_ = false;
    std::string browse_select_;
    std::shared_ptr<const Document> displayed_;
    AssetView view_;
    ContainerView container_view_;
    std::vector<ComPtr<ID3D11ShaderResourceView>> images_;
    std::vector<std::string> gpu_diagnostics_;
    std::string dialog_error_, context_details_, preferences_diagnostic_;
    Settings settings_;
    AssetTab initial_tab_;

  public:
    explicit Shell(const Options &options)
        : preferences_diagnostic_(options.preferences_diagnostic), settings_(options.settings),
          initial_tab_(tab(options.view)) {
        container_view_.set_filter(options.container_filter);
        if ((settings_.source == 2 || !settings_.before.empty()) && !options.history)
            session_.request(settings_.request());
#ifdef CYUEDIFF_HAS_GIT
        history_tip_ = options.history_revision;
        history_before_ = options.history_before;
        history_after_ = options.history_after;
        history_compare_request_ = options.history_compare;
        history_view_.set_filter(options.history_filter);
        if (options.history)
            start_history();
#endif
        catalog_view_.set_filter(options.browse_filter);
        browse_select_ = options.browse_select;
        if (options.browse)
            start_browser();
    }
    ~Shell() {
        view_.set_packages({});
        images_.clear();
    }
    DesktopPreferences preferences() const {
        return {cyui::currentThemeKey(), cyui::locale(), settings_.previews, settings_.include_engine};
    }
    const DocumentSession &session() const {
        return session_;
    }
    const std::shared_ptr<const BrowseDocument> &browser_document() const {
        return browsed_;
    }
    Json browser_report() {
        if (!browsed_)
            return nullptr;
        auto result = browsed_->report();
        result["visiblePackages"] = catalog_view_.visible_count();
        result["open"] = browser_open_;
        return result;
    }
    Json container_view_report() const {
        if (!displayed_ || !displayed_->container)
            return nullptr;
        const auto *entry = container_view_.selected_member();
        return {{"visibleMembers", container_view_.visible_count()},
                {"selectedMember", entry ? Json(entry->id) : Json(nullptr)},
                {"selectedObject", container_view_.selected_object()},
                {"tab", container_view_.active_tab()}};
    }
    Json history_report() const {
#ifdef CYUEDIFF_HAS_GIT
        if (historied_) {
            auto result = historied_->report();
            result["open"] = history_open_;
            result["visibleCommits"] = history_view_.visible_count();
            result["selected"] =
                history_view_.selected() ? Json(history_view_.selected()->id) : Json(nullptr);
            result["beforeRevision"] = settings_.before_revision;
            result["afterRevision"] = settings_.after_revision;
            return result;
        }
#endif
        return nullptr;
    }
    fs::path history_storage() const {
#ifdef CYUEDIFF_HAS_GIT
        if (historied_)
            return historied_->common_directory;
#endif
        return {};
    }
#ifdef CYUEDIFF_HAS_GIT
    bool history_can_compare() const {
        return !history_chosen_before_.empty() && !history_chosen_after_.empty() &&
               history_chosen_before_ == settings_.before_revision &&
               history_chosen_after_ == settings_.after_revision;
    }
    void start_history(std::optional<std::uint32_t> skip = {}) {
        if (!historied_ || !same_path(fs::u8path(settings_.repository), historied_->repository)) {
            history_chosen_before_.clear();
            history_chosen_after_.clear();
        }
        HistoryRequest request;
        request.repository = fs::u8path(settings_.repository);
        request.revision = history_tip_;
        if (skip && historied_) {
            request = historied_->request;
            request.repository = historied_->repository;
            request.revision = historied_->page.head;
            request.options.skip = *skip;
        }
        history_open_ = true;
        history_.request(std::move(request));
    }
    void select_history(const std::string &id, bool after) {
        if (!historied_ || history_.busy() || !history_.error().empty())
            throw Error("history-selection", "No ready history page");
        auto selection = historied_->select(id, settings_.request().git.value_or(GitSource{}), after);
        settings_.source = 1;
        settings_.repository = selection.repository.u8string();
        settings_.before_revision = selection.before_revision;
        settings_.after_revision = selection.after_revision;
        (after ? history_chosen_after_ : history_chosen_before_) = id;
    }
#endif
    bool busy() const {
#ifdef CYUEDIFF_HAS_GIT
        if (history_.busy())
            return true;
#endif
        return session_.busy() || browser_.busy();
    }
    std::string error() const {
        if (!dialog_error_.empty())
            return dialog_error_;
#ifdef CYUEDIFF_HAS_GIT
        if (!history_.error().empty())
            return history_.error();
#endif
        return !browser_.error().empty() ? browser_.error() : session_.error();
    }
    void start_browser() {
        browser_open_ = true;
        close_browser_ = false;
        browser_.request(settings_.browse_request());
    }
    void select_browser(CatalogAction action) {
        if (!browsed_ || browser_.busy() || !browser_.error().empty())
            return;
        const bool open = action == CatalogAction::open;
        auto selection = browsed_->select(catalog_view_.selected(), action == CatalogAction::use_after,
                                          open && browsed_->request.git.has_value());
        settings_.project = selection.project.u8string();
        settings_.engine = selection.engine_root.u8string();
        if (selection.git) {
            settings_.before_revision = selection.git->before_revision;
            settings_.after_revision = selection.git->after_revision;
        }
        if (open) {
            settings_.before = selection.before;
            settings_.after.clear();
        } else if (action == CatalogAction::use_after)
            settings_.after = selection.after;
        else
            settings_.before = selection.before;
        if (!settings_.before.empty())
            session_.request(settings_.request());
    }
    std::size_t images() const {
        return images_.size();
    }
    void poll(Renderer &renderer) {
#ifdef CYUEDIFF_HAS_GIT
        history_.poll();
        if (history_.document() != historied_) {
            historied_ = history_.document();
            history_view_.set_document(historied_);
            try {
                for (bool after : {false, true}) {
                    auto &id = after ? history_after_ : history_before_;
                    if (!id.empty()) {
                        if (!history_view_.select_commit(id))
                            throw Error("history-selection",
                                        "Requested commit is not visible on this history page");
                        select_history(id, after);
                        id.clear();
                    }
                }
                auto_history_compare_ = history_compare_request_;
                history_compare_request_ = false;
            } catch (const std::exception &error) {
                dialog_error_ = error.what();
            }
        }
#endif
        browser_.poll();
        if (browser_.document() != browsed_) {
            browsed_ = browser_.document();
            catalog_view_.set_items(
                std::shared_ptr<const std::vector<CatalogItem>>(browsed_, &browsed_->assets),
                browsed_->request.git.has_value());
            if (!browse_select_.empty()) {
                try {
                    auto found = std::find_if(
                        browsed_->assets.begin(), browsed_->assets.end(), [&](const auto &asset) {
                            return asset.path == browse_select_ || asset.label == browse_select_;
                        });
                    if (found == browsed_->assets.end())
                        throw Error("browser-selection", "Requested package is not in the catalogue");
                    catalog_view_.select(static_cast<std::size_t>(found - browsed_->assets.begin()));
                    select_browser(CatalogAction::open);
                    close_browser_ = true;
                } catch (const std::exception &e) {
                    dialog_error_ = e.what();
                }
                browse_select_.clear();
            }
        }
        session_.poll();
        if (session_.document() == displayed_)
            return;
        displayed_ = session_.document();
        if (displayed_->container) {
            view_.set_packages({});
            images_.clear();
            gpu_diagnostics_.clear();
            context_details_.clear();
            container_view_.set_document(displayed_->container);
            return;
        }
        container_view_.set_document({});
        view_.set_packages(displayed_->before, displayed_->after, displayed_->request.git.has_value());
        context_details_.clear();
#ifdef CYUEDIFF_HAS_GIT
        if (displayed_->git && !displayed_->git->context_changes.empty())
            context_details_ = displayed_->git->context_changes.dump(2);
#endif
        images_.clear();
        gpu_diagnostics_.clear();
        view_.select_tab(initial_tab_);
        view_.set_mesh_geometry(displayed_->before_mesh);
        view_.set_mesh_geometry(displayed_->after_mesh, true);
        for (const auto &preview : displayed_->previews) {
            try {
                auto image = renderer.upload(preview.image);
                if (view_.set_texture_preview(preview.object_path,
                                              {ImTextureID(reinterpret_cast<std::uintptr_t>(image.Get())),
                                               int(preview.image.width), int(preview.image.height)},
                                              preview.after))
                    images_.push_back(std::move(image));
            } catch (const std::exception &e) {
                gpu_diagnostics_.push_back(e.what());
            }
        }
        for (const auto &difference : displayed_->texture_differences) {
            try {
                cyui::ImageData binding{};
                Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> image;
                if (difference.comparison.comparable) {
                    image = renderer.upload(difference.comparison.heatmap);
                    binding = {ImTextureID(reinterpret_cast<std::uintptr_t>(image.Get())),
                               int(difference.comparison.heatmap.width),
                               int(difference.comparison.heatmap.height)};
                }
                bool bound =
                    difference.hdr_comparison
                        ? view_.set_texture_difference(difference.before_object, difference.after_object,
                                                       binding, *difference.hdr_comparison)
                        : view_.set_texture_difference(difference.before_object, difference.after_object,
                                                       binding, difference.comparison);
                if (bound && image)
                    images_.push_back(std::move(image));
            } catch (const std::exception &e) {
                gpu_diagnostics_.push_back(e.what());
            }
        }
    }
    std::string status() const {
#ifdef CYUEDIFF_HAS_GIT
        if (history_.busy())
            return "Reading Git history...";
#endif
        if (browser_.busy())
            return "Scanning content...";
        if (session_.busy())
            return "Loading...";
        if (session_.cancelled())
            return "Cancelled";
        if (!error().empty())
            return "Load failed";
        if (!displayed_)
            return "Ready";
        auto status = displayed_->status();
        if (status == "changed")
            return "Changed";
        if (status == "identical")
            return "Identical";
        if (status == "binary-only")
            return "Binary changes";
        if (status == "incomplete")
            return "Incomplete input";
        if (status == "added")
            return "Added";
        if (status == "removed")
            return "Removed";
        return status == "ready" ? "Ready" : "Unverified";
    }

    Action draw(bool &quit) {
        Action action = Action::none;
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu(cyui::trLabel("File").c_str())) {
                {
                    cyui::ScopedDisabled local_only(settings_.source != 0);
                    if (ImGui::MenuItem(cyui::trLabel("Open before...").c_str(), "Ctrl+O"))
                        action = Action::open_before;
                    if (ImGui::MenuItem(cyui::trLabel("Open after...").c_str(), "Ctrl+Shift+O"))
                        action = Action::open_after;
                    if (ImGui::MenuItem(cyui::trLabel("Choose project").c_str()))
                        action = Action::project;
                }
                if (ImGui::MenuItem(cyui::trLabel("Open container...").c_str()))
                    action = Action::open_container;
                if (ImGui::MenuItem(cyui::trLabel("Browse content...").c_str(), "Ctrl+B", false,
                                    settings_.browse_ready()))
                    action = Action::browse;
#ifdef CYUEDIFF_HAS_GIT
                if (ImGui::MenuItem(cyui::trLabel("Browse history...").c_str(), nullptr, false,
                                    settings_.source == 1 && !settings_.repository.empty()))
                    action = Action::history;
#endif
                ImGui::Separator();
                if (ImGui::MenuItem(cyui::trLabel("Exit").c_str()))
                    quit = true;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu(cyui::trLabel("View").c_str())) {
                if (ImGui::BeginMenu(cyui::trLabel("Theme").c_str())) {
                    for (const auto &theme : cyui::builtInThemes())
                        if (ImGui::MenuItem(theme.key.c_str(), nullptr, cyui::currentThemeKey() == theme.key))
                            cyui::setTheme(theme.key.c_str());
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu(cyui::trLabel("Language").c_str())) {
                    for (const auto &locale : cyui::availableLocales())
                        if (ImGui::MenuItem(locale.name.c_str(), nullptr, cyui::locale() == locale.code))
                            cyui::setLocale(locale.code.c_str());
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        if (cyui::beginTopBar("##actions")) {
            cyui::heading("CyUEDiff", 3);
            ImGui::SameLine();
            cyui::toolbarSeparator();
            {
                cyui::ScopedDisabled local_only(settings_.source != 0);
                if (cyui::iconButton(cyui::icon::folder, cyui::tr("Open before (Ctrl+O)").c_str()))
                    action = Action::open_before;
                ImGui::SameLine();
                if (cyui::iconButton(cyui::icon::document, cyui::tr("Open after (Ctrl+Shift+O)").c_str()))
                    action = Action::open_after;
            }
            ImGui::SameLine();
            if (cyui::iconButton(cyui::icon::refresh, cyui::tr("Reload (Ctrl+R)").c_str()))
                action = Action::load;
            ImGui::SameLine();
            {
                cyui::ScopedDisabled disabled(!settings_.browse_ready());
                if (cyui::iconButton(cyui::icon::library, cyui::tr("Browse content (Ctrl+B)").c_str()))
                    action = Action::browse;
            }
            ImGui::SameLine();
            cyui::badge(cyui::tr("Read only").c_str());
        }
        cyui::endTopBar();
        if (cyui::beginStatusBar("##status")) {
            cyui::statusText(error().empty() ? cyui::Tone::neutral : cyui::Tone::error, "%s",
                             cyui::tr(status().c_str()).c_str());
            if (busy()) {
                ImGui::SameLine();
                cyui::spinner("##loading");
            }
        }
        cyui::endStatusBar();
        if (cyui::beginSidebar("##inputs", 310)) {
            if (!preferences_diagnostic_.empty()) {
                cyui::callout(cyui::Tone::warning, cyui::tr("Preferences could not be loaded.").c_str());
                ImGui::TextWrapped("%s", preferences_diagnostic_.c_str());
            }
            cyui::heading(cyui::tr(settings_.source == 2 ? "Containers" : "Compare packages").c_str(), 3);
            const auto local_label = cyui::tr("Local files"), container_label = cyui::tr("Containers");
#ifdef CYUEDIFF_HAS_GIT
            const auto git_label = cyui::tr("Git revisions");
            const char *sources[] = {local_label.c_str(), git_label.c_str(), container_label.c_str()};
            cyui::segmented("##source", sources, 3, &settings_.source, cyui::dp(-1));
#else
            const char *sources[] = {local_label.c_str(), container_label.c_str()};
            int mode = settings_.source == 2 ? 1 : 0;
            if (cyui::segmented("##source", sources, 2, &mode, cyui::dp(-1)))
                settings_.source = mode ? 2 : 0;
#endif
            if (settings_.source == 2) {
                if (cyui::button(cyui::trLabel("Open container...").c_str()))
                    action = Action::open_container;
                if (cyui::beginProperties("##container-input", 95)) {
                    cyui::property(cyui::tr("Container file").c_str());
                    field("##container-file", settings_.container_path);
                    cyui::property(cyui::tr("Cooked profile").c_str());
                    const auto bytes = cyui::tr("Byte inspection");
                    const char *profiles[] = {bytes.c_str(), "UE 5.3", "UE 5.5"};
                    ImGui::Combo("##cooked-profile", &settings_.cooked_profile, profiles, 3);
                    cyui::property(cyui::tr("Global script TOC").c_str());
                    field("##script-toc", settings_.script_objects);
                    cyui::property(cyui::tr("Codec module").c_str());
                    field("##container-codec", settings_.codec);
                    cyui::endProperties();
                }
                ImGui::TextWrapped(
                    "%s", cyui::tr("Choose an explicit UE profile for Zen object inspection.").c_str());
                cyui::ScopedDisabled disabled(!settings_.ready());
                if (cyui::primaryButton(cyui::trLabel("Index container").c_str(), cyui::dp(-1, 0)))
                    action = Action::container_index;
            } else {
                if (settings_.source == 1) {
#ifdef CYUEDIFF_HAS_GIT
                    {
                        cyui::ScopedDisabled disabled(settings_.repository.empty());
                        if (cyui::button(cyui::trLabel("Browse history...").c_str()))
                            action = Action::history;
                    }
#endif
                    if (cyui::beginProperties("##git", 76)) {
                        cyui::property(cyui::tr("Repository").c_str());
                        field("##repository", settings_.repository);
                        cyui::property(cyui::tr("Before ref").c_str());
                        field("##before-revision", settings_.before_revision);
                        cyui::property(cyui::tr("After ref").c_str());
                        field("##after-revision", settings_.after_revision);
                        cyui::endProperties();
                    }
                    ImGui::TextWrapped(
                        "%s",
                        cyui::tr(
                            "Use paths relative to the repository, or /Game/... with a historical project.")
                            .c_str());
                }
                if (cyui::beginProperties("##files", 76)) {
                    cyui::property(cyui::tr(settings_.source == 1 ? "Before path" : "Before").c_str());
                    field("##before", settings_.before);
                    cyui::property(cyui::tr(settings_.source == 1 ? "After path" : "After").c_str());
                    field("##after", settings_.after);
                    if (settings_.source == 1 && settings_.after.empty())
                        cyui::textFaint("%s", cyui::tr("Same path when empty").c_str());
                    cyui::endProperties();
                }
                cyui::space();
                if (cyui::beginSection(cyui::trLabel(settings_.source == 1 ? "Historical project (optional)"
                                                                           : "Project (optional)")
                                           .c_str(),
                                       false)) {
                    if (cyui::beginProperties("##context", 66)) {
                        cyui::property(
                            cyui::tr(settings_.source == 1 ? "Project path" : "Project file").c_str());
                        field("##project", settings_.project);
                        cyui::property(cyui::tr("Engine root").c_str());
                        field("##engine", settings_.engine);
                        cyui::property(cyui::tr("After project").c_str());
                        field("##after-project", settings_.after_project);
                        cyui::property(cyui::tr("After engine").c_str());
                        field("##after-engine", settings_.after_engine);
                        cyui::endProperties();
                    }
                    cyui::textDim("%s", cyui::tr("Empty after context inherits the before context.").c_str());
                    if (settings_.source == 1)
                        cyui::toggleSwitch(cyui::trLabel("Discover engine").c_str(),
                                           &settings_.discover_engine);
                    if (settings_.source == 0 && cyui::button(cyui::trLabel("Choose project").c_str()))
                        action = Action::project;
                    cyui::endSection();
                }
                if (settings_.source == 1 &&
                    cyui::beginSection(cyui::trLabel("LFS cache (optional)").c_str(), false)) {
                    if (cyui::beginProperties("##lfs", 76)) {
                        cyui::property(cyui::tr("LFS objects").c_str());
                        field("##lfs-objects", settings_.lfs_objects);
                        cyui::endProperties();
                    }
                    cyui::endSection();
                }
                {
                    cyui::ScopedDisabled disabled(!settings_.browse_ready());
                    if (cyui::button(cyui::trLabel("Browse content...").c_str(), cyui::Variant::secondary,
                                     cyui::dp(-1, 0)))
                        action = Action::browse;
                }
                if (settings_.source == 0 && settings_.project.empty())
                    ImGui::TextWrapped("%s", cyui::tr("Choose a project to browse its content.").c_str());
                cyui::toggleSwitch(cyui::trLabel("Load previews").c_str(), &settings_.previews);
                {
                    cyui::ScopedDisabled disabled(!settings_.ready());
                    if (cyui::primaryButton(cyui::trLabel("Load").c_str(), cyui::dp(-1, 0)))
                        action = Action::load;
                }
                {
                    cyui::ScopedDisabled disabled(settings_.source == 0 ? settings_.after.empty()
                                                                        : !settings_.ready());
                    if (cyui::button(
                            cyui::trLabel(settings_.source == 1 ? "Swap revisions" : "Swap files").c_str(),
                            cyui::Variant::secondary, cyui::dp(-1, 0)))
                        action = Action::swap;
                }
            }
            if (session_.busy()) {
                const auto progress = session_.progress();
                float fraction =
                    progress.total ? float(double(progress.completed) / double(progress.total)) : 0.f;
                ImGui::ProgressBar(std::clamp(fraction, 0.f, 1.f), cyui::dp(-1, 0),
                                   cyui::tr(progress.phase.c_str()).c_str());
                if (cyui::button(cyui::trLabel("Cancel loading").c_str(), cyui::Variant::secondary,
                                 cyui::dp(-1, 0)))
                    action = Action::cancel_load;
            }
            if (displayed_ && !displayed_->container) {
                cyui::divider();
                const auto &present = displayed_->before ? displayed_->before : displayed_->after;
                cyui::textDim(cyui::tr("%zu exported objects").c_str(), present->exports.size());
#ifdef CYUEDIFF_HAS_GIT
                if (displayed_->git) {
                    for (const auto *revision : {&displayed_->git->before, &displayed_->git->after}) {
                        cyui::textMono(
                            "%s: %.12s",
                            cyui::tr(revision == &displayed_->git->before ? "Before" : "After").c_str(),
                            revision->commit.c_str());
                        cyui::tooltip(revision->commit.c_str());
                    }
                    if (!context_details_.empty() &&
                        cyui::beginSection(cyui::trLabel("Project context changes").c_str(), false)) {
                        ImGui::TextWrapped("%s", context_details_.c_str());
                        cyui::endSection();
                    }
                }
#endif
                if (cyui::beginSection(cyui::trLabel("Diagnostics").c_str(), false)) {
                    for (const auto *package : {displayed_->before.get(), displayed_->after.get()})
                        if (package)
                            for (const auto &message : package->diagnostics)
                                ImGui::TextWrapped("%s", message.c_str());
                    if (displayed_->project)
                        for (const auto &message : displayed_->project->diagnostics)
                            ImGui::TextWrapped("%s", message.c_str());
                    for (const auto &message : displayed_->diagnostics)
                        ImGui::TextWrapped("%s", message.c_str());
                    for (const auto &message : gpu_diagnostics_)
                        ImGui::TextWrapped("%s", message.c_str());
                    cyui::endSection();
                }
            }
        }
        cyui::endSidebar();
        const auto *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        if (ImGui::Begin("##document", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings)) {
            auto message = error();
            if (!message.empty()) {
                cyui::callout(cyui::Tone::error, message.c_str());
                if (displayed_)
                    cyui::textDim("%s", cyui::tr("The previous document remains displayed.").c_str());
            }
            if (displayed_ && (!displayed_->diagnostics.empty() || !gpu_diagnostics_.empty()))
                cyui::callout(cyui::Tone::warning, cyui::tr("Additional diagnostics are available.").c_str());
#ifdef CYUEDIFF_HAS_GIT
            if (displayed_ && displayed_->git && !displayed_->git->context_changes.empty()) {
                cyui::textDim(cyui::tr("Context changes: %zu").c_str(),
                              displayed_->git->context_changes.size());
            }
#endif
            if (displayed_ && displayed_->container) {
                const auto request = container_view_.draw(session_.busy());
                if (request == ContainerAction::inspect)
                    action = Action::container_inspect;
                else if (request == ContainerAction::select_object)
                    action = Action::container_object;
            } else if (displayed_)
                view_.draw("##asset", false);
            else if (settings_.source == 2)
                cyui::emptyState(cyui::tr("Containers").c_str(),
                                 cyui::tr("Choose a Pak or IoStore TOC, then index it.").c_str());
            else
                cyui::emptyState(
                    cyui::tr("No package loaded").c_str(),
                    cyui::tr(settings_.source == 1
                                 ? "Choose a repository, two revisions and an asset path, then load them."
                                 : "Choose a package on the left, then load it. Add a second package to "
                                   "compare versions.")
                        .c_str(),
                    cyui::icon::document);
        }
        ImGui::End();
        if (browser_open_) {
            const auto title = cyui::trLabel("Content browser");
            const auto width = std::min(980.0f, ImGui::GetMainViewport()->WorkSize.x / cyui::dpi() - 40.0f);
            if (cyui::beginModal(title.c_str(), &browser_open_, width)) {
                if (close_browser_) {
                    ImGui::CloseCurrentPopup();
                    browser_open_ = false;
                    close_browser_ = false;
                } else {
                    if (settings_.source == 0)
                        cyui::toggleSwitch(cyui::trLabel("Include engine content").c_str(),
                                           &settings_.include_engine);
                    if (cyui::button(cyui::trLabel("Refresh catalogue").c_str()))
                        action = Action::browse;
                    if (browser_.busy()) {
                        ImGui::SameLine();
                        cyui::spinner("##scan");
                        cyui::textDim("%s", cyui::tr("Scanning content...").c_str());
                    }
                    if (!browser_.error().empty())
                        cyui::callout(cyui::Tone::error, browser_.error().c_str());
                    if (browsed_ &&
                        (!browsed_->before.complete || (browsed_->request.git && !browsed_->after.complete)))
                        cyui::callout(cyui::Tone::warning,
                                      cyui::tr("Catalogue is partial. See scan diagnostics.").c_str());
                    {
                        cyui::ScopedDisabled disabled(browser_.busy() || !browser_.error().empty());
                        auto selected = catalog_view_.draw("##catalog");
                        if (selected != CatalogAction::none) {
                            action = selected == CatalogAction::open
                                         ? Action::browser_open
                                         : (selected == CatalogAction::use_before ? Action::browser_before
                                                                                  : Action::browser_after);
                            ImGui::CloseCurrentPopup();
                            browser_open_ = false;
                        }
                    }
                    if (browsed_ && cyui::beginSection(cyui::trLabel("Scan diagnostics").c_str(), false)) {
                        for (const auto *catalog : {&browsed_->before, &browsed_->after})
                            for (const auto &message : catalog->diagnostics)
                                ImGui::TextWrapped("%s", message.c_str());
                        cyui::endSection();
                    }
                }
                cyui::endModal();
            }
            if (!browser_open_)
                browser_.cancel();
        }
#ifdef CYUEDIFF_HAS_GIT
        if (history_open_) {
            const auto width = std::min(1100.f, ImGui::GetMainViewport()->WorkSize.x / cyui::dpi() - 40.f);
            if (cyui::beginModal(cyui::trLabel("Git history").c_str(), &history_open_, width)) {
                if (cyui::beginProperties("##history-start", 100)) {
                    cyui::property(cyui::tr("Start revision").c_str());
                    field("##history-tip", history_tip_);
                    cyui::endProperties();
                }
                history_action_ =
                    history_view_.draw(history_.busy(), history_.error(), settings_.before_revision,
                                       settings_.after_revision, history_can_compare());
                if (auto_history_compare_) {
                    history_action_ = HistoryAction::compare;
                    auto_history_compare_ = false;
                }
                if (history_action_ != HistoryAction::none)
                    action = Action::history_event;
                if (history_action_ == HistoryAction::compare || history_action_ == HistoryAction::close) {
                    ImGui::CloseCurrentPopup();
                    history_open_ = false;
                }
                cyui::endModal();
            }
            if (!history_open_ && history_action_ == HistoryAction::none) {
                history_action_ = HistoryAction::close;
                action = Action::history_event;
            }
        }
        const bool modal_open = browser_open_ || history_open_;
#else
        const bool modal_open = browser_open_;
#endif
        if (!modal_open && !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_O, false))
                action = ImGui::GetIO().KeyShift ? Action::open_after : Action::open_before;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false))
                action = Action::load;
            if (ImGui::IsKeyPressed(ImGuiKey_B, false) && settings_.browse_ready())
                action = Action::browse;
        }
        cyui::renderNotifications();
        return action;
    }
    void perform(Action action, NativeWindow &window) {
        try {
            if (action == Action::none)
                return;
            dialog_error_.clear();
#ifdef CYUEDIFF_HAS_GIT
            if (action == Action::history) {
                start_history();
                return;
            }
            if (action == Action::history_event) {
                const auto event = history_action_;
                history_action_ = HistoryAction::none;
                if (event == HistoryAction::refresh)
                    start_history();
                else if (event == HistoryAction::previous && historied_)
                    start_history(historied_->page.skip > historied_->request.options.max_commits
                                      ? historied_->page.skip - historied_->request.options.max_commits
                                      : 0);
                else if (event == HistoryAction::next && historied_)
                    start_history(historied_->page.next_skip);
                else if ((event == HistoryAction::before || event == HistoryAction::after) &&
                         history_view_.selected())
                    select_history(history_view_.selected()->id, event == HistoryAction::after);
                else if (event == HistoryAction::compare) {
                    if (!history_can_compare())
                        throw Error("history-selection", "Choose a before and an after commit");
                    history_.cancel();
                    if (settings_.ready())
                        session_.request(settings_.request());
                    else
                        start_browser();
                } else if (event == HistoryAction::close)
                    history_.cancel();
                return;
            }
#endif
            if (action == Action::cancel_load) {
                session_.cancel();
                return;
            }
            if (action == Action::browse) {
                start_browser();
                return;
            }
            if (action == Action::browser_open || action == Action::browser_before ||
                action == Action::browser_after) {
                select_browser(action == Action::browser_open
                                   ? CatalogAction::open
                                   : (action == Action::browser_before ? CatalogAction::use_before
                                                                       : CatalogAction::use_after));
                return;
            }
            if (action == Action::open_container) {
                auto path = window.choose_file(false, true);
                if (!path)
                    return;
                settings_.source = 2;
                settings_.container_path = path->u8string();
                settings_.container_entry.clear();
                session_.request(settings_.request());
                return;
            }
            if (action == Action::container_object) {
                container_view_.apply_object_selection();
                return;
            }
            if (action == Action::container_index || action == Action::container_inspect ||
                (action == Action::load && settings_.source == 2)) {
                if (action == Action::container_index)
                    settings_.container_entry.clear();
                else if (action == Action::container_inspect) {
                    const auto *selected = container_view_.selected_member();
                    if (!selected || !displayed_ || !displayed_->container)
                        return;
                    const auto &input = displayed_->container->source;
                    settings_.source = 2;
                    settings_.container_path = input.path.u8string();
                    settings_.container_entry = selected->id;
                }
                session_.request(settings_.request());
                return;
            }
            if (settings_.source != 0 &&
                (action == Action::open_before || action == Action::open_after || action == Action::project))
                return;
            if (action == Action::open_before || action == Action::open_after || action == Action::project) {
                auto path = window.choose_file(action == Action::project);
                if (!path)
                    return;
                if (action == Action::project)
                    settings_.project = path->u8string();
                else {
                    (action == Action::open_after ? settings_.after : settings_.before) = path->u8string();
                }
            } else if (action == Action::swap) {
                // Materialize inherited values before swapping so both contexts follow their files.
                if (!settings_.after_project.empty() || !settings_.after_engine.empty()) {
                    if (settings_.after_project.empty())
                        settings_.after_project = settings_.project;
                    if (settings_.after_engine.empty())
                        settings_.after_engine = settings_.engine;
                    std::swap(settings_.project, settings_.after_project);
                    std::swap(settings_.engine, settings_.after_engine);
                }
                if (settings_.source == 1) {
                    std::swap(settings_.before_revision, settings_.after_revision);
                    if (!settings_.after.empty())
                        std::swap(settings_.before, settings_.after);
                } else
                    std::swap(settings_.before, settings_.after);
            }
            if (action != Action::project && !settings_.before.empty())
                session_.request(settings_.request());
        } catch (const std::exception &e) {
            dialog_error_ = e.what();
        }
    }
};
struct ImGuiContextOwner {
    ImGuiContextOwner() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
    }
    ~ImGuiContextOwner() {
        ImGui::DestroyContext();
    }
};
int run(Options options) {
    if (options.help) {
        std::cout
            << "CyUEDiff desktop [BEFORE.uasset [AFTER.uasset]] [--project FILE] [--engine-root DIR]\n"
               "  --repo DIR --before-revision REF --after-revision REF --before REPO_PATH [--after "
               "REPO_PATH]\n"
               "  [--after-project FILE] [--after-engine-root DIR] : independent after context\n"
               "  [--project REPO_PROJECT] [--no-engine] [--lfs-objects DIR] : local Git/LFS comparison\n"
               "  --container FILE.pak|FILE.utoc [--container-entry ID_OR_PATH] [--container-filter TEXT]\n"
               "  [--cooked-profile ue5.3|ue5.5] [--script-objects GLOBAL.utoc] : explicit Zen inspection\n"
               "  --repo DIR --history [--history-revision REF] : choose commits in local history\n"
               "  --history --smoke [--history-before ID --history-after ID --history-compare]\n"
               "  --browse [--include-engine] [--browse-filter TEXT] : browse project or revision content\n"
               "  [--codec DLL] [--no-previews] [--locale en|fr] [--theme KEY]\n"
               "  [--view properties|blueprint|materials|textures|meshes|comparison]\n"
               "  [--preferences FILE.json | --no-preferences] : appearance and preview preferences\n"
               "  [--previews] [--exclude-engine] : override saved display options\n"
               "  --smoke --preferences FILE.json --save-preferences : explicit preference test write\n"
               "  --smoke [--capture FILE.png] [--report FILE.json] : hidden DX11/WARP validation\n";
        return 0;
    }
    PreferencesRead saved;
    if (!options.no_preferences) {
        if (options.preferences.empty() && !options.smoke) {
            options.preferences = default_preferences_path();
            if (options.preferences.empty())
                options.preferences_diagnostic = "Local application data folder is unavailable";
        }
        if (!options.preferences.empty()) {
            saved = read_preferences(options.preferences);
            options.preferences_diagnostic = saved.diagnostic;
            if (saved.loaded) {
                if (!options.theme_override)
                    options.theme = saved.value.theme;
                if (!options.locale_override)
                    options.locale = saved.value.locale;
                if (!options.previews_override)
                    options.settings.previews = saved.value.previews;
                if (!options.engine_override)
                    options.settings.include_engine = saved.value.include_engine;
            }
            options.preference_write = saved.writable && (!options.smoke || options.save_preferences);
        }
    }
    protect_outputs(options, {});
    NativeWindow window(options.smoke);
    ImGuiContextOwner context;
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    cyui::Config config;
    config.themeKey = options.theme.c_str();
    config.density = cyui::Density::compact;
    config.dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(window.handle);
    cyui::apply(config);
    cyui::loadFonts();
    register_translations();
    if (!cyui::setTheme(options.theme.c_str()) || !cyui::setLocale(options.locale.c_str()))
        throw Error("desktop-option", "Unknown theme or locale");
    Renderer renderer(window.handle, options.smoke);
    Shell shell(options);
    bool preferences_saved = false;
    auto persist = [&]() {
        if (!options.preference_write)
            return;
        try {
            protect_outputs(options, shell.session().document(), shell.browser_document(),
                            shell.history_storage());
            desktop::save_preferences(options.preferences, shell.preferences());
            preferences_saved = true;
        } catch (const std::exception &error) {
            options.preferences_diagnostic = error.what();
        }
    };
    int frames = 0, settled = 0;
    auto started = std::chrono::steady_clock::now();
    for (;;) {
        if (!window.pump())
            break;
        shell.poll(renderer);
        if (window.resize_width && window.resize_height) {
            renderer.resize(window.resize_width, window.resize_height);
            window.resize_width = window.resize_height = 0;
        }
        if (window.minimized) {
            Sleep(16);
            continue;
        }
        renderer.new_frame();
        auto action = shell.draw(window.quit);
        ImGui::Render();
        renderer.draw();
        ++frames;
        if (shell.busy())
            settled = 0;
        bool finished = options.smoke && !shell.busy() && ++settled >= 12;
        if (options.smoke && std::chrono::steady_clock::now() - started > std::chrono::seconds(30))
            throw Error("desktop-timeout", "Document load exceeded smoke-test timeout");
        if (finished) {
            persist();
            protect_outputs(options, shell.session().document(), shell.browser_document(),
                            shell.history_storage());
            if (!options.capture.empty())
                write_png(options.capture, renderer.capture());
            Json report = {{"schema", "cyuediff.desktop/1"},
                           {"status", shell.error().empty() ? "ready" : "failed"},
                           {"error", shell.error()},
                           {"frames", frames},
                           {"drawVertices", ImGui::GetDrawData()->TotalVtxCount},
                           {"renderer", "Direct3D11 WARP"},
                           {"theme", cyui::currentThemeKey()},
                           {"locale", cyui::locale()},
                           {"view", options.view},
                           {"preferences",
                            {{"enabled", !options.preferences.empty()},
                             {"loaded", saved.loaded},
                             {"saved", preferences_saved},
                             {"diagnostic", options.preferences_diagnostic},
                             {"values", shell.preferences().to_json()}}},
                           {"uploadedPreviews", shell.images()},
                           {"browser", shell.browser_report()},
                           {"history", shell.history_report()},
                           {"containerView", shell.container_view_report()},
                           {"document", shell.session().document() ? shell.session().document()->report()
                                                                   : Json(nullptr)}};
            if (!options.report.empty()) {
                std::ofstream file(options.report, std::ios::binary);
                file << report.dump(2) << '\n';
                if (!file)
                    throw Error("desktop-output", "Cannot write report");
            }
            return shell.error().empty() ? 0 : 2;
        }
        renderer.present(!options.smoke);
        shell.perform(action, window);
        if (options.smoke)
            Sleep(2);
    }
    persist();
    if (!options.preferences_diagnostic.empty() && options.preference_write && !preferences_saved)
        MessageBoxW(window.handle, wide(options.preferences_diagnostic).c_str(),
                    wide(cyui::tr("CyUEDiff preferences")).c_str(), MB_OK | MB_ICONWARNING);
    return 0;
}
} // namespace
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    bool hidden = false;
    try {
        int argc = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv)
            throw Error("desktop-option", "Cannot read command line");
        std::vector<std::string> args;
        try {
            for (int i = 0; i < argc; ++i)
                args.push_back(utf8(argv[i]));
        } catch (...) {
            LocalFree(argv);
            throw;
        }
        LocalFree(argv);
        hidden = std::find(args.begin(), args.end(), "--smoke") != args.end();
        return run(parse(args));
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        if (!hidden)
            MessageBoxW(nullptr, wide(error.what()).c_str(), L"CyUEDiff", MB_OK | MB_ICONERROR);
        return 2;
    }
}
