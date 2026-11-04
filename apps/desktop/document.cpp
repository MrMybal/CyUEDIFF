#include "document.hpp"
namespace cyuediff::desktop {
Json Document::report() const {
    auto package = [](const std::shared_ptr<const Package> &p) -> Json {
        if (!p)
            return nullptr;
        return {{"source", p->source.u8string()},
                {"exports", p->exports.size()},
                {"completeInput", p->complete_input},
                {"diagnostics", p->diagnostics}};
    };
    Json images = Json::array();
    for (const auto &preview : previews)
        images.push_back({{"object", preview.object_path},
                          {"after", preview.after},
                          {"width", preview.image.width},
                          {"height", preview.image.height}});
    auto mesh = [](const std::shared_ptr<const MeshGeometry> &m) -> Json {
        return m ? Json{{"vertices", m->vertices.size()}, {"triangles", m->triangles.size()}} : Json(nullptr);
    };
    return {{"before", package(before)},
            {"after", package(after)},
            {"project", project ? project->to_json() : Json(nullptr)},
            {"diff", diff ? diff->to_json() : Json(nullptr)},
            {"previews", images},
            {"beforeMesh", mesh(before_mesh)},
            {"afterMesh", mesh(after_mesh)},
            {"diagnostics", diagnostics}};
}
std::shared_ptr<const Document> load_document(const LoadRequest &request) {
    if (request.before.empty())
        throw Error("desktop-input", "Choose a package before loading");
    auto document = std::make_shared<Document>();
    document->request = request;
    if (!request.project.empty())
        document->project = ProjectContext::open(request.project, request.engine_root);
    else if (!request.engine_root.empty())
        throw Error("desktop-project", "An engine root requires a project descriptor");
    auto resolve = [&](const std::string &name) {
        if (document->project && name.rfind("/", 0) == 0)
            return document->project->resolve(name);
        return fs::u8path(name);
    };
    document->before = std::make_shared<const Package>(Package::open(resolve(request.before)));
    if (!request.after.empty()) {
        document->after = std::make_shared<const Package>(Package::open(resolve(request.after)));
        document->diff = compare(*document->before, *document->after);
    }
    if (!request.previews)
        return document;
    BlockDecoder decoder;
    if (!request.codec.empty()) {
        // Explicit host-supplied codec; never discovered from an asset or engine root.
        decoder = load_block_decoder(request.codec);
    }
    auto previews = [&](const std::shared_ptr<const Package> &package, bool after) {
        if (!package)
            return;
        const auto side = after ? "After: " : "Before: ";
        if (!package->textures.empty()) {
            try {
                TextureDecodeOptions options;
                options.decoder = decoder;
                options.max_pixels = 16ull * 1024 * 1024;
                options.max_payload_bytes = 128ull * 1024 * 1024;
                options.max_decoder_bytes = 128ull * 1024 * 1024;
                document->previews.push_back({package->textures.front().object_path, after,
                                              decode_texture(package->textures.front(), 0, options)});
            } catch (const std::exception &e) {
                document->diagnostics.push_back(side + std::string(e.what()));
            }
        }
        if (!package->mesh_sources.empty()) {
            try {
                MeshDecodeOptions options;
                options.decoder = decoder;
                options.max_payload_bytes = 128ull * 1024 * 1024;
                auto mesh =
                    std::make_shared<const MeshGeometry>(decode_mesh(package->mesh_source(), options));
                (after ? document->after_mesh : document->before_mesh) = std::move(mesh);
            } catch (const std::exception &e) {
                document->diagnostics.push_back(side + std::string(e.what()));
            }
        }
    };
    previews(document->before, false);
    previews(document->after, true);
    return document;
}
DocumentSession::DocumentSession(Loader loader) : loader_(std::move(loader)) {
    if (!loader_)
        throw Error("desktop-loader", "Missing document loader");
    worker_ = std::thread([this] { run(); });
}
DocumentSession::~DocumentSession() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        pending_.reset();
    }
    wake_.notify_one();
    worker_.join();
}
void DocumentSession::request(LoadRequest request) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = Work{++generation_, std::move(request)};
        result_.reset();
    }
    busy_ = true;
    error_.clear();
    wake_.notify_one();
}
bool DocumentSession::poll() {
    std::optional<Result> result;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result.swap(result_);
    }
    if (!result || result->id != generation_)
        return false;
    busy_ = false;
    error_ = std::move(result->error);
    if (result->document)
        document_ = std::move(result->document);
    return true;
}
void DocumentSession::run() {
    for (;;) {
        Work work;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return stopping_ || pending_.has_value(); });
            if (stopping_)
                return;
            work = std::move(*pending_);
            pending_.reset();
        }
        Result result{work.id, {}, {}};
        try {
            result.document = loader_(work.request);
            if (!result.document)
                throw Error("desktop-loader", "Loader returned no document");
        } catch (const Error &e) {
            result.error = e.code + ": " + e.what();
        } catch (const std::exception &e) {
            result.error = e.what();
        } catch (...) {
            result.error = "Document loader failed";
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
                return;
            if (work.id == generation_)
                result_ = std::move(result);
        }
    }
}
} // namespace cyuediff::desktop
