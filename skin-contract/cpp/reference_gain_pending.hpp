#pragma once
#include "reference_skin_state.hpp"
#include <algorithm>
#include <unordered_map>
#include <vector>

// Accumulate rapid input while the authoritative gain changes asynchronously.
// Channel identity protects a new channel that reuses an old numeric ID.
class ReferenceGainPending {
    struct Entry { ReferenceGainControl control; double target, observed; std::uint64_t request; std::vector<double> sent; };
    std::unordered_map<std::uint32_t,Entry> entries_;
    static bool identity(const ReferenceGainControl& a,const ReferenceGainControl& b) {
        return a.channel==b.channel && a.channel_identity==b.channel_identity;
    }
    static bool equal(double a,double b) { return static_cast<float>(a)==static_cast<float>(b); }
public:
    void clear() noexcept { entries_.clear(); }
    std::optional<double> base(const ReferenceGainControl& control) {
        const auto it=entries_.find(control.channel);
        if (it==entries_.end()) return {};
        if (!identity(it->second.control,control)) { entries_.erase(it); return {}; }
        return it->second.target;
    }
    void record(const ReferenceGainControl& control,double target,std::uint64_t request) {
        auto it=entries_.find(control.channel);
        if (it==entries_.end() || !identity(it->second.control,control)) {
            entries_.erase(control.channel);
            it=entries_.emplace(control.channel,Entry{control,target,control.value,request,{}}).first;
        }
        auto& entry=it->second;
        entry.target=target; entry.request=request; entry.sent.push_back(target);
        if (entry.sent.size()>256) entry.sent.erase(entry.sent.begin());
    }
    void fail(std::uint64_t request) {
        for (auto it=entries_.begin();it!=entries_.end();) if (it->second.request==request) it=entries_.erase(it); else ++it;
    }
    void reconcile(const ReferenceSkinState& state) {
        for (auto it=entries_.begin();it!=entries_.end();) {
            auto& entry=it->second;
            std::optional<ReferenceGainControl> actual;
            const auto& channels=state.channels();
            for (std::size_t i=0;i<channels.size();++i) if (channels[i].id==entry.control.channel) { actual=state.gain_control(i); break; }
            if (!actual || !identity(entry.control,*actual)) { it=entries_.erase(it); continue; }
            if (!equal(actual->value,entry.observed)) {
                if (equal(actual->value,entry.target)) { it=entries_.erase(it); continue; }
                if (std::none_of(entry.sent.begin(),entry.sent.end(),[&](double sent){return equal(actual->value,sent);})) { it=entries_.erase(it); continue; }
                entry.observed=actual->value;
            }
            ++it;
        }
    }
};
