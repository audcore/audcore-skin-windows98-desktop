#pragma once
#include <windows.h>
#include "../audcore_skin.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <cstddef>
#include <cmath>

// Standalone host-side draft for trusted in-process skins. The host owns the
// callbacks/context for at least as long as this object; it must call all
// methods on the UI thread and close attached host panels before destruction.
// This loader is not a sandbox or a signature/trust decision.
class NativeSkinModule {
public:
    NativeSkinModule() = default;
    NativeSkinModule(const NativeSkinModule&) = delete;
    NativeSkinModule& operator=(const NativeSkinModule&) = delete;
    ~NativeSkinModule() { close(); }

    bool load(const std::wstring& path) {
        if (module_ || !std::filesystem::path(path).is_absolute() || path.find(L'\0') != std::wstring::npos || !native_extension(path)) return false;
        HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) return false;
        const auto getter = reinterpret_cast<audcore_skin_get_descriptor_fn>(GetProcAddress(module, "audcore_skin_get_descriptor"));
        const auto* descriptor = getter ? getter(AUDCORE_SKIN_ABI_MAJOR) : nullptr;
        if (!valid_descriptor(descriptor)) {
            FreeLibrary(module);
            return false;
        }
        module_ = module;
        descriptor_ = descriptor;
        return true;
    }
    bool create(const audcore_skin_host& host) {
        if (!descriptor_ || instance_) return false;
        instance_ = descriptor_->create(&host);
        return instance_ != nullptr;
    }
    bool show() { return instance_ && descriptor_->show(instance_) == 0; }
    void hide() { if (instance_) descriptor_->hide(instance_); }
    void close() noexcept {
        if (instance_) { descriptor_->destroy(instance_); instance_ = nullptr; }
        descriptor_ = nullptr;
        if (module_) { FreeLibrary(module_); module_ = nullptr; }
    }
    static bool valid_descriptor(const audcore_skin_descriptor* descriptor) noexcept {
        if (!descriptor || descriptor->header.size < offsetof(audcore_skin_descriptor, themes) || descriptor->header.major != AUDCORE_SKIN_ABI_MAJOR) return false;
        if (descriptor->header.minor >= 2) {
            if (descriptor->header.size < sizeof(audcore_skin_descriptor)) return false;
            const auto themes = descriptor->themes;
            const auto min = descriptor->scale_min, max = descriptor->scale_max, initial = descriptor->scale_default;
            if (!themes || (themes & ~(AUDCORE_SKIN_THEME_DARK | AUDCORE_SKIN_THEME_LIGHT)) ||
                !std::isfinite(min) || !std::isfinite(max) || !std::isfinite(initial) || min < 0.25f || max > 4.0f || min > initial || initial > max) return false;
        }
        return
            valid_text(descriptor->id) && valid_text(descriptor->name) && valid_text(descriptor->version) &&
            descriptor->create && descriptor->show && descriptor->hide && descriptor->destroy;
    }
    static bool native_extension(std::wstring_view path) noexcept {
        const auto ends_with = [path](std::wstring_view suffix) {
            if (path.size() < suffix.size()) return false;
            const auto tail = path.substr(path.size() - suffix.size());
            for (size_t i = 0; i < suffix.size(); ++i) {
                wchar_t ch = tail[i];
                if (ch >= L'A' && ch <= L'Z') ch += L'a' - L'A';
                if (ch != suffix[i]) return false;
            }
            return true;
        };
        return ends_with(L".skin.audcore");
    }
private:
    static bool valid_text(audcore_skin_bytes text) noexcept {
        if (!text.data || !text.len || text.len > 1024) return false;
        for (uint32_t i = 0; i < text.len; ++i) if (!text.data[i]) return false;
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(text.data), static_cast<int>(text.len), nullptr, 0) > 0;
    }
    HMODULE module_ = nullptr;
    const audcore_skin_descriptor* descriptor_ = nullptr;
    void* instance_ = nullptr;
};
