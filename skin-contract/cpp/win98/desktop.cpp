#define UNICODE
#define _UNICODE
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>

#include "../reference_gain_pending.hpp"
#include "../reference_recovery.hpp"
#include "../reference_subscription.hpp"
#include "../reference_toggle_pending.hpp"
#include "../skin_protocol_limits.hpp"
#include "version.h"
#include "wallpaper.hpp"

#include <array>
#include <memory>

namespace {
constexpr COLORREF teal = RGB(0, 128, 128);
constexpr COLORREF gray = RGB(192, 192, 192);
constexpr COLORREF navy = RGB(0, 0, 128);
constexpr int width = 150, height = 286;
constexpr int taskbar_height = 42, tray_width = 168;
constexpr int bevel_thickness = 2;
constexpr int shortcut_width = 94, group_left = 44, group_width = 112;
constexpr int effects_left = group_left+group_width+4, effects_width = 112;
constexpr int append_row_height = 20;
constexpr UINT_PTR clock_timer = 1;
enum Control { start = 1, channels, channel_list, minimize, close_child, menu, mute, lower, raise, rack, editor, add_effect, sends, add_send,
    tray_sound, tray_performance, tray_clock, my_computer, task_manager, project_open, project_save, desktop_settings, desktop_fx,
    task_fx, start_list };
constexpr Control shortcut_ids[]{my_computer, task_manager, project_open, project_save, desktop_settings, desktop_fx};

struct Desktop;
struct ListRow {
    std::wstring name, value;
    bool operator==(const ListRow& other) const { return name == other.name && value == other.value; }
};
struct Channel {
    Desktop* desktop;
    nlohmann::json identity;
    std::size_t index;
    HWND window{}, title_min{}, title_close{}, menu_button{}, mute_box{}, minus{}, plus{}, effects{}, add{}, send_list{}, new_send{};
    std::wstring title;
    std::vector<std::pair<std::uint64_t, ListRow>> effect_rows;
    std::vector<std::pair<nlohmann::json, ListRow>> send_rows;
    RECT meter{};
    COLORREF color = teal;
    bool closed = false;
};

struct Desktop {
    audcore_skin_host host{};
    ReferenceSkinState state;
    ReferenceSubscription subscription;
    ReferenceRecovery recovery;
    ReferenceGainPending gains;
    ReferenceTogglePending toggles;
    std::unordered_map<std::uint32_t, std::unique_ptr<Channel>> windows;
    std::vector<Channel*> order;
    HMODULE module{};
    HWND frame{}, mdi{}, start_button{}, list_button{}, list{}, start_menu{}, effects_button{};
    HWND sound{}, performance{}, clock{}, shortcut_panel{};
    std::array<HWND, std::size(shortcut_ids)> shortcuts{};
    HICON frame_icon{};
    HFONT font{};
    std::wstring host_version = L"—";
    wallpaper::Bitmap wallpaper;
    wallpaper::Task wallpaper_task;
    wallpaper::fs::path wallpaper_file;
    HWND row_tip{}, tip_list{};
    int tip_row = LB_ERR;
    std::wstring tip_text;
    wchar_t frame_class[72]{}, channel_class[72]{};
    std::uint64_t revision = 0, request = 0;
    std::wstring status;
    bool ready = false, shown = false, registered = false;
    bool frame_class_registered = false, channel_class_registered = false;
    UINT dpi = 96;
    int px(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
};

std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const auto size = static_cast<int>(text.size());
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0);
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), size, result.data(), length);
    return result;
}

const wchar_t* label(const Desktop& d, const wchar_t* zh, const wchar_t* en, const wchar_t* ja) {
    const auto& text = d.state.labels().mute;
    return text == "静音" ? zh : text == "消音" || text == "ミュート" ? ja : en;
}

std::wstring file_version(const wchar_t* path) {
    const auto length = GetFileVersionInfoSizeW(path, nullptr);
    if (!length) return L"—";
    std::vector<std::uint8_t> resource(length);
    if (!GetFileVersionInfoW(path, 0, length, resource.data())) return L"—";
    void* value{};
    UINT size{};
    if (!VerQueryValueW(resource.data(), L"\\", &value, &size) || size < sizeof(VS_FIXEDFILEINFO)) return L"—";
    const auto& info = *static_cast<const VS_FIXEDFILEINFO*>(value);
    if (info.dwSignature != 0xfeef04bd) return L"—";
    return L"v" + std::to_wstring(HIWORD(info.dwFileVersionMS)) + L"." +
        std::to_wstring(LOWORD(info.dwFileVersionMS)) + L"." + std::to_wstring(HIWORD(info.dwFileVersionLS));
}

void fill(HDC dc, const RECT& rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void square_corners(HWND hwnd) {
    const DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
}

void bevel(HDC dc, RECT r, bool sunken) {
    for (int i = 0; i < bevel_thickness; ++i) {
        const auto light = i == 0 ? RGB(255,255,255) : RGB(223,223,223);
        const auto dark = i == 0 ? RGB(32,32,32) : RGB(128,128,128);
        fill(dc, {r.left,r.top,r.right,r.top+1}, sunken ? dark : light);
        fill(dc, {r.left,r.top,r.left+1,r.bottom}, sunken ? dark : light);
        fill(dc, {r.left,r.bottom-1,r.right,r.bottom}, sunken ? light : dark);
        fill(dc, {r.right-1,r.top,r.right,r.bottom}, sunken ? light : dark);
        InflateRect(&r, -1, -1);
    }
}

void title_bar(HDC dc, const RECT& area, bool active) {
    if (!active) {
        fill(dc, area, RGB(96, 96, 96));
        return;
    }
    TRIVERTEX colors[]{{area.left, area.top, 0, 0, 128 << 8, 0},
        {area.right, area.bottom, 16 << 8, 104 << 8, 183 << 8, 0}};
    GRADIENT_RECT span{0, 1};
    if (!GradientFill(dc, colors, 2, &span, 1, GRADIENT_FILL_RECT_H)) fill(dc, area, navy);
}

void text(HDC dc, const wchar_t* value, RECT rect, COLORREF color, UINT flags = 0) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, value, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS | flags);
}

// Original GDI drawing of a four-pane flag. All coordinates are in a 24x20 grid.
void flag(HDC dc, int x, int y, const Desktop& d) {
    const auto pen = SelectObject(dc, GetStockObject(NULL_PEN));
    const auto brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    const auto tile = [&](COLORREF color, std::initializer_list<POINT> outline) {
        POINT points[10]{};
        std::transform(outline.begin(), outline.end(), points,
            [&](POINT p) { return POINT{x+d.px(p.x), y+d.px(p.y)}; });
        SetDCBrushColor(dc, color);
        Polygon(dc, points, static_cast<int>(outline.size()));
    };
    const auto black = RGB(24,24,24);
    const auto red = RGB(223,43,37), green = RGB(39,161,60), blue = RGB(39,93,191), yellow = RGB(255,197,44);
    tile(black, {{9,2},{13,1},{19,3},{24,2},{20,18},{16,19},{10,17},{5,18}});
    tile(red, {{10,3},{13,2},{16,3},{14,9},{11,8},{8,9}});
    tile(green, {{17,4},{19,4},{23,3},{21,9},{18,10},{15,9}});
    tile(blue, {{8,10},{11,9},{14,10},{12,16},{10,15},{6,17}});
    tile(yellow, {{15,11},{18,11},{21,10},{19,17},{16,18},{13,16}});
    const auto pixel = [&](int l, int t, COLORREF color) {
        fill(dc, {x+d.px(l),y+d.px(t),x+d.px(l+2),y+d.px(t+2)}, color);
    };
    pixel(6,3,black); pixel(3,4,red); pixel(5,6,black);
    pixel(1,7,black); pixel(4,9,red); pixel(6,10,black);
    pixel(2,11,blue); pixel(0,14,black); pixel(4,14,blue); pixel(2,16,black);
    SelectObject(dc, brush);
    SelectObject(dc, pen);
}

enum class Art { folder, computer, monitor, disk, settings, remote, effects };

struct StartEntry { Control action; Art art; const wchar_t *zh, *en, *ja; };
constexpr StartEntry start_entries[]{
    {desktop_fx, Art::effects, L"效果器", L"Effects", L"効果"},
    {my_computer, Art::computer, L"新建通道", L"Add Channel", L"通路追加"},
    {project_open, Art::folder, L"打开工程", L"Open Project", L"工程を開く"},
    {project_save, Art::disk, L"保存工程", L"Save Project", L"工程を保存"},
    {task_manager, Art::monitor, L"任务管理器", L"Task Manager", L"タスク管理"},
    {desktop_settings, Art::settings, L"设置", L"Settings", L"設定"},
    {start, Art::remote, L"工程菜单", L"Project Menu", L"工程メニュー"},
};

void draw_art(HDC dc, RECT target, Art art) {
    const LONG w = target.right-target.left, h = target.bottom-target.top;
    const auto block = [&](int l, int t, int r, int b, COLORREF color) {
        fill(dc, {target.left+MulDiv(l,w,16),target.top+MulDiv(t,h,16),
            target.left+MulDiv(r,w,16),target.top+MulDiv(b,h,16)}, color);
    };
    const auto ink = RGB(34,34,34), light = RGB(246,246,246), shade = RGB(128,128,128);
    const auto monitor = [&](int x, int y, COLORREF screen) {
        block(x,y,x+12,y+9,ink);
        block(x+1,y+1,x+11,y+8,gray);
        block(x+2,y+2,x+10,y+7,screen);
        block(x+4,y+9,x+8,y+11,shade);
        block(x+2,y+11,x+10,y+12,ink);
    };
    switch (art) {
    case Art::folder:
        block(1,3,7,6,ink); block(1,5,14,14,ink);
        block(2,4,6,6,RGB(255,227,122)); block(2,6,13,13,RGB(220,155,35));
        block(3,7,15,13,ink); block(4,8,14,12,RGB(255,214,87)); block(4,8,14,9,light);
        break;
    case Art::computer:
        monitor(1,1,RGB(37,109,166));
        block(4,13,15,15,ink); block(5,13,14,14,gray); block(11,13,12,14,RGB(25,156,60));
        break;
    case Art::monitor:
        monitor(2,2,RGB(20,70,49));
        block(5,7,11,8,RGB(61,237,116)); block(8,5,9,8,RGB(61,237,116));
        block(9,5,10,6,RGB(61,237,116)); block(10,6,11,7,RGB(61,237,116));
        break;
    case Art::disk:
        block(2,1,14,15,ink); block(3,2,13,14,RGB(50,99,168));
        block(5,2,11,6,light); block(9,2,10,5,shade);
        block(4,8,12,14,light); block(5,9,11,10,shade); block(5,11,10,12,shade);
        break;
    case Art::settings:
        block(6,1,10,15,ink); block(1,6,15,10,ink); block(3,3,13,13,ink);
        block(4,4,12,12,gray); block(6,2,10,4,light); block(2,6,4,9,light);
        block(6,6,10,10,ink); block(7,7,9,9,teal);
        break;
    case Art::remote:
        monitor(0,0,RGB(31,105,170));
        block(7,7,16,14,ink); block(8,8,15,13,light);
        block(9,9,14,12,RGB(47,126,196)); block(10,14,13,15,shade);
        block(4,8,9,10,RGB(80,224,95)); block(7,7,8,11,RGB(80,224,95));
        break;
    case Art::effects:
        block(1,2,15,14,ink); block(2,3,14,13,gray); block(2,3,14,4,light);
        block(4,5,5,12,navy); block(8,5,9,12,navy); block(12,5,13,12,navy);
        block(3,6,6,8,RGB(43,110,182)); block(7,9,10,11,RGB(216,145,25)); block(11,5,14,7,RGB(25,156,60));
        break;
    }
}

HICON create_remote_icon() {
    constexpr int side = 32;
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), side, -side, 1, 32, BI_RGB};
    void* pixels{};
    const auto dc = CreateCompatibleDC(nullptr);
    const auto color = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    const auto mask = CreateBitmap(side, side, 1, 1, nullptr);
    HICON icon{};
    if (dc && color && mask && pixels) {
        const auto old = SelectObject(dc, color);
        fill(dc, {0,0,side,side}, RGB(0,0,0));
        draw_art(dc, {0,0,side,side}, Art::remote);
        GdiFlush();
        auto* pixel = static_cast<std::uint32_t*>(pixels);
        for (int i = 0; i < side*side; ++i) if (pixel[i] & 0x00ffffff) pixel[i] |= 0xff000000;
        SelectObject(dc, mask);
        PatBlt(dc, 0, 0, side, side, BLACKNESS);
        SelectObject(dc, old);
        ICONINFO icon_info{TRUE, 0, 0, mask, color};
        icon = CreateIconIndirect(&icon_info);
    }
    if (mask) DeleteObject(mask);
    if (color) DeleteObject(color);
    if (dc) DeleteDC(dc);
    return icon;
}

void tray_icon(HDC dc, const Desktop& d, RECT rect, bool speaker) {
    const int x = (rect.left + rect.right - d.px(14)) / 2;
    const int y = (rect.top + rect.bottom - d.px(14)) / 2;
    const auto block = [&](int l, int t, int r, int b, COLORREF color) {
        fill(dc, {x+d.px(l), y+d.px(t), x+d.px(r), y+d.px(b)}, color);
    };
    const auto ink = RGB(28,28,28);
    if (speaker) {
        block(0,5,4,10,ink);
        block(1,6,4,9,RGB(255,227,105));
        block(4,4,6,11,ink);
        block(6,2,8,13,ink);
        block(5,5,7,10,RGB(255,227,105));
        block(9,5,10,10,navy);
        block(11,3,12,5,navy);
        block(12,5,13,10,navy);
        block(11,10,12,12,navy);
    } else {
        block(0,1,14,11,ink);
        block(1,2,13,10,navy);
        block(3,6,5,9,RGB(76,231,115));
        block(6,4,8,9,RGB(76,231,115));
        block(9,5,11,9,RGB(255,227,105));
        block(6,11,8,13,ink);
        block(3,13,11,14,ink);
    }
}

void window_icon(HDC dc, const Desktop& d, RECT rect, UINT id) {
    const int x = (rect.left + rect.right - d.px(9)) / 2;
    const int y = (rect.top + rect.bottom - d.px(9)) / 2;
    const auto ink = RGB(24,24,24);
    const auto mark = [&](int l, int t, int r, int b) {
        fill(dc, {x+d.px(l),y+d.px(t),x+d.px(r),y+d.px(b)}, ink);
    };
    if (id == minimize) {
        mark(1,6,8,8);
    } else {
        for (int i = 1; i <= 7; ++i) {
            mark(i,i,i+1,i+1);
            mark(i,8-i,i+1,9-i);
        }
    }
}

void draw_button(const Desktop& d, const DRAWITEMSTRUCT& item, bool active = false) {
    auto rect = item.rcItem;
    const auto dc = item.hDC;
    const bool pressed = active || (item.itemState & ODS_SELECTED);
    const bool tray_button = item.CtlID == tray_sound || item.CtlID == tray_performance;
    const bool clock = item.CtlID == tray_clock;
    const bool append = item.CtlID == add_effect || item.CtlID == add_send;
    const bool flag_button = item.CtlID == start;
    const bool task_item = item.CtlID == channels || item.CtlID == task_fx;
    const auto background = (active || (flag_button && pressed)) ? RGB(223,223,223) : gray;
    fill(dc, rect, append ? (pressed ? navy : RGB(255,255,255)) : background);
    if (!flag_button && !append && !clock && (!tray_button || pressed)) bevel(dc, rect, pressed);
    InflateRect(&rect, -d.px(4), (append || clock) ? 0 : -d.px(3));
    if (!append && (item.itemState & ODS_SELECTED)) OffsetRect(&rect, 1, 1);
    const auto font = SelectObject(dc, d.font);
    if (flag_button) {
        flag(dc, (rect.left+rect.right-d.px(24))/2, (rect.top+rect.bottom-d.px(20))/2, d);
    } else if (task_item) {
        const auto left = rect.left+d.px(2), top = (rect.top+rect.bottom-d.px(16))/2;
        draw_art(dc, {left,top,left+d.px(16),top+d.px(16)}, item.CtlID == channels ? Art::folder : Art::effects);
        rect.left += d.px(20);
    }
    wchar_t title[192]{};
    GetWindowTextW(item.hwndItem, title, static_cast<int>(std::size(title)));
    if (tray_button) tray_icon(dc, d, rect, item.CtlID == tray_sound);
    else if (item.CtlID == minimize || item.CtlID == close_child) window_icon(dc, d, rect, item.CtlID);
    else if (append) text(dc, title, rect, pressed ? RGB(255,255,255) : navy);
    else if (clock) {
        if (auto* date = wcschr(title, L'\n')) {
            *date++ = L'\0';
            const auto middle = (rect.top+rect.bottom)/2;
            text(dc, title, {rect.left,rect.top,rect.right,middle}, RGB(0,0,0), DT_CENTER);
            text(dc, date, {rect.left,middle,rect.right,rect.bottom}, RGB(0,0,0), DT_CENTER);
        } else text(dc, title, rect, RGB(0,0,0), DT_CENTER);
    }
    else if (!flag_button) text(dc, title, rect, item.itemState & ODS_DISABLED ? RGB(105,105,105) : RGB(0,0,0), task_item ? DT_LEFT : DT_CENTER);
    if (item.itemState & ODS_FOCUS) DrawFocusRect(dc, &rect);
    SelectObject(dc, font);
}

void paint_wallpaper(const Desktop& d, HWND surface, HDC dc, const RECT& rect) {
    fill(dc, rect, teal);
    if (!d.wallpaper.available()) return;
    RECT client{};
    GetClientRect(d.frame, &client);
    POINT origin{};
    MapWindowPoints(surface, d.frame, &origin, 1);
    d.wallpaper.paint(dc, client.right, client.bottom-d.px(taskbar_height), origin);
}

void draw_shortcut(const Desktop& d, const DRAWITEMSTRUCT& item, Art art) {
    const auto dc = item.hDC;
    const auto r = item.rcItem;
    if (item.itemState & ODS_SELECTED) fill(dc, r, navy);
    else paint_wallpaper(d, item.hwndItem, dc, r);
    const auto x = (r.left+r.right-d.px(26))/2, y = r.top+d.px(2);
    draw_art(dc, {x,y,x+d.px(26),y+d.px(26)}, art);
    wchar_t name[96]{};
    GetWindowTextW(item.hwndItem, name, static_cast<int>(std::size(name)));
    const auto font = SelectObject(dc, d.font);
    if (d.wallpaper.available() && !(item.itemState & ODS_SELECTED)) {
        const RECT shadow{r.left+1,r.top+d.px(29)+1,r.right+1,r.bottom+1};
        text(dc, name, shadow, RGB(0,0,0), DT_CENTER);
    }
    text(dc, name, {r.left,r.top+d.px(29),r.right,r.bottom}, RGB(255,255,255), DT_CENTER);
    SelectObject(dc, font);
    if (item.itemState & ODS_FOCUS) DrawFocusRect(dc, &r);
}

RECT send_value_rect(HDC dc, RECT row) {
    SIZE minimum{}, maximum{};
    GetTextExtentPoint32W(dc, L"-60.0 dB", 8, &minimum);
    GetTextExtentPoint32W(dc, L"+6.0 dB", 7, &maximum);
    return {row.right-std::max(minimum.cx, maximum.cx), row.top, row.right, row.bottom};
}

void draw_channel_row(const Channel& c, const DRAWITEMSTRUCT& item) {
    const auto& d = *c.desktop;
    const auto dc = item.hDC;
    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    fill(dc, item.rcItem, selected ? navy : RGB(255,255,255));
    const ListRow* row = nullptr;
    const bool is_send = item.CtlID == sends;
    if (is_send && item.itemID < c.send_rows.size()) row = &c.send_rows[item.itemID].second;
    if (!is_send && item.itemID < c.effect_rows.size()) row = &c.effect_rows[item.itemID].second;
    if (!row) return;
    const auto font = SelectObject(dc, d.font);
    const auto color = selected ? RGB(255,255,255) : RGB(0,0,0);
    auto name = item.rcItem;
    InflateRect(&name, -d.px(3), 0);
    if (is_send) {
        const auto value = send_value_rect(dc, name);
        name.right = value.left-d.px(4);
        text(dc, row->value.c_str(), value, color, DT_RIGHT);
    }
    text(dc, row->name.c_str(), name, color);
    SelectObject(dc, font);
    if (item.itemState & ODS_FOCUS) DrawFocusRect(dc, &item.rcItem);
}

TOOLINFOW row_tip_tool(const Desktop& d) {
    TOOLINFOW tool{TTTOOLINFOW_V1_SIZE};
    tool.uFlags = TTF_TRACK;
    tool.hwnd = d.frame;
    tool.uId = 1;
    tool.lpszText = LPSTR_TEXTCALLBACKW;
    return tool;
}

void hide_row_tip(Desktop& d, HWND list = nullptr) noexcept {
    if (!d.tip_list || (list && d.tip_list != list)) return;
    const auto tool = row_tip_tool(d);
    SendMessageW(d.row_tip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
    d.tip_list = nullptr;
    d.tip_row = LB_ERR;
}

int row_under_pointer(HWND list, LPARAM position) {
    const auto hit = SendMessageW(list, LB_ITEMFROMPOINT, 0, position);
    const int row = LOWORD(hit);
    RECT bounds{};
    if (HIWORD(hit) || ListBox_GetItemRect(list, row, &bounds) == LB_ERR ||
        !PtInRect(&bounds, {GET_X_LPARAM(position), GET_Y_LPARAM(position)})) return LB_ERR;
    return row;
}

std::wstring full_row_text(HWND list, int row) {
    const auto length = ListBox_GetTextLen(list, row);
    if (length <= 0) return {};
    std::wstring value(static_cast<std::size_t>(length)+1, L'\0');
    const auto copied = ListBox_GetText(list, row, value.data());
    if (copied == LB_ERR) return {};
    value.resize(copied);
    return value;
}

LRESULT CALLBACK channel_list_tip(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) noexcept {
    auto& d = *reinterpret_cast<Desktop*>(ref);
    switch (msg) {
    case WM_MOUSEMOVE:
        if (d.tip_list != hwnd || d.tip_row != row_under_pointer(hwnd, lp)) {
            hide_row_tip(d);
            TRACKMOUSEEVENT track{sizeof(track), TME_HOVER | TME_LEAVE, hwnd, HOVER_DEFAULT};
            TrackMouseEvent(&track);
        }
        break;
    case WM_MOUSEHOVER:
        try {
            const int row = row_under_pointer(hwnd, lp);
            if (row == LB_ERR || !d.shown || !IsWindowVisible(hwnd)) break;
            auto value = full_row_text(hwnd, row);
            if (value.empty()) break;
            hide_row_tip(d);
            d.tip_text = std::move(value);
            d.tip_list = hwnd;
            d.tip_row = row;
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(hwnd, &p);
            SendMessageW(d.row_tip, TTM_TRACKPOSITION, 0, MAKELPARAM(p.x, p.y+d.px(19)));
            const auto tool = row_tip_tool(d);
            SendMessageW(d.row_tip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
        } catch (...) {}
        break;
    case WM_MOUSELEAVE:
    case WM_MOUSEWHEEL:
    case WM_VSCROLL:
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_NCLBUTTONDOWN:
    case WM_WINDOWPOSCHANGING:
    case WM_NCDESTROY:
        hide_row_tip(d, hwnd);
        break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK control_keys(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) noexcept {
    const HWND parent = GetParent(hwnd);
    const auto id = GetDlgCtrlID(hwnd);
    const bool popup = id == channel_list || id == start_list;
    if (popup) {
        const auto group = GetDlgItem(parent, id == start_list ? start : channels);
        if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
            SetFocus(group);
            ShowWindow(hwnd, SW_HIDE);
            InvalidateRect(group, nullptr, FALSE);
            return 0;
        }
        if (msg == WM_KILLFOCUS && group && reinterpret_cast<HWND>(wp) != group &&
            (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VISIBLE)) {
            ShowWindow(hwnd, SW_HIDE);
            InvalidateRect(group, nullptr, FALSE);
        }
    }
    if (msg == WM_MOUSEMOVE && id == start_list) {
        const auto row = row_under_pointer(hwnd, lp);
        if (row != LB_ERR && row != ListBox_GetCurSel(hwnd)) ListBox_SetCurSel(hwnd, row);
    }
    if (msg == WM_LBUTTONUP && popup) {
        const auto row = row_under_pointer(hwnd, lp);
        const auto result = DefSubclassProc(hwnd, msg, wp, lp);
        if (row != LB_ERR && row == ListBox_GetCurSel(hwnd))
            SendMessageW(parent, WM_COMMAND, MAKEWPARAM(id, LBN_DBLCLK), reinterpret_cast<LPARAM>(hwnd));
        return result;
    }
    if (msg == WM_KEYDOWN && wp == VK_TAB) {
        if (const auto next = GetNextDlgTabItem(parent, hwnd, GetKeyState(VK_SHIFT) < 0)) SetFocus(next);
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        wchar_t type[16]{};
        GetClassNameW(hwnd, type, static_cast<int>(std::size(type)));
        if (wcscmp(type, L"ListBox") == 0) {
            SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), LBN_DBLCLK), reinterpret_cast<LPARAM>(hwnd));
            return 0;
        }
    }
    if (msg == WM_CHAR && (wp == VK_TAB || wp == VK_RETURN)) return 0;
    return DefSubclassProc(hwnd, msg, wp, lp);
}

HWND control(Desktop& d, HWND parent, const wchar_t* type, const wchar_t* title, DWORD style, int id) {
    HWND window = CreateWindowExW(0, type, title, WS_CHILD | WS_VISIBLE | style, 0,0,0,0,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), d.module, nullptr);
    if (window) {
        SetWindowTheme(window, L"", L"");
        SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(d.font), FALSE);
        SetWindowSubclass(window, control_keys, 1, 0);
    }
    return window;
}

HWND button(Desktop& d, HWND parent, const wchar_t* title, int id) {
    return control(d, parent, L"BUTTON", title, BS_OWNERDRAW | WS_TABSTOP, id);
}

void set_text(HWND window, const std::wstring& value) {
    wchar_t old[192]{};
    if (value.size() >= std::size(old) || GetWindowTextW(window, old, static_cast<int>(std::size(old))) != static_cast<int>(value.size()) || value != old)
        SetWindowTextW(window, value.c_str());
}

void update_version_title(Desktop& d) {
    const auto versions = wide("v" AUDCORE_98_VERSION_TEXT) +
        label(d, L"-声核 ", L"-Audcore ", L"-声核 ") + d.host_version;
    set_text(d.frame, std::wstring(label(d, L"Audcore 98 - 远程桌面 ", L"Audcore 98 - Remote Desktop ", L"Audcore 98 - 遠隔操作 ")) +
        versions);
}

void position(const Desktop& d, HWND window, int x, int y, int w, int h) {
    MoveWindow(window, d.px(x), d.px(y), d.px(w), d.px(h), TRUE);
}

constexpr RECT effect_pane{10,130,140,200}, send_pane{10,207,140,277};

void layout_pane(const Desktop& d, HWND list, HWND append, RECT area) {
    const int left = d.px(area.left)+1, right = d.px(area.right)-1;
    const int top = d.px(area.top)+1, bottom = d.px(area.bottom)-1;
    const int count = std::max(0, ListBox_GetCount(list));
    const int row = d.px(16);
    const int append_height = d.px(append_row_height);
    SendMessageW(list, LB_SETITEMHEIGHT, 0, row);
    const int shown = std::min(count, std::max(0, (bottom-top-append_height)/row));
    MoveWindow(list, left, top, right-left, shown*row, TRUE);
    ShowWindow(list, shown ? SW_SHOWNA : SW_HIDE);
    MoveWindow(append, left, count > shown ? bottom-append_height : top+shown*row, right-left, append_height, TRUE);
}

void layout_channel(Channel& channel) {
    auto& d = *channel.desktop;
    const auto region = CreateRectRgn(0, 0, d.px(width), d.px(height));
    if (region && !SetWindowRgn(channel.window, region, TRUE)) DeleteObject(region);
    position(d, channel.title_min, 106, 5, 18, 17);
    position(d, channel.title_close, 126, 5, 18, 17);
    position(d, channel.menu_button, 10, 34, 60, 24);
    position(d, channel.mute_box, 78, 34, 62, 24);
    position(d, channel.minus, 10, 65, 26, 24);
    position(d, channel.plus, 114, 65, 26, 24);
    channel.meter = {d.px(10), d.px(98), d.px(140), d.px(123)};
    layout_pane(d, channel.effects, channel.add, effect_pane);
    layout_pane(d, channel.send_list, channel.new_send, send_pane);
}

void clamp_channel(Channel& c) {
    auto& d = *c.desktop;
    RECT bounds{}, window{};
    GetClientRect(d.mdi, &bounds);
    GetWindowRect(c.window, &window);
    POINT p{window.left, window.top};
    ScreenToClient(d.mdi, &p);
    const int x = std::clamp(static_cast<int>(p.x), 0, std::max(0, static_cast<int>(bounds.right) - d.px(width)));
    const int y = std::clamp(static_cast<int>(p.y), 0, std::max(0, static_cast<int>(bounds.bottom) - d.px(height)));
    if (x != p.x || y != p.y) SetWindowPos(c.window, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
}

RECT tray_bounds(const Desktop& d, const RECT& client) {
    const LONG right = client.right - d.px(4);
    return {right-d.px(tray_width),client.bottom-d.px(39),right,client.bottom-d.px(5)};
}

void refresh_clock(Desktop& d) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t time[48]{}, date[48]{}, weekday[32]{};
    if (GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &now, nullptr, time, static_cast<int>(std::size(time)))) {
        GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &now, nullptr, date, static_cast<int>(std::size(date)));
        GetDateFormatW(LOCALE_USER_DEFAULT, 0, &now, L"ddd", weekday, static_cast<int>(std::size(weekday)));
        auto value = std::wstring(time);
        if (date[0]) value += std::wstring(L"\n") + date + L" " + weekday;
        set_text(d.clock, value);
        InvalidateRect(d.clock, nullptr, FALSE);
    }
    if (d.shown && !IsIconic(d.frame) && IsWindowVisible(d.frame))
        SetTimer(d.frame, clock_timer, (60u-now.wSecond)*1000u-now.wMilliseconds, nullptr);
}

void layout_group(Desktop& d, int desktop_height) {
    const auto rows = static_cast<int>(std::clamp<std::size_t>(d.order.size(), 1, 9));
    const auto item_height = d.px(23);
    const auto list_height = std::min(rows*item_height+2, desktop_height);
    SendMessageW(d.list, LB_SETITEMHEIGHT, 0, item_height);
    SetWindowPos(d.list, HWND_TOP, d.px(group_left), desktop_height-list_height, d.px(202), list_height, SWP_NOACTIVATE);
}

void layout(Desktop& d) {
    RECT r{};
    GetClientRect(d.frame, &r);
    const int desktop_height = std::max(0, static_cast<int>(r.bottom)-d.px(taskbar_height));
    const int y = r.bottom-d.px(36);
    MoveWindow(d.shortcut_panel, 0, 0, d.px(shortcut_width), desktop_height, TRUE);
    for (std::size_t i=0; i<d.shortcuts.size(); ++i)
        position(d, d.shortcuts[i], 3, 7+static_cast<int>(i)*50, shortcut_width-6, 47);
    MoveWindow(d.mdi, d.px(shortcut_width), 0, std::max(0L,r.right-d.px(shortcut_width)), desktop_height, TRUE);
    MoveWindow(d.start_button, d.px(4), y, d.px(36), d.px(30), TRUE);
    MoveWindow(d.list_button, d.px(group_left), y, d.px(group_width), d.px(30), TRUE);
    layout_group(d, desktop_height);
    auto tray = tray_bounds(d, r);
    MoveWindow(d.effects_button, d.px(effects_left), y,
        std::clamp<int>(tray.left-d.px(effects_left+4), 0, d.px(effects_width)), d.px(30), TRUE);
    const auto start_height = std::min(desktop_height, d.px(26)*static_cast<int>(std::size(start_entries))+2);
    SendMessageW(d.start_menu, LB_SETITEMHEIGHT, 0, d.px(26));
    SetWindowPos(d.start_menu, HWND_TOP, d.px(4), desktop_height-start_height, d.px(178), start_height, SWP_NOACTIVATE);
    InflateRect(&tray, -bevel_thickness, -bevel_thickness);
    const auto icon_size = d.px(20);
    const auto icon_y = (tray.top+tray.bottom-icon_size)/2;
    const auto clock_left = tray.left+2*icon_size;
    MoveWindow(d.sound, tray.left, icon_y, icon_size, icon_size, TRUE);
    MoveWindow(d.performance, tray.left+icon_size, icon_y, icon_size, icon_size, TRUE);
    MoveWindow(d.clock, clock_left, tray.top, tray.right-clock_left, tray.bottom-tray.top, TRUE);
    for (auto* c : d.order) if (c->window) clamp_channel(*c);
    InvalidateRect(d.frame, nullptr, FALSE);
}

void update_font(Desktop& d) {
    const auto font = CreateFontW(-d.px(13), 0,0,0, FW_NORMAL, FALSE,FALSE,FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Tahoma");
    if (!font) return;
    const auto update = [font](HWND window) { SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); };
    for (HWND window : {d.start_button, d.list_button, d.list, d.start_menu, d.effects_button, d.sound, d.performance, d.clock}) update(window);
    for (auto window : d.shortcuts) update(window);
    for (auto* c : d.order) {
        for (HWND window : {c->title_min,c->title_close,c->menu_button,c->mute_box,c->minus,c->plus,c->effects,c->add,c->send_list,c->new_send})
            update(window);
        SetWindowPos(c->window, nullptr, 0, 0, d.px(width), d.px(height), SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE);
        layout_channel(*c);
        InvalidateRect(c->window, nullptr, TRUE);
    }
    if (d.font) DeleteObject(d.font);
    d.font = font;
}

void update_list(Desktop& d) {
    const bool visible = (GetWindowLongPtrW(d.list, GWL_STYLE) & WS_VISIBLE) != 0;
    if (visible) SendMessageW(d.list, WM_SETREDRAW, FALSE, 0);
    ListBox_ResetContent(d.list);
    for (auto* c : d.order) {
        auto item = c->title;
        if (c->closed) item += label(d, L"  [已关闭]", L"  [Closed]", L"  [閉じた]");
        else if (!(GetWindowLongPtrW(c->window, GWL_STYLE) & WS_VISIBLE))
            item += label(d, L"  [最小化]", L"  [Minimized]", L"  [最小化]");
        ListBox_AddString(d.list, item.c_str());
    }
    if (visible) {
        SendMessageW(d.list, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(d.list, nullptr, TRUE);
    }
    RECT desktop{};
    GetClientRect(d.mdi, &desktop);
    layout_group(d, desktop.bottom);
}

void open_channel(Channel& c) {
    auto& d = *c.desktop;
    c.closed = false;
    ShowWindow(d.list, SW_HIDE);
    clamp_channel(c);
    ShowWindow(c.window, SW_SHOWNA);
    SendMessageW(d.mdi, WM_MDIACTIVATE, reinterpret_cast<WPARAM>(c.window), 0);
    SetFocus(c.menu_button);
    update_list(d);
    InvalidateRect(d.list_button, nullptr, FALSE);
}

void hide_channel(Channel& c, bool close) {
    auto& d = *c.desktop;
    hide_row_tip(d, c.effects);
    hide_row_tip(d, c.send_list);
    c.closed = close;
    ShowWindow(c.window, SW_HIDE);
    const HWND active = reinterpret_cast<HWND>(SendMessageW(d.mdi, WM_MDIGETACTIVE, 0, 0));
    if (active == c.window) {
        for (auto* other : d.order) {
            if (other != &c && (GetWindowLongPtrW(other->window, GWL_STYLE) & WS_VISIBLE)) {
                SendMessageW(d.mdi, WM_MDIACTIVATE, reinterpret_cast<WPARAM>(other->window), 0);
                break;
            }
        }
    }
    update_list(d);
}

bool fresh(const Desktop& d) { return d.ready && !d.recovery.active(); }

void request_snapshot(Desktop& d) {
    if (d.recovery.start_request() && d.host.request_snapshot(d.host.ctx, d.subscription.token()) != 0)
        d.recovery.request_failed();
    if (d.mdi) InvalidateRect(d.mdi, nullptr, FALSE);
    for (auto* c : d.order) if (c->window) InvalidateRect(c->window, &c->meter, FALSE);
}

void feedback(Desktop& d, const std::string& message) {
    d.status = wide(d.state.feedback(message));
    InvalidateRect(d.frame, nullptr, FALSE);
}

std::uint64_t send(Desktop& d, const std::string& bytes, bool requires_state = true) {
    if (!d.shown || bytes.empty() || d.request == UINT64_MAX) return 0;
    if (requires_state && !fresh(d)) {
        if (d.recovery.active()) request_snapshot(d);
        return 0;
    }
    const auto request = ++d.request;
    if (d.host.command(d.host.ctx, request, {reinterpret_cast<const uint8_t*>(bytes.data()), static_cast<uint32_t>(bytes.size())}) == 0) {
        if (!d.status.empty()) {
            d.status.clear();
            InvalidateRect(d.frame, nullptr, FALSE);
        }
        return request;
    }
    feedback(d, "操作失败");
    return 0;
}

POINT anchor(HWND window) {
    RECT r{};
    GetWindowRect(window, &r);
    return {r.left, r.bottom};
}

void channel_command(Channel& c, int action, POINT point) {
    auto& d = *c.desktop;
    if (!fresh(d)) {
        if (d.recovery.active()) request_snapshot(d);
        return;
    }
    auto i = c.index;
    if (action == menu) send(d, d.state.channel_menu_command(i, point.x, point.y));
    if (action == mute) {
        const auto current = d.state.toggle_control(i, false);
        if (!current) return;
        const bool base = d.toggles.base(*current).value_or(current->value);
        if (const auto request = send(d, d.state.toggle_command(i, false, base)))
            d.toggles.record(*current, !base, request);
    }
    if (action == lower || action == raise) {
        const auto current = d.state.gain_control(i);
        if (!current) return;
        const double base = d.gains.base(*current).value_or(current->value);
        const double step = action == lower ? -1.0 : 1.0;
        const double target = std::clamp(base+step, -60., 12.);
        if (target != base) if (const auto request = send(d, d.state.gain_command(i, step, base)))
            d.gains.record(*current, target, request);
    }
    if (action == add_effect) send(d, d.state.effect_browser_command(i));
    if (action == add_send) send(d, d.state.add_send_menu_command(i, point.x, point.y));
    if (action == sends) {
        const auto selected = ListBox_GetCurSel(c.send_list);
        if (selected != LB_ERR) send(d, d.state.send_menu_command(i, selected, point.x, point.y));
    }
    if (action == editor || action == rack) {
        const auto selected = ListBox_GetCurSel(c.effects);
        if (selected == LB_ERR) return;
        if (action == editor) send(d, d.state.effect_editor_command(i, selected));
        else send(d, d.state.effect_menu_command(i, selected, point.x, point.y));
    }
}

void draw_meter(const Channel& c, HDC dc) {
    const auto& d = *c.desktop;
    const auto& source = d.state.channels()[c.index];
    auto rect = c.meter;
    fill(dc, rect, RGB(32,32,32));
    bevel(dc, rect, true);
    const auto draw_bar = [&](double peak, int y, int row_height, const wchar_t* side) {
        if (side) text(dc, side, {rect.left+d.px(5),y-1,rect.left+d.px(17),y+d.px(8)}, RGB(240,240,240));
        const auto left = rect.left + d.px(side ? 18 : 6);
        const int full = rect.right - d.px(6) - left;
        const double db = 20.0 * std::log10(std::max(0.001, peak));
        const int lit = std::clamp(static_cast<int>((db+60.0)/5.0), 0, 12);
        for (int n = 0; n < 12; ++n) {
            const COLORREF color = n >= lit || !fresh(d) ? RGB(57,64,57)
                : n < 9 ? RGB(82,214,70) : n < 11 ? RGB(241,212,52) : RGB(237,68,58);
            fill(dc, {left+full*n/12,y,left+full*(n+1)/12-1,y+row_height}, color);
        }
    };
    if (source.channel_mode != 0) {
        const int bar_height = d.px(13);
        draw_bar(std::max(source.peak_l, source.peak_r), (rect.top+rect.bottom-bar_height)/2, bar_height, nullptr);
    } else {
        draw_bar(source.peak_l, rect.top+d.px(4), d.px(7), L"L");
        draw_bar(source.peak_r, rect.top+d.px(13), d.px(7), L"R");
    }
}

LRESULT channel_proc_impl(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* c = reinterpret_cast<Channel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lp);
        const auto* mdi_create = static_cast<const MDICREATESTRUCTW*>(create->lpCreateParams);
        if (!mdi_create) return FALSE;
        c = reinterpret_cast<Channel*>(mdi_create->lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(c));
        c->window = hwnd;
    }
    if (!c) return DefMDIChildProcW(hwnd, msg, wp, lp);
    auto& d = *c->desktop;
    switch (msg) {
    case WM_NCCALCSIZE: return 0;
    case WM_NCPAINT: return 0;
    case WM_NCACTIVATE: return TRUE;
    case WM_CREATE: {
        SetWindowTheme(hwnd, L"", L"");
        square_corners(hwnd);
        c->title_min = button(d, hwnd, L"", minimize);
        c->title_close = button(d, hwnd, L"", close_child);
        c->menu_button = button(d, hwnd, L"", menu);
        c->mute_box = control(d, hwnd, L"BUTTON", L"", BS_CHECKBOX | WS_TABSTOP, mute);
        c->minus = button(d, hwnd, L"−", lower);
        c->plus = button(d, hwnd, L"+", raise);
        constexpr DWORD list_style = WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_TABSTOP;
        c->effects = control(d, hwnd, L"LISTBOX", L"", list_style, rack);
        c->add = button(d, hwnd, L"", add_effect);
        c->send_list = control(d, hwnd, L"LISTBOX", L"", list_style, sends);
        c->new_send = button(d, hwnd, L"", add_send);
        if (!c->title_min || !c->title_close || !c->menu_button || !c->mute_box || !c->minus || !c->plus ||
            !c->effects || !c->add || !c->send_list || !c->new_send) return -1;
        for (auto list : {c->effects, c->send_list})
            if (!SetWindowSubclass(list, channel_list_tip, 2, reinterpret_cast<DWORD_PTR>(&d))) return -1;
        layout_channel(*c);
        break;
    }
    case WM_NCHITTEST: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &p);
        if (p.y >= 0 && p.y < d.px(26) && p.x >= 0 && p.x < d.px(104)) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_KEYDOWN:
        if (wp == VK_TAB) { SetFocus(c->menu_button); return 0; }
        break;
    case WM_EXITSIZEMOVE:
        clamp_channel(*c);
        break;
    case WM_MDIACTIVATE:
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    case WM_CLOSE:
        hide_channel(*c, true);
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_MINIMIZE) { hide_channel(*c, false); return 0; }
        if ((wp & 0xfff0) == SC_MAXIMIZE) return 0;
        break;
    case WM_CONTEXTMENU: {
        POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        HWND from = reinterpret_cast<HWND>(wp);
        const bool keyboard = point.x == -1 && point.y == -1;
        const bool list = from == c->effects || from == c->send_list;
        if (keyboard) point = anchor(list ? from : c->menu_button);
        if (list) {
            POINT local = point;
            ScreenToClient(from, &local);
            const auto hit = SendMessageW(from, LB_ITEMFROMPOINT, 0, MAKELPARAM(local.x, local.y));
            if (!keyboard && !HIWORD(hit)) ListBox_SetCurSel(from, LOWORD(hit));
            if (!HIWORD(hit) || keyboard) channel_command(*c, from == c->effects ? rack : sends, point);
        } else channel_command(*c, menu, point);
        return 0;
    }
    case WM_COMMAND: {
        const auto id = LOWORD(wp);
        if (id == rack || id == sends) {
            if (HIWORD(wp) == LBN_DBLCLK)
                channel_command(*c, id == rack ? editor : sends, anchor(reinterpret_cast<HWND>(lp)));
        } else if (HIWORD(wp) == BN_CLICKED && lp) {
            if (id == minimize || id == close_child) hide_channel(*c, id == close_child);
            else channel_command(*c, id, anchor(reinterpret_cast<HWND>(lp)));
        }
        return 0;
    }
    case WM_DRAWITEM: {
        const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (item.CtlID == rack || item.CtlID == sends) draw_channel_row(*c, item);
        else draw_button(d, item);
        return TRUE;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetBkColor(reinterpret_cast<HDC>(wp), gray);
        SetDCBrushColor(reinterpret_cast<HDC>(wp), gray);
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    case WM_ERASEBKGND: return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(hwnd, &paint);
        const auto font = SelectObject(dc, d.font);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        if (!EqualRect(&paint.rcPaint, &c->meter)) {
            fill(dc, rect, gray);
            bevel(dc, rect, false);
            const bool active = reinterpret_cast<HWND>(SendMessageW(d.mdi, WM_MDIGETACTIVE, 0, 0)) == hwnd;
            title_bar(dc, {d.px(3),d.px(3),rect.right-d.px(3),d.px(24)}, active);
            fill(dc, {d.px(8),d.px(8),d.px(19),d.px(19)}, c->color);
            text(dc, c->title.c_str(), {d.px(25),d.px(3),d.px(103),d.px(24)}, RGB(255,255,255));
            if (c->index < d.state.channels().size()) {
                wchar_t gain[32]{};
                swprintf_s(gain, L"%.1f dB", d.state.channels()[c->index].gain_db);
                text(dc, gain, {d.px(38),d.px(65),d.px(112),d.px(89)}, RGB(0,0,0), DT_CENTER);
            }
            for (const auto pane : {effect_pane, send_pane}) {
                const RECT area{d.px(pane.left),d.px(pane.top),d.px(pane.right),d.px(pane.bottom)};
                fill(dc, area, RGB(255,255,255));
                FrameRect(dc, &area, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            }
        }
        if (c->index < d.state.channels().size()) draw_meter(*c, dc);
        SelectObject(dc, font);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_NCDESTROY:
        c->window = nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK channel_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    try { return channel_proc_impl(hwnd, msg, wp, lp); }
    catch (...) { return DefMDIChildProcW(hwnd, msg, wp, lp); }
}

template<class Key>
void refresh_rows(Desktop& d, HWND list, HWND append, RECT pane,
    std::vector<std::pair<Key, ListRow>>& previous, std::vector<std::pair<Key, ListRow>> rows) {
    if (rows == previous) return;
    hide_row_tip(d, list);
    const auto old = ListBox_GetCurSel(list);
    const auto top = ListBox_GetTopIndex(list);
    std::optional<Key> selected;
    if (old >= 0 && static_cast<std::size_t>(old) < previous.size()) selected = previous[old].first;
    previous = std::move(rows);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListBox_ResetContent(list);
    for (const auto& [key, entry] : previous) {
        const auto description = entry.value.empty() ? entry.name : entry.name + L"  " + entry.value;
        const int row = ListBox_AddString(list, description.c_str());
        if (selected == key) ListBox_SetCurSel(list, row);
    }
    if (top != LB_ERR) ListBox_SetTopIndex(list, top);
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    layout_pane(d, list, append, pane);
    InvalidateRect(list, nullptr, TRUE);
}

void refresh_channel(Channel& c) {
    auto& d = *c.desktop;
    const auto& source = d.state.channels()[c.index];
    const auto title = wide(d.state.display_name(c.index));
    c.title = title;
    set_text(c.window, title);
    const auto rgb = std::strtoul(source.display_color.c_str()+1, nullptr, 16);
    c.color = RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
    set_text(c.title_min, label(d, L"最小化", L"Minimize", L"最小化"));
    set_text(c.title_close, label(d, L"关闭", L"Close", L"閉じる"));
    set_text(c.menu_button, label(d, L"菜单", L"Menu", L"操作"));
    set_text(c.mute_box, wide(d.state.labels().mute));
    set_text(c.add, label(d, L"+ 效果器", L"+ Effect", L"+ 効果"));
    set_text(c.new_send, label(d, L"+ 发送", L"+ Send", L"+ 送信"));
    if ((SendMessageW(c.mute_box, BM_GETCHECK, 0, 0) == BST_CHECKED) != source.muted)
        SendMessageW(c.mute_box, BM_SETCHECK, source.muted ? BST_CHECKED : BST_UNCHECKED, 0);
    std::vector<std::pair<std::uint64_t, ListRow>> rows;
    for (const auto& effect : source.effects) {
        auto name = wide(d.state.effect_display_name(effect.instance, effect.name));
        if (effect.offline) name += label(d, L"  [离线]", L"  [Offline]", L"  [未接続]");
        else if (effect.bypass) name += label(d, L"  [旁通]", L"  [Bypass]", L"  [迂回]");
        rows.emplace_back(effect.instance, ListRow{std::move(name), {}});
    }
    refresh_rows(d, c.effects, c.add, effect_pane, c.effect_rows, std::move(rows));
    std::vector<std::pair<nlohmann::json, ListRow>> routes;
    for (std::size_t i=0; i<source.sends.size(); ++i) {
        auto name = wide(d.state.send_display_name(c.index, i));
        if (const auto control = d.state.send_control(c.index, i)) {
            wchar_t level[32]{};
            const auto format = std::abs(control->value) >= 1000.0 ? L"%+.1e dB" : L"%+.1f dB";
            swprintf_s(level, format, control->value);
            routes.emplace_back(control->route_identity, ListRow{std::move(name), level});
        } else {
            routes.emplace_back(source.sends[i], ListRow{std::move(name), {}});
        }
    }
    refresh_rows(d, c.send_list, c.new_send, send_pane, c.send_rows, std::move(routes));
    InvalidateRect(c.window, nullptr, FALSE);
}

void remove_channel(Desktop& d, Channel& c) {
    if (c.window) SendMessageW(d.mdi, WM_MDIDESTROY, reinterpret_cast<WPARAM>(c.window), 0);
}

void sync_channels(Desktop& d) {
    update_version_title(d);
    const auto& model = d.state.channels();
    const auto indices = d.state.visible_channels();
    std::unordered_map<std::uint32_t, std::size_t> visible;
    for (auto index : indices) visible.emplace(model[index].id, index);

    std::vector<std::pair<HWND, std::wstring>> previous;
    for (auto* c : d.order) {
        if (c->window) previous.emplace_back(c->window, c->title);
    }
    d.order.clear();
    for (auto it = d.windows.begin(); it != d.windows.end();) {
        const auto current = visible.find(it->first);
        if (!it->second || !it->second->window || current == visible.end() || model[current->second].identity != it->second->identity) {
            if (it->second) remove_channel(d, *it->second);
            it = d.windows.erase(it);
        } else {
            it->second->index = current->second;
            ++it;
        }
    }

    RECT desktop{};
    GetClientRect(d.mdi, &desktop);
    const int columns = std::max(1, static_cast<int>(desktop.right) / d.px(width+10));
    const int rows = std::max(1, static_cast<int>(desktop.bottom) / d.px(height+10));
    for (auto index : indices) {
        const auto& source = model[index];
        auto& c = d.windows[source.id];
        if (!c) {
            c = std::make_unique<Channel>();
            c->desktop = &d;
            c->identity = source.identity;
            c->index = index;
            const auto slot = static_cast<int>(d.order.size());
            const int stack = slot / (rows*columns) % 6;
            const auto window = CreateMDIWindowW(d.channel_class, L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                d.px(6+(slot%columns)*(width+10)+stack*17),
                d.px(6+(slot/columns%rows)*(height+10)+stack*16),
                d.px(width), d.px(height), d.mdi, d.module, reinterpret_cast<LPARAM>(c.get()));
            if (!window) {
                remove_channel(d, *c);
                d.windows.erase(source.id);
                feedback(d, "操作失败");
                continue;
            }
            clamp_channel(*c);
            ShowWindow(window, SW_SHOWNA);
            SendMessageW(d.mdi, WM_MDIACTIVATE, reinterpret_cast<WPARAM>(window), 0);
        }
        refresh_channel(*c);
        d.order.push_back(c.get());
    }
    std::vector<std::pair<HWND, std::wstring>> current;
    for (auto* c : d.order) current.emplace_back(c->window, c->title);
    if (current != previous) update_list(d);
    set_text(d.start_button, label(d, L"开始", L"Start", L"開始"));
    set_text(d.list_button, label(d, L"所有通道", L"All Channels", L"全通路"));
    set_text(d.sound, wide(d.state.labels().settings));
    set_text(d.performance, wide(d.state.labels().performance));
    set_text(d.shortcuts[0], label(d, L"我的电脑", L"My Computer", L"コンピュータ"));
    set_text(d.shortcuts[1], label(d, L"任务管理器", L"Task Manager", L"タスク管理"));
    set_text(d.shortcuts[2], label(d, L"打开工程", L"Open Project", L"工程を開く"));
    set_text(d.shortcuts[3], label(d, L"保存工程", L"Save Project", L"工程を保存"));
    set_text(d.shortcuts[4], wide(d.state.labels().settings));
    const auto* effects_title = label(d, L"效果器", L"Effects", L"効果");
    set_text(d.effects_button, effects_title);
    set_text(d.shortcuts[5], effects_title);
    EnableWindow(d.effects_button, !d.order.empty());
    EnableWindow(d.shortcuts[5], !d.order.empty());
    InvalidateRect(d.mdi, nullptr, FALSE);
}

void AUDCORE_SKIN_CALL on_event(void* user, const audcore_skin_event* event) noexcept {
    auto& d = *static_cast<Desktop*>(user);
    if (!event || event->size < sizeof(*event) || !d.shown) return;
    try {
        const auto kind = event->kind;
        if (kind == AUDCORE_SKIN_EVENT_ERROR) {
            d.ready = false;
            d.recovery.reset();
            d.subscription.stop(d.host);
            InvalidateRect(d.mdi, nullptr, TRUE);
            feedback(d, "操作失败");
            return;
        }
        const bool valid = event->payload.len <= skin_max_event_bytes && (!event->payload.len || event->payload.data);
        const std::string bytes(valid && event->payload.len ? reinterpret_cast<const char*>(event->payload.data) : "",
            valid ? event->payload.len : 0);
        if (kind == AUDCORE_SKIN_EVENT_SNAPSHOT) {
            const bool recovering = d.recovery.active();
            if (!valid || (d.ready && event->revision < d.revision) || !d.state.snapshot(bytes)) {
                if (recovering) d.recovery.snapshot_received(false);
                else request_snapshot(d);
                return;
            }
            d.ready = true;
            d.revision = event->revision;
            d.recovery.snapshot_received(true);
            d.gains.clear();
            d.toggles.clear();
            sync_channels(d);
        } else if (kind == AUDCORE_SKIN_EVENT_DELTA) {
            if (!valid || !fresh(d) || d.revision == UINT64_MAX || event->revision != d.revision + 1 || !d.state.delta(bytes)) {
                request_snapshot(d);
                return;
            }
            d.revision = event->revision;
            d.gains.reconcile(d.state);
            d.toggles.reconcile(d.state);
            sync_channels(d);
        } else if (kind == AUDCORE_SKIN_EVENT_TRANSIENT && valid && fresh(d) && d.state.meters(bytes)) {
            for (auto* c : d.order) if (IsWindowVisible(c->window)) InvalidateRect(c->window, &c->meter, FALSE);
        } else if (kind == AUDCORE_SKIN_EVENT_RESPONSE && event->status != 0) {
            d.gains.fail(event->request_id);
            d.toggles.fail(event->request_id);
            feedback(d, ReferenceSkinState::response_error(bytes).value_or("操作失败"));
        }
    } catch (...) {
        d.ready = false;
        if (event->kind == AUDCORE_SKIN_EVENT_SNAPSHOT && d.recovery.active()) d.recovery.snapshot_received(false);
        else request_snapshot(d);
    }
}

LRESULT CALLBACK desktop_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) noexcept {
    const auto& d = *reinterpret_cast<const Desktop*>(ref);
    if (msg == WM_ERASEBKGND) return TRUE;
    if (msg == WM_PAINT) {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(hwnd, &paint);
        paint_wallpaper(d, hwnd, dc, paint.rcPaint);
        if (!fresh(d) || d.order.empty()) {
            RECT area{};
            GetClientRect(hwnd, &area);
            const auto font = SelectObject(dc, d.font);
            const auto* hint = fresh(d) ? label(d, L"打开「我的电脑」新建通道", L"Open My Computer to add a channel", L"コンピュータから通路を追加")
                : label(d, L"等待声核状态…", L"Waiting for Audcore…", L"声核に接続中…");
            text(dc, hint, area, RGB(255,255,255), DT_CENTER);
            SelectObject(dc, font);
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK shortcut_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) noexcept {
    const auto& d = *reinterpret_cast<const Desktop*>(ref);
    if (msg == WM_COMMAND) return SendMessageW(d.frame, msg, wp, lp);
    if (msg == WM_DRAWITEM) {
        const auto& item = *reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
        constexpr Art art[]{Art::computer, Art::monitor, Art::folder, Art::disk, Art::settings, Art::effects};
        if (item.CtlID >= my_computer && item.CtlID <= desktop_fx)
            draw_shortcut(d, item, art[item.CtlID-my_computer]);
        return TRUE;
    }
    if (msg == WM_ERASEBKGND) return TRUE;
    if (msg == WM_PAINT) {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(hwnd, &paint);
        paint_wallpaper(d, hwnd, dc, paint.rcPaint);
        EndPaint(hwnd, &paint);
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void draw_group_row(const Desktop& d, const DRAWITEMSTRUCT& item) {
    auto rect = item.rcItem;
    const auto dc = item.hDC;
    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    fill(dc, rect, selected ? navy : gray);
    if (item.itemID >= d.order.size()) return;
    const auto& channel = *d.order[item.itemID];
    const int x = rect.left+d.px(5), y = rect.top+d.px(5);
    fill(dc, {x,y,x+d.px(16),y+d.px(13)}, RGB(30,30,30));
    fill(dc, {x+1,y+1,x+d.px(16)-1,y+d.px(5)}, channel.color);
    fill(dc, {x+1,y+d.px(5),x+d.px(16)-1,y+d.px(13)-1}, RGB(241,241,241));
    wchar_t title[192]{};
    if (SendMessageW(d.list, LB_GETTEXTLEN, item.itemID, 0) < static_cast<LRESULT>(std::size(title)))
        SendMessageW(d.list, LB_GETTEXT, item.itemID, reinterpret_cast<LPARAM>(title));
    const auto font = SelectObject(dc, d.font);
    rect.left += d.px(28);
    rect.right -= d.px(4);
    text(dc, title[0] ? title : channel.title.c_str(), rect, selected ? RGB(255,255,255) : RGB(0,0,0));
    SelectObject(dc, font);
    if (item.itemState & ODS_FOCUS) DrawFocusRect(dc, &item.rcItem);
}

void draw_start_row(const Desktop& d, const DRAWITEMSTRUCT& item) {
    const auto dc = item.hDC;
    const auto& r = item.rcItem;
    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    fill(dc, r, selected ? navy : gray);
    if (item.itemID >= std::size(start_entries)) return;
    const auto& entry = start_entries[item.itemID];
    const int x = r.left+d.px(6), y = (r.top+r.bottom-d.px(18))/2;
    draw_art(dc, {x,y,x+d.px(18),y+d.px(18)}, entry.art);
    const auto font = SelectObject(dc, d.font);
    const auto color = selected ? RGB(255,255,255) : entry.action == desktop_fx && d.order.empty() ? RGB(110,110,110) : RGB(0,0,0);
    text(dc, label(d, entry.zh, entry.en, entry.ja), {r.left+d.px(32),r.top,r.right-d.px(4),r.bottom}, color);
    SelectObject(dc, font);
    if (item.itemState & ODS_FOCUS) DrawFocusRect(dc, &r);
}

void desktop_command(Desktop& d, int action, POINT point) {
    switch (action) {
    case desktop_fx:
    case task_fx: {
        if (d.order.empty()) return;
        const auto active = reinterpret_cast<HWND>(SendMessageW(d.mdi, WM_MDIGETACTIVE, 0, 0));
        const auto channel = std::find_if(d.order.begin(), d.order.end(), [active](auto* c) { return c->window == active; });
        channel_command(**(channel == d.order.end() ? d.order.begin() : channel), add_effect, {});
        break;
    }
    case my_computer:
    case start:
        send(d, nlohmann::json{{"method",action == start ? "menu.openSystem" : "menu.openAddChannel"},
            {"params",{{"anchor",{{"x",point.x},{"y",point.y},{"space","screen-physical"}}}}}}.dump(), action != start);
        break;
    case project_open: send(d, R"({"method":"document.open","params":{}})", false); break;
    case project_save: send(d, R"({"method":"document.save","params":{}})", false); break;
    case tray_performance:
    case task_manager: send(d, ReferenceSkinState::panel_command(true), false); break;
    case tray_sound:
    case desktop_settings: send(d, ReferenceSkinState::panel_command(false), false); break;
    }
}

void toggle_start_menu(Desktop& d) {
    if (GetWindowLongPtrW(d.start_menu, GWL_STYLE) & WS_VISIBLE) ShowWindow(d.start_menu, SW_HIDE);
    else {
        ShowWindow(d.list, SW_HIDE);
        ListBox_ResetContent(d.start_menu);
        for (const auto& entry : start_entries) ListBox_AddString(d.start_menu, label(d, entry.zh, entry.en, entry.ja));
        SetWindowPos(d.start_menu, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ListBox_SetCurSel(d.start_menu, d.order.empty() ? 1 : 0);
        SetFocus(d.start_menu);
    }
    InvalidateRect(d.start_button, nullptr, FALSE);
    InvalidateRect(d.list_button, nullptr, FALSE);
}

bool desktop_background_at(const Desktop& d, POINT point) {
    const HWND surface = ChildWindowFromPointEx(d.frame, point, CWP_SKIPINVISIBLE);
    if (surface != d.mdi && surface != d.shortcut_panel) return false;
    MapWindowPoints(d.frame, surface, &point, 1);
    return ChildWindowFromPointEx(surface, point, CWP_SKIPINVISIBLE) == surface;
}

void drop_file(Desktop& d, HDROP drop) {
    struct Drop { HDROP handle; ~Drop() { DragFinish(handle); } } owned{drop};
    if (!d.shown || DragQueryFileW(drop, UINT_MAX, nullptr, 0) != 1) return;
    const auto length = DragQueryFileW(drop, 0, nullptr, 0);
    if (!length || length > 32767) return;
    std::wstring path(length+1, L'\0');
    if (DragQueryFileW(drop, 0, path.data(), length+1) != length) return;
    path.resize(length);
    const auto extension = wallpaper::fs::path(path).extension().wstring();
    if (_wcsicmp(extension.c_str(), L".audcore") == 0 || _wcsicmp(extension.c_str(), L".song") == 0 ||
        _wcsicmp(extension.c_str(), L".rpp") == 0) {
        const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), length, nullptr, 0, nullptr, nullptr);
        if (count <= 0) return;
        std::string utf8(count, '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), length, utf8.data(), count, nullptr, nullptr) == count)
            send(d, nlohmann::json{{"method","host.openDroppedFile"},{"params",{{"path",utf8}}}}.dump(), false);
        return;
    }
    POINT point{};
    if (!DragQueryPoint(drop, &point) || !desktop_background_at(d, point)) return;
    if (d.wallpaper_task.busy()) {
        d.status = label(d, L"正在处理背景图", L"Processing wallpaper", L"壁紙処理中");
        InvalidateRect(d.frame, nullptr, FALSE);
        return;
    }
    if (d.wallpaper_file.empty()) {
        d.status = label(d, L"无法保存背景图", L"Cannot save wallpaper", L"壁紙保存不可");
        InvalidateRect(d.frame, nullptr, FALSE);
        return;
    }
    d.wallpaper_task.start(d.frame, path, d.wallpaper_file);
}

LRESULT frame_proc_impl(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<Desktop*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        d = static_cast<Desktop*>(reinterpret_cast<const CREATESTRUCTW*>(lp)->lpCreateParams);
        d->frame = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));
    }
    if (!d) return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_CREATE: {
        d->dpi = GetDpiForWindow(hwnd);
        update_font(*d);
        d->row_tip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0,0,0,0, hwnd, nullptr, d->module, nullptr);
        if (!d->row_tip) return -1;
        SendMessageW(d->row_tip, CCM_SETUNICODEFORMAT, TRUE, 0);
        const auto tip_tool = row_tip_tool(*d);
        if (!SendMessageW(d->row_tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tip_tool))) return -1;
        SendMessageW(d->row_tip, TTM_SETMAXTIPWIDTH, 0, d->px(380));
        CLIENTCREATESTRUCT args{nullptr, 0x2000};
        d->mdi = CreateWindowExW(0, L"MDICLIENT", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | MDIS_ALLCHILDSTYLES,
            0,0,0,0, hwnd, nullptr, d->module, &args);
        if (!d->mdi || !SetWindowSubclass(d->mdi, desktop_proc, 1, reinterpret_cast<DWORD_PTR>(d))) return -1;
        d->start_button = button(*d, hwnd, L"开始", start);
        d->list_button = button(*d, hwnd, L"所有通道", channels);
        d->effects_button = button(*d, hwnd, L"效果器", task_fx);
        constexpr DWORD popup_style = WS_BORDER | WS_VSCROLL | WS_CLIPSIBLINGS | LBS_NOTIFY |
            LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_TABSTOP;
        d->list = control(*d, hwnd, L"LISTBOX", L"", popup_style, channel_list);
        d->start_menu = control(*d, hwnd, L"LISTBOX", L"", popup_style, start_list);
        d->sound = button(*d, hwnd, L"设置", tray_sound);
        d->performance = button(*d, hwnd, L"效果器性能", tray_performance);
        d->clock = control(*d, hwnd, L"STATIC", L"", SS_OWNERDRAW, tray_clock);
        d->shortcut_panel = control(*d, hwnd, L"STATIC", L"", WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0);
        if (!d->start_button || !d->list_button || !d->effects_button || !d->list || !d->start_menu || !d->sound || !d->performance ||
            !d->clock || !d->shortcut_panel || !SetWindowSubclass(d->shortcut_panel, shortcut_proc, 2, reinterpret_cast<DWORD_PTR>(d))) return -1;
        constexpr const wchar_t* names[]{L"我的电脑", L"任务管理器", L"打开工程", L"保存工程", L"设置", L"效果器"};
        for (std::size_t i=0; i<d->shortcuts.size(); ++i) {
            d->shortcuts[i] = button(*d, d->shortcut_panel, names[i], shortcut_ids[i]);
            if (!d->shortcuts[i]) return -1;
        }
        ShowWindow(d->list, SW_HIDE);
        ShowWindow(d->start_menu, SW_HIDE);
        EnableWindow(d->effects_button, FALSE);
        EnableWindow(d->shortcuts[5], FALSE);
        layout(*d);
        refresh_clock(*d);
        return 0;
    }
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) {
            hide_row_tip(*d);
            KillTimer(hwnd, clock_timer);
        }
        else if (d->list) {
            layout(*d);
            refresh_clock(*d);
        }
        return 0;
    case WM_TIMER:
        if (wp == clock_timer) { refresh_clock(*d); return 0; }
        break;
    case WM_TIMECHANGE:
    case WM_SETTINGCHANGE:
        if (d->clock) refresh_clock(*d);
        break;
    case WM_DPICHANGED:
        d->dpi = HIWORD(wp);
        SendMessageW(d->row_tip, TTM_SETMAXTIPWIDTH, 0, d->px(380));
        update_font(*d);
        layout(*d);
        break;
    case WM_CLOSE:
        send(*d, R"({"method":"window.close","params":{}})", false);
        return 0;
    case WM_DROPFILES:
        drop_file(*d, reinterpret_cast<HDROP>(wp));
        return 0;
    case wallpaper::ready_message: {
        const bool imported = d->wallpaper_task.importing();
        const auto image = d->wallpaper_task.take();
        if (image && d->wallpaper.assign(*image, teal)) {
            d->status.clear();
            InvalidateRect(d->frame, nullptr, FALSE);
            InvalidateRect(d->mdi, nullptr, FALSE);
            InvalidateRect(d->shortcut_panel, nullptr, FALSE);
            for (const auto shortcut : d->shortcuts) InvalidateRect(shortcut, nullptr, FALSE);
        } else if (imported) {
            d->status = label(*d, L"无法设置背景图", L"Cannot set wallpaper", L"壁紙設定不可");
            InvalidateRect(d->frame, nullptr, FALSE);
        }
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) hide_row_tip(*d);
        break;
    case WM_NOTIFY: {
        const auto* notice = reinterpret_cast<const NMHDR*>(lp);
        if (notice && notice->hwndFrom == d->row_tip && notice->code == TTN_GETDISPINFOW) {
            reinterpret_cast<NMTTDISPINFOW*>(lp)->lpszText = d->tip_text.data();
            return 0;
        }
        break;
    }
    case WM_COMMAND: {
        const HWND from = reinterpret_cast<HWND>(lp);
        if (!from) break;
        if (from == d->start_button && HIWORD(wp) == BN_CLICKED) {
            toggle_start_menu(*d);
        } else if (from == d->start_menu && HIWORD(wp) == LBN_DBLCLK) {
            const auto row = ListBox_GetCurSel(from);
            if (row >= 0 && static_cast<std::size_t>(row) < std::size(start_entries)) {
                ShowWindow(from, SW_HIDE);
                InvalidateRect(d->start_button, nullptr, FALSE);
                RECT origin{};
                GetWindowRect(d->start_button, &origin);
                desktop_command(*d, start_entries[row].action, {origin.left,origin.top});
            }
        } else if (from == d->list_button && HIWORD(wp) == BN_CLICKED) {
            if (GetWindowLongPtrW(d->list, GWL_STYLE) & WS_VISIBLE) ShowWindow(d->list, SW_HIDE);
            else {
                ShowWindow(d->start_menu, SW_HIDE);
                ShowWindow(d->list, SW_SHOW);
                const auto active = reinterpret_cast<HWND>(SendMessageW(d->mdi, WM_MDIGETACTIVE, 0, 0));
                const auto row = std::find_if(d->order.begin(), d->order.end(), [active](auto* c) { return c->window == active; });
                if (row != d->order.end()) ListBox_SetCurSel(d->list, std::distance(d->order.begin(), row));
                SetFocus(d->list);
            }
            InvalidateRect(d->list_button, nullptr, FALSE);
        } else if (from == d->list) {
            if (HIWORD(wp) == LBN_DBLCLK) {
                const auto row = ListBox_GetCurSel(d->list);
                if (row >= 0 && static_cast<std::size_t>(row) < d->order.size()) open_channel(*d->order[row]);
            }
        } else if (HIWORD(wp) == BN_CLICKED) {
            desktop_command(*d, GetDlgCtrlID(from), anchor(from));
        }
        return 0;
    }
    case WM_DRAWITEM: {
        const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (item.CtlID == channel_list) { draw_group_row(*d, item); return TRUE; }
        if (item.CtlID == start_list) { draw_start_row(*d, item); return TRUE; }
        const bool selected = (item.hwndItem == d->list_button && IsWindowVisible(d->list)) ||
            (item.hwndItem == d->start_button && IsWindowVisible(d->start_menu));
        draw_button(*d, item, selected);
        return TRUE;
    }
    case WM_ERASEBKGND: return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(hwnd, &paint);
        RECT rect{};
        GetClientRect(hwnd, &rect);
        RECT bar{0,rect.bottom-d->px(taskbar_height),rect.right,rect.bottom};
        fill(dc, bar, gray);
        bevel(dc, bar, false);
        const auto tray = tray_bounds(*d, rect);
        bevel(dc, tray, true);
        if (!d->status.empty()) {
            const auto font = SelectObject(dc, d->font);
            text(dc, d->status.c_str(), {d->px(effects_left+effects_width+6),bar.top,tray.left-d->px(6),bar.bottom}, RGB(128,0,0));
            SelectObject(dc, font);
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_NCDESTROY: {
        KillTimer(hwnd, clock_timer);
        const auto result = DefFrameProcW(hwnd, d->mdi, msg, wp, lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        d->frame = d->mdi = d->start_button = d->list_button = d->list = nullptr;
        d->effects_button = d->start_menu = nullptr;
        d->sound = d->performance = d->clock = d->shortcut_panel = nullptr;
        d->row_tip = d->tip_list = nullptr;
        d->shortcuts.fill(nullptr);
        return result;
    }
    }
    return DefFrameProcW(hwnd, d->mdi, msg, wp, lp);
}

LRESULT CALLBACK frame_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
    try { return frame_proc_impl(hwnd, msg, wp, lp); }
    catch (...) { return DefWindowProcW(hwnd, msg, wp, lp); }
}

void release_windows(Desktop& d) {
    hide_row_tip(d);
    d.wallpaper_task.wait();
    if (d.registered) {
        d.registered = false;
        d.host.unregister_window(d.host.ctx, d.frame);
    }
    if (d.frame) DestroyWindow(d.frame);
    d.order.clear();
    d.windows.clear();
    if (d.font) { DeleteObject(d.font); d.font = nullptr; }
    if (d.channel_class_registered) {
        UnregisterClassW(d.channel_class, d.module);
        d.channel_class_registered = false;
    }
    if (d.frame_class_registered) {
        UnregisterClassW(d.frame_class, d.module);
        d.frame_class_registered = false;
    }
    if (d.frame_icon) { DestroyIcon(d.frame_icon); d.frame_icon = nullptr; }
}

bool create_windows(Desktop& d) {
    const INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES};
    if (!InitCommonControlsEx(&controls)) return false;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&frame_proc), &d.module)) return false;
    swprintf_s(d.frame_class, L"Audcore98Desktop-%p", static_cast<void*>(&d));
    swprintf_s(d.channel_class, L"Audcore98Channel-%p", static_cast<void*>(&d));
    WNDCLASSW wc{};
    wc.hInstance = d.module;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = d.frame_class;
    wc.lpfnWndProc = frame_proc;
    d.frame_icon = create_remote_icon();
    if (!d.frame_icon) return false;
    wc.hIcon = d.frame_icon;
    d.frame_class_registered = RegisterClassW(&wc) != 0;
    if (!d.frame_class_registered) return false;
    wc.lpszClassName = d.channel_class;
    wc.lpfnWndProc = channel_proc;
    wc.hIcon = nullptr;
    d.channel_class_registered = RegisterClassW(&wc) != 0;
    if (!d.channel_class_registered) return false;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + std::max(0L, (work.right-work.left-850)/2);
    const int y = work.top + std::max(0L, (work.bottom-work.top-610)/2);
    if (!CreateWindowExW(WS_EX_APPWINDOW, d.frame_class, L"Audcore 98 - 远程桌面",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, 850, 610, nullptr, nullptr, d.module, &d)) return false;
    SendMessageW(d.frame, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(d.frame_icon));
    SendMessageW(d.frame, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(d.frame_icon));
    wchar_t host_path[32768]{};
    const auto host_path_length = GetModuleFileNameW(nullptr, host_path, static_cast<DWORD>(std::size(host_path)));
    if (host_path_length && host_path_length < std::size(host_path)) d.host_version = file_version(host_path);
    update_version_title(d);
    DragAcceptFiles(d.frame, TRUE);
    const audcore_skin_window_spec spec{sizeof(spec), 850, 610, 440, 350, 0, 0, 1, 1};
    d.registered = d.host.register_window(d.host.ctx, d.frame, &spec) == 0;
    if (d.registered) {
        d.wallpaper_file = wallpaper::storage_path();
        std::error_code error;
        if (!d.wallpaper_file.empty() && wallpaper::fs::is_regular_file(d.wallpaper_file, error))
            d.wallpaper_task.start(d.frame, d.wallpaper_file);
    }
    return d.registered;
}

void* AUDCORE_SKIN_CALL create(const audcore_skin_host* host) noexcept {
    if (!host || host->header.size < sizeof(*host) || host->header.major != AUDCORE_SKIN_ABI_MAJOR ||
        !host->register_window || !host->unregister_window || !host->command || !host->subscribe || !host->unsubscribe || !host->request_snapshot) return nullptr;
    try {
        auto desktop = std::make_unique<Desktop>();
        desktop->host = *host;
        return desktop.release();
    } catch (...) { return nullptr; }
}

int32_t AUDCORE_SKIN_CALL show(void* instance) noexcept {
    auto* d = static_cast<Desktop*>(instance);
    if (!d) return -1;
    if ((!d->frame && !create_windows(*d)) || !d->subscription.start(d->host, on_event, d)) {
        release_windows(*d);
        return -1;
    }
    d->shown = true;
    ShowWindow(d->frame, SW_SHOW);
    refresh_clock(*d);
    return 0;
}

void AUDCORE_SKIN_CALL hide(void* instance) noexcept {
    auto* d = static_cast<Desktop*>(instance);
    if (!d) return;
    d->shown = d->ready = false;
    hide_row_tip(*d);
    d->recovery.reset();
    d->gains.clear();
    d->toggles.clear();
    if (d->list) ShowWindow(d->list, SW_HIDE);
    if (d->start_menu) ShowWindow(d->start_menu, SW_HIDE);
    if (d->frame) {
        KillTimer(d->frame, clock_timer);
        ShowWindow(d->frame, SW_HIDE);
    }
    // The host suspends the subscription and sends a fresh snapshot on resume.
}

void AUDCORE_SKIN_CALL destroy(void* instance) noexcept {
    auto* d = static_cast<Desktop*>(instance);
    if (!d) return;
    d->shown = false;
    d->subscription.stop(d->host);
    release_windows(*d);
    delete d;
}

const uint8_t id[] = "cn.audcore.skin.win98";
const uint8_t skin_name[] = "Audcore 98";
const uint8_t version[] = AUDCORE_98_VERSION_TEXT;
const audcore_skin_descriptor skin_descriptor{{sizeof(audcore_skin_descriptor), AUDCORE_SKIN_ABI_MAJOR, AUDCORE_SKIN_ABI_MINOR},
    {id,sizeof(id)-1}, {skin_name,sizeof(skin_name)-1}, {version,sizeof(version)-1}, create, show, hide, destroy,
    AUDCORE_SKIN_THEME_LIGHT | AUDCORE_SKIN_THEME_DARK, 1.f, 1.f, 1.f};
}

extern "C" __declspec(dllexport) const audcore_skin_descriptor* AUDCORE_SKIN_CALL audcore_skin_get_descriptor(uint32_t major) noexcept {
    return major == AUDCORE_SKIN_ABI_MAJOR ? &skin_descriptor : nullptr;
}
