#pragma once

// UI-thread state: stay unavailable until a valid authoritative snapshot arrives.
// A failed request may be retried by the next event or user action, without polling.
class ReferenceRecovery {
public:
    bool active() const noexcept { return active_; }
    bool start_request() noexcept {
        active_=true;
        if (pending_) return false;
        pending_=true;
        return true;
    }
    void request_failed() noexcept { pending_=false; }
    void snapshot_received(bool valid) noexcept {
        pending_=false;
        if (valid) active_=false;
    }
    void reset() noexcept { active_=pending_=false; }
private:
    bool active_=false;
    bool pending_=false;
};
