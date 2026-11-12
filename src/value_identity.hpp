#pragma once
#include <cyuediff/core.hpp>
#include <utility>
namespace cyuediff::detail {
// A bounded side journal: output paths refer to decoded values, never replace them.
// Failed value/key transactions discard observations without refunding spent work.
class ValueIdentityRecorder {
    ValueIdentityOptions options_;
    OperationControl control_;
    Json names_;
    Json *target_ = &names_;
    std::string path_;
    std::uint32_t entries_ = 0;
    std::uint64_t text_ = 0;
    bool limited_ = false;
    unsigned paused_ = 0, blocked_ = 0;
    bool spend(std::uint32_t entries, std::uint64_t text) {
        if (!options_.names || limited_)
            return false;
        control_.check();
        if (entries > options_.max_names - entries_ || text > options_.max_text_bytes - text_) {
            limited_ = true;
            return false;
        }
        entries_ += entries;
        text_ += text;
        if (options_.charge)
            options_.charge(entries, text);
        return true;
    }
    void insert(Json &target, std::string path, Json name) {
        if (target.is_null())
            target = Json::object();
        auto &object = target.get_ref<Json::object_t &>();
        // try_emplace leaves `name` untouched when the path is already recorded.
        const auto [found, inserted] = object.try_emplace(std::move(path), std::move(name));
        if (!inserted && found->second != name) {
            object.erase(found);
            limited_ = true;
        }
    }
    static void append_token(std::string &out, const std::string &value) {
        if (value.find_first_of("~/") == std::string::npos) {
            out += value;
            return;
        }
        for (const char c : value) {
            if (c == '~')
                out += "~0";
            else if (c == '/')
                out += "~1";
            else
                out += c;
        }
    }

  public:
    static std::string token(const std::string &value) {
        std::string result;
        append_token(result, value);
        return result;
    }
    explicit ValueIdentityRecorder(const ValueIdentityOptions &options, const OperationControl &control)
        : options_(options), control_(control) {
        if (options_.names)
            names_ = Json::object();
    }
    ValueIdentityRecorder(const ValueIdentityRecorder &) = delete;
    ValueIdentityRecorder &operator=(const ValueIdentityRecorder &) = delete;
    ValueIdentityRecorder(ValueIdentityRecorder &&other)
        : options_(std::move(other.options_)), control_(std::move(other.control_)),
          names_(std::move(other.names_)), target_(&names_), path_(std::move(other.path_)),
          entries_(other.entries_), text_(other.text_), limited_(other.limited_), paused_(other.paused_),
          blocked_(other.blocked_) {}
    class Scope {
        ValueIdentityRecorder *journal_ = nullptr;
        std::size_t size_ = 0;
        bool blocked_ = false;
        void append(ValueIdentityRecorder &journal, const std::string &key) {
            if (!journal.options_.names || journal.limited_)
                return;
            journal.control_.check();
            if (journal.blocked_) {
                journal_ = &journal;
                blocked_ = true;
                ++journal.blocked_;
                return;
            }
            auto block = [&] {
                journal_ = &journal;
                blocked_ = true;
                ++journal.blocked_;
            };
            size_ = journal.path_.size();
            std::uint64_t encoded = 1;
            for (const char c : key) {
                if (encoded >= journal.options_.max_text_bytes ||
                    ((c == '~' || c == '/') && encoded + 1 >= journal.options_.max_text_bytes)) {
                    block();
                    return;
                }
                encoded += c == '~' || c == '/' ? 2 : 1;
            }
            if (size_ > journal.options_.max_text_bytes ||
                encoded > journal.options_.max_text_bytes - size_) {
                block();
                return;
            }
            // Measure escaping before allocation, including exact budget boundaries.
            journal.path_ += '/';
            append_token(journal.path_, key);
            journal_ = &journal;
        }

      public:
        Scope(ValueIdentityRecorder &journal, const std::string &key) { append(journal, key); }
        Scope(ValueIdentityRecorder &journal, std::size_t index) {
            if (journal.options_.names && !journal.limited_)
                append(journal, std::to_string(index));
        }
        Scope(const Scope &) = delete;
        Scope &operator=(const Scope &) = delete;
        ~Scope() {
            if (journal_) {
                if (blocked_)
                    --journal_->blocked_;
                else
                    journal_->path_.resize(size_);
            }
        }
    };
    class Buffer {
        ValueIdentityRecorder *journal_ = nullptr;
        Json *previous_ = nullptr;
        Json pending_;
        std::size_t prefix_size_ = 0;
        bool committed_ = false;

      public:
        explicit Buffer(ValueIdentityRecorder &journal) {
            if (journal.options_.names && !journal.limited_ && !journal.blocked_) {
                journal_ = &journal;
                previous_ = journal.target_;
                prefix_size_ = journal.path_.size();
                journal.target_ = &pending_;
            }
        }
        Buffer(const Buffer &) = delete;
        Buffer &operator=(const Buffer &) = delete;
        void commit() {
            if (!journal_ || committed_)
                return;
            journal_->control_.check();
            if (pending_.is_object() && !pending_.empty()) {
                if (previous_->is_null())
                    *previous_ = Json::object();
                auto &to = previous_->get_ref<Json::object_t &>();
                auto &from = pending_.get_ref<Json::object_t &>();
                to.merge(from);
                // Paths left behind were already recorded: a different identity invalidates them.
                for (const auto &left : from) {
                    journal_->control_.check();
                    const auto found = to.find(left.first);
                    if (found->second != left.second) {
                        to.erase(found);
                        journal_->limited_ = true;
                    }
                }
            }
            journal_->target_ = previous_;
            committed_ = true;
        }
        // A map key gets its final JSON location only after its stored identity is encoded.
        void commit_at(const std::initializer_list<std::string> &components) {
            if (!journal_ || committed_)
                return;
            if (pending_.empty()) {
                journal_->target_ = previous_;
                committed_ = true;
                return;
            }
            if (!journal_->limited_) {
                std::string replacement = journal_->path_.substr(0, prefix_size_);
                for (const auto &part : components) {
                    journal_->control_.check();
                    std::uint64_t encoded = 1;
                    for (const char c : part) {
                        if (encoded >= journal_->options_.max_text_bytes ||
                            ((c == '~' || c == '/') && encoded + 1 >= journal_->options_.max_text_bytes)) {
                            journal_->limited_ = true;
                            break;
                        }
                        encoded += c == '~' || c == '/' ? 2 : 1;
                    }
                    if (journal_->limited_ || replacement.size() > journal_->options_.max_text_bytes ||
                        encoded > journal_->options_.max_text_bytes - replacement.size()) {
                        journal_->limited_ = true;
                        break;
                    }
                    replacement += '/';
                    replacement += token(part);
                }
                if (!journal_->limited_)
                    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
                        const auto suffix_size = it.key().size() - prefix_size_;
                        if (replacement.size() > journal_->options_.max_text_bytes ||
                            suffix_size > journal_->options_.max_text_bytes - replacement.size()) {
                            journal_->limited_ = true;
                            break;
                        }
                        if (!journal_->spend(0, replacement.size() + suffix_size))
                            break;
                        journal_->insert(*previous_, replacement + it.key().substr(prefix_size_),
                                         std::move(it.value()));
                    }
            }
            journal_->target_ = previous_;
            committed_ = true;
        }
        ~Buffer() {
            if (journal_ && !committed_)
                journal_->target_ = previous_;
        }
    };
    // Functional key capture is independent of optional observation limits. Parent scopes
    // and transactions stay alive but dormant until this nested frame is restored.
    class KeyProbe {
        ValueIdentityRecorder &journal_;
        ValueIdentityOptions options_;
        Json names_;
        Json *target_;
        std::string path_;
        std::uint32_t entries_;
        std::uint64_t text_;
        bool limited_;
        unsigned paused_, blocked_;
        bool restored_ = false;
        void restore() noexcept {
            if (restored_)
                return;
            journal_.options_ = std::move(options_);
            journal_.names_ = std::move(names_);
            journal_.target_ = target_;
            journal_.path_ = std::move(path_);
            journal_.entries_ = entries_;
            journal_.text_ = text_;
            journal_.limited_ = limited_;
            journal_.paused_ = paused_;
            journal_.blocked_ = blocked_;
            restored_ = true;
        }

      public:
        KeyProbe(ValueIdentityRecorder &journal, ValueIdentityOptions options)
            : journal_(journal), options_(std::move(journal.options_)), names_(std::move(journal.names_)),
              target_(journal.target_), path_(std::move(journal.path_)), entries_(journal.entries_),
              text_(journal.text_), limited_(journal.limited_), paused_(journal.paused_),
              blocked_(journal.blocked_) {
            options.names = true;
            journal_.options_ = std::move(options);
            journal_.names_ = nullptr;
            journal_.target_ = &journal_.names_;
            journal_.path_.clear();
            journal_.entries_ = 0;
            journal_.text_ = 0;
            journal_.limited_ = false;
            journal_.paused_ = 0;
            journal_.blocked_ = 0;
        }
        KeyProbe(const KeyProbe &) = delete;
        KeyProbe &operator=(const KeyProbe &) = delete;
        bool complete() const { return !journal_.limited_; }
        Json names() const {
            Json result = Json::object();
            for (auto it = journal_.names_.begin(); it != journal_.names_.end(); ++it) {
                journal_.control_.check();
                result[it.key()] = {{"base", it->at("base")}, {"number", it->at("number")}};
            }
            return result;
        }
        void publish() {
            Json observed = std::move(journal_.names_);
            restore();
            journal_.replay(observed);
        }
        Json observations() const { return journal_.names_; }

        ~KeyProbe() { restore(); }
    };
    // Native text/declarative callbacks without a value path cannot claim an identity location.
    class Pause {
        ValueIdentityRecorder &journal_;

      public:
        explicit Pause(ValueIdentityRecorder &journal) : journal_(journal) { ++journal_.paused_; }
        ~Pause() { --journal_.paused_; }
        Pause(const Pause &) = delete;
    };
    void name(const std::string &base, std::uint32_t number, bool zero = false) {
        if (!options_.names || limited_ || paused_)
            return;
        if (blocked_) {
            limited_ = true;
            return;
        }
        if (base.size() > options_.max_text_bytes || path_.size() > options_.max_text_bytes - base.size()) {
            limited_ = true;
            return;
        }
        if (!spend(1, base.size() + path_.size()))
            return;
        Json identity(Json::value_t::object);
        auto &fields = identity.get_ref<Json::object_t &>();
        fields.emplace("base", base);
        fields.emplace("number", number);
        fields.emplace("source", zero ? "zero-mask" : "wire");
        insert(*target_, path_, std::move(identity));
    }
    void replay(const Json &observed) {
        if (!options_.names || limited_ || paused_)
            return;
        if (blocked_) {
            if (!observed.empty())
                limited_ = true;
            return;
        }
        const auto prefix_size = path_.size();
        struct RestorePath {
            std::string &path;
            std::size_t size;
            ~RestorePath() { path.resize(size); }
        } path_scope{path_, prefix_size};
        for (auto it = observed.begin(); it != observed.end(); ++it) {
            control_.check();
            if (prefix_size > options_.max_text_bytes ||
                it.key().size() > options_.max_text_bytes - prefix_size) {
                limited_ = true;
                break;
            }
            path_.resize(prefix_size);
            path_ += it.key();
            name(it->at("base").get_ref<const std::string &>(), it->at("number").get<std::uint32_t>(),
                 it->at("source") == "zero-mask");
        }
    }
    bool selected() const { return options_.names && !limited_; }
    Json finish() {
        if (!options_.names)
            return nullptr;
        control_.check();
        return {{"schema", "cyuediff.value-identities/1"},
                {"coverage", "typed-name-values"},
                {"status", limited_ ? "incomplete" : "complete"},
                {"names", std::move(names_)},
                {"namesRead", entries_},
                {"textBytes", text_}};
    }
};
// Mandatory typed-value correspondence is independent of optional observations.
class ValueIdentityJournal {
    ValueIdentityRecorder values_, observations_;

  public:
    ValueIdentityJournal(const ValueIdentityOptions &optional, const OperationControl &control,
                         const NameValueReadOptions &functional = {})
        : values_(
              ValueIdentityOptions{true, functional.max_names, functional.max_text_bytes, functional.charge},
              control),
          observations_(optional, control) {}
    ValueIdentityJournal(ValueIdentityJournal &&) = default;
    ValueIdentityJournal(const ValueIdentityJournal &) = delete;
    static std::string token(const std::string &s) { return ValueIdentityRecorder::token(s); }
    class Scope {
        ValueIdentityRecorder::Scope values_, observations_;

      public:
        template <class T>
        Scope(ValueIdentityJournal &j, const T &key)
            : values_(j.values_, key), observations_(j.observations_, key) {}
    };
    class Buffer {
        ValueIdentityRecorder::Buffer values_, observations_;

      public:
        explicit Buffer(ValueIdentityJournal &j) : values_(j.values_), observations_(j.observations_) {}
        void commit() {
            values_.commit();
            observations_.commit();
        }
        void commit_at(const std::initializer_list<std::string> &keys) {
            values_.commit_at(keys);
            observations_.commit_at(keys);
        }
    };
    class Pause {
        ValueIdentityRecorder::Pause values_, observations_;

      public:
        explicit Pause(ValueIdentityJournal &j) : values_(j.values_), observations_(j.observations_) {}
    };
    class KeyProbe {
        ValueIdentityJournal &journal_;
        ValueIdentityRecorder::KeyProbe values_;
        std::unique_ptr<ValueIdentityRecorder::Pause> pause_;
        bool published_ = false;

      public:
        KeyProbe(ValueIdentityJournal &j, ValueIdentityOptions o)
            : journal_(j), values_(j.values_, std::move(o)),
              pause_(std::make_unique<ValueIdentityRecorder::Pause>(j.observations_)) {}
        bool complete() const { return values_.complete(); }
        Json names() const { return values_.names(); }
        void publish() {
            if (published_)
                return;
            const auto observed = journal_.observations_.selected() ? values_.observations() : Json(nullptr);
            values_.publish();
            pause_.reset();
            if (!observed.is_null())
                journal_.observations_.replay(observed);
            published_ = true;
        }
    };
    void name(const std::string &base, std::uint32_t number, bool zero = false) {
        values_.name(base, number, zero);
        observations_.name(base, number, zero);
    }
    Json finish() { return observations_.finish(); }
    Json name_values() {
        auto result = values_.finish();
        result["schema"] = "cyuediff.name-values/1";
        return result;
    }
};
} // namespace cyuediff::detail
