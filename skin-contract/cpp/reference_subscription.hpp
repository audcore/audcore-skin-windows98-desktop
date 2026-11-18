#pragma once
#include "../audcore_skin.h"
#include <cstdint>

// UI-thread only. A skin created but not shown owns no state subscription.
// The host promises not to start callbacks before subscribe returns and not to
// start new callbacks after unsubscribe returns.
class ReferenceSubscription {
public:
    bool start(const audcore_skin_host& host, audcore_skin_event_fn callback, void* user) noexcept {
        if (active_) return true;
        std::uint64_t token = 0;
        if (host.subscribe(host.ctx, callback, user, &token) != 0) return false;
        token_ = token;
        active_ = true;
        return true;
    }
    void stop(const audcore_skin_host& host) noexcept {
        if (!active_) return;
        active_ = false;
        host.unsubscribe(host.ctx, token_);
    }
    bool active() const noexcept { return active_; }
    std::uint64_t token() const noexcept { return token_; }
private:
    bool active_ = false;
    std::uint64_t token_ = 0;
};
