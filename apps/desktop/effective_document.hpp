#pragma once
#include "document.hpp"
namespace cyuediff::desktop {
// Worker-only I/O and resolution. Context bytes are reverified for each load epoch.
class PreparedDefaultContexts {
    std::optional<DefaultContext> before_, after_;
    std::shared_ptr<EffectiveDefaultsDocument> result_;

  public:
    PreparedDefaultContexts(Document &, const LoadRequest &, ReadOptions &, ReadOptions &);
    void resolve(Document &, const LoadRequest &);
};
} // namespace cyuediff::desktop
