#include "path_guard.hpp"
#include "document.hpp"
#include "workspace.hpp"
#include "workspace_file.hpp"
#include "document_tabs.hpp"
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
#include <map>
#include <set>

using namespace cyuediff;
using namespace cyuediff::desktop;
namespace {
struct Options {
    Settings settings;
    std::vector<Options> additional_documents;
    std::optional<std::size_t> selected_document;
    std::vector<std::string> document_actions;
    bool smoke = false, help = false, browse = false;
    fs::path capture, report, preferences, workspace_input, workspace_output;
    bool document_parameters = false;
    Json view_state = Json::object();
    bool no_preferences = false, save_preferences = false, preference_write = false;
    bool theme_override = false, locale_override = false, previews_override = false, engine_override = false;
    std::string preferences_diagnostic;
    std::string browse_filter, browse_select, container_filter;
    std::string cooked_view;
    int container_tab = -1;
    bool container_parameters = false, after_container_parameters = false, container_changes_only = false;
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
    if(name=="skeleton") return AssetTab::skeleton;
    if(name=="animations")return AssetTab::animations;
    if(name=="curves")return AssetTab::curves;
    if (name == "collisions")
        return AssetTab::collisions;
    if (name == "comparison")
        return AssetTab::comparison;
    throw Error("desktop-option", "Unknown view: " + name);
}
Options parse_one(const std::vector<std::string> &args) {
    Options options;
    bool git_options = false, pose_parameters = false, pose_shared_frame = false,
         pose_before_frame = false, pose_after_frame = false;
    auto source_frame = [](const std::string &text) {
        std::uint64_t result=0;
        if(text.empty())throw Error("desktop-option","Empty source frame");
        for(char c:text){if(c<'0'||c>'9'||result>(INT32_MAX-std::uint64_t(c-'0'))/10)throw Error("desktop-option","Source frame must be an integer from 0 to INT32_MAX");result=result*10+std::uint64_t(c-'0');}
        return static_cast<int>(result);
    };
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto &argument = args[i];
        const std::set<std::string> global_options = {"--workspace",
                                                      "--save-workspace",
                                                      "--smoke",
                                                      "--capture",
                                                      "--report",
                                                      "--locale",
                                                      "--theme",
                                                      "--preferences",
                                                      "--no-preferences",
                                                      "--save-preferences",
                                                      "--document-action",
                                                      "--document-index",
                                                      "--codec",
                                                      "--previews",
                                                      "--no-previews",
                                                      "--help",
                                                      "-h"};
        if (!global_options.count(argument))
            options.document_parameters = true;
        auto value = [&]() {
            if (++i >= args.size())
                throw Error("desktop-option", "Missing value for " + argument);
            return args[i];
        };
        if(argument=="--pose-before-skeleton" || argument=="--pose-after-skeleton" || argument=="--pose-before-object" || argument=="--pose-after-object" || argument=="--pose-before-skeleton-object" || argument=="--pose-after-skeleton-object") {
            pose_parameters=options.settings.pose_enabled=true;
            auto &settings=options.settings;auto text=value();
            if(argument=="--pose-before-skeleton")settings.pose_before_skeleton=std::move(text);
            else if(argument=="--pose-after-skeleton")settings.pose_after_skeleton=std::move(text);
            else if(argument=="--pose-before-object")settings.pose_before_object=std::move(text);
            else if(argument=="--pose-after-object")settings.pose_after_object=std::move(text);
            else if(argument=="--pose-before-skeleton-object")settings.pose_before_skeleton_object=std::move(text);
            else settings.pose_after_skeleton_object=std::move(text);
        } else if(argument=="--pose-frame" || argument=="--pose-before-frame" || argument=="--pose-after-frame") {
            pose_parameters=options.settings.pose_enabled=true;const auto frame=source_frame(value());
            if(argument=="--pose-frame"){pose_shared_frame=true;options.settings.pose_before_frame=options.settings.pose_after_frame=frame;}
            else if(argument=="--pose-before-frame"){pose_before_frame=true;options.settings.pose_before_frame=frame;}
            else {pose_after_frame=true;options.settings.pose_after_frame=frame;}
        } else if (argument == "--workspace")
            options.workspace_input = fs::u8path(value());
        else if (argument == "--save-workspace")
            options.workspace_output = fs::u8path(value());
        else if (argument == "--document-action") {
            auto action = value();
            if (action.empty() || action.size() > 1024 || options.document_actions.size() >= 64)
                throw Error("desktop-option", "Document action limit exceeded");
            options.document_actions.push_back(std::move(action));
        } else if (argument == "--document-index") {
            const auto text = value();
            if (text.size() != 1 || text[0] < '0' || text[0] > '7')
                throw Error("desktop-option", "Document index must be between 0 and 7");
            options.selected_document = static_cast<std::size_t>(text[0] - '0');
        } else if (argument == "--smoke")
            options.smoke = true;
        else if (argument == "--container") {
            options.settings.container_path = value();
            options.settings.source = 2;
        } else if (argument == "--cooked-view") {
            options.cooked_view = value();
            options.container_parameters = true;
            if (options.cooked_view != "properties" && options.cooked_view != "object")
                throw Error("desktop-option", "Cooked views are properties or object");
        } else if (argument == "--container-tab") {
            const auto name = value();
            options.container_parameters = true;
            if (name == "members")
                options.container_tab = 0;
            else if (name == "inspection")
                options.container_tab = 1;
            else if (name == "objects")
                options.container_tab = 2;
            else if (name == "schema")
                options.container_tab = 4;
            else if (name == "comparison") {
                options.container_tab = 3;
                options.after_container_parameters = true;
            } else
                throw Error("desktop-option", "Unknown container tab");
        } else if (argument == "--after-container") {
            options.settings.after_container_path = value();
            options.container_parameters = true;
        } else if (argument == "--compare-container-bytes" || argument == "--container-changes-only") {
            options.container_parameters = true;
            options.after_container_parameters = true;
            if (argument == "--compare-container-bytes")
                options.settings.compare_container_payloads = true;
            else
                options.container_changes_only = true;
        } else if (argument == "--schema" || argument == "--after-schema") {
            auto text = value();
            options.container_parameters = true;
            if (argument == "--schema")
                options.settings.container_schema = text;
            else {
                options.settings.after_container_schema = text;
                options.after_container_parameters = true;
            }
        } else if (argument == "--after-script-objects") {
            options.settings.after_script_objects = value();
            options.container_parameters = options.after_container_parameters = true;
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
        } else if (argument == "--cooked-profile" || argument == "--after-cooked-profile") {
            const auto profile = value();
            options.container_parameters = true;
            auto &target = argument == "--after-cooked-profile" ? options.settings.after_cooked_profile
                                                                : options.settings.cooked_profile;
            if (argument == "--after-cooked-profile")
                options.after_container_parameters = true;
            if (profile == "ue5.3")
                target = 1;
            else if (profile == "ue5.5")
                target = 2;
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
    if(pose_parameters) {
        if(options.settings.source==2 || options.browse || options.settings.before.empty())throw Error("desktop-option","Source pose options require loose or Git package inputs");
        if(options.settings.pose_before_skeleton.empty() || ((options.settings.source==1 || !options.settings.after.empty()) && options.settings.pose_after_skeleton.empty()))throw Error("desktop-option","Choose an explicit Skeleton for each requested animation side");
        if((pose_shared_frame&&(pose_before_frame||pose_after_frame)) || pose_before_frame!=pose_after_frame)throw Error("desktop-option","Choose a shared --pose-frame or both independent pose frame options");
        options.settings.pose_independent_frames=pose_before_frame && pose_after_frame;
    }
    if (!options.workspace_output.empty() && !options.smoke)
        throw Error("desktop-option", "--save-workspace requires --smoke; use the File menu interactively");
    for (const auto &path : {options.workspace_input, options.workspace_output})
        if (!path.empty() && path.extension() != ".json")
            throw Error("desktop-option", "Workspace requires a .json path");
    if (!options.document_actions.empty() && !options.smoke)
        throw Error("desktop-option", "Document action sequences require --smoke");
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
    if (options.after_container_parameters && options.settings.after_container_path.empty())
        throw Error("desktop-option", "After container options require --after-container");
    if (options.settings.source == 2 &&
        (options.settings.container_path.empty() || !options.settings.before.empty() ||
         !options.settings.after.empty() || !options.settings.repository.empty() ||
         !options.settings.project.empty() || !options.settings.engine.empty() ||
         !options.settings.after_project.empty() || !options.settings.after_engine.empty() ||
         options.browse || options.history))
        throw Error("desktop-option", "Choose a container without loose, project or Git source options");
    if ((!options.settings.container_schema.empty() && !options.settings.cooked_profile) ||
        (!options.settings.after_container_schema.empty() && !options.settings.after_cooked_profile))
        throw Error("desktop-option", "Schema contexts require an explicit cooked profile on that side");
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
Options parse(const std::vector<std::string> &args) {
    std::vector<std::vector<std::string>> groups(1, {args.front()});
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--new-document") {
            if (groups.size() >= 8)
                throw Error("document-limit", "At most eight documents may open");
            groups.push_back({args.front()});
        } else
            groups.back().push_back(args[i]);
    }
    auto result = parse_one(groups.front());
    for (std::size_t i = 1; i < groups.size(); ++i) {
        auto entry = parse_one(groups[i]);
        if (entry.smoke || entry.help || entry.history || entry.browse || !entry.capture.empty() ||
            !entry.report.empty() || !entry.preferences.empty() || entry.no_preferences ||
            !entry.workspace_input.empty() || !entry.workspace_output.empty() ||
            !entry.document_actions.empty() || entry.save_preferences || entry.theme_override ||
            entry.locale_override || entry.selected_document)
            throw Error("desktop-option", "Global options must precede --new-document");
        result.additional_documents.push_back(std::move(entry));
    }
    if (!result.additional_documents.empty() && (result.history || result.browse))
        throw Error("desktop-option", "Automatic history/browser launch requires a single document");
    if (!result.workspace_input.empty() &&
        (result.document_parameters || !result.additional_documents.empty()))
        throw Error("desktop-option", "Workspace launch conflicts with document source options");
    if (result.workspace_input.empty() && result.selected_document &&
        *result.selected_document >= groups.size())
        throw Error("desktop-option", "Selected document index is not open");
    return result;
}
void validate_workspace_host(const WorkspaceFile &file) {
#ifndef CYUEDIFF_HAS_GIT
    for (const auto &doc : file.documents)
        if (doc.settings.source == 1)
            throw Error("desktop-option", "This desktop build does not include the Git adapter");
#endif
}
Options workspace_options(Options options, const WorkspaceFile &file) {
    validate_workspace_host(file);
    const auto codec = options.settings.codec;
    const auto previews = options.settings.previews;
    const bool override_previews = options.previews_override;
    auto apply = [&](Options &entry, const SavedDocument &doc) {
        entry.settings = doc.settings;
        entry.settings.codec = codec;
        if (override_previews)
            entry.settings.previews = previews;
        entry.previews_override = entry.engine_override = true;
        entry.view = doc.view;
        entry.container_tab = doc.container_tab;
        entry.cooked_view = doc.cooked_view;
        entry.view_state = doc.view_state;
    };
    options.additional_documents.clear();
    apply(options, file.documents.front());
    for (std::size_t i = 1; i < file.documents.size(); ++i) {
        Options entry;
        apply(entry, file.documents[i]);
        options.additional_documents.push_back(std::move(entry));
    }
    if (!options.selected_document)
        options.selected_document = file.active;
    if (*options.selected_document >= file.documents.size())
        throw Error("desktop-option", "Selected document index is not open");
    return options;
}
void register_translations() {
#ifdef CYUEDIFF_HAS_GIT
    HistoryView::register_translations();
#endif
    ContainerView::register_translations();
    DocumentTabsView::register_translations();
    AssetView::register_translations();
    CatalogView::register_translations();
    cyui::registerTranslations(
        "fr",
        {{"Skeleton paths use each animation revision and its historical project mounts.", "Les chemins des squelettes utilisent la révision de chaque animation et ses montages de projet historiques."},
         {"Evaluate source hierarchy pose", "Évaluer la pose hiérarchique source"},
         {"Animation Skeleton binding", "Association du squelette d’animation"},
         {"Skeleton before", "Squelette avant"}, {"Skeleton after", "Squelette après"},
         {"Frame before", "Frame avant"}, {"Frame after", "Frame après"},
         {"Independent pose frames", "Frames de pose indépendantes"},
         {"Choose Skeleton before", "Choisir le squelette avant"}, {"Choose Skeleton after", "Choisir le squelette après"},
         {"Stored owner selections", "Choix des objets enregistrés"},
         {"Model before", "Modèle avant"}, {"Model after", "Modèle après"},
         {"Rig object before", "Objet avant"}, {"Rig object after", "Objet après"},
         {"Empty selectors require a sole stored object.", "Les sélecteurs vides exigent un seul objet enregistré."},
         {"Choose Skeleton paths, then Load to prepare the selected frames.", "Choisissez les squelettes, puis Charger pour préparer les frames sélectionnées."},
         {"CyUEDiff preferences", "Préférences CyUEDiff"},
         {"Preferences could not be loaded.", "Les préférences n'ont pas pu être chargées."},
         {"Open workspace...", "Ouvrir un espace de travail..."},
         {"Save workspace", "Enregistrer l'espace de travail"},
         {"Save workspace as...", "Enregistrer l'espace de travail sous..."},
         {"Choose workspace", "Choisir un espace de travail"},
         {"CyUEDiff workspace (*.json)", "Espace de travail CyUEDiff (*.json)"},
         {"Workspace saved", "Espace de travail enregistr\u00e9"},
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
          "Choisissez un dépôt, deux révisions et un chemin d’asset, "
          "puis chargez-les."},
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
    auto left = resolved_output_path(a).native(),
         right = resolved_output_path(b).native();
    std::transform(left.begin(), left.end(), left.begin(), [](wchar_t c) { return std::towlower(c); });
    std::transform(right.begin(), right.end(), right.begin(), [](wchar_t c) { return std::towlower(c); });
    return left == right;
}
void protect_outputs(const Options &options, const std::shared_ptr<const Document> &document,
                     const std::shared_ptr<const BrowseDocument> &browser = {},
                     const fs::path &history_storage = {}) {
    std::vector<fs::path> outputs;
    for (const auto &path : {options.capture, options.report, options.workspace_output})
        if (!path.empty()) {
            if (!options.preferences.empty() && same_path(path, options.preferences))
                throw Error("desktop-output", "Output overlaps preferences input");
            if (!options.workspace_input.empty() && same_path(path, options.workspace_input) &&
                (options.workspace_output.empty() || !same_path(path, options.workspace_output)))
                throw Error("desktop-output", "Output overlaps workspace input");
            outputs.push_back(path);
        }
    if (options.preference_write && !options.preferences.empty()) {
        if (!options.workspace_input.empty() && same_path(options.preferences, options.workspace_input))
            throw Error("desktop-output", "Preferences overlap workspace input");
        outputs.push_back(options.preferences);
    }
    if (outputs.empty())
        return;
    for (const auto &entry : options.additional_documents) {
        auto other = entry;
        other.capture = options.capture;
        other.report = options.report;
        other.workspace_input = options.workspace_input;
        other.workspace_output = options.workspace_output;
        other.preferences = options.preferences;
        other.preference_write = options.preference_write;
        protect_outputs(other, {}, {}, {});
    }

    std::vector<fs::path> inputs;
    if (options.settings.source == 2) {
        for (const auto &path :
             {options.settings.container_path, options.settings.script_objects,
              options.settings.after_container_path, options.settings.after_script_objects}) {
            if (path.empty())
                continue;
            ContainerOptions limits;
            limits.max_entries = 100'000;
            limits.max_index_bytes = 64ull * 1024 * 1024;
            auto input = Container::open(fs::u8path(path), limits);
            inputs.insert(inputs.end(), input.files().begin(), input.files().end());
        }
    }
    if (options.settings.source == 2)
        for (const auto &path :
             {options.settings.container_schema, options.settings.after_container_schema}) {
            if (path.empty())
                continue;
            auto manifest = ZenSchemaManifest::read(fs::u8path(path));
            inputs.push_back(manifest.source_path());
            inputs.push_back(manifest.mapping_path());
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
    if (options.settings.source == 1 && !options.settings.repository.empty()) {
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
        if (options.settings.pose_enabled) {
            for (bool after : {false, true}) {
                const auto &settings = options.settings;
                const auto &project = after && !settings.after_project.empty() ? settings.after_project : settings.project;
                const auto &revision = after ? settings.after_revision : settings.before_revision;
                std::optional<GitProjectContext> context;
                if (!project.empty()) {
                    GitProjectOptions selection;
                    selection.engine_root = fs::u8path(after && !settings.after_engine.empty() ? settings.after_engine : settings.engine);
                    selection.discover_engine = settings.discover_engine;
                    context = repository.project(revision, project, selection);
                    for (const auto &entry : context->documents)
                        inputs.push_back(entry.file.blob_oid.empty() ? fs::u8path(entry.file.path) : repository.root() / fs::u8path(entry.file.path));
                    if (!context->engine.root.empty()) directories.push_back(context->engine.root / "Engine");
                }
                const auto &skeleton = after ? settings.pose_after_skeleton : settings.pose_before_skeleton;
                const auto &asset = after && !settings.after.empty() ? settings.after : settings.before;
                for (const auto &name : {asset, skeleton}) {
                    if (name.empty()) continue;
                    if (name.front() != '/') { package(repository.root() / fs::u8path(name)); continue; }
                    if (!context) continue;
                    GitPackageLocation location;
                    try { location = context->locate(name); }
                    catch (const Error &) { continue; } // Unresolvable bindings are reported by the worker.
                    auto base = location.storage == "git" ? repository.root() / fs::u8path(location.path) : fs::u8path(location.path);
                    auto file = base; file += ".uasset"; package(file);
                    file = base; file += ".umap"; package(file);
                }
            }
        }
        for (const auto &project : {options.settings.project, options.settings.after_project})
            if (!project.empty())
                inputs.push_back(repository.root() / fs::u8path(project));
#endif
    } else {
        auto request = options.settings.request();
        for (bool after : {false, true}) {
            auto project = after && !request.after_project.empty() ? request.after_project : request.project;
            auto engine = after && !request.after_engine_root.empty() ? request.after_engine_root : request.engine_root;
            const auto skeleton = options.settings.pose_enabled && (!after || !request.after.empty())
                ? (after ? options.settings.pose_after_skeleton : options.settings.pose_before_skeleton) : std::string{};
            for(const auto &path:{after?request.after:request.before, skeleton}) {
                if(path.empty())continue;
                if(!project.empty() && path.front()=='/')package(ProjectContext::open(project,engine).resolve(path));
                else package(fs::u8path(path));
            }
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
        auto name = resolved_output_path(path).make_preferred().native();
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
    pose_before_skeleton,
    pose_after_skeleton,
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
    container_object,
    before_schema,
    after_schema,
    document_tab,
    new_document,
    close_document,
    duplicate_document,
    reopen_document,
    close_other_documents,
    next_document,
    previous_document,
    open_workspace,
    save_workspace,
    save_workspace_as
};
struct DocumentContext {
    Settings settings;
    AssetTab initial_tab = AssetTab::properties;
    int container_tab = -1;
    std::string cooked_view;
    Json retained_state = Json::object();
};
struct DocumentPresentation : DocumentContext {
    ViewLayout layout;
    Json pending_view_state = Json::object();
    std::shared_ptr<const Document> displayed;
    AssetView view;
    ContainerView container_view;
    std::vector<ComPtr<ID3D11ShaderResourceView>> images;
    std::vector<std::string> gpu_diagnostics;
    std::string dialog_error, context_details;
    ~DocumentPresentation() {
        view.set_packages({});
        images.clear();
    }
};
void pending_state(DocumentPresentation &p, const Json &state) {
    p.pending_view_state = state;
    if (state.contains("layout")) {
        p.layout.restore(state["layout"], "shell-layout");
        p.pending_view_state.erase("layout");
    }
}
class Shell {
    DocumentWorkspace session_;
    DocumentTabsView document_tabs_;
    DocumentTabAction tab_action_;
    std::map<DocumentId, std::unique_ptr<DocumentPresentation>> presentations_;
    std::map<DocumentId, DocumentContext> closed_contexts_;
    std::deque<std::string> smoke_actions_;
    Json action_results_ = Json::array();
    Options protections_;
    fs::path workspace_path_;
    bool workspace_loaded_ = false, workspace_saved_ = false;
    DocumentPresentation &current() {
        return *presentations_.at(session_.active_id());
    }
    const DocumentPresentation &current() const {
        return *presentations_.at(session_.active_id());
    }
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
    std::string preferences_diagnostic_;

  public:
    explicit Shell(const Options &options)
        : protections_(options), workspace_path_(options.workspace_input),
          workspace_loaded_(!options.workspace_input.empty()),
          preferences_diagnostic_(options.preferences_diagnostic) {
        auto initialize = [&](DocumentId id, const Options &entry) {
            auto state = std::make_unique<DocumentPresentation>();
            state->settings = entry.settings;
            if (!entry.previews_override)
                state->settings.previews = options.settings.previews;
            if (!entry.engine_override)
                state->settings.include_engine = options.settings.include_engine;
            state->initial_tab = tab(entry.view);
            state->container_tab = entry.container_tab;
            state->cooked_view = entry.cooked_view;
            pending_state(*state, entry.view_state);
            state->container_view.set_filter(entry.container_filter);
            state->container_view.set_changed_only(entry.container_changes_only);
            presentations_[id] = std::move(state);
            if (presentations_.at(id)->settings.ready() && !entry.history)
                session_.request(id, presentations_.at(id)->settings.request());
        };
        smoke_actions_.assign(options.document_actions.begin(), options.document_actions.end());
        initialize(session_.active_id(), options);
        for (const auto &entry : options.additional_documents)
            initialize(session_.create(), entry);
        if (options.selected_document)
            session_.select(session_.documents()[*options.selected_document].id);
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
    ~Shell() = default;
    DesktopPreferences preferences() const {
        return {cyui::currentThemeKey(), cyui::locale(), current().settings.previews,
                current().settings.include_engine};
    }
    const DocumentWorkspace &session() const {
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
        if (!current().displayed || !current().displayed->container)
            return nullptr;
        const auto *entry = current().container_view.selected_member();
        return {{"visibleMembers", current().container_view.visible_count()},
                {"selectedMember", entry ? Json(entry->id) : Json(nullptr)},
                {"selectedObject", current().container_view.selected_object()},
                {"objectProperties", current().container_view.property_report()},
                {"selectedKey", current().container_view.selected_key()},
                {"tab", current().container_view.active_tab()}};
    }
    Json history_report() const {
#ifdef CYUEDIFF_HAS_GIT
        if (historied_) {
            auto result = historied_->report();
            result["open"] = history_open_;
            result["visibleCommits"] = history_view_.visible_count();
            result["selected"] =
                history_view_.selected() ? Json(history_view_.selected()->id) : Json(nullptr);
            result["beforeRevision"] = current().settings.before_revision;
            result["afterRevision"] = current().settings.after_revision;
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
               history_chosen_before_ == current().settings.before_revision &&
               history_chosen_after_ == current().settings.after_revision;
    }
    void start_history(std::optional<std::uint32_t> skip = {}) {
        if (!historied_ || !same_path(fs::u8path(current().settings.repository), historied_->repository)) {
            history_chosen_before_.clear();
            history_chosen_after_.clear();
        }
        HistoryRequest request;
        request.repository = fs::u8path(current().settings.repository);
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
        auto selection =
            historied_->select(id, current().settings.request().git.value_or(GitSource{}), after);
        if (current().settings.source != 1)
            current().settings.pose_enabled = false;
        current().settings.source = 1;
        current().settings.repository = selection.repository.u8string();
        current().settings.before_revision = selection.before_revision;
        current().settings.after_revision = selection.after_revision;
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
        if (!current().dialog_error.empty())
            return current().dialog_error;
#ifdef CYUEDIFF_HAS_GIT
        if (!history_.error().empty())
            return history_.error();
#endif
        return !browser_.error().empty() ? browser_.error() : session_.error();
    }
    void start_browser() {
        browser_open_ = true;
        close_browser_ = false;
        browser_.request(current().settings.browse_request());
    }
    void select_browser(CatalogAction action) {
        if (!browsed_ || browser_.busy() || !browser_.error().empty())
            return;
        const bool open = action == CatalogAction::open;
        auto selection = browsed_->select(catalog_view_.selected(), action == CatalogAction::use_after,
                                          open && browsed_->request.git.has_value());
        current().settings.project = selection.project.u8string();
        current().settings.engine = selection.engine_root.u8string();
        if (selection.git) {
            current().settings.before_revision = selection.git->before_revision;
            current().settings.after_revision = selection.git->after_revision;
        }
        if (open) {
            current().settings.before = selection.before;
            current().settings.after.clear();
        } else if (action == CatalogAction::use_after)
            current().settings.after = selection.after;
        else
            current().settings.before = selection.before;
        if (!current().settings.before.empty())
            session_.request(current().settings.request());
    }
    std::size_t images() const {
        return current().images.size();
    }
    void reset_navigation() {
        browser_.cancel();
        browser_open_ = close_browser_ = false;
        browse_select_.clear();
#ifdef CYUEDIFF_HAS_GIT
        history_.cancel();
        history_open_ = auto_history_compare_ = history_compare_request_ = false;
        history_before_.clear();
        history_after_.clear();
        history_chosen_before_.clear();
        history_chosen_after_.clear();
#endif
    }
    void select_document(DocumentId id) {
        if (session_.active_id() == id || !session_.find(id))
            return;
        reset_navigation();
        session_.select(id);
    }
    void create_document() {
        auto state = std::make_unique<DocumentPresentation>();
        state->settings.previews = current().settings.previews;
        state->settings.include_engine = current().settings.include_engine;
        const auto id = session_.create();
        presentations_[id] = std::move(state);
        reset_navigation();
    }
    void close_document(DocumentId id) {
        if (!session_.find(id))
            return;
        reset_navigation();
        closed_contexts_[id] = retained_context(*presentations_.at(id));
        session_.close(id);
        presentations_.erase(id);
        prune_closed_contexts();
        if (!session_.active_id()) {
            const auto empty = session_.create();
            auto state = std::make_unique<DocumentPresentation>();
            state->settings.previews = closed_contexts_.at(id).settings.previews;
            state->settings.include_engine = closed_contexts_.at(id).settings.include_engine;
            presentations_[empty] = std::move(state);
        }
    }
    Json presentation_state(const DocumentPresentation &p) const {
        Json result = !p.displayed             ? p.pending_view_state
                      : p.displayed->container ? Json{{"container", p.container_view.view_state()}}
                                               : Json{{"asset", p.view.view_state()}};
        if (!p.layout.snapshot().empty())
            result["layout"] = p.layout.snapshot();
        return result;
    }
    DocumentContext retained_context(const DocumentPresentation &p) const {
        auto result=static_cast<const DocumentContext &>(p);
        result.retained_state=presentation_state(p);
        if(p.displayed) {
            if(p.displayed->container)result.container_tab=p.container_view.active_tab();
            else result.initial_tab=p.view.active_tab();
        }
        return result;
    }
    void apply_presentation_state(const Json &state) {
        cyuediff::view_state::validate(state);
        if (!current().displayed && (state.contains("asset") || state.contains("container")))
            throw Error("desktop-document", "No displayed document view");
        if (state.contains("layout"))
            current().layout.restore(state["layout"], "shell-layout");
        if (!current().displayed)
            return;
        if (current().displayed->container) {
            if (state.contains("container"))
                current().container_view.restore_view_state(state["container"]);
        } else if (state.contains("asset"))
            current().view.restore_view_state(state["asset"]);
    }
    void apply_pending_state() {
        if (!current().pending_view_state.empty()) {
            apply_presentation_state(current().pending_view_state);
            current().pending_view_state = Json::object();
        }
    }
    WorkspaceFile saved_workspace() const {
        WorkspaceFile result;
        result.documents.clear();
        const char *views[] = {"properties", "blueprint",  "materials", "textures",
                               "meshes",     "comparison", "collisions", "skeleton", "animations", "curves"};
        for (const auto &entry : session_.documents()) {
            const auto &p = *presentations_.at(entry.id);
            SavedDocument d;
            d.settings = p.settings;
            d.view_state = presentation_state(p);
            d.view = views[static_cast<int>(p.displayed ? p.view.active_tab() : p.initial_tab)];
            d.container_tab =
                p.displayed && p.displayed->container ? p.container_view.active_tab() : p.container_tab;
            d.cooked_view = p.displayed && p.displayed->container
                                ? p.container_view.property_report()["mode"].get<std::string>()
                                : p.cooked_view;
            if (entry.id == session_.active_id())
                result.active = result.documents.size();
            result.documents.push_back(std::move(d));
        }
        return result;
    }
    void write_workspace(const fs::path &path) {
        auto guard = protections_;
        guard.workspace_output = path;
        guard.workspace_input = workspace_path_;
        protect(guard);
        desktop::save_workspace(path, saved_workspace());
        workspace_path_ = fs::absolute(path);
        workspace_saved_ = true;
    }
    void open_workspace(const fs::path &path) {
        auto file = desktop::read_workspace(path);
        auto guard = protections_;
        guard.workspace_input = path;
        guard.selected_document.reset();
        guard = workspace_options(std::move(guard), file);
        protect_outputs(guard, {});
        reset_navigation();
        session_.clear();
        presentations_.clear();
        closed_contexts_.clear();
        for (const auto &d : file.documents) {
            const auto id = session_.create();
            auto state = std::make_unique<DocumentPresentation>();
            state->settings = d.settings;
            state->initial_tab = tab(d.view);
            state->container_tab = d.container_tab;
            state->cooked_view = d.cooked_view;
            pending_state(*state, d.view_state);
            presentations_[id] = std::move(state);
            if (d.settings.ready())
                session_.request(id, d.settings.request());
        }
        session_.select(session_.documents()[file.active].id);
        workspace_path_ = fs::absolute(path);
        workspace_loaded_ = true;
        workspace_saved_ = false;
    }
    Json workspace_file_report() const {
        return {{"path", workspace_path_.u8string()},
                {"loaded", workspace_loaded_},
                {"saved", workspace_saved_},
                {"context", saved_workspace().to_json()}};
    }
    void prune_closed_contexts() {
        for (auto it = closed_contexts_.begin(); it != closed_contexts_.end();) {
            const auto id = it->first;
            if (std::none_of(session_.closed().begin(), session_.closed().end(),
                             [=](const auto &closed) { return closed.id == id; }))
                it = closed_contexts_.erase(it);
            else
                ++it;
        }
    }
    void duplicate_document(DocumentId id) {
        const auto context = retained_context(*presentations_.at(id));
        const auto created = session_.duplicate(id, false);
        auto state = std::make_unique<DocumentPresentation>();
        static_cast<DocumentContext &>(*state) = context;
        pending_state(*state,context.retained_state);
        presentations_[created] = std::move(state);
        reset_navigation();
        if (current().settings.ready())
            session_.request(current().settings.request());
    }
    void reopen_document() {
        if (session_.closed().empty())
            throw Error("desktop-document", "No closed document to reopen");
        const auto old = session_.closed().back().id;
        const auto context = closed_contexts_.at(old);
        const auto created = session_.reopen(false);
        auto state = std::make_unique<DocumentPresentation>();
        static_cast<DocumentContext &>(*state) = context;
        pending_state(*state,context.retained_state);
        presentations_[created] = std::move(state);
        prune_closed_contexts();
        reset_navigation();
        if (current().settings.ready())
            session_.request(current().settings.request());
    }
    void close_other_documents(DocumentId keep) {
        for (const auto &entry : session_.documents())
            if (entry.id != keep)
                closed_contexts_[entry.id] = retained_context(*presentations_.at(entry.id));
        if (!session_.close_others(keep))
            return;
        for (auto it = presentations_.begin(); it != presentations_.end();) {
            if (it->first != keep)
                it = presentations_.erase(it);
            else
                ++it;
        }
        prune_closed_contexts();
        reset_navigation();
    }
    void cycle_document(bool previous) {
        const auto &entries = session_.documents();
        auto found = std::find_if(entries.begin(), entries.end(),
                                  [&](const auto &entry) { return entry.id == session_.active_id(); });
        auto at = static_cast<std::size_t>(found - entries.begin());
        select_document(entries[(at + (previous ? entries.size() - 1 : 1)) % entries.size()].id);
    }
    bool perform_next_document_action() {
        if (smoke_actions_.empty())
            return false;
        auto text = smoke_actions_.front();
        smoke_actions_.pop_front();
        const auto before = session_.active_id();
        const auto colon = text.find(':');
        const auto command = text.substr(0, colon);
        const auto argument = colon == std::string::npos ? std::string{} : text.substr(colon + 1);
        auto index = [&]() {
            if (argument.size() != 1 || argument[0] < '0' || argument[0] > '7')
                throw Error("desktop-document", "Invalid document action index");
            auto at = static_cast<std::size_t>(argument[0] - '0');
            if (at >= session_.documents().size())
                throw Error("desktop-document", "Document action index is not open");
            return session_.documents()[at].id;
        };
        try {
            if (command == "select" && colon != std::string::npos)
                select_document(index());
            else if (command == "close")
                close_document(colon == std::string::npos ? before : index());
            else if (command == "duplicate")
                duplicate_document(colon == std::string::npos ? before : index());
            else if (command == "new" && argument.empty())
                create_document();
            else if (command == "reopen" && argument.empty())
                reopen_document();
            else if (command == "close-others" && argument.empty())
                close_other_documents(before);
            else if (command == "next" && argument.empty())
                cycle_document(false);
            else if (command == "previous" && argument.empty())
                cycle_document(true);
            else if (command == "view-state" && !argument.empty())
                apply_presentation_state(Json::parse(argument));
            else if (command == "workspace-save" && !argument.empty())
                write_workspace(fs::u8path(argument));
            else if (command == "workspace-open" && !argument.empty())
                open_workspace(fs::u8path(argument));
            else if (command == "reload" && argument.empty()) {
                if (!current().settings.ready())
                    throw Error("desktop-document", "No document context to reload");
                session_.request(current().settings.request());
            } else if (command == "member" && !argument.empty()) {
                if (!current().container_view.select_member(argument))
                    throw Error("desktop-document", "Member is not visible in this document");
            } else if (command == "filter" && colon != std::string::npos)
                current().container_view.set_filter(argument);
            else
                throw Error("desktop-option", "Unknown document action");
            action_results_.push_back(
                {{"action", text}, {"before", before}, {"after", session_.active_id()}, {"ok", true}});
        } catch (const std::exception &error) {
            current().dialog_error = error.what();
            action_results_.push_back(
                {{"action", text}, {"before", before}, {"after", session_.active_id()}, {"ok", false}});
            smoke_actions_.clear();
        }
        return true;
    }
    void protect(const Options &options) const {
        auto workspace_guard = options;
        workspace_guard.workspace_input = workspace_path_;
        protect_outputs(workspace_guard, {}, browsed_, history_storage());
        for (const auto &closed : closed_contexts_) {
            auto local = options;
            local.additional_documents.clear();
            local.settings = closed.second.settings;
            protect_outputs(local, {}, browsed_, history_storage());
        }
        for (const auto &entry : session_.documents()) {
            auto local = options;
            local.additional_documents.clear();
            local.settings = presentations_.at(entry.id)->settings;
            protect_outputs(local, entry.document, browsed_, history_storage());
        }
    }
    std::string result_error() const {
        if (!error().empty())
            return error();
        for (const auto &entry : session_.documents()) {
            if (!entry.error.empty())
                return "One or more document loads failed";
            if (!presentations_.at(entry.id)->dialog_error.empty())
                return "One or more document actions failed";
        }
        return {};
    }
    Json workspace_report() const {
        Json entries = Json::array();
        for (const auto &entry : session_.documents()) {
            const auto &view = *presentations_.at(entry.id);
            entries.push_back({{"id", entry.id},
                               {"active", entry.id == session_.active_id()},
                               {"busy", entry.busy},
                               {"cancelled", entry.cancelled},
                               {"error", entry.error},
                               {"actionError", view.dialog_error},
                               {"revision", entry.revision},
                               {"before", view.settings.before},
                               {"after", view.settings.after},
                               {"project", view.settings.project},
                               {"engine", view.settings.engine},
                               {"container", view.settings.container_path},
                               {"source", view.settings.source},
                               {"uploadedPreviews", view.images.size()},
                               {"containerSelection", view.container_view.selected_key()},
                               {"visibleMembers", view.container_view.visible_count()},
                               {"containerTab", view.container_view.active_tab()},
                               {"viewState", presentation_state(view)},
                               {"document", entry.document ? entry.document->report() : Json(nullptr)}});
        }
        return {{"schema", "cyuediff.desktop-workspace/1"},
                {"active", session_.active_id()},
                {"maximum", session_.maximum()},
                {"closedDocuments", session_.closed().size()},
                {"actions", action_results_},
                {"documents", std::move(entries)}};
    }
    void update_tabs() {
        std::vector<DocumentTab> tabs;
        for (const auto &entry : session_.documents()) {
            const auto &state = *presentations_.at(entry.id);
            const auto source =
                state.settings.source == 2 ? state.settings.container_path : state.settings.before;
            auto label = source.empty() ? cyui::tr("Untitled") : fs::u8path(source).filename().u8string();
            if (!state.settings.after.empty() || !state.settings.after_container_path.empty())
                label += " / " + cyui::tr("After");
            tabs.push_back(
                {entry.id, label, source, entry.busy, !entry.error.empty() || !state.dialog_error.empty()});
        }
        document_tabs_.set_tabs(std::move(tabs), session_.active_id(),
                                session_.documents().size() < session_.maximum());
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
                current().dialog_error = error.what();
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
                    current().dialog_error = e.what();
                }
                browse_select_.clear();
            }
        }
        session_.poll();
        update_tabs();
        if (session_.document() == current().displayed)
            return;
        Json previous_pose_state=Json::object();
        if(current().displayed && !current().displayed->container) {
            const auto previous=current().view.view_state();
            for(const char *key:{"animationPose","animationPosePreview","animationSockets","animationSocketsPreview"})
                if(previous.contains(key))previous_pose_state[key]=previous.at(key);
        }
        current().displayed = session_.document();
        if (!current().displayed)
            return;
        if (current().displayed->container) {
            current().view.set_packages({});
            current().images.clear();
            current().gpu_diagnostics.clear();
            current().context_details.clear();
            current().container_view.set_document(current().displayed->container);
            if (!current().cooked_view.empty())
                current().container_view.set_property_mode(current().cooked_view == "properties");
            if (current().container_tab >= 0 && !current().container_view.select_tab(current().container_tab))
                throw Error("desktop-option", "Requested container tab is unavailable");
            apply_pending_state();
            return;
        }
        current().container_view.set_document({});
        current().view.set_packages(current().displayed->before, current().displayed->after,
                                    current().displayed->request.git.has_value());
        current().view.set_animation_poses(current().displayed->before_animation_pose?&*current().displayed->before_animation_pose:nullptr,
                                            current().displayed->after_animation_pose?&*current().displayed->after_animation_pose:nullptr,
                                            current().displayed->request.git.has_value() || current().displayed->after!=nullptr);
        current().view.set_animation_socket_poses(current().displayed->before_animation_sockets?&*current().displayed->before_animation_sockets:nullptr,
                                                 current().displayed->after_animation_sockets?&*current().displayed->after_animation_sockets:nullptr,
                                                 current().displayed->request.git.has_value() || current().displayed->after!=nullptr);
        if(current().displayed->request.animation_pose && !previous_pose_state.empty())
            current().view.restore_view_state(previous_pose_state);
        current().context_details.clear();
#ifdef CYUEDIFF_HAS_GIT
        if (current().displayed->git && !current().displayed->git->context_changes.empty())
            current().context_details = current().displayed->git->context_changes.dump(2);
#endif
        current().images.clear();
        current().gpu_diagnostics.clear();
        current().view.select_tab(current().initial_tab);
        current().view.set_mesh_geometry(current().displayed->before_mesh);
        current().view.set_mesh_geometry(current().displayed->after_mesh, true);
        for (const auto &preview : current().displayed->previews) {
            try {
                auto image = renderer.upload(preview.image);
                if (current().view.set_texture_preview(
                        preview.object_path,
                        {ImTextureID(reinterpret_cast<std::uintptr_t>(image.Get())), int(preview.image.width),
                         int(preview.image.height)},
                        preview.after))
                    current().images.push_back(std::move(image));
            } catch (const std::exception &e) {
                current().gpu_diagnostics.push_back(e.what());
            }
        }
        for (const auto &difference : current().displayed->texture_differences) {
            try {
                cyui::ImageData binding{};
                Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> image;
                if (difference.comparison.comparable) {
                    image = renderer.upload(difference.comparison.heatmap);
                    binding = {ImTextureID(reinterpret_cast<std::uintptr_t>(image.Get())),
                               int(difference.comparison.heatmap.width),
                               int(difference.comparison.heatmap.height)};
                }
                bool bound = difference.hdr_comparison
                                 ? current().view.set_texture_difference(difference.before_object,
                                                                         difference.after_object, binding,
                                                                         *difference.hdr_comparison)
                                 : current().view.set_texture_difference(difference.before_object,
                                                                         difference.after_object, binding,
                                                                         difference.comparison);
                if (bound && image)
                    current().images.push_back(std::move(image));
            } catch (const std::exception &e) {
                current().gpu_diagnostics.push_back(e.what());
            }
        }
        apply_pending_state();
    }
    std::string status() const {
#ifdef CYUEDIFF_HAS_GIT
        if (history_.busy())
            return "Reading Git history...";
#endif
        if (browser_.busy())
            return "Scanning content...";
        if (session_.active_busy())
            return "Loading...";
        if (session_.cancelled())
            return "Cancelled";
        if (!error().empty())
            return "Load failed";
        if (!current().displayed)
            return "Ready";
        auto status = current().displayed->status();
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
                    cyui::ScopedDisabled local_only(current().settings.source != 0);
                    if (ImGui::MenuItem(cyui::trLabel("Open before...").c_str(), "Ctrl+O"))
                        action = Action::open_before;
                    if (ImGui::MenuItem(cyui::trLabel("Open after...").c_str(), "Ctrl+Shift+O"))
                        action = Action::open_after;
                    if (ImGui::MenuItem(cyui::trLabel("Choose project").c_str()))
                        action = Action::project;
                }
                if (ImGui::MenuItem(cyui::trLabel("Open workspace...").c_str(), "Ctrl+Alt+O"))
                    action = Action::open_workspace;
                if (ImGui::MenuItem(cyui::trLabel("Save workspace").c_str(), "Ctrl+S"))
                    action = Action::save_workspace;
                if (ImGui::MenuItem(cyui::trLabel("Save workspace as...").c_str(), "Ctrl+Shift+S"))
                    action = Action::save_workspace_as;
                if (ImGui::MenuItem(cyui::trLabel("New document").c_str(), "Ctrl+N", false,
                                    session_.documents().size() < session_.maximum()))
                    action = Action::new_document;
                if (ImGui::MenuItem(cyui::trLabel("Close document").c_str(), "Ctrl+W"))
                    action = Action::close_document;
                if (ImGui::MenuItem(cyui::trLabel("Duplicate document").c_str(), "Ctrl+D", false,
                                    session_.documents().size() < session_.maximum()))
                    action = Action::duplicate_document;
                if (ImGui::MenuItem(cyui::trLabel("Reopen closed document").c_str(), "Ctrl+Shift+T", false,
                                    !session_.closed().empty() &&
                                        session_.documents().size() < session_.maximum()))
                    action = Action::reopen_document;
                if (ImGui::MenuItem(cyui::trLabel("Close other documents").c_str(), nullptr, false,
                                    session_.documents().size() > 1))
                    action = Action::close_other_documents;
                if (ImGui::MenuItem(cyui::trLabel("Open container...").c_str()))
                    action = Action::open_container;
                if (ImGui::MenuItem(cyui::trLabel("Browse content...").c_str(), "Ctrl+B", false,
                                    current().settings.browse_ready()))
                    action = Action::browse;
#ifdef CYUEDIFF_HAS_GIT
                if (ImGui::MenuItem(cyui::trLabel("Browse history...").c_str(), nullptr, false,
                                    current().settings.source == 1 && !current().settings.repository.empty()))
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
                cyui::ScopedDisabled local_only(current().settings.source != 0);
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
                cyui::ScopedDisabled disabled(!current().settings.browse_ready());
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
        if (cyui::beginSidebar("##inputs", current().layout.sidebar_width())) {
            const auto key = std::to_string(session_.active_id());
            cyui::ScopedID document_scope(key.c_str());
            const bool sidebar_content = ImGui::BeginChild("##input-content", ImVec2(-cyui::dp(6), 0));
            if (sidebar_content) {
                if (!preferences_diagnostic_.empty()) {
                    cyui::callout(cyui::Tone::warning, cyui::tr("Preferences could not be loaded.").c_str());
                    ImGui::TextWrapped("%s", preferences_diagnostic_.c_str());
                }
                cyui::heading(
                    cyui::tr(current().settings.source == 2 ? "Containers" : "Compare packages").c_str(), 3);
                const auto local_label = cyui::tr("Local files"), container_label = cyui::tr("Containers");
#ifdef CYUEDIFF_HAS_GIT
                const auto git_label = cyui::tr("Git revisions");
                const char *sources[] = {local_label.c_str(), git_label.c_str(), container_label.c_str()};
                if(cyui::segmented("##source", sources, 3, &current().settings.source, cyui::dp(-1)))current().settings.pose_enabled=false;
#else
                const char *sources[] = {local_label.c_str(), container_label.c_str()};
                int mode = current().settings.source == 2 ? 1 : 0;
                if (cyui::segmented("##source", sources, 2, &mode, cyui::dp(-1)))
                    {current().settings.source = mode ? 2 : 0;current().settings.pose_enabled=false;}
#endif
                if (current().settings.source == 2) {
                    if (cyui::button(cyui::trLabel("Open container...").c_str()))
                        action = Action::open_container;
                    if (cyui::beginProperties("##container-input", 95)) {
                        cyui::property(cyui::tr("Container file").c_str());
                        field("##container-file", current().settings.container_path);
                        cyui::property(cyui::tr("Cooked profile").c_str());
                        const auto bytes = cyui::tr("Byte inspection");
                        const char *profiles[] = {bytes.c_str(), "UE 5.3", "UE 5.5"};
                        ImGui::Combo("##cooked-profile", &current().settings.cooked_profile, profiles, 3);
                        cyui::property(cyui::tr("Global script TOC").c_str());
                        field("##script-toc", current().settings.script_objects);
                        cyui::property(cyui::tr("After container").c_str());
                        field("##after-container-file", current().settings.after_container_path);
                        cyui::property(cyui::tr("After cooked profile").c_str());
                        ImGui::Combo("##after-cooked-profile", &current().settings.after_cooked_profile,
                                     profiles, 3);
                        cyui::property(cyui::tr("After global script TOC").c_str());
                        field("##after-script-toc", current().settings.after_script_objects);
                        cyui::property(cyui::tr("Codec module").c_str());
                        field("##container-codec", current().settings.codec);
                        cyui::endProperties();
                    }
                    if (current().layout.section("schema", "Schema contexts (optional)",
                                                 !current().settings.container_schema.empty() ||
                                                     !current().settings.after_container_schema.empty())) {
                        if (cyui::beginProperties("##schema-input", 95)) {
                            cyui::property(cyui::tr("Before schema").c_str());
                            field("##schema-before", current().settings.container_schema);
                            cyui::property(cyui::tr("After schema").c_str());
                            field("##schema-after", current().settings.after_container_schema);
                            cyui::endProperties();
                        }
                        if (cyui::button(cyui::trLabel("Choose before schema...").c_str()))
                            action = Action::before_schema;
                        {
                            cyui::ScopedDisabled unavailable(current().settings.after_container_path.empty());
                            if (cyui::button(cyui::trLabel("Choose after schema...").c_str()))
                                action = Action::after_schema;
                        }
                        cyui::endSection();
                    }
                    ImGui::TextWrapped(
                        "%s", cyui::tr("Choose an explicit UE profile for Zen object inspection.").c_str());
                    {
                        cyui::ScopedDisabled unavailable(current().settings.after_container_path.empty());
                        ImGui::Checkbox(cyui::trLabel("Compare all decoded members").c_str(),
                                        &current().settings.compare_container_payloads);
                    }
                    cyui::textDim("%s", cyui::tr("Up to 512 MiB total; 64 MiB per member.").c_str());
                    cyui::ScopedDisabled disabled(!current().settings.ready());
                    if (cyui::primaryButton(cyui::trLabel(current().settings.after_container_path.empty()
                                                              ? "Index container"
                                                              : "Index comparison")
                                                .c_str(),
                                            cyui::dp(-1, 0)))
                        action = Action::container_index;
                } else {
                    if (current().settings.source == 1) {
#ifdef CYUEDIFF_HAS_GIT
                        {
                            cyui::ScopedDisabled disabled(current().settings.repository.empty());
                            if (cyui::button(cyui::trLabel("Browse history...").c_str()))
                                action = Action::history;
                        }
#endif
                        if (cyui::beginProperties("##git", 76)) {
                            cyui::property(cyui::tr("Repository").c_str());
                            field("##repository", current().settings.repository);
                            cyui::property(cyui::tr("Before ref").c_str());
                            field("##before-revision", current().settings.before_revision);
                            cyui::property(cyui::tr("After ref").c_str());
                            field("##after-revision", current().settings.after_revision);
                            cyui::endProperties();
                        }
                        ImGui::TextWrapped("%s", cyui::tr("Use paths relative to the repository, or "
                                                          "/Game/... with a historical project.")
                                                     .c_str());
                    }
                    if (cyui::beginProperties("##files", 76)) {
                        cyui::property(
                            cyui::tr(current().settings.source == 1 ? "Before path" : "Before").c_str());
                        field("##before", current().settings.before);
                        cyui::property(
                            cyui::tr(current().settings.source == 1 ? "After path" : "After").c_str());
                        field("##after", current().settings.after);
                        if (current().settings.source == 1 && current().settings.after.empty())
                            cyui::textFaint("%s", cyui::tr("Same path when empty").c_str());
                        cyui::endProperties();
                    }
                    cyui::space();
                    if (current().layout.section(
                            current().settings.source == 1 ? "historicalProject" : "project",
                            current().settings.source == 1 ? "Historical project (optional)"
                                                           : "Project (optional)")) {
                        if (cyui::beginProperties("##context", 66)) {
                            cyui::property(
                                cyui::tr(current().settings.source == 1 ? "Project path" : "Project file")
                                    .c_str());
                            field("##project", current().settings.project);
                            cyui::property(cyui::tr("Engine root").c_str());
                            field("##engine", current().settings.engine);
                            cyui::property(cyui::tr("After project").c_str());
                            field("##after-project", current().settings.after_project);
                            cyui::property(cyui::tr("After engine").c_str());
                            field("##after-engine", current().settings.after_engine);
                            cyui::endProperties();
                        }
                        cyui::textDim("%s",
                                      cyui::tr("Empty after context inherits the before context.").c_str());
                        if (current().settings.source == 1)
                            cyui::toggleSwitch(cyui::trLabel("Discover engine").c_str(),
                                               &current().settings.discover_engine);
                        if (current().settings.source == 0 &&
                            cyui::button(cyui::trLabel("Choose project").c_str()))
                            action = Action::project;
                        cyui::endSection();
                    }
                    if (current().settings.source == 1 &&
                        current().layout.section("lfs", "LFS cache (optional)")) {
                        if (cyui::beginProperties("##lfs", 76)) {
                            cyui::property(cyui::tr("LFS objects").c_str());
                            field("##lfs-objects", current().settings.lfs_objects);
                            cyui::endProperties();
                        }
                        cyui::endSection();
                    }
                    {
                        cyui::ScopedDisabled disabled(!current().settings.browse_ready());
                        if (cyui::button(cyui::trLabel("Browse content...").c_str(), cyui::Variant::secondary,
                                         cyui::dp(-1, 0)))
                            action = Action::browse;
                    }
                    if (current().settings.source == 0 && current().settings.project.empty())
                        ImGui::TextWrapped("%s", cyui::tr("Choose a project to browse its content.").c_str());
                    if(current().settings.source!=2) {
                        auto &settings=current().settings;
                        const bool inactive_after=settings.source==0 && settings.after.empty();
                        cyui::toggleSwitch(cyui::trLabel("Evaluate source hierarchy pose").c_str(),&settings.pose_enabled);
                        if(settings.pose_enabled && current().layout.section("animationPose","Animation Skeleton binding",true)) {
                            if(cyui::beginProperties("##pose-inputs",105)) {
                                cyui::property(cyui::tr("Skeleton before").c_str());field("##pose-rig-before",settings.pose_before_skeleton);
                                {cyui::ScopedDisabled inactive(inactive_after);
                                cyui::property(cyui::tr("Skeleton after").c_str());field("##pose-rig-after",settings.pose_after_skeleton);}
                                cyui::property(cyui::tr("Frame before").c_str());
                                if(ImGui::InputInt("##pose-frame-before",&settings.pose_before_frame))settings.pose_before_frame=std::max(0,settings.pose_before_frame);
                                cyui::endProperties();
                            }
                            cyui::toggleSwitch(cyui::trLabel("Independent pose frames").c_str(),&settings.pose_independent_frames);
                            if(settings.pose_independent_frames) {
                                cyui::ScopedDisabled inactive(inactive_after);
                                if(cyui::beginProperties("##pose-frame-input",105)) {
                                    cyui::property(cyui::tr("Frame after").c_str());
                                    if(ImGui::InputInt("##pose-frame-after",&settings.pose_after_frame))settings.pose_after_frame=std::max(0,settings.pose_after_frame);
                                    cyui::endProperties();
                                }
                            }
                            if(settings.source==0) {
                            if(cyui::button(cyui::trLabel("Choose Skeleton before").c_str()))action=Action::pose_before_skeleton;
                            {cyui::ScopedDisabled inactive(inactive_after);if(cyui::button(cyui::trLabel("Choose Skeleton after").c_str()))action=Action::pose_after_skeleton;}
                            } else ImGui::TextWrapped("%s",cyui::tr("Skeleton paths use each animation revision and its historical project mounts.").c_str());
                            if(ImGui::TreeNode(cyui::trLabel("Stored owner selections").c_str())) {
                                if(cyui::beginProperties("##pose-owners",105)) {
                                    cyui::property(cyui::tr("Model before").c_str());field("##pose-model-before",settings.pose_before_object);
                                    cyui::property(cyui::tr("Rig object before").c_str());field("##pose-owner-before",settings.pose_before_skeleton_object);
                                    {cyui::ScopedDisabled inactive(inactive_after);
                                    cyui::property(cyui::tr("Model after").c_str());field("##pose-model-after",settings.pose_after_object);
                                    cyui::property(cyui::tr("Rig object after").c_str());field("##pose-owner-after",settings.pose_after_skeleton_object);}
                                    cyui::endProperties();
                                }
                                ImGui::TextWrapped("%s",cyui::tr("Empty selectors require a sole stored object.").c_str());
                                ImGui::TreePop();
                            }
                            ImGui::TextWrapped("%s",cyui::tr("Choose Skeleton paths, then Load to prepare the selected frames.").c_str());
                            cyui::endSection();
                        }
                    }
                    cyui::toggleSwitch(cyui::trLabel("Load previews").c_str(), &current().settings.previews);
                    {
                        cyui::ScopedDisabled disabled(!current().settings.ready());
                        if (cyui::primaryButton(cyui::trLabel("Load").c_str(), cyui::dp(-1, 0)))
                            action = Action::load;
                    }
                    {
                        cyui::ScopedDisabled disabled(current().settings.source == 0
                                                          ? current().settings.after.empty()
                                                          : !current().settings.ready());
                        if (cyui::button(cyui::trLabel(current().settings.source == 1 ? "Swap revisions"
                                                                                      : "Swap files")
                                             .c_str(),
                                         cyui::Variant::secondary, cyui::dp(-1, 0)))
                            action = Action::swap;
                    }
                }
                if (session_.active_busy()) {
                    const auto progress = session_.progress();
                    float fraction =
                        progress.total ? float(double(progress.completed) / double(progress.total)) : 0.f;
                    ImGui::ProgressBar(std::clamp(fraction, 0.f, 1.f), cyui::dp(-1, 0),
                                       cyui::tr(progress.phase.c_str()).c_str());
                    if (cyui::button(cyui::trLabel("Cancel loading").c_str(), cyui::Variant::secondary,
                                     cyui::dp(-1, 0)))
                        action = Action::cancel_load;
                }
                if (current().displayed && !current().displayed->container) {
                    cyui::divider();
                    const auto &present = current().displayed->before ? current().displayed->before
                                                                      : current().displayed->after;
                    cyui::textDim(cyui::tr("%zu exported objects").c_str(), present->exports.size());
#ifdef CYUEDIFF_HAS_GIT
                    if (current().displayed->git) {
                        for (const auto *revision :
                             {&current().displayed->git->before, &current().displayed->git->after}) {
                            cyui::textMono(
                                "%s: %.12s",
                                cyui::tr(revision == &current().displayed->git->before ? "Before" : "After")
                                    .c_str(),
                                revision->commit.c_str());
                            cyui::tooltip(revision->commit.c_str());
                        }
                        if (!current().context_details.empty() &&
                            current().layout.section("contextChanges", "Project context changes")) {
                            ImGui::TextWrapped("%s", current().context_details.c_str());
                            cyui::endSection();
                        }
                    }
#endif
                    if (current().layout.section("diagnostics", "Diagnostics")) {
                        for (const auto *package :
                             {current().displayed->before.get(), current().displayed->after.get()})
                            if (package)
                                for (const auto &message : package->diagnostics)
                                    ImGui::TextWrapped("%s", message.c_str());
                        if (current().displayed->project)
                            for (const auto &message : current().displayed->project->diagnostics)
                                ImGui::TextWrapped("%s", message.c_str());
                        for (const auto &message : current().displayed->diagnostics)
                            ImGui::TextWrapped("%s", message.c_str());
                        for (const auto &message : current().gpu_diagnostics)
                            ImGui::TextWrapped("%s", message.c_str());
                        cyui::endSection();
                    }
                }
            }
            ImGui::EndChild();
            current().layout.resize_sidebar();
        }
        cyui::endSidebar();
        const auto *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        if (ImGui::Begin("##document", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings)) {
            tab_action_ = document_tabs_.draw();
            if (tab_action_.operation != DocumentTabOperation::none)
                action = Action::document_tab;
            const auto key = std::to_string(session_.active_id());
            cyui::ScopedID document_scope(key.c_str());
            auto message = error();
            if (!message.empty()) {
                cyui::callout(cyui::Tone::error, message.c_str());
                if (current().displayed)
                    cyui::textDim("%s", cyui::tr("The previous document remains displayed.").c_str());
            }
            if (current().displayed &&
                (!current().displayed->diagnostics.empty() || !current().gpu_diagnostics.empty()))
                cyui::callout(cyui::Tone::warning, cyui::tr("Additional diagnostics are available.").c_str());
#ifdef CYUEDIFF_HAS_GIT
            if (current().displayed && current().displayed->git &&
                !current().displayed->git->context_changes.empty()) {
                cyui::textDim(cyui::tr("Context changes: %zu").c_str(),
                              current().displayed->git->context_changes.size());
            }
#endif
            if (current().displayed && current().displayed->container) {
                const auto request = current().container_view.draw(session_.active_busy());
                if (request == ContainerAction::inspect)
                    action = Action::container_inspect;
                else if (request == ContainerAction::select_object)
                    action = Action::container_object;
            } else if (current().displayed)
                current().view.draw("##asset", false);
            else if (current().settings.source == 2)
                cyui::emptyState(cyui::tr("Containers").c_str(),
                                 cyui::tr("Choose a Pak or IoStore TOC, then index it.").c_str());
            else
                cyui::emptyState(
                    cyui::tr("No package loaded").c_str(),
                    cyui::tr(current().settings.source == 1
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
                    if (current().settings.source == 0)
                        cyui::toggleSwitch(cyui::trLabel("Include engine content").c_str(),
                                           &current().settings.include_engine);
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
                    history_view_.draw(history_.busy(), history_.error(), current().settings.before_revision,
                                       current().settings.after_revision, history_can_compare());
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
                action = ImGui::GetIO().KeyAlt
                             ? Action::open_workspace
                             : (ImGui::GetIO().KeyShift ? Action::open_after : Action::open_before);
            if (ImGui::IsKeyPressed(ImGuiKey_S, false))
                action = ImGui::GetIO().KeyShift ? Action::save_workspace_as : Action::save_workspace;
            if (ImGui::IsKeyPressed(ImGuiKey_N, false))
                action = Action::new_document;
            if (ImGui::IsKeyPressed(ImGuiKey_D, false))
                action = Action::duplicate_document;
            if (ImGui::IsKeyPressed(ImGuiKey_T, false) && ImGui::GetIO().KeyShift)
                action = Action::reopen_document;
            if (ImGui::IsKeyPressed(ImGuiKey_Tab, false))
                action = ImGui::GetIO().KeyShift ? Action::previous_document : Action::next_document;
            if (ImGui::IsKeyPressed(ImGuiKey_W, false))
                action = Action::close_document;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false))
                action = Action::load;
            if (ImGui::IsKeyPressed(ImGuiKey_B, false) && current().settings.browse_ready())
                action = Action::browse;
        }
        cyui::renderNotifications();
        return action;
    }
    void perform(Action action, NativeWindow &window) {
        try {
            if (action == Action::none)
                return;
            if (action == Action::open_workspace) {
                if (auto path = window.choose_workspace(false))
                    open_workspace(*path);
                return;
            }
            if (action == Action::save_workspace || action == Action::save_workspace_as) {
                auto path = workspace_path_.empty() || action == Action::save_workspace_as
                                ? window.choose_workspace(true)
                                : std::optional<fs::path>(workspace_path_);
                if (path) {
                    write_workspace(*path);
                    cyui::notify(cyui::tr("Workspace saved").c_str(), cyui::Tone::success);
                }
                return;
            }
            if (action == Action::document_tab) {
                if (tab_action_.operation == DocumentTabOperation::create)
                    create_document();
                else if (tab_action_.operation == DocumentTabOperation::select)
                    select_document(tab_action_.id);
                else if (tab_action_.operation == DocumentTabOperation::close)
                    close_document(tab_action_.id);
                else if (tab_action_.operation == DocumentTabOperation::duplicate)
                    duplicate_document(tab_action_.id);
                else if (tab_action_.operation == DocumentTabOperation::close_others)
                    close_other_documents(tab_action_.id);
                return;
            }
            if (action == Action::duplicate_document) {
                duplicate_document(session_.active_id());
                return;
            }
            if (action == Action::reopen_document) {
                reopen_document();
                return;
            }
            if (action == Action::close_other_documents) {
                close_other_documents(session_.active_id());
                return;
            }
            if (action == Action::next_document || action == Action::previous_document) {
                cycle_document(action == Action::previous_document);
                return;
            }
            if (action == Action::new_document) {
                create_document();
                return;
            }
            if (action == Action::close_document) {
                close_document(session_.active_id());
                return;
            }

            current().dialog_error.clear();
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
                    if (current().settings.ready())
                        session_.request(current().settings.request());
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
                current().settings.source = 2;
                current().settings.pose_enabled = false;
                current().settings.container_path = path->u8string();
                current().settings.container_entry.clear();
                current().settings.after_container_path.clear();
                current().settings.after_script_objects.clear();
                current().settings.container_schema.clear();
                current().settings.after_container_schema.clear();
                current().settings.after_cooked_profile = 0;
                current().settings.compare_container_payloads = false;
                session_.request(current().settings.request());
                return;
            }
            if(action==Action::pose_before_skeleton || action==Action::pose_after_skeleton) {
                if(current().settings.source!=0)return;
                if(auto path=window.choose_file())(action==Action::pose_before_skeleton?current().settings.pose_before_skeleton:current().settings.pose_after_skeleton)=path->u8string();
                return;
            }
            if (action == Action::before_schema || action == Action::after_schema) {
                auto path = window.choose_file(false, false, true);
                if (path)
                    (action == Action::before_schema ? current().settings.container_schema
                                                     : current().settings.after_container_schema) =
                        path->u8string();
                return;
            }
            if (action == Action::container_object) {
                current().container_view.apply_object_selection();
                return;
            }
            if (action == Action::container_index || action == Action::container_inspect ||
                (action == Action::load && current().settings.source == 2)) {
                if (action == Action::container_index)
                    current().settings.container_entry.clear();
                else if (action == Action::container_inspect) {
                    const auto *selected = current().container_view.selected_member();
                    if (!selected || !current().displayed || !current().displayed->container)
                        return;
                    const auto &input = current().displayed->container->source;
                    current().settings.source = 2;
                current().settings.pose_enabled = false;
                    current().settings.container_path = input.path.u8string();
                    current().settings.after_container_path = input.after_path.u8string();
                    current().settings.container_entry = current().container_view.selected_key();
                }
                session_.request(current().settings.request());
                return;
            }
            if (current().settings.source != 0 &&
                (action == Action::open_before || action == Action::open_after || action == Action::project))
                return;
            if (action == Action::open_before || action == Action::open_after || action == Action::project) {
                auto path = window.choose_file(action == Action::project);
                if (!path)
                    return;
                if (action == Action::project)
                    current().settings.project = path->u8string();
                else {
                    (action == Action::open_after ? current().settings.after : current().settings.before) =
                        path->u8string();
                }
            } else if (action == Action::swap) {
                std::swap(current().settings.pose_before_skeleton,current().settings.pose_after_skeleton);
                std::swap(current().settings.pose_before_object,current().settings.pose_after_object);
                std::swap(current().settings.pose_before_skeleton_object,current().settings.pose_after_skeleton_object);
                if(current().settings.pose_independent_frames)std::swap(current().settings.pose_before_frame,current().settings.pose_after_frame);
                // Materialize inherited values before swapping so both contexts follow their files.
                if (!current().settings.after_project.empty() || !current().settings.after_engine.empty()) {
                    if (current().settings.after_project.empty())
                        current().settings.after_project = current().settings.project;
                    if (current().settings.after_engine.empty())
                        current().settings.after_engine = current().settings.engine;
                    std::swap(current().settings.project, current().settings.after_project);
                    std::swap(current().settings.engine, current().settings.after_engine);
                }
                if (current().settings.source == 1) {
                    std::swap(current().settings.before_revision, current().settings.after_revision);
                    if (!current().settings.after.empty())
                        std::swap(current().settings.before, current().settings.after);
                } else
                    std::swap(current().settings.before, current().settings.after);
            }
            if (action != Action::project && !current().settings.before.empty())
                session_.request(current().settings.request());
        } catch (const std::exception &e) {
            current().dialog_error = e.what();
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
               "  [--pose-before-skeleton FILE] [--pose-after-skeleton FILE] : loose or Git source hierarchy poses\n"
               "  [--pose-frame N | --pose-before-frame N --pose-after-frame N]\n"
               "  [--pose-before-object PATH] [--pose-after-object PATH]\n"
               "  [--pose-before-skeleton-object PATH] [--pose-after-skeleton-object PATH]\n"
               "  [--project REPO_PROJECT] [--no-engine] [--lfs-objects DIR] : local Git/LFS comparison\n"
               "  --container FILE.pak|FILE.utoc [--container-entry ID_OR_PATH] [--container-filter TEXT]\n"
               "  [--document-action select:N|close|duplicate|reopen|close-others|next|previous|reload] : "
               "smoke only\n"
               "  [--new-document SOURCE_OPTIONS ...] [--document-index 0..7]\n"
               "  --workspace FILE.json : restore documents and contexts\n"
               "  --smoke --save-workspace FILE.json : explicit workspace test write\n"
               "  [--schema CONTEXT.json] [--after-schema CONTEXT.json] : explicit cooked bindings\n"
               "  [--cooked-view properties|object] : select cooked object inspector\n"
               "  [--container-tab members|inspection|objects|comparison|schema]\n"
               "  [--after-container FILE] [--compare-container-bytes] [--container-changes-only]\n"
               "  [--after-cooked-profile ue5.3|ue5.5] [--after-script-objects GLOBAL.utoc]\n"
               "  [--cooked-profile ue5.3|ue5.5] [--script-objects GLOBAL.utoc] : explicit Zen inspection\n"
               "  --repo DIR --history [--history-revision REF] : choose commits in local history\n"
               "  --history --smoke [--history-before ID --history-after ID --history-compare]\n"
               "  --browse [--include-engine] [--browse-filter TEXT] : browse project or revision content\n"
               "  [--codec DLL] [--no-previews] [--locale en|fr] [--theme KEY]\n"
               "  [--view properties|blueprint|materials|textures|meshes|comparison|collisions|skeleton|animations|curves]\n"
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
    if (!options.workspace_input.empty()) {
        options.workspace_input = fs::absolute(options.workspace_input);
        const auto file = desktop::read_workspace(options.workspace_input);
        options = workspace_options(std::move(options), file);
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
            shell.protect(options);
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
        if (options.smoke && !shell.busy() && settled >= 2 && shell.perform_next_document_action())
            settled = 0;
        bool finished = options.smoke && !shell.busy() && ++settled >= 12;
        if (options.smoke && std::chrono::steady_clock::now() - started > std::chrono::seconds(30))
            throw Error("desktop-timeout", "Document load exceeded smoke-test timeout");
        if (finished) {
            if (!options.workspace_output.empty())
                shell.write_workspace(options.workspace_output);
            persist();
            shell.protect(options);
            if (!options.capture.empty())
                write_png(options.capture, renderer.capture());
            Json report = {{"schema", "cyuediff.desktop/1"},
                           {"status", shell.result_error().empty() ? "ready" : "failed"},
                           {"error", shell.result_error()},
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
                           {"workspace", shell.workspace_report()},
                           {"workspaceFile", shell.workspace_file_report()},
                           {"containerView", shell.container_view_report()},
                           {"document", shell.session().document() ? shell.session().document()->report()
                                                                   : Json(nullptr)}};
            if (!options.report.empty()) {
                std::ofstream file(options.report, std::ios::binary);
                file << report.dump(2) << '\n';
                if (!file)
                    throw Error("desktop-output", "Cannot write report");
            }
            return shell.result_error().empty() ? 0 : 2;
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
