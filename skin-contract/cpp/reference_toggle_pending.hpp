#pragma once
#include "reference_skin_state.hpp"
#include <unordered_map>

// Keep rapid toggles relative to the last submitted target, while display
// continues to follow host state. Identity prevents actions crossing channels.
class ReferenceTogglePending {
    struct Entry { ReferenceToggleControl control; bool target, observed; std::uint64_t request; };
    std::unordered_map<std::uint64_t,Entry> entries_;
    static std::uint64_t key(const ReferenceToggleControl& c) { return (std::uint64_t(c.channel)<<2) | static_cast<std::uint64_t>(c.kind); }
    static bool identity(const ReferenceToggleControl& a,const ReferenceToggleControl& b) {
        return a.channel==b.channel && a.kind==b.kind && a.channel_identity==b.channel_identity;
    }
public:
    void clear() noexcept { entries_.clear(); }
    std::optional<bool> base(const ReferenceToggleControl& control) {
        const auto it=entries_.find(key(control));
        if (it==entries_.end()) return {};
        if (!identity(it->second.control,control)) { entries_.erase(it); return {}; }
        return it->second.target;
    }
    void record(const ReferenceToggleControl& control,bool target,std::uint64_t request) {
        const auto id=key(control);
        auto it=entries_.find(id);
        if (it==entries_.end() || !identity(it->second.control,control)) {
            entries_.erase(id);
            it=entries_.emplace(id,Entry{control,target,control.value,request}).first;
        }
        it->second.target=target; it->second.request=request;
    }
    void fail(std::uint64_t request) {
        for (auto it=entries_.begin();it!=entries_.end();) if (it->second.request==request) it=entries_.erase(it); else ++it;
    }
    void reconcile(const ReferenceSkinState& state) {
        for (auto it=entries_.begin();it!=entries_.end();) {
            auto& entry=it->second;
            std::optional<ReferenceToggleControl> actual;
            const auto& channels=state.channels();
            for (std::size_t i=0;i<channels.size();++i) if (channels[i].id==entry.control.channel) { actual=state.toggle_control(i,entry.control.kind); break; }
            if (!actual || !identity(entry.control,*actual)) { it=entries_.erase(it); continue; }
            // An unchanged value can equal a double-click's final target
            // before either request has reached the host; do not clear yet.
            if (actual->value!=entry.observed) {
                entry.observed=actual->value;
                if (actual->value==entry.target) { it=entries_.erase(it); continue; }
            }
            ++it;
        }
    }
};
