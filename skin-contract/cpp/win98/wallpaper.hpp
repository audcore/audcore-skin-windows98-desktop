#pragma once

#include <wincodec.h>
#include <wrl/client.h>
#include <webp/decode.h>
#include <webp/encode.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace wallpaper {
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
constexpr UINT ready_message = WM_APP + 31;
constexpr std::uintmax_t max_file_bytes = 128 * 1024 * 1024;

struct Image {
    UINT width, height;
    std::vector<std::uint8_t> straight_bgra;
};

inline std::array<UINT, 2> fitted_size(UINT width, UINT height) {
    const auto shortest = std::min(width, height), longest = std::max(width, height);
    const auto scale = std::min({1., 800. / shortest, static_cast<double>(WEBP_MAX_DIMENSION) / longest});
    return {std::max(1u, static_cast<UINT>(width * scale + .5)),
            std::max(1u, static_cast<UINT>(height * scale + .5))};
}

inline fs::path storage_path() {
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (!length || length >= std::size(path)) return {};
    const fs::path exe(path);
    auto stem = exe.stem().wstring();
    std::transform(stem.begin(), stem.end(), stem.begin(), [](wchar_t c) -> wchar_t {
        return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c - L'A' + L'a') : c;
    });
    fs::path root;
    if (stem.size() >= 8 && stem.compare(stem.size()-8, 8, L"portable") == 0) {
        root = exe.parent_path() / L"Audcore-data";
    } else {
        const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", path, static_cast<DWORD>(std::size(path)));
        if (!count || count >= std::size(path)) return {};
        root = fs::path(path) / L"AudCore";
    }
    if (!root.is_absolute()) return {};
    return root / L"skin-wallpapers" / L"cn.audcore.skin.win98" / L"desktop.webp";
}

inline std::optional<Image> decode_webp(const std::uint8_t* bytes, std::size_t size, bool& resized) {
    WebPDecoderConfig config{};
    if (!WebPInitDecoderConfig(&config) || WebPGetFeatures(bytes, size, &config.input) != VP8_STATUS_OK) return {};
    const auto target = fitted_size(config.input.width, config.input.height);
    resized = target[0] != static_cast<UINT>(config.input.width) || target[1] != static_cast<UINT>(config.input.height);
    Image result{target[0],target[1],std::vector<std::uint8_t>(std::size_t(target[0])*target[1]*4)};
    config.options.use_scaling = resized;
    config.options.scaled_width = target[0];
    config.options.scaled_height = target[1];
    config.output.colorspace = MODE_BGRA;
    config.output.is_external_memory = 1;
    config.output.u.RGBA = {result.straight_bgra.data(), static_cast<int>(target[0]*4), result.straight_bgra.size()};
    const auto status = WebPDecode(bytes, size, &config);
    WebPFreeDecBuffer(&config.output);
    if (status != VP8_STATUS_OK) return {};
    return result;
}

struct ComApartment {
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComApartment() { if (SUCCEEDED(initialized)) CoUninitialize(); }
};

inline std::optional<Image> decode_wic(const fs::path& path, bool& resized) {
    const ComApartment apartment;
    if (FAILED(apartment.initialized) && apartment.initialized != RPC_E_CHANGED_MODE) return {};
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.GetAddressOf())))) return {};
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
        decoder.GetAddressOf()))) return {};
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return {};
    IWICBitmapSource* source = frame.Get();
    ComPtr<IWICBitmapFlipRotator> rotator;
    ComPtr<IWICMetadataQueryReader> metadata;
    if (SUCCEEDED(frame->GetMetadataQueryReader(metadata.GetAddressOf()))) {
        PROPVARIANT value{};
        metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}", &value);
        constexpr WICBitmapTransformOptions orientations[]{WICBitmapTransformRotate0, WICBitmapTransformRotate0,
            WICBitmapTransformFlipHorizontal, WICBitmapTransformRotate180, WICBitmapTransformFlipVertical,
            static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal),
            WICBitmapTransformRotate90,
            static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal),
            WICBitmapTransformRotate270};
        const auto orientation = value.vt == VT_UI2 && value.uiVal < std::size(orientations) ? orientations[value.uiVal] : WICBitmapTransformRotate0;
        PropVariantClear(&value);
        if (orientation != WICBitmapTransformRotate0) {
            if (FAILED(factory->CreateBitmapFlipRotator(rotator.GetAddressOf())) ||
                FAILED(rotator->Initialize(source, orientation))) return {};
            source = rotator.Get();
        }
    }
    UINT width{}, height{};
    if (FAILED(source->GetSize(&width, &height)) || !width || !height ||
        std::uint64_t(width)*height > 100'000'000) return {};
    const auto target = fitted_size(width, height);
    resized = target[0] != width || target[1] != height;
    ComPtr<IWICBitmapScaler> scaler;
    if (resized) {
        if (FAILED(factory->CreateBitmapScaler(scaler.GetAddressOf())) ||
            FAILED(scaler->Initialize(source, target[0], target[1], WICBitmapInterpolationModeFant))) return {};
        source = scaler.Get();
    }
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
            nullptr, 0., WICBitmapPaletteTypeCustom))) return {};
    Image result{target[0],target[1],std::vector<std::uint8_t>(std::size_t(target[0])*target[1]*4)};
    if (FAILED(converter->CopyPixels(nullptr, target[0]*4, static_cast<UINT>(result.straight_bgra.size()),
        result.straight_bgra.data()))) return {};
    return result;
}

inline std::optional<Image> read_image(const fs::path& path, bool& resized) {
    std::error_code error;
    const auto size = fs::file_size(path, error);
    if (error || !size || size > max_file_bytes) return {};
    std::ifstream input(path, std::ios::binary);
    char header[12]{};
    if (input.read(header, sizeof(header)) && !std::memcmp(header, "RIFF", 4) &&
        !std::memcmp(header+8, "WEBP", 4)) {
        input.seekg(0);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) return {};
        if (auto image = decode_webp(bytes.data(), bytes.size(), resized)) return image;
    }
    input.close();
    return decode_wic(path, resized);
}

inline bool write_atomic(const fs::path& path, const std::uint8_t* data, std::size_t length) {
    if (path.empty() || !length || length > UINT32_MAX) return false;
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error) return false;
    auto temporary = path;
    temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetCurrentThreadId()) + L".tmp";
    const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written{};
    const bool flushed = WriteFile(file, data, static_cast<DWORD>(length), &written, nullptr) &&
        written == length && FlushFileBuffers(file);
    CloseHandle(file);
    const bool moved = flushed && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!moved) DeleteFileW(temporary.c_str());
    return moved;
}

inline std::optional<Image> import(const fs::path& source, const fs::path& target) {
    bool resized = false;
    const auto image = read_image(source, resized);
    if (!image) return {};
    std::uint8_t* bytes = nullptr;
    const auto size = resized
        ? WebPEncodeBGRA(image->straight_bgra.data(), image->width, image->height, image->width*4, 80.f, &bytes)
        : WebPEncodeLosslessBGRA(image->straight_bgra.data(), image->width, image->height, image->width*4, &bytes);
    const std::unique_ptr<std::uint8_t, decltype(&WebPFree)> buffer(bytes, WebPFree);
    if (!size || !bytes) return {};
    bool ignored = false;
    auto saved = decode_webp(bytes, size, ignored);
    if (!saved || !write_atomic(target, bytes, size)) return {};
    return saved;
}

class Task {
public:
    ~Task() { wait(); }
    bool busy() const { return worker_.joinable(); }
    bool importing() const { return importing_; }
    void start(HWND frame, fs::path source, fs::path target = {}) {
        if (busy()) return;
        importing_ = !target.empty();
        worker_ = std::thread([this, frame, source = std::move(source), target = std::move(target)] {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            try {
                bool resized = false;
                result_ = importing_ ? import(source, target) : read_image(source, resized);
            } catch (...) { result_.reset(); }
            PostMessageW(frame, ready_message, 0, 0);
        });
    }
    std::optional<Image> take() { wait(); return std::move(result_); }
    void wait() { if (busy()) worker_.join(); }

private:
    std::thread worker_;
    std::optional<Image> result_;
    bool importing_ = false;
};

class Bitmap {
public:
    ~Bitmap() { if (bitmap_) DeleteObject(bitmap_); }
    bool assign(const Image& image, COLORREF background) {
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), static_cast<LONG>(image.width), -static_cast<LONG>(image.height), 1, 32, BI_RGB};
        void* pixels = nullptr;
        const auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap) return false;
        auto* out = static_cast<std::uint8_t*>(pixels);
        const auto* in = image.straight_bgra.data();
        const std::uint8_t backdrop[]{GetBValue(background), GetGValue(background), GetRValue(background)};
        for (std::size_t i=0; i<image.straight_bgra.size(); i+=4) {
            const auto alpha = in[i+3];
            for (int c=0; c<3; ++c) out[i+c] = static_cast<std::uint8_t>((in[i+c]*alpha+backdrop[c]*(255-alpha)+127)/255);
            out[i+3] = 255;
        }
        if (bitmap_) DeleteObject(bitmap_);
        bitmap_ = bitmap;
        width_ = image.width;
        height_ = image.height;
        return true;
    }
    bool available() const { return bitmap_ != nullptr; }
    void paint(HDC dc, int desktop_width, int desktop_height, POINT origin) const {
        if (!bitmap_ || desktop_width <= 0 || desktop_height <= 0) return;
        const auto src = CreateCompatibleDC(dc);
        if (!src) return;
        const auto previous = SelectObject(src, bitmap_);
        const int w = std::clamp(static_cast<int>(double(height_)*desktop_width/desktop_height+.5), 1, width_);
        const int h = std::clamp(static_cast<int>(double(width_)*desktop_height/desktop_width+.5), 1, height_);
        const int saved = SaveDC(dc);
        if (saved) {
            SetStretchBltMode(dc, HALFTONE);
            SetBrushOrgEx(dc, 0, 0, nullptr);
            StretchBlt(dc, -origin.x, -origin.y, desktop_width, desktop_height,
                src, (width_-w)/2, (height_-h)/2, w, h, SRCCOPY);
            RestoreDC(dc, saved);
        }
        SelectObject(src, previous);
        DeleteDC(src);
    }
private:
    HBITMAP bitmap_{};
    int width_{}, height_{};
};
}
