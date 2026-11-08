#include <cyuediff/cooked_curve_view.hpp>
#include <cyuediff/view_state.hpp>
#include "diff_style.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace cyuediff {
namespace {
struct Selection {
    std::string curve;
    double times[2]{}, tolerance = 0, empty_default = 0, range[2]{0, 1};
    std::uint32_t keys[2]{};
    bool comparing = false;
};
struct Row {
    std::string key, name, encoding = "unavailable", value = "unavailable";
    std::size_t indices[2]{SIZE_MAX, SIZE_MAX};
    bool changed() const {
        return (encoding != "unavailable" && encoding != "identical") ||
               (value != "unavailable" && value != "identical");
    }
};
void native_float(double x) {
    if (!std::isfinite(x) || std::abs(x) > std::numeric_limits<float>::max())
        throw Error("view-state", "Curve input exceeds finite native float32 domain");
}
const char *status(const std::string &s) {
    return s == "changed"     ? "Modified"
           : s == "added"     ? "Added"
           : s == "removed"   ? "Removed"
           : s == "identical" ? "Unchanged"
                              : "Unavailable";
}
ChangeKind style(const std::string &s) {
    return s == "changed"   ? ChangeKind::modified
           : s == "added"   ? ChangeKind::added
           : s == "removed" ? ChangeKind::removed
                            : ChangeKind::unchanged;
}
} // namespace
struct CookedCurveView::Review {
    Selection selection;
    std::vector<Row> rows;
    Json summary, coverage[2], details[2], metrics = nullptr;
    std::vector<std::array<double, 2>> plots[2], stored[2];
    std::size_t unavailable_points[2]{};
};
struct CookedCurveView::Worker {
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
    std::shared_ptr<std::atomic<bool>> cancel;
    bool stopping = false;
    std::thread thread;
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
        std::map<std::string, Row> rows;
        for (unsigned side = 0; side < 2; ++side) {
            if (!w.models[side])
                continue;
            out->coverage[side] = w.models[side]->curve_information();
            const auto &info = out->coverage[side];
            if (!info.contains("names"))
                continue;
            const auto &names = info.at("names");
            if (names.size() > 10000)
                throw Error("ui-curve-limit", "Curve review exceeds 10000 names per side");
            for (std::size_t i = 0; i < names.size(); ++i) {
                control.check();
                const auto name = names[i].get<std::string>();
                if (name.size() > 4091)
                    throw Error("ui-curve-limit", "Curve identity exceeds view-state text budget");
                auto &row = rows["name:" + name];
                row.name = name;
                row.key = "name:" + name;
                row.indices[side] = i;
            }
        }
        if (rows.size() > 10000)
            throw Error("ui-curve-limit", "Curve review exceeds 10000 pairs");
        if (out->selection.curve.empty() && !rows.empty())
            out->selection.curve = rows.begin()->first;
        Json comparison = nullptr;
        if (w.selection.comparing && w.models[0] && w.models[1]) {
            CookedAnimationCurveDiffOptions o;
            o.before_time = w.selection.times[0];
            o.after_time = w.selection.times[1];
            o.value_tolerance = w.selection.tolerance;
            o.empty_default = w.selection.empty_default;
            o.max_curve_pairs = 10000;
            o.max_output_bytes = 128ull * 1024 * 1024;
            o.control = control;
            comparison = compare_cooked_animation_curves(*w.models[0], *w.models[1], o);
            out->summary = comparison;
            out->summary.erase("curves");
        } else
            out->summary = {{"schema", "cyuediff.cooked-curve-review/1"},
                            {"scope", "stored curve encoding and selected encoded-order scalar values"},
                            {"complete", false},
                            {"encodingChanges", 0},
                            {"valueChanges", 0},
                            {"unavailableCurves", w.selection.comparing ? rows.size() : 0},
                            {"status", w.selection.comparing ? "incomplete" : "inspection"}};
        for (auto &entry : rows) {
            control.check();
            auto &row = entry.second;
            if (comparison.is_object() && comparison.at("curves").contains(row.key)) {
                const auto &diff = comparison.at("curves").at(row.key);
                row.encoding = diff.at("encodingStatus").get<std::string>();
                row.value = diff.at("valueStatus").get<std::string>();
                if (row.key == out->selection.curve)
                    out->metrics = diff;
            }
            if (row.key == out->selection.curve)
                for (unsigned side = 0; side < 2; ++side) {
                    auto model = w.models[side] ? w.models[side]->compressed_curves() : nullptr;
                    if (!model || row.indices[side] == SIZE_MAX)
                        continue;
                    const auto index = row.indices[side];
                    auto preview = model->key_preview(index, 2048, control);
                    for (const auto &p : preview.at("points"))
                        out->stored[side].push_back(
                            {p.at("time").get<double>(), p.at("value").get<double>()});
                    Json key = nullptr;
                    if (w.selection.keys[side] < preview.at("keys").get<std::uint64_t>())
                        key = model->stored_key(index, w.selection.keys[side], control);
                    preview.erase("points");
                    Json sample;
                    try {
                        sample = {{"ready", true},
                                  {"time", w.selection.times[side]},
                                  {"nativeTime", static_cast<float>(w.selection.times[side])},
                                  {"value", model->sample_curve(index, w.selection.times[side],
                                                                w.selection.empty_default, control)}};
                    } catch (const Error &e) {
                        if (e.code == "cancelled")
                            throw;
                        sample = {{"ready", false}, {"unavailable", e.code + ": " + e.what()}};
                    }
                    out->details[side] = {{"scalarSample", std::move(sample)},
                                          {"storedCurve", std::move(preview)},
                                          {"selectedStoredKey",
                                           {{"requestedIndex", w.selection.keys[side]},
                                            {"ready", !key.is_null()},
                                            {"value", std::move(key)}}}};
                    for (unsigned i = 0; i < 257; ++i) {
                        control.check();
                        const double time =
                            w.selection.range[0] +
                            (w.selection.range[1] - w.selection.range[0]) * (double(i) / 256.);
                        try {
                            out->plots[side].push_back(
                                {time, model->sample_curve(index, time, w.selection.empty_default, control)});
                        } catch (const Error &e) {
                            if (e.code == "cancelled")
                                throw;
                            out->plots[side].push_back({time, std::numeric_limits<double>::quiet_NaN()});
                            ++out->unavailable_points[side];
                        }
                    }
                }
            out->rows.push_back(std::move(row));
        }
        control.check();
        return out;
    }
    void run() {
        for (;;) {
            std::optional<Work> w;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [this] { return stopping || pending.has_value(); });
                if (stopping)
                    return;
                w = std::move(pending);
                pending.reset();
            }
            Result r;
            r.id = w->id;
            try {
                r.value = prepare(*w);
            } catch (const Error &e) {
                if (e.code == "cancelled")
                    continue;
                r.error = e.code + ": " + e.what();
            } catch (const std::exception &e) {
                r.error = e.what();
            }
            std::lock_guard<std::mutex> lock(mutex);
            if (!stopping && !w->cancel->load())
                result = std::move(r);
        }
    }
};
CookedCurveView::CookedCurveView() = default;
CookedCurveView::~CookedCurveView() = default;
CookedCurveView::CookedCurveView(CookedCurveView &&) noexcept = default;
CookedCurveView &CookedCurveView::operator=(CookedCurveView &&) noexcept = default;
void CookedCurveView::request() {
    ++generation_;
    error_.clear();
    if (!models_[0] && !models_[1]) {
        worker_.reset();
        review_.reset();
        visible_.clear();
        busy_ = false;
        return;
    }
    if (!worker_)
        worker_ = std::make_unique<Worker>();
    Selection s;
    s.curve = selected_;
    s.times[0] = times_[0];
    s.times[1] = independent_ ? times_[1] : times_[0];
    s.tolerance = tolerance_;
    s.empty_default = default_;
    s.range[0] = range_[0];
    s.range[1] = range_[1];
    s.keys[0] = keys_[0];
    s.keys[1] = keys_[1];
    s.comparing = comparing_;
    busy_ = true;
    worker_->submit({generation_, {models_[0], models_[1]}, s, std::make_shared<std::atomic<bool>>(false)});
}
void CookedCurveView::set_animations(std::shared_ptr<const CookedAnimation> a,
                                     std::shared_ptr<const CookedAnimation> b, bool compare) {
    models_[0] = std::move(a);
    models_[1] = std::move(b);
    comparing_ = compare || bool(models_[1]);
    review_.reset();
    selected_.clear();
    times_[0] = times_[1] = 0;
    keys_[0] = keys_[1] = 0;
    request();
}
bool CookedCurveView::poll() {
    if (!worker_)
        return false;
    auto r = worker_->take();
    if (!r || r->id != generation_)
        return false;
    busy_ = false;
    error_ = r->error;
    if (r->value) {
        review_ = std::move(r->value);
        if (selected_.empty())
            selected_ = review_->selection.curve;
        rebuild();
        inspect();
    }
    return true;
}
void CookedCurveView::rebuild() {
    visible_.clear();
    if (review_)
        for (std::size_t i = 0; i < review_->rows.size(); ++i) {
            const auto &r = review_->rows[i];
            if ((!comparing_ || !changed_only_ || r.changed()) && filter_.matches(r.name, r.key))
                visible_.push_back(i);
        }
}
void CookedCurveView::inspect() {
    const auto state = details_state_.is_object() ? details_state_ : details_.view_state();
    if (review_) {
        const auto &a = review_->details[0];
        const auto &b = review_->details[1];
        details_.set_values(a.is_null() ? nullptr : &a, b.is_null() ? nullptr : &b,
                            comparing_ && !review_->metrics.is_null() &&
                                review_->metrics.at("encodingStatus") != "unavailable");
    }
    details_.restore_view_state(state);
    details_state_ = nullptr;
}
bool CookedCurveView::select_curve(const std::string &key) {
    selected_ = key;
    scroll_selection_ = true;
    request();
    return review_ && std::any_of(review_->rows.begin(), review_->rows.end(),
                                  [&](const auto &r) { return r.key == key; });
}
void CookedCurveView::set_times(double a, double b, bool independent) {
    native_float(a);
    native_float(b);
    times_[0] = a;
    times_[1] = b;
    independent_ = independent;
    request();
}
void CookedCurveView::set_tolerance(double t) {
    if (!std::isfinite(t) || t < 0)
        throw Error("view-state", "Invalid scalar tolerance");
    tolerance_ = t;
    request();
}
void CookedCurveView::set_empty_default(double d) {
    native_float(d);
    default_ = d;
    request();
}
void CookedCurveView::set_range(double a, double b) {
    native_float(a);
    native_float(b);
    if (!(a < b))
        throw Error("view-state", "Curve plot start must precede end");
    range_[0] = a;
    range_[1] = b;
    request();
}
void CookedCurveView::set_key(std::uint32_t k, bool after) {
    if (k > INT32_MAX)
        throw Error("view-state", "Invalid scalar key index");
    keys_[after ? 1 : 0] = k;
    request();
}
void CookedCurveView::set_filter(std::string s) {
    filter_.query = std::move(s);
    rebuild();
}
void CookedCurveView::set_changed_only(bool b) {
    changed_only_ = b;
    rebuild();
}
bool CookedCurveView::next_change(bool previous) {
    if (!review_ || visible_.empty())
        return false;
    auto it = std::find_if(visible_.begin(), visible_.end(),
                           [&](auto i) { return review_->rows[i].key == selected_; });
    const auto start = it == visible_.end() ? std::size_t(previous ? 0 : visible_.size() - 1)
                                            : std::size_t(it - visible_.begin());
    for (std::size_t n = 1; n <= visible_.size(); ++n) {
        const auto i =
            previous ? (start + visible_.size() - n) % visible_.size() : (start + n) % visible_.size();
        const auto &r = review_->rows[visible_[i]];
        if (r.changed())
            return select_curve(r.key);
    }
    return false;
}
Json CookedCurveView::view_state() const {
    return {{"selected", selected_},
            {"filter", filter_.query},
            {"changedOnly", changed_only_},
            {"time", times_[0]},
            {"afterTime", times_[1]},
            {"independentTimes", independent_},
            {"valueTolerance", tolerance_},
            {"emptyDefault", default_},
            {"rangeStart", range_[0]},
            {"rangeEnd", range_[1]},
            {"beforeKey", keys_[0]},
            {"afterKey", keys_[1]},
            {"properties", details_state_.is_object() ? details_state_ : details_.view_state()}};
}
void CookedCurveView::restore_view_state(const Json &j) {
    view_state::validate(j, "cooked-curves");
    const auto start = j.value("rangeStart", range_[0]), end = j.value("rangeEnd", range_[1]);
    if (!(start < end))
        throw Error("view-state", "Invalid scalar plot range");
    selected_ = j.value("selected", selected_);
    scroll_selection_ = j.contains("selected");
    filter_.query = j.value("filter", filter_.query);
    changed_only_ = j.value("changedOnly", changed_only_);
    times_[0] = j.value("time", times_[0]);
    times_[1] = j.value("afterTime", times_[1]);
    independent_ = j.value("independentTimes", independent_);
    tolerance_ = j.value("valueTolerance", tolerance_);
    default_ = j.value("emptyDefault", default_);
    range_[0] = start;
    range_[1] = end;
    keys_[0] = j.value("beforeKey", keys_[0]);
    keys_[1] = j.value("afterKey", keys_[1]);
    if (j.contains("properties"))
        details_state_ = j.at("properties");
    rebuild();
    request();
}
Json CookedCurveView::report() const {
    Json j = {{"busy", busy_},
              {"error", error_},
              {"current", !busy_ && error_.empty() && bool(review_)},
              {"selected", selected_},
              {"visible", visible_.size()},
              {"renderedRows", rendered_rows_},
              {"selectedVisible", selected_visible_},
              {"requestedTime", times_[0]},
              {"requestedAfterTime", independent_ ? times_[1] : times_[0]}};
    if (review_) {
        j["comparison"] = review_->summary;
        j["preparedTime"] = review_->selection.times[0];
        j["preparedAfterTime"] = review_->selection.times[1];
        j["metrics"] = review_->metrics;
        j["before"] = review_->details[0];
        j["after"] = review_->details[1];
        j["beforeCoverage"] = review_->coverage[0];
        j["afterCoverage"] = review_->coverage[1];
        j["plotPoints"] = {review_->plots[0].size(), review_->plots[1].size()};
        j["plotSamples"] = {review_->plots[0], review_->plots[1]};
        j["unavailablePlotPoints"] = {review_->unavailable_points[0], review_->unavailable_points[1]};
        j["storedPoints"] = {review_->stored[0].size(), review_->stored[1].size()};
        j["curves"] = Json::array();
        for (const auto &r : review_->rows)
            j["curves"].push_back(
                {{"key", r.key}, {"name", r.name}, {"encodingStatus", r.encoding}, {"valueStatus", r.value}});
    }
    return j;
}
void CookedCurveView::chart() {
    if (!review_)
        return;
    bool first = true;
    double low = 0, high = 0;
    for (unsigned side = 0; side < 2; ++side)
        for (const auto *points : {&review_->plots[side], &review_->stored[side]})
            for (const auto &p : *points)
                if (std::isfinite(p[1]) && p[0] >= review_->selection.range[0] &&
                    p[0] <= review_->selection.range[1]) {
                    if (first) {
                        low = high = p[1];
                        first = false;
                    } else {
                        low = std::min(low, p[1]);
                        high = std::max(high, p[1]);
                    }
                }
    if (first) {
        cyui::textDim("%s", cyui::tr("No scalar samples in this range").c_str());
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
    const ImVec2 size{std::max(1.f, ImGui::GetContentRegionAvail().x), cyui::dp(140)};
    ImGui::InvisibleButton("##scalar-plot", size);
    const ImVec2 b{a.x + size.x, a.y + size.y};
    auto *draw = ImGui::GetWindowDrawList();
    const auto &p = cyui::palette();
    draw->AddRect(a, b, p.textDim);
    draw->PushClipRect(a, b, true);
    const auto start = review_->selection.range[0], end = review_->selection.range[1];
    for (unsigned side = 0; side < 2; ++side) {
        const auto color = side ? p.success : p.warning;
        auto point = [&](const auto &v) {
            return ImVec2{a.x + float((v[0] - start) / (end - start)) * size.x,
                          b.y - float((v[1] / scale - lo) / (hi - lo)) * size.y};
        };
        const auto &line = review_->plots[side];
        for (std::size_t i = 1; i < line.size(); ++i)
            if (std::isfinite(line[i - 1][1]) && std::isfinite(line[i][1]))
                draw->AddLine(point(line[i - 1]), point(line[i]), color, cyui::dp(1.5f));
        for (const auto &v : review_->stored[side])
            if (v[0] >= start && v[0] <= end)
                draw->AddCircleFilled(point(v), cyui::dp(2), color);
        const auto time = review_->selection.times[side];
        if ((side == 0 || comparing_) && time >= start && time <= end) {
            const auto x = a.x + float((time - start) / (end - start)) * size.x;
            draw->AddLine({x, a.y}, {x, b.y}, color, cyui::dp(1));
        }
    }
    draw->PopClipRect();
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const auto t =
            start + std::clamp(double((ImGui::GetIO().MousePos.x - a.x) / size.x), 0., 1.) * (end - start);
        if (comparing_ && ImGui::GetIO().KeyShift)
            set_times(times_[0], t, true);
        else
            set_times(t, times_[1], independent_);
    }
    ImGui::TextWrapped("%s", cyui::tr("Before: orange | After: green | Dots: stored key preview.").c_str());
}
void CookedCurveView::draw(const char *id) {
    poll();
    cyui::ScopedID scope(id);
    rendered_rows_ = 0;
    selected_visible_ = false;
    ImGui::TextWrapped(
        "%s", cyui::tr("Encoded scalar curves in seconds; no runtime blending or filtering.").c_str());
    if (busy_)
        cyui::textDim("%s", cyui::tr("Preparing scalar curve review…").c_str());
    if (!error_.empty())
        cyui::callout(cyui::Tone::error, error_.c_str());
    if (!review_)
        return;
    for (unsigned side = 0; side < 2; ++side)
        if (review_->coverage[side].is_object()) {
            const auto &s = review_->coverage[side];
            ImGui::Text("%s: %s", cyui::tr(side ? "After" : "Before").c_str(),
                        cyui::tr(s.value("status", std::string("unavailable")).c_str()).c_str());
            if (s.value("status", std::string{}) == "opaque")
                cyui::textDim(
                    "%s", cyui::tr("Bind the exact curve settings; unknown data is not a removal.").c_str());
        }
    for (unsigned side = 0; side < (comparing_ && independent_ ? 2u : 1u); ++side) {
        double t = times_[side];
        ImGui::SetNextItemWidth(std::min(cyui::dp(160), ImGui::GetContentRegionAvail().x));
        if (ImGui::InputDouble(
                cyui::trLabel(side ? "Scalar time after (s)" : "Scalar time before (s)").c_str(), &t, .01, .1,
                "%.6f") &&
            std::isfinite(t) && std::abs(t) <= std::numeric_limits<float>::max())
            set_times(side ? times_[0] : t, side ? t : times_[1], independent_);
    }
    if (comparing_) {
        bool independent = independent_;
        if (ImGui::Checkbox(cyui::trLabel("Independent scalar times").c_str(), &independent))
            set_times(times_[0], times_[1], independent);
    }
    cyui::FilterOptions f;
    f.hint = "Find scalar curves";
    f.width = std::min(cyui::dp(250), ImGui::GetContentRegionAvail().x);
    if (cyui::filterBox("##scalar-filter", filter_, f))
        rebuild();
    if (comparing_) {
        if (ImGui::Checkbox(cyui::trLabel("Changed curves").c_str(), &changed_only_))
            rebuild();
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::undo, cyui::tr("Previous changed entry").c_str()))
            next_change(true);
        ImGui::SameLine();
        if (cyui::iconButton(cyui::icon::redo, cyui::tr("Next changed entry").c_str()))
            next_change();
    }
    if (ImGui::BeginTable(
            "##scalar-curves", comparing_ ? 3 : 1,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
            {0, std::clamp(ImGui::GetContentRegionAvail().y * .23f, cyui::dp(85), cyui::dp(160))})) {
        ImGui::TableSetupColumn(cyui::tr("Scalar curve").c_str(), ImGuiTableColumnFlags_WidthStretch, 2);
        if (comparing_) {
            ImGui::TableSetupColumn(cyui::tr("Stored encoding").c_str());
            ImGui::TableSetupColumn(cyui::tr("Selected scalar value").c_str());
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
                if (ImGui::Selectable(r.name.c_str(), r.key == selected_,
                                      ImGuiSelectableFlags_SpanAllColumns))
                    select_curve(r.key);
                if (r.key == selected_) {
                    if (scroll_selection_)
                        ImGui::SetScrollHereY();
                    selected_visible_ = ImGui::IsItemVisible();
                }
                if (comparing_)
                    for (const auto *s : {&r.encoding, &r.value}) {
                        ImGui::TableNextColumn();
                        const auto value = busy_ || !error_.empty() ? std::string("unavailable") : *s;
                        ImGui::PushStyleColor(ImGuiCol_Text, value == "unavailable"
                                                                 ? cyui::palette().textDim
                                                                 : detail::change_color(style(value)));
                        ImGui::TextUnformatted(cyui::tr(status(value)).c_str());
                        ImGui::PopStyleColor();
                    }
            }
        ImGui::EndTable();
        scroll_selection_ = false;
    }
    if (visible_.empty())
        cyui::textDim("%s", cyui::tr("No matching scalar curves").c_str());
    if (ImGui::TreeNode(cyui::trLabel("Scalar options").c_str())) {
        double tolerance = tolerance_, empty = default_, start = range_[0], end = range_[1];
        ImGui::SetNextItemWidth(cyui::dp(150));
        if (ImGui::InputDouble(cyui::trLabel("Scalar tolerance").c_str(), &tolerance, 0, 0, "%.7g") &&
            std::isfinite(tolerance) && tolerance >= 0)
            set_tolerance(tolerance);
        ImGui::SetNextItemWidth(cyui::dp(150));
        if (ImGui::InputDouble(cyui::trLabel("Empty curve default").c_str(), &empty, 0, 0, "%.7g") &&
            std::isfinite(empty) && std::abs(empty) <= std::numeric_limits<float>::max())
            set_empty_default(empty);
        ImGui::SetNextItemWidth(cyui::dp(150));
        const bool changeStart = ImGui::InputDouble(cyui::trLabel("Scalar range start (s)").c_str(), &start);
        ImGui::SetNextItemWidth(cyui::dp(150));
        const bool changeEnd = ImGui::InputDouble(cyui::trLabel("Scalar range end (s)").c_str(), &end);
        if ((changeStart || changeEnd) && std::isfinite(start) && std::isfinite(end) &&
            std::abs(start) <= std::numeric_limits<float>::max() &&
            std::abs(end) <= std::numeric_limits<float>::max() && start < end)
            set_range(start, end);
        for (unsigned side = 0; side < (comparing_ ? 2u : 1u); ++side) {
            int k = int(keys_[side]);
            ImGui::SetNextItemWidth(cyui::dp(150));
            if (ImGui::InputInt(cyui::trLabel(side ? "Scalar key after" : "Scalar key before").c_str(), &k) &&
                k >= 0)
                set_key(std::uint32_t(k), side != 0);
        }
        ImGui::TreePop();
    }
    if (busy_ || !error_.empty())
        return;
    for (unsigned side = 0; side < (comparing_ ? 2u : 1u); ++side) {
        const auto &details = review_->details[side];
        if (details.is_object() && details.at("scalarSample").value("ready", false))
            ImGui::Text(cyui::tr(side ? "Scalar after: %.7g" : "Scalar before: %.7g").c_str(),
                        details.at("scalarSample").at("value").get<double>());
        else
            cyui::textDim("%s: %s", cyui::tr(side ? "After" : "Before").c_str(),
                          cyui::tr("Unavailable").c_str());
    }
    if (comparing_ && review_->metrics.is_object() && review_->metrics.at("valueDelta").is_number())
        ImGui::Text(cyui::tr("Value delta: %.7g | Tolerance: %.7g").c_str(),
                    review_->metrics.at("valueDelta").get<double>(), review_->selection.tolerance);
    chart();
    ImGui::TextWrapped(
        "%s",
        cyui::tr(
            "The line is a bounded sampled preview. Encoding and selected values are compared separately.")
            .c_str());
    details_.draw("##scalar-details");
}
void CookedCurveView::register_translations() {
    PropertyView::register_translations();
    cyui::registerTranslations(
        "fr",
        {{"Scalar before: %.7g", "Valeur avant : %.7g"},
         {"Scalar after: %.7g", "Valeur après : %.7g"},
         {"Value delta: %.7g | Tolerance: %.7g", "Écart de valeur : %.7g | Tolérance : %.7g"},
         {"Cooked scalar curves", "Courbes scalaires cooked"},
         {"Encoded scalar curves in seconds; no runtime blending or filtering.",
          "Courbes scalaires encodées en secondes ; sans mélange ni filtrage runtime."},
         {"Preparing scalar curve review…", "Préparation de la comparaison des courbes scalaires…"},
         {"Bind the exact curve settings; unknown data is not a removal.",
          "Liez les réglages exacts des courbes ; une donnée inconnue ne confirme pas une suppression."},
         {"Scalar time before (s)", "Instant scalaire avant (s)"},
         {"Scalar time after (s)", "Instant scalaire après (s)"},
         {"Independent scalar times", "Instants scalaires indépendants"},
         {"Find scalar curves", "Rechercher des courbes scalaires"},
         {"Changed curves", "Courbes modifiées"},
         {"Scalar curve", "Courbe scalaire"},
         {"Selected scalar value", "Valeur scalaire sélectionnée"},
         {"No matching scalar curves", "Aucune courbe scalaire correspondante"},
         {"Scalar options", "Options scalaires"},
         {"Scalar tolerance", "Tolérance scalaire"},
         {"Empty curve default", "Valeur par défaut des courbes vides"},
         {"Scalar range start (s)", "Début de plage scalaire (s)"},
         {"Scalar range end (s)", "Fin de plage scalaire (s)"},
         {"Scalar key before", "Clé scalaire avant"},
         {"Scalar key after", "Clé scalaire après"},
         {"No scalar samples in this range", "Aucun échantillon scalaire dans cette plage"},
         {"The line is a bounded sampled preview. Encoding and selected values are compared separately.",
          "La ligne est un aperçu échantillonné borné. Encodage et valeurs sélectionnées sont comparés "
          "séparément."},
         {"decoded", "décodé"},
         {"empty", "vide"},
         {"opaque", "opaque"},
         {"unavailable", "indisponible"},
         {"Stored encoding", "Encodage enregistré"},
         {"Unavailable", "Indisponible"},
         {"Modified", "Modifié"},
         {"Added", "Ajouté"},
         {"Removed", "Supprimé"},
         {"Unchanged", "Inchangé"},
         {"Previous changed entry", "Élément modifié précédent"},
         {"Next changed entry", "Élément modifié suivant"},
         {"Before: orange | After: green | Dots: stored key preview.",
          "Avant : orange | Après : vert | Points : aperçu des clés enregistrées."}});
}
} // namespace cyuediff
