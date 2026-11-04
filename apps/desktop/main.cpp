#include "document.hpp"
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
    std::string before, after, project, engine, codec;
    bool previews = true;
    LoadRequest request() const {
        return {before, after, fs::u8path(project), fs::u8path(engine), fs::u8path(codec), previews};
    }
};
struct Options {
    Settings settings;
    bool smoke = false, help = false;
    fs::path capture, report;
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
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto &argument = args[i];
        auto value = [&]() {
            if (++i >= args.size())
                throw Error("desktop-option", "Missing value for " + argument);
            return args[i];
        };
        if (argument == "--smoke")
            options.smoke = true;
        else if (argument == "--help" || argument == "-h")
            options.help = true;
        else if (argument == "--before")
            options.settings.before = value();
        else if (argument == "--after")
            options.settings.after = value();
        else if (argument == "--project")
            options.settings.project = value();
        else if (argument == "--engine-root")
            options.settings.engine = value();
        else if (argument == "--codec")
            options.settings.codec = value();
        else if (argument == "--no-previews")
            options.settings.previews = false;
        else if (argument == "--capture")
            options.capture = fs::u8path(value());
        else if (argument == "--report")
            options.report = fs::u8path(value());
        else if (argument == "--theme")
            options.theme = value();
        else if (argument == "--locale")
            options.locale = value();
        else if (argument == "--view")
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
    tab(options.view);
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
    AssetView::register_translations();
    cyui::registerTranslations(
        "fr", {{"File", "Fichier"},
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
               {"Some previews could not be loaded. See Diagnostics.",
                "Certains aperçus n’ont pas pu être chargés. Consultez les diagnostics."},
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
void protect_outputs(const Options &options, const std::shared_ptr<const Document> &document) {
    if (options.capture.empty() && options.report.empty())
        return;
    std::vector<fs::path> inputs;
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
    std::optional<ProjectContext> project;
    for (const auto &path : {options.settings.before, options.settings.after}) {
        if (path.empty())
            continue;
        if (!options.settings.project.empty() && path.front() == '/') {
            if (!project)
                project = ProjectContext::open(fs::u8path(options.settings.project),
                                               fs::u8path(options.settings.engine));
            package(project->resolve(path));
        } else
            package(fs::u8path(path));
    }
    if (document) {
        package(document->before->source);
        if (document->after)
            package(document->after->source);
    }
    for (auto path : {options.settings.project, options.settings.codec})
        if (!path.empty())
            inputs.push_back(fs::u8path(path));
    if (!options.settings.engine.empty())
        inputs.push_back(fs::u8path(options.settings.engine) / "Engine/Build/Build.version");
    for (const auto &output : {options.capture, options.report})
        if (!output.empty())
            for (const auto &input : inputs)
                if (same_path(input, output))
                    throw Error("desktop-output", "Output overlaps an input");
    if (!options.capture.empty() && !options.report.empty() && same_path(options.capture, options.report))
        throw Error("desktop-output", "Outputs overlap");
}
enum class Action { none, open_before, open_after, project, load, swap };
class Shell {
    DocumentSession session_;
    std::shared_ptr<const Document> displayed_;
    AssetView view_;
    std::vector<ComPtr<ID3D11ShaderResourceView>> images_;
    std::vector<std::string> gpu_diagnostics_;
    std::string dialog_error_;
    Settings settings_;
    AssetTab initial_tab_;

  public:
    explicit Shell(const Options &options) : settings_(options.settings), initial_tab_(tab(options.view)) {
        if (!settings_.before.empty())
            session_.request(settings_.request());
    }
    ~Shell() {
        view_.set_packages({});
        images_.clear();
    }
    const DocumentSession &session() const {
        return session_;
    }
    std::size_t images() const {
        return images_.size();
    }
    void poll(Renderer &renderer) {
        session_.poll();
        if (session_.document() == displayed_)
            return;
        displayed_ = session_.document();
        view_.set_packages(displayed_->before, displayed_->after);
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
    }
    std::string status() const {
        if (session_.busy())
            return "Loading...";
        if (!session_.error().empty() || !dialog_error_.empty())
            return "Load failed";
        if (!displayed_)
            return "Ready";
        if (displayed_->diff) {
            auto status = displayed_->diff->status();
            if (status == "changed")
                return "Changed";
            if (status == "identical")
                return "Identical";
            if (status == "binary-only")
                return "Binary changes";
            if (status == "incomplete")
                return "Incomplete input";
            return "Unverified";
        }
        return displayed_->before->complete_input ? "Ready" : "Incomplete input";
    }
    Action draw(bool &quit) {
        Action action = Action::none;
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu(cyui::trLabel("File").c_str())) {
                if (ImGui::MenuItem(cyui::trLabel("Open before...").c_str(), "Ctrl+O"))
                    action = Action::open_before;
                if (ImGui::MenuItem(cyui::trLabel("Open after...").c_str(), "Ctrl+Shift+O"))
                    action = Action::open_after;
                if (ImGui::MenuItem(cyui::trLabel("Choose project").c_str()))
                    action = Action::project;
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
            if (cyui::iconButton(cyui::icon::folder, cyui::tr("Open before (Ctrl+O)").c_str()))
                action = Action::open_before;
            ImGui::SameLine();
            if (cyui::iconButton(cyui::icon::document, cyui::tr("Open after (Ctrl+Shift+O)").c_str()))
                action = Action::open_after;
            ImGui::SameLine();
            if (cyui::iconButton(cyui::icon::refresh, cyui::tr("Reload (Ctrl+R)").c_str()))
                action = Action::load;
            ImGui::SameLine();
            cyui::badge(cyui::tr("Read only").c_str());
        }
        cyui::endTopBar();
        if (cyui::beginStatusBar("##status")) {
            cyui::statusText(session_.error().empty() ? cyui::Tone::neutral : cyui::Tone::error, "%s",
                             cyui::tr(status().c_str()).c_str());
            if (session_.busy()) {
                ImGui::SameLine();
                cyui::spinner("##loading");
            }
        }
        cyui::endStatusBar();
        if (cyui::beginSidebar("##inputs", 310)) {
            cyui::heading(cyui::tr("Compare packages").c_str(), 3);
            if (cyui::beginProperties("##files", 66)) {
                cyui::property(cyui::tr("Before").c_str());
                field("##before", settings_.before);
                cyui::property(cyui::tr("After").c_str());
                field("##after", settings_.after);
                cyui::endProperties();
            }
            cyui::space();
            if (cyui::beginSection(cyui::trLabel("Project (optional)").c_str(), false)) {
                if (cyui::beginProperties("##context", 66)) {
                    cyui::property(cyui::tr("Project file").c_str());
                    field("##project", settings_.project);
                    cyui::property(cyui::tr("Engine root").c_str());
                    field("##engine", settings_.engine);
                    cyui::endProperties();
                }
                if (cyui::button(cyui::trLabel("Choose project").c_str()))
                    action = Action::project;
                cyui::endSection();
            }
            cyui::toggleSwitch(cyui::trLabel("Load previews").c_str(), &settings_.previews);
            {
                cyui::ScopedDisabled disabled(settings_.before.empty());
                if (cyui::primaryButton(cyui::trLabel("Load").c_str(), cyui::dp(-1, 0)))
                    action = Action::load;
            }
            {
                cyui::ScopedDisabled disabled(settings_.after.empty());
                if (cyui::button(cyui::trLabel("Swap files").c_str(), cyui::Variant::secondary,
                                 cyui::dp(-1, 0)))
                    action = Action::swap;
            }
            if (displayed_) {
                cyui::divider();
                cyui::textDim(cyui::tr("%zu exported objects").c_str(), displayed_->before->exports.size());
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
            const auto &error = dialog_error_.empty() ? session_.error() : dialog_error_;
            if (!error.empty()) {
                cyui::callout(cyui::Tone::error, error.c_str());
                if (displayed_)
                    cyui::textDim("%s", cyui::tr("The previous document remains displayed.").c_str());
            }
            if (displayed_ && (!displayed_->diagnostics.empty() || !gpu_diagnostics_.empty()))
                cyui::callout(cyui::Tone::warning,
                              cyui::tr("Some previews could not be loaded. See Diagnostics.").c_str());
            if (displayed_)
                view_.draw("##asset", false);
            else
                cyui::emptyState(cyui::tr("No package loaded").c_str(),
                                 cyui::tr("Choose a package on the left, then load it. Add a second package "
                                          "to compare versions.")
                                     .c_str(),
                                 cyui::icon::document);
        }
        ImGui::End();
        if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_O, false))
                action = ImGui::GetIO().KeyShift ? Action::open_after : Action::open_before;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false))
                action = Action::load;
        }
        cyui::renderNotifications();
        return action;
    }
    void perform(Action action, NativeWindow &window) {
        try {
            if (action == Action::none)
                return;
            dialog_error_.clear();
            if (action == Action::open_before || action == Action::open_after || action == Action::project) {
                auto path = window.choose_file(action == Action::project);
                if (!path)
                    return;
                if (action == Action::project)
                    settings_.project = path->u8string();
                else {
                    (action == Action::open_after ? settings_.after : settings_.before) = path->u8string();
                }
            } else if (action == Action::swap)
                std::swap(settings_.before, settings_.after);
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
int run(const Options &options) {
    if (options.help) {
        std::cout << "CyUEDiff desktop [BEFORE.uasset [AFTER.uasset]] [--project FILE] [--engine-root DIR]\n"
                     "  [--codec DLL] [--no-previews] [--locale en|fr] [--theme KEY]\n"
                     "  [--view properties|blueprint|materials|textures|meshes|comparison]\n"
                     "  --smoke [--capture FILE.png] [--report FILE.json] : hidden DX11/WARP validation\n";
        return 0;
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
        bool finished = options.smoke && !shell.session().busy() && ++settled >= 12;
        if (options.smoke && std::chrono::steady_clock::now() - started > std::chrono::seconds(30))
            throw Error("desktop-timeout", "Document load exceeded smoke-test timeout");
        if (finished) {
            protect_outputs(options, shell.session().document());
            if (!options.capture.empty())
                write_png(options.capture, renderer.capture());
            Json report = {{"schema", "cyuediff.desktop/1"},
                           {"status", shell.session().error().empty() ? "ready" : "failed"},
                           {"error", shell.session().error()},
                           {"frames", frames},
                           {"drawVertices", ImGui::GetDrawData()->TotalVtxCount},
                           {"renderer", "Direct3D11 WARP"},
                           {"theme", cyui::currentThemeKey()},
                           {"locale", cyui::locale()},
                           {"view", options.view},
                           {"uploadedPreviews", shell.images()},
                           {"document", shell.session().document() ? shell.session().document()->report()
                                                                   : Json(nullptr)}};
            if (!options.report.empty()) {
                std::ofstream file(options.report, std::ios::binary);
                file << report.dump(2) << '\n';
                if (!file)
                    throw Error("desktop-output", "Cannot write report");
            }
            return shell.session().error().empty() ? 0 : 2;
        }
        renderer.present(!options.smoke);
        shell.perform(action, window);
        if (options.smoke)
            Sleep(2);
    }
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
