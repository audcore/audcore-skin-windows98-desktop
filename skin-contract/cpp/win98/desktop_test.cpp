#include "desktop.cpp"
#include "../native_skin_loader.hpp"
#include <cassert>

namespace {
struct Host {
    HWND main{};
    audcore_skin_event_fn callback{};
    void* user{};
    std::vector<nlohmann::json> commands;
    int snapshots = 0, subscriptions = 0, unregistered = 0;

    audcore_skin_host abi() {
        return {{sizeof(audcore_skin_host), AUDCORE_SKIN_ABI_MAJOR, AUDCORE_SKIN_ABI_MINOR}, this,
            [](void* p, void* window, const audcore_skin_window_spec*) -> int32_t {
                auto& host = *static_cast<Host*>(p);
                assert(!host.main && IsWindow(static_cast<HWND>(window)));
                host.main = static_cast<HWND>(window);
                return 0;
            },
            [](void* p, void* window) {
                auto& host = *static_cast<Host*>(p);
                assert(host.main == window && IsWindow(host.main));
                host.main = nullptr;
                ++host.unregistered;
            },
            [](void* p, uint64_t, audcore_skin_bytes bytes) -> int32_t {
                static_cast<Host*>(p)->commands.push_back(nlohmann::json::parse(bytes.data, bytes.data+bytes.len));
                return 0;
            },
            [](void* p, audcore_skin_event_fn callback, void* user, uint64_t* token) -> int32_t {
                auto& host = *static_cast<Host*>(p);
                assert(!host.callback);
                host.callback = callback;
                host.user = user;
                ++host.subscriptions;
                *token = 17;
                return 0;
            },
            [](void* p, uint64_t token) {
                assert(token == 17);
                static_cast<Host*>(p)->callback = nullptr;
            },
            [](void* p, uint64_t token) -> int32_t {
                assert(token == 17);
                ++static_cast<Host*>(p)->snapshots;
                return 0;
            }};
    }

    void event(uint32_t kind, uint64_t revision, std::string_view payload) {
        assert(callback);
        const audcore_skin_event event{sizeof(audcore_skin_event), kind, revision, 0, 0,
            {reinterpret_cast<const uint8_t*>(payload.data()), static_cast<uint32_t>(payload.size())}};
        callback(user, &event);
    }
};

bool displayed(HWND window) { return (GetWindowLongPtrW(window, GWL_STYLE) & WS_VISIBLE) != 0; }

void check_art() {
    Desktop d;
    const HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), 64, -24, 1, 32, BI_RGB};
    void* pixels{};
    const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    assert(dc && bitmap && pixels);
    const auto before = SelectObject(dc, bitmap);
    fill(dc, {0,0,64,24}, teal);
    flag(dc, 0, 0, d);
    assert(GetPixel(dc, 10, 5) == RGB(223,43,37));
    assert(GetPixel(dc, 18, 7) == RGB(39,161,60));
    assert(GetPixel(dc, 9, 13) == RGB(39,93,191));
    assert(GetPixel(dc, 16, 15) == RGB(255,197,44));
    assert(GetPixel(dc, 28, 2) == teal);
    title_bar(dc, {32,0,64,24}, true);
    assert(GetPixel(dc, 32, 12) != GetPixel(dc, 63, 12));
    SelectObject(dc, before);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

void check_task_and_append_art(const Desktop& d, const Channel& channel) {
    const auto dc = CreateCompatibleDC(nullptr);
    const int w = d.px(width), h = d.px(44);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
    void* pixels{};
    const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    assert(dc && bitmap && pixels);
    const auto old_bitmap = SelectObject(dc, bitmap);
    DRAWITEMSTRUCT item{};
    item.hDC = dc;
    item.CtlID = channels;
    item.hwndItem = d.list_button;
    item.rcItem = {0,0,d.px(group_width),d.px(30)};
    draw_button(d, item);
    assert(GetPixel(dc, 0, 0) == RGB(255,255,255));
    bool has_left_aligned_text = false;
    for (int y=d.px(5); y<d.px(25); ++y) {
        for (int x=d.px(24); x<d.px(36); ++x)
            if (GetPixel(dc, x, y) == RGB(0,0,0)) has_left_aligned_text = true;
    }
    assert(has_left_aligned_text);
    draw_button(d, item, true);
    assert(GetPixel(dc, 0, 0) == RGB(32,32,32));

    const auto ink = [&](RECT bounds) {
        int count = 0;
        for (int y=bounds.top; y<bounds.bottom; ++y)
            for (int x=bounds.left; x<bounds.right; ++x)
                if (GetPixel(dc, x, y) != RGB(255,255,255)) ++count;
        return count;
    };
    for (const auto control : {channel.add, channel.new_send}) {
        wchar_t label[64]{};
        GetWindowTextW(control, label, static_cast<int>(std::size(label)));
        item.CtlID = GetDlgCtrlID(control);
        item.hwndItem = control;
        GetClientRect(control, &item.rcItem);
        assert(item.rcItem.bottom == d.px(append_row_height));
        draw_button(d, item);
        const auto actual_ink = ink(item.rcItem);
        fill(dc, {0,0,w,h}, RGB(255,255,255));
        const auto font = SelectObject(dc, d.font);
        text(dc, label, {d.px(4),0,item.rcItem.right-d.px(4),h}, navy);
        SelectObject(dc, font);
        assert(actual_ink > 0 && actual_ink == ink({0,0,item.rcItem.right,h}));
    }
    SelectObject(dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}

void check_wallpaper() {
    using namespace wallpaper;
    assert((fitted_size(2000,1000) == std::array<UINT,2>{1600,800}));
    assert((fitted_size(1000,2000) == std::array<UINT,2>{800,1600}));
    assert((fitted_size(600,400) == std::array<UINT,2>{600,400}));
    const auto directory = fs::temp_directory_path() / (L"audcore98-test-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directory(directory);
    const auto bmp = directory / L"large.bmp";
    const auto output = directory / L"desktop.webp";
    constexpr UINT w=2000, h=1000, bytes=w*h*4;
    BITMAPFILEHEADER file{0x4d42, sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER)+bytes, 0,0,
        sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER)};
    BITMAPINFOHEADER info{sizeof(info), w, h, 1, 32, BI_RGB};
    {
        std::ofstream stream(bmp, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(&file), sizeof(file));
        stream.write(reinterpret_cast<const char*>(&info), sizeof(info));
        const std::vector<char> row(w*4, 48);
        for (UINT y=0; y<h; ++y) stream.write(row.data(), row.size());
        assert(stream.good());
    }
    const auto large = import(bmp, output);
    assert(large && large->width == 1600 && large->height == 800);
    bool resized = false;
    auto loaded = read_image(output, resized);
    assert(loaded && !resized && loaded->straight_bgra == large->straight_bgra);
    assert(!import(directory / L"missing.png", output));
    assert(read_image(output, resized)->straight_bgra == large->straight_bgra);

    const auto small_image = directory / L"small.webp";
    const std::uint8_t translucent[]{0,0,255,128};
    std::uint8_t* data = nullptr;
    const auto size = WebPEncodeLosslessBGRA(translucent, 1, 1, 4, &data);
    assert(size && write_atomic(small_image, data, size));
    WebPFree(data);
    loaded = import(small_image, output);
    assert(loaded && loaded->width == 1 && loaded->height == 1 &&
        std::equal(loaded->straight_bgra.begin(), loaded->straight_bgra.end(), std::begin(translucent)));
    Bitmap wallpaper;
    assert(wallpaper.assign(*loaded, teal));
    const auto dc = CreateCompatibleDC(nullptr);
    const auto screen = GetDC(nullptr);
    const auto canvas = CreateCompatibleBitmap(screen, 8, 8);
    ReleaseDC(nullptr, screen);
    const auto old = SelectObject(dc, canvas);
    wallpaper.paint(dc, 8, 8, {});
    assert(GetPixel(dc, 4, 4) == RGB(128,64,64));
    SelectObject(dc, old);
    DeleteObject(canvas);
    DeleteDC(dc);
    fs::remove(bmp);
    fs::remove(small_image);
    fs::remove(output);
    fs::remove(directory);
}

void check_meter(const Channel& channel, bool mono) {
    const auto& d = *channel.desktop;
    const auto dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), d.px(width), -channel.meter.bottom, 1, 32, BI_RGB};
    void* pixels{};
    const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    assert(dc && bitmap && pixels);
    const auto old_bitmap = SelectObject(dc, bitmap);
    const auto font = SelectObject(dc, d.font);
    draw_meter(channel, dc);
    const auto x = channel.meter.left+d.px(19);
    const auto top = channel.meter.top;
    const auto lit = RGB(82,214,70), background = RGB(32,32,32);
    assert(GetPixel(dc, x, top+d.px(5)) == (mono ? background : lit));
    assert(GetPixel(dc, x, top+d.px(12)) == (mono ? lit : background));
    assert(GetPixel(dc, x, top+d.px(19)) == (mono ? background : lit));
    SelectObject(dc, font);
    SelectObject(dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
}
}

int wmain(int argc, wchar_t** argv) {
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    assert(argc == 2);
    check_art();
    check_wallpaper();
    assert(file_version(argv[1]) == wide("v" AUDCORE_98_VERSION_TEXT));
    assert(skin_descriptor.themes == (AUDCORE_SKIN_THEME_LIGHT | AUDCORE_SKIN_THEME_DARK));
    Host host;
    const auto abi = host.abi();
    NativeSkinModule package;
    assert(package.load(argv[1]));
    assert(package.create(abi));
    package.hide();
    package.close();
    assert(!host.main && !host.callback && host.subscriptions == 0);

    const auto d = static_cast<Desktop*>(create(&abi));
    assert(d && create_windows(*d));
    d->host_version = L"v0.0.3210";
    assert(host.main == d->frame && !IsWindowVisible(d->frame));
    assert((GetWindowLongPtrW(d->frame, GWL_STYLE) & WS_CAPTION) == WS_CAPTION);
    assert(d->frame_icon && reinterpret_cast<HICON>(SendMessageW(d->frame, WM_GETICON, ICON_SMALL, 0)) == d->frame_icon);
    RECT frame_client{}, frame_outer{};
    GetClientRect(d->frame, &frame_client);
    GetWindowRect(d->frame, &frame_outer);
    assert(frame_client.right < frame_outer.right-frame_outer.left && frame_client.bottom < frame_outer.bottom-frame_outer.top);
    const auto desktop_top = anchor(d->mdi);
    POINT origin_frame{0, 0};
    ClientToScreen(d->frame, &origin_frame);
    RECT desktop_client{}, list_button{}, start_button{}, effects_button{}, tray_icon_rect{};
    GetClientRect(d->mdi, &desktop_client);
    GetWindowRect(d->list_button, &list_button);
    GetWindowRect(d->start_button, &start_button);
    GetWindowRect(d->effects_button, &effects_button);
    GetWindowRect(d->sound, &tray_icon_rect);
    assert(desktop_top.y == origin_frame.y+desktop_client.bottom);
    assert(desktop_client.bottom == frame_client.bottom-d->px(taskbar_height));
    assert(start_button.left == origin_frame.x+d->px(4) && start_button.right < list_button.left && list_button.right <= tray_icon_rect.left);
    assert(list_button.right < effects_button.left && effects_button.right < tray_icon_rect.left);
    assert(list_button.top >= desktop_top.y && start_button.top >= desktop_top.y && tray_icon_rect.top >= desktop_top.y);
    assert(start_button.right-start_button.left == d->px(36));
    auto tray = tray_bounds(*d, frame_client);
    InflateRect(&tray, -bevel_thickness, -bevel_thickness);
    OffsetRect(&tray, origin_frame.x, origin_frame.y);
    for (const auto control : {d->sound, d->performance, d->clock}) {
        RECT box{}, visible{};
        GetWindowRect(control, &box);
        assert(IntersectRect(&visible, &tray, &box) && EqualRect(&visible, &box));
    }
    RECT clock_box{};
    GetWindowRect(d->clock, &clock_box);
    assert(clock_box.bottom-clock_box.top >= d->px(30));
    wchar_t clock[128]{};
    GetWindowTextW(d->clock, clock, static_cast<int>(std::size(clock)));
    assert(clock[0] && wcschr(clock, L'\n') && wcschr(clock, L'\n')[1]);
    for (const auto shortcut : d->shortcuts) {
        RECT box{};
        GetWindowRect(shortcut, &box);
        assert(GetParent(shortcut) == d->shortcut_panel && box.top >= origin_frame.y && box.bottom < desktop_top.y);
    }
    assert(d->subscription.start(abi, on_event, d));
    d->shown = true; // Feed the actual callback without exposing a desktop window.
    assert(desktop_background_at(*d, {frame_client.right-10, frame_client.bottom-d->px(taskbar_height+10)}));
    assert(!desktop_background_at(*d, {frame_client.right-10, frame_client.bottom-10}));

    host.event(AUDCORE_SKIN_EVENT_SNAPSHOT, 1, R"({"channels":[
        {"id":1,"name":"Voice","chain_uuid":"voice","displayColor":"#123456",
         "rack":[{"instance":10,"path":"fx.vst3","effect_uuid":"fx"},{"instance":11,"path":"fx11.vst3"},
                 {"instance":12,"path":"fx12.vst3"},{"instance":13,"path":"fx13.vst3"},
                 {"instance":14,"path":"fx14.vst3"},{"instance":15,"path":"fx15.vst3"}],
         "sends":[{"dest":{"kind":"chain","id":2},"level_db":-60.0}]},
        {"id":2,"name":"PLAYBACK 5/6 Long Output Name","chain_uuid":"music","channel_mode":2},
        {"id":3,"name":"Hidden","hidden":true}]})");
    assert(d->ready && d->order.size() == 2 && ListBox_GetCount(d->list) == 2 && !displayed(d->list));
    RECT group{};
    GetWindowRect(d->list, &group);
    assert(group.left == list_button.left && group.bottom == desktop_top.y && group.bottom-group.top == 2*d->px(23)+2);
    auto& voice = *d->windows.at(1);
    auto& music = *d->windows.at(2);
    check_task_and_append_art(*d, voice);
    assert(voice.window != music.window && GetParent(voice.window) == d->mdi && GetParent(music.window) == d->mdi);
    assert((GetWindowLongPtrW(voice.window, GWL_EXSTYLE) & WS_EX_MDICHILD) != 0);
    assert(voice.title == L"Voice" && voice.color == RGB(0x12,0x34,0x56));
    const auto versions = wide("v" AUDCORE_98_VERSION_TEXT) + L"-声核 v0.0.3210";
    const std::pair<HWND, std::wstring> titles[]{{d->frame, L"Audcore 98 - 远程桌面 " + versions},
        {voice.window, voice.title}, {music.window, music.title}, {d->effects_button, L"效果器"}, {d->shortcuts[5], L"效果器"}};
    for (const auto& [titled, expected] : titles) {
        wchar_t title[128]{};
        GetWindowTextW(titled, title, static_cast<int>(std::size(title)));
        assert(title == expected);
    }
    assert(voice.effect_rows.size() == 6 && ListBox_GetCount(voice.effects) == 6);
    assert(voice.send_rows.size() == 1 && ListBox_GetCount(voice.send_list) == 1);
    assert(voice.send_rows[0].second.name == L"PLAYBACK 5/6 Long Output Name");
    assert(voice.send_rows[0].second.value == L"-60.0 dB");
    assert(full_row_text(voice.send_list, 0) == L"PLAYBACK 5/6 Long Output Name  -60.0 dB");
    const auto value_dc = CreateCompatibleDC(nullptr);
    const auto old_font = SelectObject(value_dc, d->font);
    RECT row{};
    GetClientRect(voice.send_list, &row);
    InflateRect(&row, -d->px(3), 0);
    const auto value = send_value_rect(value_dc, row);
    SIZE minimum{}, maximum{}, name{};
    GetTextExtentPoint32W(value_dc, L"-60.0 dB", 8, &minimum);
    GetTextExtentPoint32W(value_dc, L"+6.0 dB", 7, &maximum);
    GetTextExtentPoint32W(value_dc, voice.send_rows[0].second.name.c_str(),
        static_cast<int>(voice.send_rows[0].second.name.size()), &name);
    assert(value.right-value.left == std::max(minimum.cx, maximum.cx));
    assert(value.left > row.left && name.cx > value.left-row.left);
    SelectObject(value_dc, old_font);
    DeleteDC(value_dc);
    RECT fx_add{}, send_add{}, music_send{};
    GetWindowRect(voice.add, &fx_add);
    GetWindowRect(voice.new_send, &send_add);
    GetWindowRect(music.new_send, &music_send);
    POINT fx{fx_add.left,fx_add.bottom}, empty{music_send.left,music_send.top};
    ScreenToClient(voice.window, &fx);
    ScreenToClient(music.window, &empty);
    assert(fx.y == d->px(effect_pane.bottom)-1);
    assert(empty.y == d->px(send_pane.top)+1 && !displayed(music.send_list));
    assert((GetWindowLongPtrW(voice.effects, GWL_STYLE) & WS_VSCROLL) != 0);
    assert(ListBox_SetTopIndex(voice.effects, 4) != LB_ERR);
    RECT scrolled{};
    GetWindowRect(voice.add, &scrolled);
    assert(EqualRect(&fx_add, &scrolled));
    RECT voice_rect{};
    GetWindowRect(voice.window, &voice_rect);
    assert(voice_rect.right-voice_rect.left == d->px(150));
    const auto region = CreateRectRgn(0,0,0,0);
    assert(region && GetWindowRgn(voice.window, region) == SIMPLEREGION);
    RECT square{};
    assert(GetRgnBox(region, &square) == SIMPLEREGION && square.left == 0 && square.top == 0 && square.right == d->px(150) && square.bottom == d->px(height));
    DeleteObject(region);

    const auto voice_window = voice.window;
    const auto music_window = music.window;
    SetWindowPos(voice_window, nullptr, -30, -10, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
    clamp_channel(voice);
    RECT bounds{};
    GetWindowRect(voice_window, &bounds);
    POINT origin{bounds.left, bounds.top};
    ScreenToClient(d->mdi, &origin);
    assert(origin.x == 0 && origin.y == 0);
    hide_channel(voice, false);
    assert(!displayed(voice_window) && !voice.closed && ListBox_GetCount(d->list) == 2);
    open_channel(voice);
    assert(displayed(voice_window));
    hide_channel(voice, true);
    assert(voice.closed && !displayed(voice_window) && ListBox_GetCount(d->list) == 2);
    assert(host.commands.empty());
    frame_proc_impl(d->frame, WM_COMMAND, MAKEWPARAM(channels, BN_CLICKED), reinterpret_cast<LPARAM>(d->list_button));
    assert(displayed(d->list));
    ListBox_SetCurSel(d->list, 0);
    frame_proc_impl(d->frame, WM_COMMAND, MAKEWPARAM(channel_list, LBN_DBLCLK), reinterpret_cast<LPARAM>(d->list));
    assert(displayed(voice_window) && !voice.closed && !displayed(d->list));

    channel_command(voice, mute, {});
    channel_command(voice, raise, {});
    channel_command(voice, menu, {-20,35});
    ListBox_SetCurSel(voice.effects, 0);
    channel_command(voice, editor, {});
    assert(host.commands.size() == 4);
    assert(host.commands[0]["method"] == "channel.setMute");
    assert(host.commands[0]["params"]["expectedChannel"]["chain_uuid"] == "voice");
    assert(host.commands[1]["method"] == "channel.setGain" && host.commands[1]["params"]["db"] == 1.0);
    assert(host.commands[2]["params"]["anchor"]["x"] == -20);
    assert(host.commands[3]["method"] == "effect.openEditor" && host.commands[3]["params"]["instance"] == 10);
    channel_command(voice, add_effect, {});
    channel_command(voice, add_send, {10,20});
    ListBox_SetCurSel(voice.send_list, 0);
    channel_command(voice, sends, {12,35});
    assert(host.commands[4]["method"] == "effect.openBrowser");
    assert(host.commands[5]["method"] == "menu.openAddSend");
    assert(host.commands[6]["method"] == "menu.openSend" && host.commands[6]["params"]["index"] == 0);
    host.commands.resize(4);

    host.event(AUDCORE_SKIN_EVENT_TRANSIENT, 1, R"({"type":"meters","meters":[{"id":1,"peak":0.5,"peak_l":0.5,"peak_r":0.125},{"id":2,"peak":0.5,"peak_l":0,"peak_r":0.5}]})");
    assert(d->state.channels()[voice.index].peak_r == 0.125);
    check_meter(voice, false);
    check_meter(music, true);
    host.event(AUDCORE_SKIN_EVENT_DELTA, 2, R"({"type":"channel","id":1,"field":"channel_mode","value":1})");
    check_meter(voice, true);
    host.event(AUDCORE_SKIN_EVENT_DELTA, 3, R"({"type":"order","ids":[2,3,1]})");
    assert(d->order.front()->window == music_window && d->order.back()->window == voice_window);

    host.event(AUDCORE_SKIN_EVENT_DELTA, 5, R"({"type":"order","ids":[3,1,2]})");
    assert(!fresh(*d) && host.snapshots == 1);
    channel_command(voice, mute, {});
    assert(host.commands.size() == 4 && host.snapshots == 1);
    host.event(AUDCORE_SKIN_EVENT_SNAPSHOT, 4, R"({"channels":[{"id":1,"name":"Voice","chain_uuid":"voice"},{"id":2,"name":"Music","chain_uuid":"music"}]})");
    assert(fresh(*d) && d->windows.at(1)->window == voice_window);
    hide_channel(voice, true);
    host.event(AUDCORE_SKIN_EVENT_DELTA, 5, R"({"type":"structure","channels":[{"id":1,"name":"Replacement","chain_uuid":"new"},{"id":5,"name":"New"}]})");
    assert(!IsWindow(voice_window) && !IsWindow(music_window));
    assert(d->order.size() == 2 && d->windows.at(1)->title == L"Replacement" && !d->windows.at(1)->closed);
    assert(host.commands.size() == 4);

    for (const auto [control, method] : {std::pair{d->sound, "settings.open"}, {d->performance, "performance.open"},
        {d->shortcuts[0], "menu.openAddChannel"}, {d->shortcuts[1], "performance.open"}, {d->shortcuts[2], "document.open"},
        {d->shortcuts[3], "document.save"}, {d->shortcuts[4], "settings.open"}}) {
        SendMessageW(GetParent(control), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(control), BN_CLICKED), reinterpret_cast<LPARAM>(control));
        assert(host.commands.back()["method"] == method);
    }
    open_channel(*d->windows.at(5));
    for (const auto control : {d->effects_button, d->shortcuts[5]}) {
        SendMessageW(GetParent(control), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(control), BN_CLICKED), reinterpret_cast<LPARAM>(control));
        assert(host.commands.back()["method"] == "effect.openBrowser" && host.commands.back()["params"]["id"] == 5);
    }
    const auto before_start = host.commands.size();
    SendMessageW(d->frame, WM_COMMAND, MAKEWPARAM(start, BN_CLICKED), reinterpret_cast<LPARAM>(d->start_button));
    assert(displayed(d->start_menu) && ListBox_GetCount(d->start_menu) == std::size(start_entries) && host.commands.size() == before_start);
    assert(full_row_text(d->start_menu, 0) == L"效果器");
    ListBox_SetCurSel(d->start_menu, static_cast<int>(std::size(start_entries)-1));
    SendMessageW(d->frame, WM_COMMAND, MAKEWPARAM(start_list, LBN_DBLCLK), reinterpret_cast<LPARAM>(d->start_menu));
    assert(!displayed(d->start_menu) && host.commands.back()["method"] == "menu.openSystem");
    assert(host.commands.back()["params"]["anchor"]["y"] == start_button.top);

    const auto frame = d->frame;
    const auto replacement = d->windows.at(1)->window;
    hide(d);
    assert(!d->shown && !d->ready && host.callback && !IsWindowVisible(frame));
    d->shown = true;
    host.event(AUDCORE_SKIN_EVENT_SNAPSHOT, 5, R"({"channels":[{"id":1,"name":"Replacement","chain_uuid":"new"}]})");
    assert(fresh(*d) && d->windows.at(1)->window == replacement);
    destroy(d);
    assert(!host.main && !host.callback && host.unregistered == 1);
    assert(!IsWindow(frame) && !IsWindow(replacement));
    puts("Audcore 98 native package, hidden MDI lifecycle and host commands passed");
}
