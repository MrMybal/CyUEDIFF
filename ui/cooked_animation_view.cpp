#include <cyuediff/cooked_animation_view.hpp>
#include <cyuediff/view_state.hpp>
#include <cmath>
#include "../src/animation_transform.hpp"
#include "diff_style.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace cyuediff {
namespace {
struct Selection {
    std::string track;
    double frames[2]{}, position = 0, rotation = 0, scale = 0;
    unsigned component = 0;
    std::uint32_t keys[2]{};
    bool comparing = false;
};
struct TrackRow {
    std::string key, name, stored = "unavailable", frame = "unavailable";
    std::size_t tracks[2]{SIZE_MAX, SIZE_MAX};
    bool stored_known = false, frame_known = false;
};
const char *components[] = {"Position X", "Position Y", "Position Z", "Rotation X", "Rotation Y",
                            "Rotation Z", "Rotation W", "Scale X",    "Scale Y",    "Scale Z"};
const char *channel(unsigned component) {
    return component < 3 ? "translation" : component < 7 ? "rotation" : "scale";
}
const char *pose_channel(unsigned component) {
    return component < 3 ? "positions" : component < 7 ? "rotations" : "scales";
}
unsigned axis(unsigned component) {
    return component < 3 ? component : component < 7 ? component - 3 : component - 7;
}
bool changed(const TrackRow &r) {
    return (r.stored_known && r.stored != "identical") || (r.frame_known && r.frame != "identical");
}
ChangeKind style(const std::string &s) {
    return s == "added"     ? ChangeKind::added
           : s == "removed" ? ChangeKind::removed
           : s == "changed" ? ChangeKind::modified
                            : ChangeKind::unchanged;
}
Json encoding(const Json &meta, std::size_t track) {
    const auto &a = meta.at("animation");
    const auto &t = a.at("tracks").at(track);
    Json j = {{"frames", a.at("frames")},
              {"scaleOffsetsPresent", a.at("scaleOffsetsPresent")},
              {"interpolation",
               meta.at("interpolation").is_object() ? meta.at("interpolation").at("mode") : Json(nullptr)}};
    for (const auto *c : {"translation", "rotation", "scale"}) {
        const auto &v = t.at(c);
        j[c] = {{"sha256", v.at("encodedSha256")},
                {"keys", v.at("keys")},
                {"format", v.at("format")},
                {"flags", v.at("flags")}};
    }
    return j;
}
bool usable(const std::shared_ptr<const CookedAnimation> &a, const Json &meta, double frame) {
    return a && a->decoded() && a->frame_count() && frame <= double(*a->frame_count() - 1) &&
           meta.at("interpolation").is_object();
}
Json local(const std::shared_ptr<const CookedAnimation> &a, std::size_t track, double frame,
           const OperationControl &c) {
    if (!a || track == SIZE_MAX)
        return nullptr;
    try {
        return a->sample_track(track, frame, c);
    } catch (const Error &e) {
        if (e.code == "cancelled")
            throw;
        return {{"ready", false}, {"unavailable", e.code}, {"frame", frame}};
    }
}
} // namespace
struct CookedAnimationView::Review {
    Selection selection;
    std::shared_ptr<const Json> metadata[2];
    std::vector<TrackRow> rows;
    std::vector<std::array<double, 2>> plots[2], stored[2];
    Json details[2], metrics = nullptr, summary;
};
struct CookedAnimationView::Worker {
    struct Work {
        std::uint64_t id;
        std::shared_ptr<const CookedAnimation> models[2];
        Selection selection;
        std::shared_ptr<std::atomic<bool>> cancel;
    };
    struct Result {
        std::uint64_t id = 0;
        std::shared_ptr<const Review> value;
        std::string error;
    };
    std::mutex mutex;
    std::condition_variable wake;
    std::optional<Work> pending;
    std::optional<Result> result;
    bool stopping = false;
    std::shared_ptr<std::atomic<bool>> cancel;
    std::thread thread;
    std::shared_ptr<const CookedAnimation> cached[2];
    std::shared_ptr<const Json> metadata[2];
    Worker() : thread([this] { run(); }) {}
    ~Worker() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            if (cancel)
                cancel->store(true);
            pending.reset();
        }
        wake.notify_one();
        thread.join();
    }
    void submit(Work w) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (cancel)
                cancel->store(true);
            cancel = w.cancel;
            pending = std::move(w);
            result.reset();
        }
        wake.notify_one();
    }
    std::optional<Result> take() {
        std::lock_guard<std::mutex> lock(mutex);
        auto r = std::move(result);
        result.reset();
        return r;
    }
    std::shared_ptr<const Review> prepare(const Work &w) {
        OperationControl control;
        control.cancelled = [flag = w.cancel] { return flag->load(); };
        control.check();
        auto out = std::make_shared<Review>();
        out->selection = w.selection;
        std::map<std::string, TrackRow> rows;
        for (unsigned side = 0; side < 2; ++side) {
            if (cached[side] != w.models[side]) {
                auto fresh = w.models[side] ? std::make_shared<Json>(w.models[side]->catalog(control))
                                            : std::make_shared<Json>(Json(nullptr));
                control.check();
                cached[side] = w.models[side];
                metadata[side] = std::move(fresh);
            }
            if (!metadata[side])
                metadata[side] = std::make_shared<Json>(Json(nullptr));
            out->metadata[side] = metadata[side];
            const auto &m = *metadata[side];
            if (!w.models[side] || !m.at("boneChannelsDecoded").get<bool>())
                continue;
            std::map<std::int32_t, std::string> names;
            if (m.at("identitiesComplete").get<bool>())
                for (const auto &b : m.at("skeleton").at("bones"))
                    names[b.at("id").get<std::int32_t>()] = b.at("name").get<std::string>();
            const auto &tracks = m.at("animation").at("tracks");
            for (std::size_t i = 0; i < tracks.size(); ++i) {
                control.check();
                const auto bone = tracks[i].at("skeletonIndex").get<std::int32_t>();
                auto name = names.find(bone);
                const auto key =
                    name == names.end() ? "index:" + std::to_string(bone) : "name:" + name->second;
                auto &row = rows[key];
                row.key = key;
                row.name = name == names.end() ? std::to_string(bone) : name->second;
                row.tracks[side] = i;
            }
        }
        const bool both = w.models[0] && w.models[1] && w.models[0]->decoded() && w.models[1]->decoded();
        const bool identified = both && w.models[0]->skeleton_bound() && w.models[1]->skeleton_bound();
        const bool available[2] = {w.models[0] && !metadata[0]->is_null() &&
                                       usable(w.models[0], *metadata[0], w.selection.frames[0]),
                                   w.models[1] && !metadata[1]->is_null() &&
                                       usable(w.models[1], *metadata[1], w.selection.frames[1])};
        std::size_t changed_frames = 0, unknown = 0, stored_changes = 0;
        bool complete = w.selection.comparing && identified && available[0] && available[1];
        if (out->selection.track.empty() && !rows.empty())
            out->selection.track = rows.begin()->first;
        for (auto &entry : rows) {
            control.check();
            auto &row = entry.second;
            const bool present[2] = {row.tracks[0] != SIZE_MAX, row.tracks[1] != SIZE_MAX};
            if (identified) {
                row.stored_known = true;
                row.stored =
                    present[0] && present[1]
                        ? (encoding(*metadata[0], row.tracks[0]) == encoding(*metadata[1], row.tracks[1])
                               ? "identical"
                               : "changed")
                    : present[0] ? "removed"
                                 : "added";
                stored_changes += row.stored != "identical";
            }
            Json sample[2] = {local(w.models[0], row.tracks[0], w.selection.frames[0], control),
                              local(w.models[1], row.tracks[1], w.selection.frames[1], control)};
            Json metrics = nullptr;
            if (identified && available[0] && available[1]) {
                if (present[0] && present[1]) {
                    metrics = detail::compare_local_animation_values(
                        sample[0], sample[1], w.selection.position, w.selection.rotation, w.selection.scale,
                        control);
                    if (metrics.value("ready", false)) {
                        row.frame_known = true;
                        row.frame = metrics.at("changed").get<bool>() ? "changed" : "identical";
                    }
                } else {
                    row.frame_known = true;
                    row.frame = present[0] ? "removed" : "added";
                }
            }
            if (w.selection.comparing) {
                if (row.frame_known)
                    changed_frames += row.frame != "identical";
                else {
                    ++unknown;
                    complete = false;
                }
            }
            if (row.key == out->selection.track) {
                out->metrics = metrics;
                for (unsigned side = 0; side < 2; ++side)
                    if (w.models[side] && present[side]) {
                        Json preview = w.models[side]->key_preview(row.tracks[side], 2048, control);
                        const auto &chosen = preview.at(channel(w.selection.component));
                        for (const auto &point : chosen.at("points"))
                            out->stored[side].push_back(
                                {point.at("frame").get<double>(),
                                 point.at("value").at(axis(w.selection.component)).get<double>()});
                        Json native_key = nullptr;
                        if (chosen.at("keys").get<std::size_t>() > w.selection.keys[side])
                            native_key = w.models[side]->stored_key(
                                row.tracks[side],
                                static_cast<CompressedAnimationChannel>(w.selection.component < 3   ? 0
                                                                        : w.selection.component < 7 ? 1
                                                                                                    : 2),
                                w.selection.keys[side], control);
                        for (const auto *c : {"translation", "rotation", "scale"})
                            preview[c].erase("points");
                        out->details[side] = {{"currentFrame", sample[side]},
                                              {"storedChannels", std::move(preview)},
                                              {"selectedStoredKey",
                                               {{"requestedIndex", w.selection.keys[side]},
                                                {"ready", !native_key.is_null()},
                                                {"value", std::move(native_key)}}}};
                        if (w.models[side]->frame_count() &&
                            metadata[side]->at("interpolation").is_object()) {
                            const auto count = *w.models[side]->frame_count();
                            const unsigned n = count > 1 ? 257 : 1;
                            for (unsigned i = 0; i < n; ++i) {
                                control.check();
                                const double frame =
                                    n > 1 ? double(i) * double(count - 1) / double(n - 1) : 0;
                                const auto p = w.models[side]->sample_track(row.tracks[side], frame, control);
                                if (!p.value("ready", false))
                                    break;
                                out->plots[side].push_back({frame, p.at(pose_channel(w.selection.component))
                                                                       .at(axis(w.selection.component))
                                                                       .get<double>()});
                            }
                        }
                    }
            }
            out->rows.push_back(std::move(row));
        }
        out->summary = {{"schema", "cyuediff.cooked-animation-review/1"},
                        {"scope", "stored encoding and sampled compressed bone-local frames"},
                        {"complete", complete},
                        {"changedFrames", changed_frames},
                        {"unavailableTracks", unknown},
                        {"changedStoredTracks", stored_changes},
                        {"status", !w.selection.comparing               ? "inspection"
                                   : !complete                          ? "incomplete"
                                   : (changed_frames || stored_changes) ? "changed"
                                                                        : "identical"}};
        control.check();
        return out;
    }
    void run() {
        for (;;) {
            std::optional<Work> work;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [this] { return stopping || pending.has_value(); });
                if (stopping)
                    return;
                work = std::move(pending);
                pending.reset();
            }
            Result output;
            output.id = work->id;
            try {
                output.value = prepare(*work);
            } catch (const Error &e) {
                if (e.code == "cancelled")
                    continue;
                output.error = e.code + ": " + e.what();
            } catch (const std::exception &e) {
                output.error = e.what();
            }
            std::lock_guard<std::mutex> lock(mutex);
            if (!stopping && !work->cancel->load())
                result = std::move(output);
        }
    }
};
CookedAnimationView::CookedAnimationView() = default;
CookedAnimationView::~CookedAnimationView() = default;
CookedAnimationView::CookedAnimationView(CookedAnimationView &&) noexcept = default;
CookedAnimationView &CookedAnimationView::operator=(CookedAnimationView &&) noexcept = default;
void CookedAnimationView::request() {
    ++generation_;
    error_.clear();
    if (!before_ && !after_) {
        worker_.reset();
        busy_ = false;
        review_.reset();
        visible_.clear();
        return;
    }
    if (!worker_)
        worker_ = std::make_unique<Worker>();
    Selection s;
    s.track = selected_;
    s.frames[0] = frame_;
    s.frames[1] = independent_ ? after_frame_ : frame_;
    s.position = position_tolerance_;
    s.rotation = rotation_tolerance_;
    s.scale = scale_tolerance_;
    s.component = component_;
    s.keys[0] = keys_[0];
    s.keys[1] = keys_[1];
    s.comparing = comparing_;
    Worker::Work w{generation_, {before_, after_}, s, std::make_shared<std::atomic<bool>>(false)};
    busy_ = true;
    worker_->submit(std::move(w));
}
void CookedAnimationView::set_animations(std::shared_ptr<const CookedAnimation> a,
                                         std::shared_ptr<const CookedAnimation> b, bool comparing) {
    before_ = std::move(a);
    after_ = std::move(b);
    comparing_ = comparing || bool(after_);
    curves_.set_animations(before_, after_, comparing_);
    review_.reset();
    selected_.clear();
    frame_ = after_frame_ = 0;
    keys_[0] = keys_[1] = 0;
    request();
}
bool CookedAnimationView::poll() {
    const bool curves_changed = curves_.poll();
    if (!worker_)
        return curves_changed;
    auto output = worker_->take();
    if (!output || output->id != generation_)
        return curves_changed;
    busy_ = false;
    error_ = output->error;
    if (output->value) {
        review_ = std::move(output->value);
        if (selected_.empty())
            selected_ = review_->selection.track;
        rebuild();
        inspect();
    }
    return true;
}
void CookedAnimationView::rebuild() {
    visible_.clear();
    if (!review_)
        return;
    for (std::size_t i = 0; i < review_->rows.size(); ++i) {
        const auto &r = review_->rows[i];
        if ((!comparing_ || !changed_only_ || changed(r)) && filter_.matches(r.name, r.key))
            visible_.push_back(i);
    }
}
void CookedAnimationView::inspect() {
    const auto state = details_state_.is_object() ? details_state_ : details_.view_state();
    Json values[2];
    bool known = false;
    if (review_) {
        values[0] = review_->details[0];
        values[1] = review_->details[1];
        for (const auto &r : review_->rows)
            if (r.key == selected_)
                known = r.frame_known;
    }
    if (comparing_ && known)
        details_.set_values(values[0].is_null() ? nullptr : &values[0],
                            values[1].is_null() ? nullptr : &values[1], true);
    else
        details_.set_values(!values[0].is_null()   ? &values[0]
                            : !values[1].is_null() ? &values[1]
                                                   : nullptr,
                            nullptr, false);
    details_.restore_view_state(state);
    details_state_ = nullptr;
}
bool CookedAnimationView::select_track(const std::string &key) {
    selected_ = key;
    scroll_selection_ = true;
    request();
    if (!review_)
        return false;
    return std::any_of(review_->rows.begin(), review_->rows.end(),
                       [&](const auto &r) { return r.key == key; });
}
void CookedAnimationView::set_frames(double a, double b, bool independent) {
    view_state::scalar(Json(a), 0, 2147483646.);
    view_state::scalar(Json(b), 0, 2147483646.);
    frame_ = a;
    after_frame_ = b;
    independent_ = independent;
    request();
}
void CookedAnimationView::set_component(unsigned n) {
    if (n > 9)
        throw Error("view-state", "Invalid animation component");
    component_ = n;
    request();
}
void CookedAnimationView::set_key(std::uint32_t n, bool after) {
    if (n > INT32_MAX)
        throw Error("view-state", "Invalid stored key index");
    keys_[after ? 1 : 0] = n;
    request();
}
void CookedAnimationView::set_tolerances(double p, double r, double s) {
    detail::validate_animation_tolerances(p, r, s);
    position_tolerance_ = p;
    rotation_tolerance_ = r;
    scale_tolerance_ = s;
    request();
}
void CookedAnimationView::set_filter(std::string s) {
    filter_.query = std::move(s);
    rebuild();
}
void CookedAnimationView::set_changed_only(bool b) {
    changed_only_ = b;
    rebuild();
}
bool CookedAnimationView::next_change(bool previous) {
    if (!review_ || visible_.empty())
        return false;
    auto selected = std::find_if(visible_.begin(), visible_.end(),
                                 [&](auto i) { return review_->rows[i].key == selected_; });
    const auto start = selected == visible_.end() ? std::size_t(previous ? 0 : visible_.size() - 1)
                                                  : std::size_t(selected - visible_.begin());
    for (std::size_t n = 1; n <= visible_.size(); ++n) {
        auto pos = previous ? (start + visible_.size() - n) % visible_.size() : (start + n) % visible_.size();
        const auto &r = review_->rows[visible_[pos]];
        if (changed(r))
            return select_track(r.key);
    }
    return false;
}
Json CookedAnimationView::view_state() const {
    return {{"curves", curves_.view_state()},
            {"scalarMode", scalar_mode_},
            {"selected", selected_},
            {"frame", frame_},
            {"afterFrame", after_frame_},
            {"independentFrames", independent_},
            {"component", component_},
            {"beforeKey", keys_[0]},
            {"afterKey", keys_[1]},
            {"positionTolerance", position_tolerance_},
            {"rotationToleranceDegrees", rotation_tolerance_},
            {"scaleTolerance", scale_tolerance_},
            {"filter", filter_.query},
            {"changedOnly", changed_only_},
            {"properties", details_state_.is_object() ? details_state_ : details_.view_state()}};
}
void CookedAnimationView::restore_view_state(const Json &j) {
    view_state::validate(j, "cooked-animation");
    scalar_mode_ = j.value("scalarMode", scalar_mode_);
    if (j.contains("curves"))
        curves_.restore_view_state(j.at("curves"));
    selected_ = j.value("selected", selected_);
    if (j.contains("selected"))
        scroll_selection_ = true;
    frame_ = j.value("frame", frame_);
    after_frame_ = j.value("afterFrame", after_frame_);
    independent_ = j.value("independentFrames", independent_);
    component_ = j.value("component", component_);
    keys_[0] = j.value("beforeKey", keys_[0]);
    keys_[1] = j.value("afterKey", keys_[1]);
    position_tolerance_ = j.value("positionTolerance", position_tolerance_);
    rotation_tolerance_ = j.value("rotationToleranceDegrees", rotation_tolerance_);
    scale_tolerance_ = j.value("scaleTolerance", scale_tolerance_);
    filter_.query = j.value("filter", filter_.query);
    changed_only_ = j.value("changedOnly", changed_only_);
    if (j.contains("properties"))
        details_state_ = j.at("properties");
    rebuild();
    request();
}
Json CookedAnimationView::report() const {
    Json j = {{"scalarMode", scalar_mode_},
              {"curves", curves_.report()},
              {"busy", busy()},
              {"error", error_},
              {"current", !busy() && error_.empty() && bool(review_)},
              {"selected", selected_},
              {"visible", visible_.size()},
              {"renderedRows", rendered_rows_},
              {"selectedVisible", selected_visible_},
              {"requestedFrame", frame_},
              {"requestedAfterFrame", independent_ ? after_frame_ : frame_}};
    if (review_) {
        j["comparison"] = review_->summary;
        j["preparedFrame"] = review_->selection.frames[0];
        j["preparedAfterFrame"] = review_->selection.frames[1];
        j["metrics"] = review_->metrics;
        j["before"] = review_->details[0];
        j["after"] = review_->details[1];
        j["plotPoints"] = {review_->plots[0].size(), review_->plots[1].size()};
        j["storedPoints"] = {review_->stored[0].size(), review_->stored[1].size()};
        j["tracks"] = Json::array();
        for (const auto &r : review_->rows)
            j["tracks"].push_back({{"key", r.key},
                                   {"name", r.name},
                                   {"storedStatus", r.stored},
                                   {"frameStatus", r.frame},
                                   {"storedKnown", r.stored_known},
                                   {"frameKnown", r.frame_known}});
    }
    return j;
}

void CookedAnimationView::register_translations() {
    PropertyView::register_translations();
    CookedCurveView::register_translations();
    cyui::registerTranslations("fr", {{"Scalar curves", "Courbes scalaires"}});
    cyui::registerTranslations("en", {{"decode-compressed-animation", "Decoding compressed bone tracks"},
                                      {"sample-compressed-animation", "Sampling compressed bone tracks"},
                                      {"compare-cooked-animation", "Comparing compressed bone tracks"}});
    cyui::registerTranslations(
        "fr", {{"decode-compressed-animation", "Décodage des pistes d’os compressées"},
               {"sample-compressed-animation", "Échantillonnage des pistes d’os compressées"},
               {"compare-cooked-animation", "Comparaison des pistes d’os compressées"}});
    cyui::registerTranslations("fr", {{"%s frame domain: 0 .. %u", "%s : plage de frames 0 .. %u"}});
    cyui::registerTranslations(
        "fr",
        {{"Bind the referenced compression settings to decode bone tracks.",
          "Liez les réglages de compression référencés pour décoder les pistes osseuses."},
         {"Bind the referenced Skeleton to name and compare bone tracks.",
          "Liez le Skeleton référencé pour nommer et comparer les pistes osseuses."},
         {"Choose the producer interpolation when the file omits it.",
          "Choisissez l’interpolation du producteur lorsque le fichier l’omet."},
         {"Compressed curve values remain unavailable.",
          "Les valeurs des courbes compressées restent indisponibles."},
         {"Selected compression decoder or payload is unavailable.",
          "Le décodeur de compression sélectionné ou son payload est indisponible."},
         {"Tolerances", "Tolérances"},
         {"Bone", "Os"},
         {"Unavailable", "Indisponible"},
         {"Modified", "Modifié"},
         {"Added", "Ajouté"},
         {"Removed", "Supprimé"},
         {"Unchanged", "Inchangé"},
         {"No matching bone track", "Aucune piste d’os correspondante"},
         {"Previous changed entry", "Élément modifié précédent"},
         {"Next changed entry", "Élément modifié suivant"},
         {"Cooked animation", "Animation cooked"},
         {"Cooked animations", "Animations cooked"},
         {"Compressed bone-local samples; no retargeting, additive base or skinning.",
          "Échantillons compressés locaux aux os ; sans retargeting, base additive ni skinning."},
         {"Preparing compressed animation review…", "Préparation de la comparaison d’animation compressée…"},
         {"No decoded cooked animation", "Aucune animation cooked décodée"},
         {"Stored encoding", "Encodage enregistré"},
         {"Selected frame", "Frame sélectionnée"},
         {"Compressed frame before", "Frame compressée avant"},
         {"Compressed frame after", "Frame compressée après"},
         {"Compressed frame", "Frame compressée"},
         {"Independent compressed frames", "Frames compressées indépendantes"},
         {"Changed tracks", "Pistes modifiées"},
         {"Sampled compressed channel", "Canal compressé échantillonné"},
         {"Stored key before", "Clé enregistrée avant"},
         {"Stored key after", "Clé enregistrée après"},
         {"Stored key", "Clé enregistrée"},
         {"Before: orange | After: green | Dots: stored key preview.",
          "Avant : orange | Après : vert | Points : aperçu des clés enregistrées."},
         {"Click chooses the before frame; Shift-click chooses the after frame.",
          "Cliquez pour choisir la frame avant ; Maj-clic choisit la frame après."},
         {"The line is a bounded sampled preview; stored encoding and current-frame changes are separate.",
          "La ligne est un aperçu échantillonné borné ; les changements d’encodage et de frame courante sont "
          "distincts."},
         {"Unknown data is not a confirmed addition or removal.",
          "Une donnée inconnue ne confirme ni ajout ni suppression."},
         {"Selected track is unavailable", "La piste sélectionnée est indisponible"},
         {"No stored bone tracks", "Aucune piste osseuse enregistrée"},
         {"No sampled values at the selected context",
          "Aucune valeur échantillonnée dans le contexte sélectionné"},
         {"Prepared frames: %.4g | %.4g", "Frames préparées : %.4g | %.4g"},
         {"Position: %.7g | Rotation: %.7g deg | Scale: %.7g",
          "Position : %.7g | Rotation : %.7g degrés | Échelle : %.7g"},
         {"Frame comparison unavailable", "Comparaison de frame indisponible"},
         {"Selected compressed values", "Valeurs compressées sélectionnées"},
         {"Find bone tracks", "Rechercher des pistes d’os"},
         {"Position tolerance", "Tolérance de position"},
         {"Rotation tolerance (degrees)", "Tolérance angulaire (degrés)"},
         {"Scale tolerance", "Tolérance d’échelle"},
         {"Position X", "Position X"},
         {"Position Y", "Position Y"},
         {"Position Z", "Position Z"},
         {"Rotation X", "Rotation X"},
         {"Rotation Y", "Rotation Y"},
         {"Rotation Z", "Rotation Z"},
         {"Rotation W", "Rotation W"},
         {"Scale X", "Échelle X"},
         {"Scale Y", "Échelle Y"},
         {"Scale Z", "Échelle Z"}});
}
void CookedAnimationView::chart() {
    if (!review_)
        return;
    bool first = true;
    double low = 0, high = 0, end = 1;
    for (unsigned side = 0; side < 2; ++side) {
        if (!review_->metadata[side]->is_null() && !review_->metadata[side]->at("frameCount").is_null())
            end = std::max(end, review_->metadata[side]->at("frameCount").get<double>() - 1);
        for (const auto *points : {&review_->plots[side], &review_->stored[side]})
            for (const auto &p : *points) {
                if (first) {
                    low = high = p[1];
                    first = false;
                } else {
                    low = std::min(low, p[1]);
                    high = std::max(high, p[1]);
                }
            }
    }
    if (first) {
        cyui::textDim("%s", cyui::tr("No sampled values at the selected context").c_str());
        return;
    }
    const auto scale = std::max({std::abs(low), std::abs(high), 1e-30});
    auto lo = low / scale, hi = high / scale;
    if (hi - lo < 1e-12) {
        lo -= .5;
        hi += .5;
    }
    ImGui::Text("%.6g .. %.6g", low, high);
    const auto a = ImGui::GetCursorScreenPos();
    const ImVec2 size{std::max(1.f, ImGui::GetContentRegionAvail().x), cyui::dp(110)};
    ImGui::InvisibleButton("##compressed-plot", size);
    const auto b = ImVec2{a.x + size.x, a.y + size.y};
    auto *draw = ImGui::GetWindowDrawList();
    const auto &palette = cyui::palette();
    draw->AddRect(a, b, palette.textDim);
    draw->PushClipRect(a, b, true);
    for (unsigned side = 0; side < 2; ++side) {
        const auto color = side ? palette.success : palette.warning;
        auto point = [&](const std::array<double, 2> &p) {
            return ImVec2{a.x + float(p[0] / end) * size.x,
                          b.y - float((p[1] / scale - lo) / (hi - lo)) * size.y};
        };
        const auto &line = review_->plots[side];
        for (std::size_t i = 1; i < line.size(); ++i)
            draw->AddLine(point(line[i - 1]), point(line[i]), color, cyui::dp(1.5f));
        if (line.size() == 1)
            draw->AddCircleFilled(point(line[0]), cyui::dp(2.5f), color);
        for (const auto &p : review_->stored[side])
            draw->AddCircleFilled(point(p), cyui::dp(2), color);
        const auto frame = review_->selection.frames[side];
        if ((side == 0 || comparing_) && frame <= end) {
            auto x = a.x + float(frame / end) * size.x;
            draw->AddLine({x, a.y}, {x, b.y}, color, cyui::dp(1));
        }
    }
    draw->PopClipRect();
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const double frame = std::clamp(double((ImGui::GetIO().MousePos.x - a.x) / size.x), 0., 1.) * end;
        if (comparing_ && ImGui::GetIO().KeyShift)
            set_frames(frame_, frame, true);
        else
            set_frames(frame, after_frame_, independent_);
    }
    ImGui::TextWrapped("%s", cyui::tr("Before: orange | After: green | Dots: stored key preview.").c_str());
}
void CookedAnimationView::draw(const char *id) {
    poll();
    cyui::ScopedID scope(id);
    ImGui::Checkbox(cyui::trLabel("Scalar curves").c_str(), &scalar_mode_);
    if (scalar_mode_) {
        curves_.draw();
        return;
    }
    rendered_rows_ = 0;
    selected_visible_ = false;
    ImGui::TextWrapped(
        "%s", cyui::tr("Compressed bone-local samples; no retargeting, additive base or skinning.").c_str());
    if (busy_)
        cyui::textDim("%s", cyui::tr("Preparing compressed animation review…").c_str());
    if (!error_.empty())
        cyui::callout(cyui::Tone::error, error_.c_str());
    if (!review_) {
        if (!busy_)
            cyui::textDim("%s", cyui::tr("No decoded cooked animation").c_str());
        return;
    }
    const bool current = !busy_ && error_.empty();
    for (unsigned side = 0; side < 2; ++side) {
        if (review_->metadata[side]->is_null())
            continue;
        const auto &m = *review_->metadata[side];
        if (!m.at("boneChannelsDecoded").get<bool>()) {
            const auto reason = m.value("unavailable", Json(nullptr));
            const bool binding = reason == "bone-compression-settings-not-bound" ||
                                 reason == "compression-settings-properties-not-decoded" ||
                                 reason == "compression-codec-list-not-stored";
            cyui::textDim("%s",
                          cyui::tr(binding ? "Bind the referenced compression settings to decode bone tracks."
                                           : "Selected compression decoder or payload is unavailable.")
                              .c_str());
        } else if (!m.at("identitiesComplete").get<bool>())
            cyui::textDim("%s",
                          cyui::tr("Bind the referenced Skeleton to name and compare bone tracks.").c_str());
        if (m.at("interpolation").is_null())
            cyui::textDim("%s",
                          cyui::tr("Choose the producer interpolation when the file omits it.").c_str());
        if (m.at("curves").is_object() && m.at("curves").value("status", std::string{}) == "opaque")
            cyui::textDim("%s", cyui::tr("Compressed curve values remain unavailable.").c_str());
    }
    const float available_width = ImGui::GetContentRegionAvail().x;
    bool domain_shown = false;
    for (unsigned side = 0; side < 2; ++side) {
        const auto &m = *review_->metadata[side];
        if (!m.is_null() && !m.at("frameCount").is_null()) {
            if (domain_shown && available_width > cyui::dp(650))
                ImGui::SameLine(0, cyui::dp(24));
            ImGui::Text(cyui::tr("%s frame domain: 0 .. %u").c_str(),
                        cyui::tr(side ? "After" : "Before").c_str(), m.at("frameCount").get<unsigned>() - 1);
            domain_shown = true;
        }
    }
    for (unsigned side = 0; side < (comparing_ && independent_ ? 2u : 1u); ++side) {
        if (side && available_width > cyui::dp(850))
            ImGui::SameLine(0, cyui::dp(24));
        const auto *label =
            comparing_ ? (side ? "Compressed frame after" : "Compressed frame before") : "Compressed frame";
        double frame = side ? after_frame_ : frame_;
        ImGui::SetNextItemWidth(std::min(cyui::dp(160), ImGui::GetContentRegionAvail().x));
        if (ImGui::InputDouble(cyui::trLabel(label).c_str(), &frame, .5, 1, "%.4f") && std::isfinite(frame) &&
            frame >= 0 && frame <= 2147483646.)
            set_frames(side ? frame_ : frame, side ? frame : after_frame_, independent_);
    }
    if (comparing_) {
        bool independent = independent_;
        if (ImGui::Checkbox(cyui::trLabel("Independent compressed frames").c_str(), &independent))
            set_frames(frame_, after_frame_, independent);
    }
    cyui::FilterOptions filter;
    filter.hint = "Find bone tracks";
    filter.width = std::min(cyui::dp(250), ImGui::GetContentRegionAvail().x);
    if (cyui::filterBox("##compressed-filter", filter_, filter))
        rebuild();
    if (comparing_) {
        if (ImGui::GetContentRegionAvail().x > cyui::dp(450))
            ImGui::SameLine();
        if (ImGui::Checkbox(cyui::trLabel("Changed tracks").c_str(), &changed_only_))
            rebuild();
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::undo, cyui::tr("Previous changed entry").c_str()))
            next_change(true);
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::redo, cyui::tr("Next changed entry").c_str()))
            next_change();
    }
    const float height = std::clamp(ImGui::GetContentRegionAvail().y * .23f, cyui::dp(85), cyui::dp(150));
    if (ImGui::BeginTable("##compressed-tracks", comparing_ ? 3 : 1,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                          {0, height})) {
        ImGui::TableSetupColumn(cyui::tr("Bone").c_str(), ImGuiTableColumnFlags_WidthStretch, 2);
        if (comparing_) {
            ImGui::TableSetupColumn(cyui::tr("Stored encoding").c_str(), ImGuiTableColumnFlags_WidthStretch,
                                    1);
            ImGui::TableSetupColumn(cyui::tr("Selected frame").c_str(), ImGuiTableColumnFlags_WidthStretch,
                                    1);
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        ImGuiListClipper clip;
        clip.Begin(int(visible_.size()));
        if (scroll_selection_)
            for (std::size_t i = 0; i < visible_.size(); ++i)
                if (review_->rows[visible_[i]].key == selected_) {
                    clip.IncludeItemByIndex(int(i));
                    break;
                }
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                ++rendered_rows_;
                const auto &r = review_->rows[visible_[std::size_t(i)]];
                cyui::ScopedID row(r.key.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Selectable(r.name.c_str(), selected_ == r.key,
                                      ImGuiSelectableFlags_SpanAllColumns))
                    select_track(r.key);
                if (r.key == selected_) {
                    if (scroll_selection_)
                        ImGui::SetScrollHereY();
                    selected_visible_ = ImGui::IsItemVisible();
                }
                if (comparing_)
                    for (unsigned col = 0; col < 2; ++col) {
                        ImGui::TableNextColumn();
                        const bool known = (col ? r.frame_known : r.stored_known) && current;
                        const auto &status = col ? r.frame : r.stored;
                        ImGui::PushStyleColor(ImGuiCol_Text, known ? detail::change_color(style(status))
                                                                   : cyui::palette().textDim);
                        ImGui::TextUnformatted(cyui::tr(known ? (status == "changed"   ? "Modified"
                                                                 : status == "added"   ? "Added"
                                                                 : status == "removed" ? "Removed"
                                                                                       : "Unchanged")
                                                              : "Unavailable")
                                                   .c_str());
                        ImGui::PopStyleColor();
                    }
            }
        ImGui::EndTable();
        scroll_selection_ = false;
    }
    if (visible_.empty())
        cyui::textDim("%s", cyui::tr(review_->rows.empty()
                                         ? ((before_ && before_->decoded()) || (after_ && after_->decoded())
                                                ? "No stored bone tracks"
                                                : "No decoded cooked animation")
                                         : "No matching bone track")
                                .c_str());
    if (selected_.empty())
        return;
    ImGui::TextWrapped("%s", selected_.c_str());
    ImGui::SetNextItemWidth(std::min(cyui::dp(180), ImGui::GetContentRegionAvail().x));
    if (ImGui::BeginCombo(cyui::trLabel("Sampled compressed channel").c_str(),
                          cyui::tr(components[component_]).c_str())) {
        for (unsigned i = 0; i < 10; ++i)
            if (ImGui::Selectable(cyui::tr(components[i]).c_str(), i == component_))
                set_component(i);
        ImGui::EndCombo();
    }
    if (comparing_ && ImGui::TreeNode(cyui::trLabel("Tolerances").c_str())) {
        double p = position_tolerance_, r = rotation_tolerance_, s = scale_tolerance_;
        bool update = false;
        for (auto item :
             {std::make_pair("Position tolerance", &p), std::make_pair("Rotation tolerance (degrees)", &r),
              std::make_pair("Scale tolerance", &s)}) {
            ImGui::SetNextItemWidth(cyui::dp(120));
            update =
                ImGui::InputDouble(cyui::trLabel(item.first).c_str(), item.second, 0, 0, "%.6g") || update;
        }
        if (update && std::isfinite(p) && p >= 0 && std::isfinite(r) && r >= 0 && r <= 180 &&
            std::isfinite(s) && s >= 0)
            set_tolerances(p, r, s);
        ImGui::TreePop();
    }
    if (current)
        chart();
    ImGui::TextWrapped(
        "%s",
        cyui::tr(
            "The line is a bounded sampled preview; stored encoding and current-frame changes are separate.")
            .c_str());
    if (comparing_) {
        if (current && review_->metrics.is_object() && review_->metrics.value("ready", false))
            ImGui::TextWrapped(cyui::tr("Position: %.7g | Rotation: %.7g deg | Scale: %.7g").c_str(),
                               review_->metrics.at("positionDistance").get<double>(),
                               review_->metrics.at("rotationDeltaDegrees").get<double>(),
                               review_->metrics.at("scaleMaxDelta").get<double>());
        else
            cyui::textDim("%s", cyui::tr("Frame comparison unavailable").c_str());
    }
    for (unsigned side = 0; side < (comparing_ ? 2u : 1u); ++side) {
        int key = int(std::min<std::uint32_t>(keys_[side], INT32_MAX));
        ImGui::SetNextItemWidth(cyui::dp(110));
        if (ImGui::InputInt(
                cyui::trLabel(comparing_ ? (side ? "Stored key after" : "Stored key before") : "Stored key")
                    .c_str(),
                &key) &&
            key >= 0)
            set_key(std::uint32_t(key), side != 0);
        if (side == 0 && comparing_ && ImGui::GetContentRegionAvail().x > cyui::dp(600))
            ImGui::SameLine();
    }
    if (busy_)
        ImGui::Text(cyui::tr("Prepared frames: %.4g | %.4g").c_str(), review_->selection.frames[0],
                    review_->selection.frames[1]);
    if (!current)
        return;
    ImGui::BeginChild("##compressed-details", {0, std::max(cyui::dp(90), ImGui::GetContentRegionAvail().y)},
                      ImGuiChildFlags_Borders);
    details_.draw("Selected compressed values");
    ImGui::EndChild();
}
} // namespace cyuediff
