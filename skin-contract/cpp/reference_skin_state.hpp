#pragma once
#include "../../tools/mixer-console/vendor/json.hpp"
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <string>
#include <string_view>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// UI state only. The host owns the graph and audio; never optimistically change
// this model after sending a command. Snapshot/delta JSON is copied and parsed
// within the callback because the ABI byte span is borrowed.
struct ReferenceEffect {
    std::uint64_t instance = 0;
    std::string name;
    bool bypass = false;
    bool offline = false;
    std::string fallback_name;
    bool has_alias = false;
    bool source_offline = false;
    double wet = 1.0;
    nlohmann::json identity = nlohmann::json::object();
};
struct ReferenceChannel {
    std::uint32_t id = 0;
    std::string name;
    std::string icon;
    std::string color;
    std::string display_color;
    std::string link_group;
    std::string recording_group;
    std::string mute_shortcut;
    int channel_mode = 0;
    bool ignore_latency = false;
    bool muted = false;
    bool solo = false;
    bool solo_safe = false;
    bool hidden = false;
    bool align_right = false;
    bool fx_bypass = false;
    bool recording = false;
    bool recording_stopping = false;
    double gain_db = 0;
    double phase = 0;
    double peak = 0;
    double peak_l = 0;
    double peak_r = 0;
    double loudness_l = 0;
    double loudness_r = 0;
    std::vector<ReferenceEffect> effects;
    // Includes hidden routing bridges so their IDs remain globally unique.
    std::vector<std::uint64_t> all_effect_ids;
    // Preserve the complete host rack for guarded cross-channel copies.
    nlohmann::json rack_snapshot = nlohmann::json::array();
    std::vector<nlohmann::json> sends;
    nlohmann::json identity = {{"chain_uuid",nullptr},{"source_uuid",nullptr}};
};
struct ReferenceEffectControl {
    std::uint32_t channel;
    std::uint64_t instance;
    nlohmann::json channel_identity, effect_identity;
    std::optional<std::uint32_t> parameter;
    double value;
};
struct ReferenceSendControl {
    std::uint32_t channel;
    std::size_t index;
    nlohmann::json channel_identity, route_identity;
    double value;
};
struct ReferenceGainControl {
    std::uint32_t channel;
    nlohmann::json channel_identity;
    double value;
};
enum class ReferenceToggleKind : std::uint8_t { Mute, Solo, FxBypass, Phase };
struct ReferenceToggleControl {
    std::uint32_t channel;
    nlohmann::json channel_identity;
    ReferenceToggleKind kind;
    bool value;
};
struct ReferenceLabels {
    std::string menu = "工程菜单";
    std::string settings = "设置";
    std::string performance = "效果器性能";
    std::string add = "添加通道";
    std::string mute = "静音";
    std::string solo = "独奏";
    std::string input = "输入";
    std::string effects = "效果器";
    std::string recording = "录音";
    std::string send = "发送";
    std::string output = "输出";
    std::string sidechain = "侧链";
    std::string plugin = "插件";
    std::string channel_template = "通道 {}";
    std::unordered_map<std::string, std::string> icon_labels;
    std::unordered_map<std::string, std::string> feedback;
};
class ReferenceSkinState {
public:
    bool snapshot(const std::string& bytes) noexcept {
        try {
            const auto value = nlohmann::json::parse(bytes);
            bool next_dark = true;
            double next_scale = 1.0;
            ReferenceLabels next_labels;
            if (value.contains("i18n") && !read_i18n(value.at("i18n"), next_labels)) return false;
            if (value.contains("appearance") && !read_appearance(value.at("appearance"), next_dark, next_scale)) return false;
            ReferenceSkinState next = *this;
            next.controls_ = nlohmann::json::object();
            if (value.contains("channelControls") && !next.read_controls(value.at("channelControls"))) return false;
            next.loudness_enabled_ = false;
            if (value.contains("meterOptions") && !next.read_meter_options(value.at("meterOptions"))) return false;
            next.graph_identity_known_ = value.contains("graph") && value.at("graph").is_object() && value.at("graph").contains("chains");
            if (next.graph_identity_known_ && !consistent_channel_graph(value.at("channels"), value.at("graph").at("chains"))) return false;
            if (!next.replace_channels(value.at("channels"))) return false;
            next.transport_playing_.reset();
            if (value.contains("transport") && !read_transport(value.at("transport"),next.transport_playing_)) return false;
            next.audio_running_.reset();
            if (value.contains("audio") && !read_audio(value.at("audio"),next.audio_running_)) return false;
            next.output_devices_ = nlohmann::json::object();
            next.output_pairs_ = nlohmann::json::array();
            next.link_groups_ = nlohmann::json::object();
            next.link_slots_ = nlohmann::json::object();
            if (value.contains("graph") && !next.read_output_devices(value.at("graph"))) return false;
            next.primary_ = nlohmann::json::object();
            if (value.contains("primary")) {
                if (!value.at("primary").is_object()) return false;
                for (auto it = value.at("primary").begin(); it != value.at("primary").end(); ++it) {
                    if (it.value().is_null() || !next.read_primary_entry(it.key(), it.value())) return false;
                }
            }
            next.effect_info_ = nlohmann::json::object();
            next.missing_effects_ = nlohmann::json::object();
            next.input_labels_ = nlohmann::json::object();
            next.channel_labels_ = nlohmann::json::object();
            next.sidechain_labels_ = nlohmann::json::object();
            next.effect_labels_ = nlohmann::json::object();
            next.output_labels_ = nlohmann::json::object();
            next.output_reasons_ = nlohmann::json::object();
            next.send_reasons_ = nlohmann::json::object();
            if (!next.read_device_labels(value)) return false;
            if (!next.read_route_reasons(value)) return false;
            if (!next.read_effect_runtime(value)) return false;
            next.apply_effect_runtime();
            channels_ = std::move(next.channels_);
            graph_identity_known_ = next.graph_identity_known_;
            primary_ = std::move(next.primary_);
            effect_info_ = std::move(next.effect_info_);
            missing_effects_ = std::move(next.missing_effects_);
            input_labels_ = std::move(next.input_labels_);
            channel_labels_ = std::move(next.channel_labels_);
            sidechain_labels_ = std::move(next.sidechain_labels_);
            effect_labels_ = std::move(next.effect_labels_);
            output_labels_ = std::move(next.output_labels_);
            output_reasons_ = std::move(next.output_reasons_);
            send_reasons_ = std::move(next.send_reasons_);
            output_devices_ = std::move(next.output_devices_);
            output_pairs_ = std::move(next.output_pairs_);
            link_groups_ = std::move(next.link_groups_);
            link_slots_ = std::move(next.link_slots_);
            transport_playing_ = next.transport_playing_;
            audio_running_ = next.audio_running_;
            controls_ = std::move(next.controls_);
            loudness_enabled_ = next.loudness_enabled_;
            dark_ = next_dark;
            scale_ = next_scale;
            labels_ = std::move(next_labels);
            return true;
        } catch (...) { return false; }
    }
    bool delta(const std::string& bytes, bool* transport_changed = nullptr) noexcept {
        if (transport_changed) *transport_changed = false;
        try {
            const auto value = nlohmann::json::parse(bytes);
            const auto type = value.at("type").get<std::string>();
            if (type == "structure") {
                const bool has_graph = value.contains("chains");
                if ((graph_identity_known_ && !has_graph) || (has_graph && !consistent_channel_graph(value.at("channels"), value.at("chains")))) return false;
                if (!replace_channels(value.at("channels"), true)) return false;
                graph_identity_known_ = has_graph;
                apply_effect_runtime(); return true;
            }
            if (type == "order") {
                const auto& ids = value.at("ids");
                if (!ids.is_array() || ids.size() != channels_.size()) return false;
                std::vector<ReferenceChannel> next;
                next.reserve(channels_.size());
                std::unordered_map<std::uint32_t, const ReferenceChannel*> by_id;
                by_id.reserve(channels_.size());
                for (const auto& channel : channels_) by_id.emplace(channel.id, &channel);
                std::unordered_set<std::uint32_t> seen;
                seen.reserve(channels_.size());
                for (const auto& value : ids) {
                    if (!value.is_number_unsigned()) return false;
                    const auto id = value.get<std::uint64_t>();
                    if (id > UINT32_MAX || !seen.insert(static_cast<std::uint32_t>(id)).second) return false;
                    const auto found = by_id.find(static_cast<std::uint32_t>(id));
                    if (found == by_id.end()) return false;
                    next.push_back(*found->second);
                }
                channels_ = std::move(next);
                return true;
            }
            if (type == "primary") {
                if (!value.at("instance").is_number_unsigned()) return false;
                const auto instance = value.at("instance").get<std::uint64_t>();
                if (instance > 9007199254740991ULL) return false;
                return read_primary_entry(std::to_string(instance), value.at("parameter"));
            }
            if (type == "graph_patch") {
                const auto& changes = value.at("changes");
                if (!changes.is_object() || changes.empty()) return false;
                for (auto item = changes.begin(); item != changes.end(); ++item)
                    if (item.key() != "link_groups" && item.key() != "link_slots") return false;
                if (!valid_link_fields(changes)) return false;
                if (changes.contains("link_groups")) link_groups_ = changes.at("link_groups");
                if (changes.contains("link_slots")) link_slots_ = changes.at("link_slots");
                return true;
            }
            if (type == "change" && value.value("key", "") == "i18n") {
                ReferenceLabels next;
                if (!read_i18n(value.at("value"), next)) return false;
                labels_ = std::move(next);
                return true;
            }
            if (type == "change" && value.value("key", "") == "appearance") {
                bool next_dark = dark_;
                double next_scale = scale_;
                if (!read_appearance(value.at("value"), next_dark, next_scale)) return false;
                dark_ = next_dark;
                scale_ = next_scale;
                return true;
            }
            if (type == "change") {
                const auto key = value.at("key").get<std::string>();
                if (key == "outputReasons" || key == "sendReasons") return read_route_reasons(nlohmann::json{{key,value.at("value")}});
                if (key == "inputLabels" || key == "channelLabels" || key == "sidechainLabels" || key == "effectLabels" || key == "outputLabels") return read_device_labels(nlohmann::json{{key,value.at("value")}});
                if (key == "effectInfo" || key == "missingEffects") {
                    if (!read_effect_runtime(nlohmann::json{{key,value.at("value")}})) return false;
                    apply_effect_runtime();
                    return true;
                }
                if (key == "sidechains") return value.at("value").is_array() && std::all_of(value.at("value").begin(),value.at("value").end(),[](const auto& id){return id.is_number_unsigned() && id.template get<std::uint64_t>() <= 9007199254740991ULL;});
                if (key == "channelControls") return read_controls(value.at("value"));
                if (key == "meterOptions") return read_meter_options(value.at("value"));
                if (key == "transport") {
                    if (!read_transport(value.at("value"),transport_playing_)) return false;
                    if (transport_changed) *transport_changed = true;
                    return true;
                }
                if (key == "audio") {
                    const auto& audio=value.at("value");
                    std::optional<bool> running;
                    if (!read_audio(audio,running)) return false;
                    audio_running_=running;
                    if (running == false) {
                        for (auto& channel:channels_) channel.peak=channel.peak_l=channel.peak_r=channel.loudness_l=channel.loudness_r=0;
                    }
                    return true;
                }
                // These host-owned groups do not affect this minimal renderer.
                // Consume their revisions without requesting a full snapshot.
                if (key == "display" || key == "meterOptions" || key == "channelControls" || key == "inputLabels" || key == "channelLabels" || key == "sidechainLabels" || key == "effectLabels" || key == "outputLabels" || key == "inputReasons" || key == "outputReasons" || key == "sendReasons" || key == "document") return value.at("value").is_object();
                return false;
            }
            if (type == "patch" && value.at("changes").is_object() && !value.at("changes").empty()) {
                const auto& changes = value.at("changes");
                for (auto it = changes.begin(); it != changes.end(); ++it) if (it.key() != "appearance" && it.key() != "i18n" && it.key() != "graph" && it.key() != "channels" && it.key() != "primary" && it.key() != "effectInfo" && it.key() != "missingEffects") return false;
                if (changes.contains("graph") && !changes.at("graph").is_object()) return false;
                const bool has_graph = changes.contains("graph") && changes.at("graph").contains("chains");
                if (graph_identity_known_ && changes.contains("graph") && !has_graph) return false;
                if (has_graph || (graph_identity_known_ && changes.contains("channels"))) {
                    const auto existing = channel_identities();
                    const auto& channels = changes.contains("channels") ? changes.at("channels") : existing;
                    const auto& chains = has_graph ? changes.at("graph").at("chains") : existing;
                    if (!consistent_channel_graph(channels, chains)) return false;
                }
                ReferenceSkinState next_state = *this;
                if (has_graph) next_state.graph_identity_known_ = true;
                if (changes.contains("channels") && !next_state.replace_channels(changes.at("channels"), true)) return false;
                if (changes.contains("graph") && !next_state.read_output_devices(changes.at("graph"))) return false;
                if (changes.contains("primary")) {
                    if (!changes.at("primary").is_object()) return false;
                    next_state.primary_ = nlohmann::json::object();
                    for (auto it = changes.at("primary").begin(); it != changes.at("primary").end(); ++it)
                        if (it.value().is_null() || !next_state.read_primary_entry(it.key(), it.value())) return false;
                }
                if (!next_state.read_effect_runtime(changes)) return false;
                next_state.apply_effect_runtime();
                bool next_dark = dark_;
                double next_scale = scale_;
                ReferenceLabels next_labels = labels_;
                if (changes.contains("appearance") && !read_appearance(changes.at("appearance"), next_dark, next_scale)) return false;
                if (changes.contains("i18n")) {
                    next_labels = ReferenceLabels{};
                    if (!read_i18n(changes.at("i18n"), next_labels)) return false;
                }
                dark_ = next_dark;
                scale_ = next_scale;
                labels_ = std::move(next_labels);
                channels_ = std::move(next_state.channels_);
                graph_identity_known_ = next_state.graph_identity_known_;
                output_devices_ = std::move(next_state.output_devices_);
                output_pairs_ = std::move(next_state.output_pairs_);
                link_groups_ = std::move(next_state.link_groups_);
                link_slots_ = std::move(next_state.link_slots_);
                primary_ = std::move(next_state.primary_);
                effect_info_ = std::move(next_state.effect_info_);
                missing_effects_ = std::move(next_state.missing_effects_);
                return true;
            }
            if (type == "effect_batch") {
                const auto& changes = value.at("changes");
                if (!changes.is_array() || changes.empty()) return false;
                auto next = channels_;
                std::vector<std::uint32_t> seen;
                for (const auto& change : changes) {
                    if (!change.at("id").is_number_unsigned()) return false;
                    const auto id = change.at("id").get<std::uint64_t>();
                    if (id > UINT32_MAX || std::find(seen.begin(), seen.end(), id) != seen.end()) return false;
                    auto it = std::find_if(next.begin(), next.end(), [id](const auto& channel) { return channel.id == id; });
                    if (it == next.end()) return false;
                    std::vector<ReferenceEffect> effects;
                    std::vector<std::uint64_t> ids;
                    if (!read_effects(change.at("rack"), effects, ids)) return false;
                    it->effects = std::move(effects);
                    it->all_effect_ids = std::move(ids);
                    it->rack_snapshot = change.at("rack");
                    seen.push_back(static_cast<std::uint32_t>(id));
                }
                if (!unique_effect_instances(next)) return false;
                channels_ = std::move(next);
                retain_live_primary();
                apply_effect_runtime();
                return true;
            }
            if (type == "channel_batch") {
                const auto& changes = value.at("changes");
                if (!changes.is_array() || changes.empty()) return false;
                auto next = channels_;
                std::vector<std::uint32_t> seen;
                for (const auto& change : changes) {
                    if (!change.at("id").is_number_unsigned() || !change.at("fields").is_object() || change.at("fields").empty()) return false;
                    const auto id = change.at("id").get<std::uint64_t>();
                    if (id > UINT32_MAX || std::find(seen.begin(), seen.end(), id) != seen.end()) return false;
                    auto it = std::find_if(next.begin(), next.end(), [id](const auto& c) { return c.id == id; });
                    if (it == next.end()) return false;
                    seen.push_back(static_cast<std::uint32_t>(id));
                    for (auto field = change.at("fields").begin(); field != change.at("fields").end(); ++field) {
                        if (!field.value().is_boolean()) return false;
                        if (field.key() == "solo") it->solo = field.value().get<bool>();
                        else if (field.key() == "solo_safe") it->solo_safe = field.value().get<bool>();
                        else if (field.key() == "recording") it->recording = field.value().get<bool>();
                        else if (field.key() == "recording_stopping") it->recording_stopping = field.value().get<bool>();
                        else return false;
                    }
                }
                channels_ = std::move(next);
                return true;
            }
            if (type != "channel") return false; // Request a snapshot for unimplemented updates.
            if (!value.at("id").is_number_unsigned()) return false;
            const auto raw_id = value.at("id").get<std::uint64_t>();
            if (raw_id > UINT32_MAX) return false;
            auto it = std::find_if(channels_.begin(), channels_.end(), [raw_id](const auto& channel) { return channel.id == raw_id; });
            if (it == channels_.end()) return false;
            const auto field = value.at("field").get<std::string>();
            if (graph_identity_known_ && value.contains("graphField") && value.at("graphField").is_string()) {
                const auto& graph_field=value.at("graphField").get_ref<const std::string&>();
                if ((graph_field=="chain_uuid" || graph_field=="source_uuid") && field!=graph_field) return false;
            }
            const auto& data = value.at("value");
            if (field == "muted" && data.is_boolean()) it->muted = data.get<bool>();
            else if (field == "solo" && data.is_boolean()) it->solo = data.get<bool>();
            else if (field == "solo_safe" && data.is_boolean()) it->solo_safe = data.get<bool>();
            else if (field == "hidden" && data.is_boolean()) it->hidden = data.get<bool>();
            else if (field == "align_right" && data.is_boolean()) it->align_right = data.get<bool>();
            else if (field == "fxBypass" && data.is_boolean()) it->fx_bypass = data.get<bool>();
            else if (field == "recording" && data.is_boolean()) it->recording = data.get<bool>();
            else if (field == "recording_stopping" && data.is_boolean()) it->recording_stopping = data.get<bool>();
            else if (field == "gainDb" && data.is_number()) it->gain_db = data.get<double>();
            else if (field == "phase" && (data.is_null() || (data.is_number() && std::isfinite(data.get<double>())))) it->phase = data.is_null() ? 0.0 : data.get<double>();
            else if (field == "name" && data.is_string() && data.get_ref<const std::string&>().size() <= 4096) it->name = data.get<std::string>();
            else if (field == "icon" && (data.is_null() || (data.is_string() && data.get_ref<const std::string&>().size() <= 128))) it->icon = data.is_null() ? std::string{} : data.get<std::string>();
            else if (field == "color") {
                const auto display = value.find("displayColor");
                if (!read_channel_color(&data, display == value.end() ? nullptr : &*display, *it)) return false;
            }
            else if (field == "mute_group" && (data.is_null() || (data.is_string() && data.get_ref<const std::string&>().size() <= 256))) it->link_group = data.is_null() ? std::string{} : data.get<std::string>();
            else if (field == "recording_group" && (data.is_null() || (data.is_string() && data.get_ref<const std::string&>().size() <= 256))) it->recording_group = data.is_null() ? std::string{} : data.get<std::string>();
            else if (field == "channel_mode" && (data.is_null() || (data.is_number_integer() && data.get<int>() >= 0 && data.get<int>() <= 2))) it->channel_mode = data.is_null() ? 0 : data.get<int>();
            else if (field == "ignore_latency" && data.is_boolean()) it->ignore_latency = data.get<bool>();
            else if (field == "recording_tap" && (data.is_null() || (data.is_number_unsigned() && data.get<std::uint64_t>() <= 2))) { /* host menu owns the recording position */ }
            else if (field == "mute_shortcut" && (data.is_null() || (data.is_string() && data.get_ref<const std::string&>().size() <= 4096))) it->mute_shortcut = data.is_null() ? std::string{} : data.get<std::string>();
            else if (field == "purpose" && (data.is_null() || (data.is_string() && data.get_ref<const std::string&>().size() <= 4096))) { /* host manages this property */ }
            else if ((field == "chain_uuid" || field == "source_uuid") && (data.is_null() || data.is_string())) {
                // A graph-backed stream must update the same identity in both views.
                if (graph_identity_known_ && (!value.contains("graphField") || value.at("graphField") != field)) return false;
                if (it->identity[field] != data) it->peak = it->peak_l = it->peak_r = it->loudness_l = it->loudness_r = 0;
                it->identity[field] = data;
            }
            else if (field == "inputs" && data.is_array() && std::all_of(data.begin(),data.end(),[](const auto& input){return input.is_number_unsigned() && input.template get<std::uint64_t>() <= UINT32_MAX;})) { /* label arrives from the host */ }
            else if (field == "virtual_input" && (data.is_null() || data == "music")) { /* label arrives from the host */ }
            else if (field == "sends") { std::vector<nlohmann::json> next; if (!read_sends(data, next)) return false; it->sends = std::move(next); }
            else if (field == "rack") {
                std::vector<ReferenceEffect> next;
                std::vector<std::uint64_t> ids;
                if (!read_effects(data, next, ids)) return false;
                for (const auto& other : channels_) if (other.id != it->id) for (const auto id : other.all_effect_ids)
                    if (std::find(ids.begin(), ids.end(), id) != ids.end()) return false;
                it->effects = std::move(next); it->all_effect_ids = std::move(ids); it->rack_snapshot = data; retain_live_primary(); apply_effect_runtime();
            }
            else return false;
            return true;
        } catch (...) { return false; }
    }
    // Meters are transient: malformed frames are dropped without resyncing the graph.
    bool meters(const std::string& bytes) noexcept {
        try {
            const auto value = nlohmann::json::parse(bytes);
            if (value.at("type") != "meters" || !value.at("meters").is_array() || value.at("meters").size() > 4096) return false;
            if (audio_running_ == false) return true;
            auto next = channels_;
            std::vector<std::uint32_t> seen;
            for (const auto& meter : value.at("meters")) {
                if (!meter.at("id").is_number_unsigned() || !meter.at("peak").is_number()) return false;
                const auto raw = meter.at("id").get<std::uint64_t>();
                const double peak = meter.at("peak").get<double>();
                if (raw > UINT32_MAX || !std::isfinite(peak) || peak < 0 || std::find(seen.begin(), seen.end(), raw) != seen.end()) return false;
                const auto read_peak = [&](const char* key, double& out) {
                    if (!meter.contains(key)) { out = peak; return true; }
                    if (!meter.at(key).is_number()) return false;
                    out = meter.at(key).get<double>();
                    return std::isfinite(out) && out >= 0;
                };
                double left = 0, right = 0;
                if (!read_peak("peak_l",left) || !read_peak("peak_r",right)) return false;
                const auto loudness_level = [](const nlohmann::json& value,double& out) {
                    if (value.is_null()) { out=0; return true; }
                    if (!value.is_number()) return false;
                    const auto db=value.get<double>();
                    if (!std::isfinite(db)) return false;
                    out=std::clamp((db+60.)/72.,0.,1.); return true;
                };
                double loud_l=0, loud_r=0;
                if (meter.contains("stereo_loudness") && !meter["stereo_loudness"].is_null()) {
                    const auto& stereo=meter["stereo_loudness"];
                    if (!stereo.is_array() || stereo.size()!=2 || !loudness_level(stereo[0],loud_l) || !loudness_level(stereo[1],loud_r)) return false;
                } else if (meter.contains("loudness") && !loudness_level(meter["loudness"],loud_l)) return false;
                else loud_r=loud_l;
                seen.push_back(static_cast<std::uint32_t>(raw));
                const auto it = std::find_if(next.begin(), next.end(), [raw](const auto& channel) { return channel.id == raw; });
                if (it != next.end()) { // A frame may race a removed channel.
                    it->peak = peak;
                    it->peak_l = left;
                    it->peak_r = right;
                    it->loudness_l = loudness_enabled_ ? loud_l : 0;
                    it->loudness_r = loudness_enabled_ ? loud_r : 0;
                }
            }
            channels_ = std::move(next);
            return true;
        } catch (...) { return false; }
    }
    // The host remains authoritative; this is only the visible ordering.
    std::vector<std::size_t> visible_channels() const {
        std::vector<std::size_t> result;
        result.reserve(channels_.size());
        for (std::size_t i = 0; i < channels_.size(); ++i) if (!channels_[i].hidden && !channels_[i].align_right) result.push_back(i);
        for (std::size_t i = 0; i < channels_.size(); ++i) if (!channels_[i].hidden && channels_[i].align_right) result.push_back(i);
        return result;
    }
    // A drag uses this snapshot to avoid applying an old order after the host
    // changes channel identity, visibility, alignment or order mid-gesture.
    nlohmann::json channel_order_guard() const {
        auto guard = nlohmann::json::array();
        for (const auto& channel : channels_) guard.push_back({channel.id,channel.identity,channel.hidden,channel.align_right});
        return guard;
    }
    // Move within the same visible alignment group; hidden channels remain in
    // the complete order sent to the host and are never silently discarded.
    std::string channel_order_command(std::size_t from, std::size_t to) const {
        if (from >= channels_.size() || to >= channels_.size() || from == to || channels_[from].hidden || channels_[to].hidden || channels_[from].align_right != channels_[to].align_right) return {};
        std::vector<std::uint32_t> ids;
        ids.reserve(channels_.size());
        for (const auto& channel : channels_) ids.push_back(channel.id);
        const auto id=ids[from];
        ids.erase(ids.begin()+from);
        ids.insert(ids.begin()+to,id);
        return nlohmann::json({{"method","channel.setOrder"},{"params",{{"ids",ids},{"expected",channel_order_guard()}}}}).dump();
    }
    std::string channel_order_step(std::size_t from, bool right) const {
        if (from >= channels_.size() || channels_[from].hidden) return {};
        const auto visible=visible_channels();
        const auto pos=std::find(visible.begin(),visible.end(),from);
        if (pos == visible.end() || (right ? pos+1 == visible.end() : pos == visible.begin())) return {};
        const auto to=right ? *(pos+1) : *(pos-1);
        return channel_order_command(from,to);
    }
    // The host compares the entire route list and channel identity before it
    // changes anything, so a delayed gesture cannot reorder replaced routes.
    std::string send_order_command(std::size_t channel, std::size_t from, std::size_t to) const {
        if (channel>=channels_.size() || from>=channels_[channel].sends.size() || to>=channels_[channel].sends.size() || from==to) return {};
        auto reordered=channels_[channel].sends;
        auto route=reordered[from];
        reordered.erase(reordered.begin()+from);
        reordered.insert(reordered.begin()+to,std::move(route));
        // Equal routes can produce an unchanged host graph and no delta. Do
        // not start a pending operation that would then wait indefinitely.
        if (reordered==channels_[channel].sends) return {};
        return nlohmann::json({{"method","channel.moveSend"},{"params",{{"id",channels_[channel].id},{"from",from},{"to",to},{"expected",channels_[channel].sends},{"expectedChannel",channels_[channel].identity}}}}).dump();
    }
    // Display-only fallback. Stored names are never replaced by translations.
    std::string display_name(std::size_t index) const {
        if (index >= channels_.size()) return {};
        const auto& channel = channels_[index];
        const auto key = std::to_string(channel.id);
        if (channel_labels_.contains(key)) return channel_labels_[key].get<std::string>();
        const auto number = std::to_string(channel.id == UINT32_MAX ? UINT32_MAX : channel.id + 1);
        if (!channel.name.empty() && channel.name != "通道 " + number && channel.name != "Ch " + number && channel.name != "通路 " + number) return channel.name;
        static const std::pair<const char*, const char*> icons[] = {
            {"🎤","唱歌"},{"🎙️","麦克风"},{"🎧","监听"},{"🎸","吉他"},{"🎹","键盘"},{"🥁","鼓"},
            {"🎺","小号"},{"🎻","小提琴"},{"🔊","扬声器"},{"🎚️","混音"},{"🎛️","效果器"},{"🎵","音乐"},
            {"📻","电台"},{"💬","聊天"},{"⭐","收藏"},{"🔥","热情"},{"🏛️","大厅"},{"🎶","伴奏"},
            {"🗣️","人声湿声"},{"🔀","总混音"},{"📤","输出"},{"🌊","混响"},
        };
        for (const auto& [icon, key] : icons) if (channel.icon == icon) {
            const auto found = labels_.icon_labels.find(key);
            return found == labels_.icon_labels.end() ? key : found->second;
        }
        auto result = labels_.channel_template;
        const auto position = result.find("{}");
        if (position == std::string::npos) return result + " " + number;
        result.replace(position, 2, number);
        return result;
    }
    std::string effect_display_name(std::uint64_t instance, const std::string& fallback) const {
        const auto key = std::to_string(instance);
        return effect_labels_.contains(key) ? effect_labels_[key].get<std::string>() : fallback;
    }
    std::string input_display_name(std::size_t index) const {
        if (index >= channels_.size()) return {};
        const auto key=std::to_string(channels_[index].id);
        if (input_labels_.contains(key)) return input_labels_[key].get<std::string>();
        return labels_.input;
    }
    static std::optional<std::string> response_error(const std::string& bytes) noexcept {
        try {
            const auto value=nlohmann::json::parse(bytes);
            if (!value.is_string() || value.get_ref<const std::string&>().size()>4096) return std::nullopt;
            return value.get<std::string>();
        } catch (...) { return std::nullopt; }
    }
    static std::optional<std::string> notice(const std::string& bytes) noexcept {
        try {
            const auto value=nlohmann::json::parse(bytes);
            if (value.value("type","") != "notice" || (value.value("level","") != "error" && value.value("level","") != "info")) return std::nullopt;
            if (!value.contains("message") || !value["message"].is_string() || value["message"].get_ref<const std::string&>().size()>4096) return std::nullopt;
            return value["message"].get<std::string>();
        } catch (...) { return std::nullopt; }
    }
    std::string feedback(const std::string& message) const {
        static const std::unordered_map<std::string,std::string> keys={
            {"channel IDs exhausted","通道数量已满"},{"channel limit reached","通道数量已满"},
            {"channel identity changed","通道已变化，请重试"},{"effect identity changed","工程已变化，请重试"},
            {"effect target changed","工程已变化，请重试"},{"send route changed","发送路由已变化，请重试"},
            {"sends changed; retry","发送路由已变化，请重试"},{"graph changed; retry","工程已变化，请重试"},
            {"snapshot required","状态尚未同步，请稍候"},{"skin session inactive","界面已休眠，请重试"},
            {"edit already pending","请等待当前操作完成"}
        };
        const auto mapped=keys.find(message);
        const auto& key=mapped == keys.end() ? message : mapped->second;
        const auto translated=labels_.feedback.find(key);
        return translated == labels_.feedback.end() ? key : translated->second;
    }
    // Identity accompanies every targeted command so a delayed action cannot
    // affect an object that reused the same channel, effect or send index.
    std::string channel_request(std::size_t index, nlohmann::json command) const {
        command["params"]["expectedChannel"] = channels_[index].identity;
        return command.dump();
    }
    std::string effect_request(std::size_t channel, std::size_t effect, nlohmann::json command) const {
        auto identity=channels_[channel].effects[effect].identity;
        identity["channel"]=channels_[channel].identity;
        command["params"]["expectedEffect"]=std::move(identity);
        return command.dump();
    }
    std::string send_request(std::size_t channel, std::size_t send, nlohmann::json command) const {
        const auto& route=channels_[channel].sends[send];
        command["params"]["expected"]={{"id",route.value("id",nlohmann::json(nullptr))},{"dest",route.at("dest")},{"output_channel",route.value("output_channel",nlohmann::json(nullptr))},{"channel",channels_[channel].identity}};
        return command.dump();
    }
    // Build a command from authoritative state; do not change state here.
    std::optional<ReferenceToggleControl> toggle_control(std::size_t index, bool solo) const {
        return toggle_control(index,solo ? ReferenceToggleKind::Solo : ReferenceToggleKind::Mute);
    }
    std::optional<ReferenceToggleControl> toggle_control(std::size_t index, ReferenceToggleKind kind) const {
        if (index >= channels_.size()) return {};
        const auto& channel=channels_[index];
        bool value=false;
        switch (kind) {
            case ReferenceToggleKind::Mute: value=channel.muted; break;
            case ReferenceToggleKind::Solo: value=channel.solo; break;
            case ReferenceToggleKind::FxBypass: value=channel.fx_bypass; break;
            case ReferenceToggleKind::Phase: if (!std::isfinite(channel.phase)) return {}; value=std::abs(channel.phase)>90.; break;
        }
        return ReferenceToggleControl{channel.id,channel.identity,kind,value};
    }
    std::string toggle_command(std::size_t index, bool solo, std::optional<bool> pending_base = {}) const {
        if (index >= channels_.size()) return {};
        const auto& channel = channels_[index];
        if (solo) return channel_request(index, {{"method", "channel.setSolo"}, {"params", {{"id", channel.id}, {"solo", !pending_base.value_or(channel.solo)}}}});
        return channel_request(index, {{"method", "channel.setMute"}, {"params", {{"id", channel.id}, {"muted", !pending_base.value_or(channel.muted)}}}});
    }
    std::string recording_command(std::size_t index) const {
        if (index >= channels_.size() || channels_[index].recording_stopping) return {};
        const auto& channel = channels_[index];
        return channel_request(index, {{"method", "channel.setRecording"}, {"params", {{"id", channel.id}, {"recording", !channel.recording}}}});
    }
    std::string link_menu_command(std::size_t index, long x, long y, bool settings) const {
        if (index >= channels_.size()) return {};
        return channel_request(index, {{"method", settings ? "menu.openLinkSettings" : "menu.openLink"}, {"params", {{"id", channels_[index].id}, {"anchor", {{"x", x}, {"y", y}, {"space", "screen-physical"}}}}}});
    }
    std::string solo_menu_command(std::size_t index, long x, long y) const {
        if (index >= channels_.size()) return {};
        return channel_request(index, {{"method", "menu.openSolo"}, {"params", {{"id", channels_[index].id}, {"anchor", {{"x", x}, {"y", y}, {"space", "screen-physical"}}}}}});
    }
    // Shortcut capture is a host-owned panel; screen coordinates are physical.
    std::string shortcut_menu_command(std::size_t index, long x, long y) const {
        if (index >= channels_.size()) return {};
        return channel_request(index, {{"method", "menu.openShortcut"}, {"params", {{"id", channels_[index].id}, {"anchor", {{"x", x}, {"y", y}, {"space", "screen-physical"}}}}}});
    }
    std::string recording_menu_command(std::size_t index, long x, long y) const {
        if (index >= channels_.size()) return {};
        return channel_request(index, {{"method", "menu.openRecording"}, {"params", {{"id", channels_[index].id}, {"anchor", {{"x", x}, {"y", y}, {"space", "screen-physical"}}}}}});
    }
    // Gain is a host command in dB; clamp to the existing console range.
    std::optional<ReferenceGainControl> gain_control(std::size_t index) const {
        if (index >= channels_.size() || !std::isfinite(channels_[index].gain_db)) return {};
        return ReferenceGainControl{channels_[index].id,channels_[index].identity,channels_[index].gain_db};
    }
    std::string gain_command(std::size_t index, double delta, std::optional<double> pending_base = {}) const {
        const auto control=gain_control(index);
        if (!control || !std::isfinite(delta)) return {};
        const double current=pending_base.value_or(control->value);
        if (!std::isfinite(current)) return {};
        const double value = std::clamp(current + delta, -60.0, 12.0);
        return channel_request(index, {{"method", "channel.setGain"}, {"params", {{"id", channels_[index].id}, {"db", value}}}});
    }
    std::string fx_bypass_command(std::size_t index, std::optional<bool> pending_base = {}) const {
        if (index >= channels_.size()) return {};
        return channel_request(index, {{"method", "channel.setFxBypass"}, {"params", {{"id", channels_[index].id}, {"bypass", !pending_base.value_or(channels_[index].fx_bypass)}}}});
    }
    std::string phase_command(std::size_t index, std::optional<bool> pending_base = {}) const {
        if (index >= channels_.size() || !std::isfinite(channels_[index].phase)) return {};
        const int degrees = pending_base.value_or(std::abs(channels_[index].phase) > 90) ? 0 : 180;
        return channel_request(index, {{"method", "channel.setPhase"}, {"params", {{"id", channels_[index].id}, {"degrees", degrees}}}});
    }
    std::string effect_editor_command(std::size_t channel, std::size_t effect) const {
        if (channel >= channels_.size() || effect >= channels_[channel].effects.size() || channels_[channel].effects[effect].offline) return {};
        return effect_request(channel,effect, {{"method", "effect.openEditor"}, {"params", {{"id", channels_[channel].id}, {"instance", channels_[channel].effects[effect].instance}}}});
    }
    std::string effect_menu_command(std::size_t channel, std::size_t effect, long x, long y) const {
        if (channel >= channels_.size() || effect >= channels_[channel].effects.size()) return {};
        return effect_request(channel,effect, {{"method", "menu.openEffect"}, {"params", {{"id", channels_[channel].id}, {"instance", channels_[channel].effects[effect].instance}, {"anchor", {{"x", x}, {"y", y}, {"space", "screen-physical"}}}}}});
    }
    // A wheel tick changes the displayed primary parameter when available;
    // other effects use wet mix. Values remain authoritative from the host.
    std::optional<ReferenceEffectControl> effect_control(std::size_t channel, std::size_t effect) const {
        if (channel >= channels_.size() || effect >= channels_[channel].effects.size()) return {};
        const auto& slot=channels_[channel].effects[effect];
        if (slot.offline) return {};
        const auto key=std::to_string(slot.instance);
        const auto parameter=primary_.find(key);
        if (parameter != primary_.end()) return ReferenceEffectControl{channels_[channel].id,slot.instance,channels_[channel].identity,slot.identity,parameter->at("id").get<std::uint32_t>(),parameter->at("normalized").get<double>()};
        return ReferenceEffectControl{channels_[channel].id,slot.instance,channels_[channel].identity,slot.identity,std::nullopt,slot.wet};
    }
    std::string effect_adjust_command(std::size_t channel, std::size_t effect, int steps, std::optional<double> pending_base = {}) const {
        if (channel >= channels_.size() || effect >= channels_[channel].effects.size() || !steps) return {};
        const auto& slot=channels_[channel].effects[effect];
        if (slot.offline) return {};
        const auto key=std::to_string(slot.instance);
        if (primary_.contains(key)) {
            const auto& parameter=primary_[key];
            const double base=pending_base.value_or(parameter.at("normalized").get<double>());
            if (!std::isfinite(base) || base<0 || base>1) return {};
            const double minimum=parameter.at("minimum").get<double>();
            const double maximum=parameter.at("maximum").get<double>();
            const double step=parameter.at("step").get<double>();
            const double skew=parameter.at("skew").get<double>();
            const bool inverted=parameter.at("inverted").get<bool>();
            const double position=inverted ? 1.0-base : base;
            const double moved=std::clamp(position+steps*0.01,0.0,1.0);
            const double raw=inverted ? 1.0-moved : moved;
            const double range=maximum-minimum;
            double plain=std::clamp(minimum+std::round((std::pow(raw,1.0/skew)*range)/step)*step,minimum,maximum);
            double normalized=std::pow((plain-minimum)/range,skew);
            if (moved != position && std::abs(normalized-base)<1e-9) {
                const int direction=(steps>0 ? 1 : -1)*(inverted ? -1 : 1);
                plain=std::clamp(plain+direction*step,minimum,maximum);
                normalized=std::pow((plain-minimum)/range,skew);
            }
            if (std::abs(normalized-base)<1e-9) return {};
            return effect_request(channel,effect, {{"method","effect.setPrimary"},{"params",{{"id",channels_[channel].id},{"instance",slot.instance},{"parameter",parameter.at("id")},{"normalized",normalized}}}});
        }
        const double base=pending_base.value_or(slot.wet);
        if (!std::isfinite(base) || base<0 || base>1) return {};
        const double wet=std::clamp(std::round((base+steps*0.01)*100.0)/100.0,0.0,1.0);
        if (std::abs(wet-base)<1e-9) return {};
        return effect_request(channel,effect, {{"method","effect.setWet"},{"params",{{"id",channels_[channel].id},{"instance",slot.instance},{"wet",wet}}}});
    }
    std::string effect_value_text(std::size_t channel, std::size_t effect) const {
        if (channel >= channels_.size() || effect >= channels_[channel].effects.size()) return {};
        const auto& slot=channels_[channel].effects[effect];
        const auto key=std::to_string(slot.instance);
        char number[64]{};
        if (primary_.contains(key)) {
            const auto& parameter=primary_[key];
            const double minimum=parameter.at("minimum").get<double>();
            const double step=parameter.at("step").get<double>();
            const double raw=minimum+std::pow(parameter.at("normalized").get<double>(),1.0/parameter.at("skew").get<double>())*(parameter.at("maximum").get<double>()-minimum);
            const double value=std::clamp(minimum+std::round((raw-minimum)/step)*step,minimum,parameter.at("maximum").get<double>());
            std::snprintf(number,sizeof(number),"%.3g",value);
            return std::string(number)+parameter.at("units").get<std::string>();
        }
        std::snprintf(number,sizeof(number),"%.0f%%",slot.wet*100.0);
        return number;
    }
    // Settings and performance are host-owned windows, never skin-rendered.
    static std::string panel_command(bool performance) {
        return nlohmann::json({{"method", performance ? "performance.open" : "settings.open"}, {"params", nlohmann::json::object()}}).dump();
    }
    // The host owns standard menus; the skin provides a physical desktop anchor.
    std::string channel_menu_command(std::size_t index, long x, long y, bool input_context = false) const {
        if (index >= channels_.size()) return {};
        auto request = nlohmann::json({{"method", input_context ? "menu.openInputContext" : "menu.openChannel"}, {"params", {{"id", channels_[index].id}, {"anchor", {{"x", x}, {"y", y}, {"space", "screen-physical"}}}}}});
        return channel_request(index, std::move(request));
    }
    std::string send_display_name(std::size_t channel, std::size_t send) const {
        if (channel >= channels_.size() || send >= channels_[channel].sends.size()) return {};
        const auto& dest = channels_[channel].sends[send].at("dest");
        const auto kind = dest.value("kind", "");
        if (kind == "sidechain" && dest.contains("instance") && dest["instance"].is_number_unsigned()) {
            const auto key = std::to_string(dest["instance"].get<std::uint64_t>());
            if (sidechain_labels_.contains(key)) return sidechain_labels_[key].get<std::string>();
        }
        if (kind == "chain" && dest.contains("id") && dest["id"].is_number_unsigned()) {
            const auto id = dest["id"].get<std::uint64_t>();
            for (std::size_t i = 0; i < channels_.size(); ++i) if (channels_[i].id == id) return display_name(i);
        } else if (kind == "output" && dest.contains("pair") && dest["pair"].is_number_unsigned()) {
            const auto pair = dest["pair"].get<std::uint64_t>();
            const auto key = std::to_string(pair);
            const auto& route = channels_[channel].sends[send];
            const int side = route.contains("output_channel") && route["output_channel"].is_number_unsigned() && route["output_channel"].get<std::uint64_t>() <= 1 ? static_cast<int>(route["output_channel"].get<std::uint64_t>()) : -1;
            if (output_labels_.contains(key)) {
                const auto& names=output_labels_[key];
                const auto field=side < 0 ? "stereo" : side == 0 ? "0" : "1";
                if (names.contains(field) && !names[field].get_ref<const std::string&>().empty()) return names[field].get<std::string>();
            }
            if (output_devices_.contains(key) && output_devices_[key].is_object() && output_devices_[key].contains("name") && output_devices_[key]["name"].is_string()) {
                const auto name = output_devices_[key]["name"].get<std::string>();
                if (!name.empty()) return side < 0 ? name : name + " · " + std::to_string(side + 1);
            }
            if (pair < output_pairs_.size()) {
                const auto offset = output_pairs_[pair].get<std::uint64_t>();
                return labels_.output + " " + std::to_string(offset + (side < 0 ? 1 : side + 1)) + (side < 0 ? "/" + std::to_string(offset + 2) : "");
            }
            if (pair == UINT64_MAX) return labels_.output;
            return labels_.output + " " + std::to_string(pair + 1) + (side < 0 ? "" : " · " + std::to_string(side + 1));
        } else if (kind == "sidechain" && dest.contains("instance") && dest["instance"].is_number_unsigned()) {
            const auto instance = dest["instance"].get<std::uint64_t>();
            for (const auto& candidate : channels_) for (const auto& effect : candidate.effects)
                if (effect.instance == instance) return effect.name + " · " + labels_.sidechain;
            return labels_.plugin + " · " + labels_.sidechain;
        } else if (kind == "virtual_recording") {
            return dest.value("endpoint", "") == "vocal" ? "Audcore REC Vocal" : "Audcore REC Mix";
        }
        return labels_.send + " " + std::to_string(send + 1);
    }
    // The host owns availability; an absent reason means no known failure.
    std::string send_reason(std::size_t channel, std::size_t send) const {
        if (channel >= channels_.size() || send >= channels_[channel].sends.size()) return {};
        const auto id=std::to_string(channels_[channel].id), index=std::to_string(send);
        const auto& dest=channels_[channel].sends[send].at("dest");
        if (send_reasons_.contains(id) && send_reasons_[id].contains(index)) {
            const auto& entry=send_reasons_[id][index];
            if (entry.at("dest")==dest && entry.at("channel")==channels_[channel].identity) return entry.at("reason").get<std::string>();
        }
        if (dest.value("kind","")=="output" && dest.contains("pair") && dest["pair"].is_number_unsigned()) {
            const auto pair=std::to_string(dest["pair"].get<std::uint64_t>());
            if (output_reasons_.contains(pair)) return output_reasons_[pair].get<std::string>();
        }
        return {};
    }
    std::optional<ReferenceSendControl> send_control(std::size_t channel, std::size_t send) const {
        if (channel >= channels_.size() || send >= channels_[channel].sends.size()) return {};
        const auto& item=channels_[channel].sends[send];
        const double value=item.value("level_db",0.0);
        if (!std::isfinite(value)) return {};
        return ReferenceSendControl{channels_[channel].id,send,channels_[channel].identity,{{"id",item.value("id",nlohmann::json(nullptr))},{"dest",item.at("dest")},{"output_channel",item.value("output_channel",nlohmann::json(nullptr))}},value};
    }
    std::string send_gain_command(std::size_t channel, std::size_t send, double delta, std::optional<double> pending_base = {}) const {
        if (channel >= channels_.size() || send >= channels_[channel].sends.size() || !std::isfinite(delta)) return {};
        const auto& item = channels_[channel].sends[send];
        const double current = pending_base.value_or(item.value("level_db", 0.0));
        if (!std::isfinite(current)) return {};
        const double next=std::clamp(current + delta, -60.0, 6.0);
        if (next == current) return {};
        return send_request(channel,send, {{"method", "channel.setSendLevel"}, {"params", {{"id", channels_[channel].id}, {"index", send}, {"db", next}}}});
    }
    std::string send_menu_command(std::size_t channel, std::size_t send, long x, long y, bool primary = false) const {
        if (channel >= channels_.size() || send >= channels_[channel].sends.size()) return {};
        return send_request(channel,send, {{"method", primary ? "menu.openSendPrimary" : "menu.openSend"}, {"params", {{"id", channels_[channel].id}, {"index", send}, {"anchor", {{"x", x}, {"y", y}, {"space", "screen-physical"}}}}}});
    }
    std::string add_send_menu_command(std::size_t index, long x, long y) const {
        if (index >= channels_.size()) return {};
        return channel_request(index, {{"method", "menu.openAddSend"}, {"params", {{"id", channels_[index].id}, {"anchor", {{"x", x}, {"y", y}, {"space", "screen-physical"}}}}}});
    }
    std::string input_menu_command(std::size_t index, long x, long y) const {
        if (index >= channels_.size()) return {};
        return channel_request(index, {{"method", "menu.openInput"}, {"params", {{"id", channels_[index].id}, {"anchor", {{"x", x}, {"y", y}, {"space", "screen-physical"}}}}}});
    }
    std::string effect_browser_command(std::size_t index) const {
        if (index >= channels_.size()) return {};
        return channel_request(index, {{"method", "effect.openBrowser"}, {"params", {{"id", channels_[index].id}}}});
    }
    // The host checks this channel and full rack again before loading. Rows
    // can include empty linked slots, whereas the host index addresses the
    // actual rack (including entries hidden from the skin).
    std::string effect_file_command(std::size_t channel, std::size_t row, const std::string& path) const {
        if (channel >= channels_.size() || path.empty()) return {};
        const auto& target=channels_[channel];
        const auto rows=effect_slot_rows(channel);
        if (row>rows.size()) return {};
        std::size_t index=target.all_effect_ids.size();
        if (row<rows.size() && rows[row]) {
            const auto instance=target.effects[*rows[row]].instance;
            const auto position=std::find(target.all_effect_ids.begin(),target.all_effect_ids.end(),instance);
            if (position==target.all_effect_ids.end()) return {};
            index=static_cast<std::size_t>(position-target.all_effect_ids.begin());
        }
        return nlohmann::json({{"method","effect.insertDroppedFile"},{"params",{{"path",path},{"channel",target.id},{"index",index},{"expectedChannel",target.identity},{"expectedRack",target.rack_snapshot}}}}).dump();
    }
    std::string audio_file_command(std::optional<std::size_t> channel, const std::string& path) const {
        if (path.empty() || (channel && *channel>=channels_.size())) return {};
        nlohmann::json params={{"path",path},{"channel",channel ? nlohmann::json(channels_[*channel].id) : nlohmann::json(nullptr)}};
        if (channel) params["expectedChannel"]=channels_[*channel].identity;
        return nlohmann::json({{"method","audio.dropFile"},{"params",params}}).dump();
    }
    // Copying uses the host's state transfer, never a locally fabricated slot.
    // The full target rack protects the insertion index even if hidden bridge
    // effects or linked slots change before the host applies the command.
    std::string effect_copy_command(std::uint32_t source_id, std::uint64_t instance, std::size_t target_channel, std::size_t target_row) const {
        if (target_channel >= channels_.size()) return {};
        const auto source=std::find_if(channels_.begin(),channels_.end(),[source_id](const auto& channel){return channel.id==source_id;});
        const auto& target=channels_[target_channel];
        if (source==channels_.end() || source->id==target.id) return {};
        const auto effect=std::find_if(source->effects.begin(),source->effects.end(),[instance](const auto& item){return item.instance==instance;});
        if (effect==source->effects.end()) return {};
        const auto rows=effect_slot_rows(target_channel);
        if (target_row>rows.size()) return {};
        std::size_t index=target.all_effect_ids.size();
        if (target_row<rows.size() && rows[target_row]) {
            const auto target_instance=target.effects[*rows[target_row]].instance;
            const auto position=std::find(target.all_effect_ids.begin(),target.all_effect_ids.end(),target_instance);
            if (position==target.all_effect_ids.end()) return {};
            index=static_cast<std::size_t>(position-target.all_effect_ids.begin());
        }
        auto identity=effect->identity;
        identity["channel"]=source->identity;
        return nlohmann::json({{"method","effect.copyTo"},{"params",{{"id",source_id},{"instance",instance},{"target",target.id},{"index",index},
            {"expectedEffect",identity},{"expectedChannel",target.identity},{"expectedRack",target.rack_snapshot}}}}).dump();
    }
    bool linked_effect_slots(std::size_t index) const {
        if (index >= channels_.size()) return false;
        const auto group = normalize_link_group(channels_[index].link_group);
        return !group.empty() && link_groups_.contains(group) && link_groups_[group].is_object() && link_groups_[group].value("slots", false);
    }
    std::string effect_slot_move_command(std::size_t channel, std::size_t effect, std::size_t target) const {
        if (!linked_effect_slots(channel) || effect >= channels_[channel].effects.size() || target >= effect_slot_rows(channel).size()) return {};
        const auto rows=effect_slot_rows(channel);
        nlohmann::json expected_target=nullptr;
        if (rows[target]) { expected_target=channels_[channel].effects[*rows[target]].identity; expected_target["channel"]=channels_[channel].identity; expected_target["instance"]=channels_[channel].effects[*rows[target]].instance; }
        return effect_request(channel,effect, {{"method", "effect.moveSlot"}, {"params", {{"id", channels_[channel].id}, {"instance", channels_[channel].effects[effect].instance}, {"index", target},{"expectedTarget",expected_target}}}});
    }
    // Ordinary racks move by visible effect index; linked racks move by slot.
    // Both forms carry the target identity so a concurrent reorder is rejected.
    std::string effect_move_command(std::size_t channel, std::size_t effect, std::size_t target) const {
        if (channel >= channels_.size()) return {};
        if (linked_effect_slots(channel)) return effect_slot_move_command(channel, effect, target);
        const auto& rack = channels_[channel].effects;
        if (effect >= rack.size() || target >= rack.size()) return {};
        const auto& all = channels_[channel].all_effect_ids;
        const auto position = std::find(all.begin(), all.end(), rack[target].instance);
        if (position == all.end()) return {};
        nlohmann::json expected_target = rack[target].identity;
        expected_target["channel"] = channels_[channel].identity;
        expected_target["instance"] = rack[target].instance;
        return effect_request(channel,effect, {{"method", "effect.move"}, {"params", {{"id", channels_[channel].id}, {"instance", rack[effect].instance}, {"index", static_cast<std::size_t>(position - all.begin())}, {"expectedTarget", expected_target}}}});
    }
    // A drag retains identities, not vector indices, across host updates.
    std::string effect_slot_drag_command(std::uint32_t channel_id, std::uint64_t instance, const std::string& group, std::size_t target, std::optional<std::uint64_t> expected_target) const {
        const auto channel = std::find_if(channels_.begin(), channels_.end(), [channel_id](const auto& item) { return item.id == channel_id; });
        if (channel == channels_.end() || channel->link_group != group) return {};
        const auto effect = std::find_if(channel->effects.begin(), channel->effects.end(), [instance](const auto& item) { return item.instance == instance; });
        if (effect == channel->effects.end()) return {};
        const auto channel_index = static_cast<std::size_t>(channel - channels_.begin());
        const auto rows = effect_slot_rows(channel_index);
        if (target >= rows.size()) return {};
        std::optional<std::uint64_t> current_target;
        if (rows[target]) current_target = channel->effects[*rows[target]].instance;
        if (current_target != expected_target) return {};
        return effect_slot_move_command(channel_index, static_cast<std::size_t>(effect - channel->effects.begin()), target);
    }
    std::string effect_drag_command(std::uint32_t channel_id, std::uint64_t instance, const std::string& group, bool linked, std::size_t target, std::optional<std::uint64_t> expected_target) const {
        const auto channel = std::find_if(channels_.begin(), channels_.end(), [channel_id](const auto& item) { return item.id == channel_id; });
        if (channel == channels_.end() || channel->link_group != group) return {};
        const auto index = static_cast<std::size_t>(channel - channels_.begin());
        if (linked_effect_slots(index) != linked) return {};
        const auto effect = std::find_if(channel->effects.begin(), channel->effects.end(), [instance](const auto& item) { return item.instance == instance; });
        if (effect == channel->effects.end()) return {};
        const auto rows = effect_slot_rows(index);
        if (target >= rows.size()) return {};
        std::optional<std::uint64_t> current_target;
        if (rows[target]) current_target = channel->effects[*rows[target]].instance;
        if (current_target != expected_target) return {};
        return effect_move_command(index, static_cast<std::size_t>(effect - channel->effects.begin()), target);
    }
    // Each row holds an index in the visible rack; empty rows are linked slots
    // owned by other channels. The host remains authoritative for positions.
    std::vector<std::optional<std::size_t>> effect_slot_rows(std::size_t index) const {
        if (index >= channels_.size()) return {};
        const auto& source = channels_[index];
        const auto group = normalize_link_group(source.link_group);
        const bool linked = linked_effect_slots(index);
        std::size_t count = source.effects.size();
        if (linked) for (const auto& peer : channels_) if (normalize_link_group(peer.link_group) == group) count = (std::max)(count, peer.effects.size());
        std::vector<std::optional<std::size_t>> rows(count);
        std::size_t cursor = 0;
        for (std::size_t ordinal = 0; ordinal < source.effects.size(); ++ordinal) {
            const auto key = std::to_string(source.effects[ordinal].instance);
            std::size_t target = cursor;
            if (linked && link_slots_.contains(key) && link_slots_[key].is_number_unsigned()) {
                const auto stored = link_slots_[key].get<std::uint64_t>();
                target = stored > count ? count : static_cast<std::size_t>(stored);
            }
            const auto position = (std::max)(cursor, (std::min)(target, count - (source.effects.size() - ordinal)));
            rows[position] = ordinal;
            cursor = position + 1;
        }
        return rows;
    }
    const std::vector<ReferenceChannel>& channels() const noexcept { return channels_; }
    const nlohmann::json& primary() const noexcept { return primary_; }
    bool dark() const noexcept { return dark_; }
    double scale() const noexcept { return scale_; }
    const ReferenceLabels& labels() const noexcept { return labels_; }
    bool loudness_enabled() const noexcept { return loudness_enabled_; }
    bool control_visible(const char* key) const noexcept {
        const auto it=controls_.find(key);
        return it == controls_.end() || !it->is_boolean() || it->get<bool>();
    }
    std::optional<bool> transport_playing() const noexcept { return transport_playing_; }
    std::optional<bool> audio_running() const noexcept { return audio_running_; }
    std::string transport_command(std::optional<bool> base = {}) const {
        if (!transport_playing_) return {};
        return nlohmann::json({{"method","transport.setPlaying"},{"params",{{"playing",!base.value_or(*transport_playing_)}}}}).dump();
    }
    static std::string transport_menu_command(long x,long y) {
        return nlohmann::json({{"method","menu.openTransport"},{"params",{{"anchor",{{"x",x},{"y",y},{"space","screen-physical"}}}}}}).dump();
    }
    static std::string channel_list_command(long x,long y) {
        return nlohmann::json({{"method","menu.openChannelList"},{"params",{{"anchor",{{"x",x},{"y",y},{"space","screen-physical"}}}}}}).dump();
    }
    static std::string history_command(bool redo) {
        return nlohmann::json({{"method",redo ? "history.redo" : "history.undo"},{"params",nlohmann::json::object()}}).dump();
    }
private:
    // Runtime-only properties can differ, but the two channel identities must
    // identify the same graph object in the same order.
    static bool consistent_channel_graph(const nlohmann::json& channels, const nlohmann::json& chains) {
        if (!channels.is_array() || !chains.is_array() || channels.size() != chains.size()) return false;
        for (std::size_t i=0; i<channels.size(); ++i) {
            const auto& channel=channels[i]; const auto& chain=chains[i];
            if (!channel.is_object() || !chain.is_object() || !channel.contains("id") || !chain.contains("id") || channel.at("id") != chain.at("id")) return false;
            for (const auto* key : {"chain_uuid", "source_uuid"}) {
                const auto left=channel.value(key,nlohmann::json(nullptr));
                const auto right=chain.value(key,nlohmann::json(nullptr));
                if ((!left.is_null() && !left.is_string()) || left != right) return false;
            }
        }
        return true;
    }
    nlohmann::json channel_identities() const {
        auto values=nlohmann::json::array();
        for (const auto& channel:channels_) values.push_back({{"id",channel.id},{"chain_uuid",channel.identity.at("chain_uuid")},{"source_uuid",channel.identity.at("source_uuid")}});
        return values;
    }
    static bool read_audio(const nlohmann::json& value,std::optional<bool>& running) {
        if (!value.is_object() || (value.contains("running") && !value.at("running").is_boolean())) return false;
        running=value.contains("running") ? std::optional<bool>(value.at("running").get<bool>()) : std::nullopt;
        return true;
    }
    bool read_meter_options(const nlohmann::json& value) {
        if (!value.is_object() || (value.contains("loudness") && !value.at("loudness").is_boolean())) return false;
        const bool enabled=value.value("loudness",false);
        if (enabled != loudness_enabled_) for (auto& channel:channels_) channel.loudness_l=channel.loudness_r=0;
        loudness_enabled_=enabled; return true;
    }
    bool read_controls(const nlohmann::json& value) {
        if (!value.is_object()) return false;
        for (const auto* key : {"input","sends","phase","fx","solo","recording","effects","gainLimit"})
            if (value.contains(key) && !value.at(key).is_boolean()) return false;
        controls_=value; return true;
    }
    static bool read_transport(const nlohmann::json& value,std::optional<bool>& output) {
        if (!value.is_object()) return false;
        if (!value.contains("playing") || value["playing"].is_null()) {output.reset();return true;}
        if (!value["playing"].is_boolean()) return false;
        output=value["playing"].get<bool>(); return true;
    }
    static bool route_bridge(const std::string& alias) {
        std::size_t start = alias.rfind("@wdm-play-", 0) == 0 ? 10 : alias.rfind("@wdm-rec-", 0) == 0 ? 9 : std::string::npos;
        if (start == std::string::npos) return false;
        if (start < alias.size() && alias[start] == '+') ++start;
        if (start == alias.size()) return false;
        unsigned value = 0;
        for (; start < alias.size(); ++start) {
            if (alias[start] < '0' || alias[start] > '9') return false;
            value = value * 10 + static_cast<unsigned>(alias[start] - '0');
            if (value > 255) return false;
        }
        return true;
    }
    static bool read_sends(const nlohmann::json& values, std::vector<nlohmann::json>& output) {
        if (!values.is_array() || values.size() > 4096) return false;
        for (const auto& value : values) {
            if (!value.is_object() || !value.contains("dest") || !value["dest"].is_object()) return false;
            output.push_back(value);
        }
        return true;
    }
    static bool read_effects(const nlohmann::json& values, std::vector<ReferenceEffect>& output, std::vector<std::uint64_t>& ids) {
        if (!values.is_array() || values.size() > 4096) return false;
        std::vector<std::uint64_t> seen;
        for (const auto& value : values) {
            if (!value.is_object() || !value.contains("instance") || !value["instance"].is_number_unsigned()) return false;
            const auto id = value["instance"].get<std::uint64_t>();
            if (id > 9007199254740991ULL || std::find(seen.begin(), seen.end(), id) != seen.end()) return false;
            seen.push_back(id);
            ids.push_back(id);
            std::string alias;
            if (value.contains("alias") && !value["alias"].is_null()) {
                if (!value["alias"].is_string() || value["alias"].get_ref<const std::string&>().size() > 4096) return false;
                alias = value["alias"].get<std::string>();
            }
            if (route_bridge(alias)) continue;
            ReferenceEffect effect;
            effect.instance = id;
            effect.identity = {{"effect_uuid",value.value("effect_uuid",nlohmann::json(nullptr))},{"path",value.value("path",nlohmann::json(""))},{"fuid",value.value("fuid",nlohmann::json(nullptr))},{"cid",value.value("cid",nlohmann::json(nullptr))}};
            effect.name = alias;
            effect.has_alias = !alias.empty();
            if (effect.name.empty() && value.contains("name") && value["name"].is_string()) effect.name = value["name"].get<std::string>();
            if (effect.name.empty() && value.contains("path") && value["path"].is_string()) {
                effect.name = value["path"].get<std::string>();
                const auto slash = effect.name.find_last_of("/\\");
                if (slash != std::string::npos) effect.name.erase(0, slash + 1);
            }
            if (effect.name.size() > 4096) return false;
            if (value.contains("bypass")) effect.bypass = value["bypass"].get<bool>();
            if (value.contains("offline")) effect.offline = value["offline"].get<bool>();
            if (value.contains("wet") && !value["wet"].is_null()) {
                if (!value["wet"].is_number()) return false;
                effect.wet=value["wet"].get<double>();
                if (!std::isfinite(effect.wet) || effect.wet<0.0 || effect.wet>1.0) return false;
            }
            effect.fallback_name = effect.name;
            effect.source_offline = effect.offline;
            output.push_back(std::move(effect));
        }
        return true;
    }
    static bool read_i18n(const nlohmann::json& value, ReferenceLabels& labels) {
        if (!value.is_object() || !value.contains("language") || !value["language"].is_string() ||
            !value.contains("messages") || !value["messages"].is_object()) return false;
        const auto& messages = value["messages"];
        const auto assign = [&](const char* key, std::string& output) {
            const auto it = messages.find(key);
            if (it == messages.end()) return true;
            if (!it->is_string() || it->get_ref<const std::string&>().size() > 128) return false;
            output = it->get<std::string>();
            return true;
        };
        static const char* icon_keys[] = {"唱歌","麦克风","监听","吉他","键盘","鼓","小号","小提琴","扬声器","混音","效果器","音乐","电台","聊天","收藏","热情","大厅","伴奏","人声湿声","总混音","输出","混响"};
        for (const auto* key : icon_keys) {
            std::string text = key;
            if (!assign(key, text)) return false;
            labels.icon_labels.emplace(key, std::move(text));
        }
        for (const auto* key : {"操作失败","通道数量已满","通道已变化，请重试","工程已变化，请重试","发送路由已变化，请重试","状态尚未同步，请稍候","界面已休眠，请重试","请等待当前操作完成","导入完成","录音已保存：","打开文件夹"}) {
            std::string text=key;
            if (!assign(key,text)) return false;
            labels.feedback.emplace(key,std::move(text));
        }
        return assign("工程菜单", labels.menu) && assign("通道 {}", labels.channel_template) && assign("设置", labels.settings) && assign("效果器性能", labels.performance) && assign("添加通道", labels.add) && assign("静音", labels.mute) &&
            assign("独奏", labels.solo) && assign("输入", labels.input) && assign("效果器", labels.effects) && assign("发送", labels.send) && assign("输出", labels.output) && assign("侧链", labels.sidechain) && assign("插件", labels.plugin) &&
            assign("录音", labels.recording);
    }
    static bool read_appearance(const nlohmann::json& value, bool& dark, double& scale) {
        if (!value.is_object() || !value.contains("theme") || !value["theme"].is_string()) return false;
        const auto theme = value["theme"].get<std::string>();
        if (theme != "dark" && theme != "light") return false;
        if (value.contains("scale")) {
            if (!value["scale"].is_number()) return false;
            const double candidate = value["scale"].get<double>();
            if (!std::isfinite(candidate) || candidate < 0.75 || candidate > 2.0) return false;
            scale = candidate;
        }
        dark = theme == "dark";
        return true;
    }
    bool read_primary_entry(const std::string& key, const nlohmann::json& parameter) {
        if (key.empty() || key.size() > 16 || key.find_first_not_of("0123456789") != std::string::npos) return false;
        const auto instance = std::stoull(key);
        if (instance > 9007199254740991ULL || std::to_string(instance) != key ||
            std::none_of(channels_.begin(), channels_.end(), [instance](const auto& channel) {
                return std::any_of(channel.effects.begin(), channel.effects.end(), [instance](const auto& effect) { return effect.instance == instance; });
            })) return false;
        if (parameter.is_null()) { primary_.erase(key); return true; }
        if (!parameter.is_object() || !parameter.at("id").is_number_unsigned() || parameter.at("id").get<std::uint64_t>() > UINT32_MAX ||
            !parameter.at("label").is_string() || parameter.at("label").get_ref<const std::string&>().size() > 1024 ||
            !parameter.at("units").is_string() || parameter.at("units").get_ref<const std::string&>().size() > 256 || !parameter.at("inverted").is_boolean()) return false;
        const auto normalized = parameter.at("normalized").get<double>(), fallback = parameter.at("default_normalized").get<double>();
        const auto minimum = parameter.at("minimum").get<double>(), maximum = parameter.at("maximum").get<double>();
        const auto step = parameter.at("step").get<double>(), skew = parameter.at("skew").get<double>();
        if (!std::isfinite(normalized) || normalized < 0 || normalized > 1 || !std::isfinite(fallback) || fallback < 0 || fallback > 1 ||
            !std::isfinite(minimum) || !std::isfinite(maximum) || maximum <= minimum || !std::isfinite(step) || step <= 0 || !std::isfinite(skew) || skew <= 0) return false;
        primary_[key] = parameter;
        return true;
    }
    static std::string normalize_link_group(std::string group) {
        const auto first = group.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return {};
        const auto last = group.find_last_not_of(" \t\r\n");
        group = group.substr(first, last - first + 1);
        std::transform(group.begin(), group.end(), group.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return group;
    }
    static bool read_color(const nlohmann::json& value, std::string& output) {
        if (value.is_null()) { output.clear(); return true; }
        if (!value.is_string()) return false;
        const auto& text = value.get_ref<const std::string&>();
        if (text.size() != 7 || text[0] != '#' || !std::all_of(text.begin() + 1, text.end(), [](unsigned char c) { return std::isxdigit(c) != 0; })) return false;
        output = text;
        return true;
    }
    static bool read_channel_color(const nlohmann::json* stored, const nlohmann::json* display, ReferenceChannel& channel) {
        std::string raw;
        std::string resolved;
        if (stored && !stored->is_null()) {
            if (!stored->is_string()) return false;
            raw = stored->get<std::string>();
        }
        if (display) {
            if (!read_color(*display, resolved) || resolved.empty()) return false;
        } else {
            // The same fallback as audcore_protocol::channel_display::CHANNEL_COLORS.
            static constexpr const char* palette[] = {"#38b9ec", "#f3ad50", "#33d69a", "#a879e8", "#24c5d9", "#9baab9"};
            resolved = palette[channel.id % std::size(palette)];
            const auto first = raw.find_first_not_of('#');
            if (first != std::string::npos) {
                const auto start = raw.data() + first + (raw[first] == '+' ? 1 : 0);
                const auto end = raw.data() + raw.size();
                std::uint32_t rgb = 0;
                const auto parsed = std::from_chars(start, end, rgb, 16);
                if (parsed.ec == std::errc{} && parsed.ptr == end) {
                    char text[8];
                    std::snprintf(text, sizeof(text), "#%06x", static_cast<unsigned>(rgb & 0x00ffffffu));
                    resolved = text;
                }
            }
        }
        channel.color = std::move(raw);
        channel.display_color = std::move(resolved);
        return true;
    }
    bool replace_channels(const nlohmann::json& values, bool preserve_meters = false) {
        if (!values.is_array() || values.size() > 4096) return false;
        std::vector<ReferenceChannel> next;
        for (const auto& value : values) {
            if (!value.is_object() || !value.contains("id") || !value["id"].is_number_unsigned() || !value.contains("name") || !value["name"].is_string()) return false;
            const auto id = value["id"].get<std::uint64_t>();
            if (id > UINT32_MAX || std::any_of(next.begin(), next.end(), [id](const auto& channel) { return channel.id == id; })) return false;
            ReferenceChannel channel;
            channel.id = static_cast<std::uint32_t>(id);
            channel.identity = {{"chain_uuid",value.value("chain_uuid",nlohmann::json(nullptr))},{"source_uuid",value.value("source_uuid",nlohmann::json(nullptr))}};
            if (preserve_meters) {
                const auto previous = std::find_if(channels_.begin(), channels_.end(), [id](const auto& item) { return item.id == id; });
                if (previous != channels_.end() && previous->identity == channel.identity) {
                    channel.peak = previous->peak; channel.peak_l = previous->peak_l; channel.peak_r = previous->peak_r;
                    channel.loudness_l = previous->loudness_l; channel.loudness_r = previous->loudness_r;
                }
            }
            channel.name = value["name"].get<std::string>();
            if (value.contains("mute_shortcut") && !value.at("mute_shortcut").is_null()) {
                if (!value.at("mute_shortcut").is_string() || value.at("mute_shortcut").get_ref<const std::string&>().size() > 4096) return false;
                channel.mute_shortcut = value.at("mute_shortcut").get<std::string>();
            }
            if (value.contains("icon") && !value.at("icon").is_null()) {
                if (!value.at("icon").is_string() || value.at("icon").get_ref<const std::string&>().size() > 128) return false;
                channel.icon = value.at("icon").get<std::string>();
            }
            const auto color = value.find("color"), display = value.find("displayColor");
            if (!read_channel_color(color == value.end() ? nullptr : &*color,
                    display == value.end() ? nullptr : &*display, channel)) return false;
            if (value.contains("mute_group") && !value.at("mute_group").is_null()) {
                if (!value.at("mute_group").is_string() || value.at("mute_group").get_ref<const std::string&>().size() > 256) return false;
                channel.link_group = value.at("mute_group").get<std::string>();
            }
            if (value.contains("recording_group") && !value.at("recording_group").is_null()) {
                if (!value.at("recording_group").is_string() || value.at("recording_group").get_ref<const std::string&>().size() > 256) return false;
                channel.recording_group = value.at("recording_group").get<std::string>();
            }
            if (value.contains("channel_mode") && !value.at("channel_mode").is_null()) {
                if (!value.at("channel_mode").is_number_integer()) return false;
                channel.channel_mode = value.at("channel_mode").get<int>();
                if (channel.channel_mode < 0 || channel.channel_mode > 2) return false;
            }
            if (value.contains("ignore_latency")) {
                if (!value.at("ignore_latency").is_boolean()) return false;
                channel.ignore_latency = value.at("ignore_latency").get<bool>();
            }
            if (channel.name.size() > 4096) return false;
            if (value.contains("muted")) channel.muted = value.at("muted").get<bool>();
            if (value.contains("solo")) channel.solo = value.at("solo").get<bool>();
            if (value.contains("solo_safe")) channel.solo_safe = value.at("solo_safe").get<bool>();
            if (value.contains("hidden")) channel.hidden = value.at("hidden").get<bool>();
            if (value.contains("align_right")) channel.align_right = value.at("align_right").get<bool>();
            if (value.contains("rack")) {
                if (!read_effects(value.at("rack"), channel.effects, channel.all_effect_ids)) return false;
                channel.rack_snapshot = value.at("rack");
            }
            if (value.contains("sends") && !read_sends(value.at("sends"), channel.sends)) return false;
            if (value.contains("fxBypass")) channel.fx_bypass = value.at("fxBypass").get<bool>();
            if (value.contains("recording")) channel.recording = value.at("recording").get<bool>();
            if (value.contains("recording_stopping")) channel.recording_stopping = value.at("recording_stopping").get<bool>();
            if (value.contains("gainDb")) channel.gain_db = value.at("gainDb").get<double>();
            if (value.contains("phase") && !value.at("phase").is_null()) {
                channel.phase = value.at("phase").get<double>();
                if (!std::isfinite(channel.phase)) return false;
            }
            next.push_back(std::move(channel));
        }
        if (!unique_effect_instances(next)) return false;
        channels_ = std::move(next);
        retain_live_primary();
        return true;
    }
    static bool unique_effect_instances(const std::vector<ReferenceChannel>& channels) {
        std::unordered_set<std::uint64_t> instances;
        for (const auto& channel : channels) for (const auto id : channel.all_effect_ids)
            if (!instances.insert(id).second) return false;
        return true;
    }
    void retain_live_primary() {
        for (auto it = primary_.begin(); it != primary_.end();) {
            const auto instance = std::stoull(it.key());
            const bool found = std::any_of(channels_.begin(), channels_.end(), [instance](const auto& channel) {
                return std::any_of(channel.effects.begin(), channel.effects.end(), [instance](const auto& effect) { return effect.instance == instance; });
            });
            if (!found) it = primary_.erase(it); else ++it;
        }
    }
    static bool valid_link_fields(const nlohmann::json& changes) {
                if (changes.contains("link_groups")) {
                    const auto& groups = changes.at("link_groups");
                    if (!groups.is_object()) return false;
                    for (auto it = groups.begin(); it != groups.end(); ++it) {
                        if (it.key().empty() || it.key().size() > 256 || !it.value().is_object()) return false;
                        for (auto item = it.value().begin(); item != it.value().end(); ++item) {
                            const auto& key = item.key();
                            if ((key != "mute" && key != "gain" && key != "fx" && key != "slots" && key != "solo") || !item.value().is_boolean()) return false;
                        }
                    }
                }
                if (changes.contains("link_slots")) {
                    const auto& slots = changes.at("link_slots");
                    if (!slots.is_object()) return false;
                    for (auto it = slots.begin(); it != slots.end(); ++it) {
                        const auto& key = it.key();
                        if (key.empty() || (key.size() > 1 && key[0] == '0') || !it.value().is_number_unsigned()) return false;
                        std::uint64_t id = 0;
                        const auto parsed = std::from_chars(key.data(), key.data() + key.size(), id);
                        if (parsed.ec != std::errc{} || parsed.ptr != key.data() + key.size() || id > 9007199254740991ULL || it.value().get<std::uint64_t>() > 9007199254740991ULL) return false;
                    }
                }
        return true;
    }
    bool read_output_devices(const nlohmann::json& graph) {
        if (!graph.is_object()) return false;
        if (graph.contains("output_devices") && !graph["output_devices"].is_object()) return false;
        if (graph.contains("output_pairs") && (!graph["output_pairs"].is_array() || std::any_of(graph["output_pairs"].begin(), graph["output_pairs"].end(), [](const auto& value) { return !value.is_number_unsigned() || value.template get<std::uint64_t>() > UINT32_MAX - 2ULL; }))) return false;
        if (!valid_link_fields(graph)) return false;
        output_devices_ = graph.value("output_devices", nlohmann::json::object());
        output_pairs_ = graph.value("output_pairs", nlohmann::json::array());
        link_groups_ = graph.value("link_groups", nlohmann::json::object());
        link_slots_ = graph.value("link_slots", nlohmann::json::object());
        return true;
    }
    bool read_route_reasons(const nlohmann::json& value) {
        if (value.contains("outputReasons")) {
            const auto& reasons=value.at("outputReasons");
            if (!reasons.is_object()) return false;
            for (auto it=reasons.begin();it!=reasons.end();++it)
                if (!it.value().is_string() || it.value().get_ref<const std::string&>().size()>4096) return false;
        }
        if (value.contains("sendReasons")) {
            const auto& channels=value.at("sendReasons");
            if (!channels.is_object()) return false;
            for (auto channel=channels.begin();channel!=channels.end();++channel) {
                if (!channel.value().is_object()) return false;
                for (auto reason=channel.value().begin();reason!=channel.value().end();++reason) {
                    const auto& entry=reason.value();
                    if (!entry.is_object() || !entry.contains("dest") || !entry["dest"].is_object() || !entry.contains("channel") || !entry["channel"].is_object() || !entry["channel"].contains("chain_uuid") || !entry["channel"].contains("source_uuid") || !entry.contains("reason") || !entry["reason"].is_string() || entry["reason"].get_ref<const std::string&>().size()>4096) return false;
                }
            }
        }
        if (value.contains("outputReasons")) output_reasons_=value.at("outputReasons");
        if (value.contains("sendReasons")) send_reasons_=value.at("sendReasons");
        return true;
    }
    bool read_device_labels(const nlohmann::json& value) {
        for (const auto* key : {"inputLabels", "channelLabels", "sidechainLabels", "effectLabels"}) if (value.contains(key)) {
            const auto& labels=value.at(key);
            if (!labels.is_object()) return false;
            for (auto it=labels.begin();it!=labels.end();++it) if (!it.value().is_string() || it.value().get_ref<const std::string&>().size()>4096) return false;
        }
        if (value.contains("outputLabels")) {
            const auto& labels=value.at("outputLabels");
            if (!labels.is_object()) return false;
            for (auto it=labels.begin();it!=labels.end();++it) {
                if (!it.value().is_object()) return false;
                for (auto field=it.value().begin();field!=it.value().end();++field) if (!field.value().is_string() || field.value().get_ref<const std::string&>().size()>4096) return false;
            }
        }
        if (value.contains("inputLabels")) input_labels_=value.at("inputLabels");
        if (value.contains("channelLabels")) channel_labels_=value.at("channelLabels");
        if (value.contains("sidechainLabels")) sidechain_labels_=value.at("sidechainLabels");
        if (value.contains("effectLabels")) effect_labels_=value.at("effectLabels");
        if (value.contains("outputLabels")) output_labels_=value.at("outputLabels");
        return true;
    }
    bool read_effect_runtime(const nlohmann::json& value) {
        for (const auto* key : {"effectInfo", "missingEffects"}) {
            if (!value.contains(key)) continue;
            if (!value.at(key).is_object()) return false;
            for (auto it = value.at(key).begin(); it != value.at(key).end(); ++it) {
                if (it.key().empty() || it.key().size() > 16 || !std::all_of(it.key().begin(),it.key().end(),[](unsigned char c){return std::isdigit(c) != 0;})) return false;
                if (it.key().size() > 1 && it.key()[0] == '0') return false;
                std::uint64_t id = 0;
                const auto parsed = std::from_chars(it.key().data(), it.key().data() + it.key().size(), id);
                if (parsed.ec != std::errc{} || parsed.ptr != it.key().data() + it.key().size() || id > 9007199254740991ULL) return false;
                if (std::string_view(key) == "missingEffects") { if (!it.value().is_boolean()) return false; }
                else if (!it.value().is_object() || (it.value().contains("name") && (!it.value().at("name").is_string() || it.value().at("name").get_ref<const std::string&>().size() > 4096))) return false;
            }
        }
        if (value.contains("effectInfo")) effect_info_ = value.at("effectInfo");
        if (value.contains("missingEffects")) missing_effects_ = value.at("missingEffects");
        return true;
    }
    void apply_effect_runtime() {
        for (auto& channel : channels_) for (auto& effect : channel.effects) {
            const auto key = std::to_string(effect.instance);
            effect.name = effect.fallback_name;
            if (!effect.has_alias && effect_info_.contains(key) && effect_info_[key].contains("name")) {
                const auto& name = effect_info_[key]["name"].get_ref<const std::string&>();
                if (!name.empty()) effect.name = name;
            }
            effect.offline = effect.source_offline || missing_effects_.value(key,false);
        }
    }
    nlohmann::json effect_info_ = nlohmann::json::object();
    nlohmann::json missing_effects_ = nlohmann::json::object();
    nlohmann::json input_labels_ = nlohmann::json::object();
    nlohmann::json channel_labels_ = nlohmann::json::object();
    nlohmann::json sidechain_labels_ = nlohmann::json::object();
    nlohmann::json effect_labels_ = nlohmann::json::object();
    nlohmann::json output_labels_ = nlohmann::json::object();
    nlohmann::json output_reasons_ = nlohmann::json::object();
    nlohmann::json send_reasons_ = nlohmann::json::object();
    nlohmann::json output_devices_ = nlohmann::json::object();
    nlohmann::json output_pairs_ = nlohmann::json::array();
    nlohmann::json link_groups_ = nlohmann::json::object();
    nlohmann::json link_slots_ = nlohmann::json::object();
    nlohmann::json primary_ = nlohmann::json::object();
    nlohmann::json controls_ = nlohmann::json::object();
    std::vector<ReferenceChannel> channels_;
    std::optional<bool> transport_playing_;
    std::optional<bool> audio_running_;
    bool loudness_enabled_ = false;
    bool graph_identity_known_ = false;
    bool dark_ = true;
    double scale_ = 1.0;
    ReferenceLabels labels_;
};
