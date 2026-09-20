#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <string>
#include <cwchar>
#include <cwctype>
#include <algorithm>
#include <iterator>
#include <limits>

#include "resource.h"

#ifdef _MSC_VER
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace {
constexpr wchar_t kWindowClass[] = L"AwakeStandaloneWindowV2";
constexpr wchar_t kAppName[] = L"Awake";
constexpr wchar_t kVersion[] = L"2.0.0";
constexpr wchar_t kMutexName[] = L"Local\\devbloxstrap.Awake";
constexpr wchar_t kRegistryKey[] = L"Software\\devbloxstrap\\Awake";
constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT_PTR TIMER_STATUS = 1;

constexpr UINT ID_TRAY_OPEN = 1000;
constexpr UINT ID_TRAY_OFF = 1001;
constexpr UINT ID_TRAY_PASSIVE = 1002;
constexpr UINT ID_TRAY_INDEFINITE = 1003;
constexpr UINT ID_TRAY_30M = 1004;
constexpr UINT ID_TRAY_1H = 1005;
constexpr UINT ID_TRAY_2H = 1006;
constexpr UINT ID_TRAY_DISPLAY = 1007;
constexpr UINT ID_TRAY_EXIT = 1099;

constexpr UINT IDC_MODE = 2001;
constexpr UINT IDC_INTERVAL_HOURS = 2002;
constexpr UINT IDC_INTERVAL_MINUTES = 2003;
constexpr UINT IDC_EXPIRY_DATE = 2004;
constexpr UINT IDC_EXPIRY_TIME = 2005;
constexpr UINT IDC_APPLY = 2006;

enum class Mode : DWORD {
    Passive = 0,
    Indefinite = 1,
    Timed = 2,
    Expiration = 3,
};

HWND g_hwnd = nullptr;
HWND g_hMode = nullptr; // retained for compatibility; v2 glass UI uses a custom selector
HWND g_hHours = nullptr;
HWND g_hMinutes = nullptr;
HWND g_hDate = nullptr;
HWND g_hTime = nullptr;
HWND g_hApply = nullptr;
NOTIFYICONDATAW g_nid{};
HICON g_appIcon = nullptr;
HFONT g_fontTitle = nullptr;
HFONT g_fontSection = nullptr;
HFONT g_fontBody = nullptr;
HFONT g_fontSmall = nullptr;
Mode g_mode = Mode::Indefinite;
bool g_enabled = true;
bool g_keepDisplayOn = false;
DWORD g_intervalHours = 1;
DWORD g_intervalMinutes = 0;
ULONGLONG g_expireTick = 0;
SYSTEMTIME g_expirationLocal{};
RECT g_enableToggleRect{};
RECT g_screenToggleRect{};
RECT g_modeSelectorRect{};
bool g_hoverEnable = false;
bool g_hoverScreen = false;
bool g_hoverMode = false;
bool g_trackingMouse = false;
UINT g_taskbarCreated = 0;

COLORREF kBackgroundTop = RGB(241, 246, 252);
COLORREF kBackgroundBottom = RGB(230, 239, 249);
COLORREF kGlass = RGB(250, 252, 255);
COLORREF kGlassStrong = RGB(255, 255, 255);
COLORREF kGlassBorder = RGB(205, 216, 229);
COLORREF kShadow = RGB(205, 216, 228);
COLORREF kText = RGB(24, 33, 47);
COLORREF kMuted = RGB(92, 104, 122);
COLORREF kAccent = RGB(0, 120, 212);
COLORREF kAccentHover = RGB(0, 104, 184);
COLORREF kAccentSoft = RGB(226, 240, 253);
COLORREF kDisabled = RGB(160, 169, 181);

RECT MakeRect(int left, int top, int right, int bottom)
{
    RECT r{left, top, right, bottom};
    return r;
}

void FillSolid(HDC dc, const RECT& r, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

void DrawSoftBackground(HDC dc, const RECT& r)
{
    const int height = std::max(1L, r.bottom - r.top);
    const int bands = 40;
    for (int i = 0; i < bands; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(bands - 1);
        const int rr = static_cast<int>(GetRValue(kBackgroundTop) * (1.0 - t) + GetRValue(kBackgroundBottom) * t);
        const int gg = static_cast<int>(GetGValue(kBackgroundTop) * (1.0 - t) + GetGValue(kBackgroundBottom) * t);
        const int bb = static_cast<int>(GetBValue(kBackgroundTop) * (1.0 - t) + GetBValue(kBackgroundBottom) * t);
        RECT band{r.left, r.top + (height * i) / bands, r.right, r.top + (height * (i + 1)) / bands + 1};
        FillSolid(dc, band, RGB(rr, gg, bb));
    }

    // Subtle Fluent-style ambient glows. They are deliberately low contrast so text remains legible.
    HBRUSH glow1 = CreateSolidBrush(RGB(225, 240, 255));
    HBRUSH glow2 = CreateSolidBrush(RGB(236, 232, 252));
    HGDIOBJ oldBrush = SelectObject(dc, glow1);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, r.right - 310, -120, r.right + 120, 245);
    SelectObject(dc, glow2);
    Ellipse(dc, -150, r.bottom - 250, 300, r.bottom + 120);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(glow1);
    DeleteObject(glow2);
}

void DrawGlassCard(HDC dc, const RECT& r, bool strong = false)
{
    // Layered soft shadow + cool translucent-looking fill. In Win11 the window also requests Mica.
    RECT shadow = r;
    OffsetRect(&shadow, 0, 3);
    HBRUSH shadowBrush = CreateSolidBrush(kShadow);
    HPEN noPen = static_cast<HPEN>(GetStockObject(NULL_PEN));
    HGDIOBJ oldBrush = SelectObject(dc, shadowBrush);
    HGDIOBJ oldPen = SelectObject(dc, noPen);
    RoundRect(dc, shadow.left, shadow.top, shadow.right, shadow.bottom, 18, 18);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(shadowBrush);

    HBRUSH brush = CreateSolidBrush(strong ? kGlassStrong : kGlass);
    HPEN pen = CreatePen(PS_SOLID, 1, kGlassBorder);
    oldBrush = SelectObject(dc, brush);
    oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, 18, 18);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawPill(HDC dc, const RECT& r, COLORREF fill, COLORREF border)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    const int radius = r.bottom - r.top;
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawTextLine(HDC dc, const std::wstring& text, RECT r, HFONT font, COLORREF color,
                  UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE)
{
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &r, format);
    SelectObject(dc, oldFont);
}

void DrawToggle(HDC dc, const RECT& r, bool on, bool enabled, bool hovered = false)
{
    const COLORREF trackFill = !enabled ? RGB(224, 229, 236)
                              : on ? (hovered ? kAccentHover : kAccent)
                                   : (hovered ? RGB(235, 242, 249) : RGB(244, 247, 250));
    const COLORREF trackBorder = !enabled ? RGB(202, 210, 220)
                                : on ? trackFill
                                     : (hovered ? RGB(126, 157, 190) : RGB(151, 162, 176));

    HBRUSH brush = CreateSolidBrush(trackFill);
    HPEN pen = CreatePen(PS_SOLID, 1, trackBorder);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    const int radius = r.bottom - r.top;
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);

    const int diameter = (r.bottom - r.top) - 6;
    const int x = on ? r.right - diameter - 3 : r.left + 3;
    RECT knob{x, r.top + 3, x + diameter, r.top + 3 + diameter};
    HBRUSH knobBrush = CreateSolidBrush(enabled ? RGB(255, 255, 255) : RGB(245, 247, 249));
    HPEN knobPen = CreatePen(PS_SOLID, 1, enabled ? RGB(202, 210, 220) : RGB(218, 223, 229));
    oldBrush = SelectObject(dc, knobBrush);
    oldPen = SelectObject(dc, knobPen);
    Ellipse(dc, knob.left, knob.top, knob.right, knob.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(knobBrush);
    DeleteObject(knobPen);
}

std::wstring ModeDisplayText(Mode mode)
{
    switch (mode) {
        case Mode::Passive: return L"Selected power plan";
        case Mode::Indefinite: return L"Awake indefinitely";
        case Mode::Timed: return L"Time interval";
        case Mode::Expiration: return L"Until expiration";
    }
    return L"Selected power plan";
}

void DrawModeSelector(HDC dc, const RECT& r, bool enabled, bool hovered)
{
    const COLORREF fill = !enabled ? RGB(242, 245, 248) : (hovered ? RGB(241, 248, 254) : RGB(255, 255, 255));
    const COLORREF border = !enabled ? RGB(217, 224, 232) : (hovered ? RGB(121, 166, 207) : RGB(198, 211, 225));
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, 12, 12);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);

    DrawTextLine(dc, ModeDisplayText(g_mode), MakeRect(r.left + 16, r.top, r.right - 42, r.bottom),
                 g_fontBody, enabled ? kText : kDisabled);

    HPEN chevronPen = CreatePen(PS_SOLID, 2, enabled ? kMuted : kDisabled);
    oldPen = SelectObject(dc, chevronPen);
    const int cx = r.right - 21;
    const int cy = (r.top + r.bottom) / 2;
    MoveToEx(dc, cx - 4, cy - 2, nullptr);
    LineTo(dc, cx, cy + 2);
    LineTo(dc, cx + 4, cy - 2);
    SelectObject(dc, oldPen);
    DeleteObject(chevronPen);
}

bool ReadDword(HKEY key, const wchar_t* name, DWORD& value)
{
    DWORD type = 0;
    DWORD size = sizeof(value);
    return RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
           type == REG_DWORD;
}

bool ReadQword(HKEY key, const wchar_t* name, ULONGLONG& value)
{
    DWORD type = 0;
    DWORD size = sizeof(value);
    return RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
           type == REG_QWORD;
}

void WriteDword(HKEY key, const wchar_t* name, DWORD value)
{
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

void WriteQword(HKEY key, const wchar_t* name, ULONGLONG value)
{
    RegSetValueExW(key, name, 0, REG_QWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

bool LocalToUtcFileTime(const SYSTEMTIME& local, FILETIME& ft)
{
    SYSTEMTIME utc{};
    if (!TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc)) return false;
    return SystemTimeToFileTime(&utc, &ft) != FALSE;
}

ULONGLONG FileTimeToUll(const FILETIME& ft)
{
    ULARGE_INTEGER v{};
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    return v.QuadPart;
}

FILETIME UllToFileTime(ULONGLONG value)
{
    ULARGE_INTEGER v{};
    v.QuadPart = value;
    FILETIME ft{v.LowPart, v.HighPart};
    return ft;
}

ULONGLONG CurrentUtcFileTime()
{
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    return FileTimeToUll(ft);
}

void SetDefaultExpiration()
{
    SYSTEMTIME local{};
    GetLocalTime(&local);
    FILETIME utc{};
    if (LocalToUtcFileTime(local, utc)) {
        ULONGLONG value = FileTimeToUll(utc) + 60ULL * 60ULL * 10000000ULL;
        FILETIME futureUtc = UllToFileTime(value);
        SYSTEMTIME utcSystem{}, futureLocal{};
        if (FileTimeToSystemTime(&futureUtc, &utcSystem) &&
            SystemTimeToTzSpecificLocalTime(nullptr, &utcSystem, &futureLocal)) {
            g_expirationLocal = futureLocal;
            return;
        }
    }
    g_expirationLocal = local;
    g_expirationLocal.wHour = static_cast<WORD>((g_expirationLocal.wHour + 1) % 24);
}

void LoadSettings()
{
    SetDefaultExpiration();
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, KEY_READ, &key) != ERROR_SUCCESS) return;

    DWORD value = 0;
    if (ReadDword(key, L"Enabled", value)) g_enabled = value != 0;
    if (ReadDword(key, L"Mode", value) && value <= static_cast<DWORD>(Mode::Expiration)) {
        g_mode = static_cast<Mode>(value);
    }
    if (ReadDword(key, L"KeepDisplayOn", value)) g_keepDisplayOn = value != 0;
    if (ReadDword(key, L"IntervalHours", value)) g_intervalHours = std::min<DWORD>(value, 999);
    if (ReadDword(key, L"IntervalMinutes", value)) g_intervalMinutes = std::min<DWORD>(value, 59);

    ULONGLONG expiry = 0;
    if (ReadQword(key, L"ExpiryUtc", expiry) && expiry > CurrentUtcFileTime()) {
        FILETIME ft = UllToFileTime(expiry);
        SYSTEMTIME utc{}, local{};
        if (FileTimeToSystemTime(&ft, &utc) && SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
            g_expirationLocal = local;
        }
    }
    RegCloseKey(key);
}

void SaveSettings()
{
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, &disposition) != ERROR_SUCCESS) return;

    WriteDword(key, L"Enabled", g_enabled ? 1 : 0);
    WriteDword(key, L"Mode", static_cast<DWORD>(g_mode));
    WriteDword(key, L"KeepDisplayOn", g_keepDisplayOn ? 1 : 0);
    WriteDword(key, L"IntervalHours", g_intervalHours);
    WriteDword(key, L"IntervalMinutes", g_intervalMinutes);
    FILETIME expiry{};
    if (LocalToUtcFileTime(g_expirationLocal, expiry)) {
        WriteQword(key, L"ExpiryUtc", FileTimeToUll(expiry));
    }
    RegCloseKey(key);
}

std::wstring FormatRemaining()
{
    if (!g_enabled) return L"Off";
    if (g_mode == Mode::Timed && g_expireTick != 0) {
        ULONGLONG now = GetTickCount64();
        if (now >= g_expireTick) return L"0m";
        ULONGLONG seconds = (g_expireTick - now + 999) / 1000;
        ULONGLONG hours = seconds / 3600;
        ULONGLONG minutes = (seconds % 3600 + 59) / 60;
        if (hours > 0) return std::to_wstring(hours) + L"h " + std::to_wstring(minutes) + L"m remaining";
        return std::to_wstring(minutes) + L"m remaining";
    }
    if (g_mode == Mode::Expiration) {
        FILETIME expiry{};
        if (LocalToUtcFileTime(g_expirationLocal, expiry)) {
            ULONGLONG target = FileTimeToUll(expiry);
            ULONGLONG now = CurrentUtcFileTime();
            if (target <= now) return L"expired";
            ULONGLONG seconds = (target - now) / 10000000ULL;
            ULONGLONG hours = seconds / 3600;
            ULONGLONG minutes = (seconds % 3600 + 59) / 60;
            if (hours > 0) return std::to_wstring(hours) + L"h " + std::to_wstring(minutes) + L"m remaining";
            return std::to_wstring(minutes) + L"m remaining";
        }
    }
    return L"";
}

std::wstring StatusText()
{
    if (!g_enabled) return L"Awake - Off";
    switch (g_mode) {
        case Mode::Passive:
            return L"Awake - Selected power plan";
        case Mode::Indefinite:
            return g_keepDisplayOn ? L"Awake - Indefinite + screen on" : L"Awake - Indefinite";
        case Mode::Timed:
            return L"Awake - " + FormatRemaining();
        case Mode::Expiration:
            return L"Awake - " + FormatRemaining();
    }
    return kAppName;
}

void AddTrayIcon()
{
    if (!g_hwnd) return;
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = g_appIcon ? g_appIcon : LoadIconW(nullptr, IDI_APPLICATION);
    std::wstring text = StatusText();
    wcsncpy_s(g_nid.szTip, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void UpdateTray()
{
    if (!g_hwnd) return;
    std::wstring text = StatusText();
    wcsncpy_s(g_nid.szTip, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

bool ApplyExecutionState()
{
    EXECUTION_STATE state = ES_CONTINUOUS;
    if (g_enabled && g_mode != Mode::Passive) {
        state = static_cast<EXECUTION_STATE>(state | ES_SYSTEM_REQUIRED);
        if (g_keepDisplayOn) state = static_cast<EXECUTION_STATE>(state | ES_DISPLAY_REQUIRED);
    }
    return SetThreadExecutionState(state) != 0;
}

void StartIntervalTimer()
{
    ULONGLONG seconds = static_cast<ULONGLONG>(g_intervalHours) * 3600ULL +
                        static_cast<ULONGLONG>(g_intervalMinutes) * 60ULL;
    if (seconds == 0) seconds = 60;
    ULONGLONG now = GetTickCount64();
    ULONGLONG maxAdd = std::numeric_limits<ULONGLONG>::max() - now;
    ULONGLONG requestedMs = seconds > maxAdd / 1000ULL ? maxAdd : seconds * 1000ULL;
    g_expireTick = now + requestedMs;
}

void EnsureModeTimer()
{
    if (g_enabled && g_mode == Mode::Timed && g_expireTick == 0) StartIntervalTimer();
    if (g_mode != Mode::Timed) g_expireTick = 0;
}

void SetEnabled(bool enabled)
{
    g_enabled = enabled;
    EnsureModeTimer();
    ApplyExecutionState();
    SaveSettings();
    UpdateTray();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void SetMode(Mode mode)
{
    g_mode = mode;
    g_expireTick = 0;
    EnsureModeTimer();
    ApplyExecutionState();
    SaveSettings();
    UpdateTray();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ApplyTimedPreset(DWORD minutes)
{
    g_enabled = true;
    g_mode = Mode::Timed;
    g_intervalHours = minutes / 60;
    g_intervalMinutes = minutes % 60;
    g_expireTick = 0;
    EnsureModeTimer();
    ApplyExecutionState();
    SaveSettings();
    UpdateTray();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

DWORD ReadEditNumber(HWND control, DWORD maxValue)
{
    wchar_t buffer[32]{};
    GetWindowTextW(control, buffer, static_cast<int>(std::size(buffer)));
    wchar_t* end = nullptr;
    unsigned long value = wcstoul(buffer, &end, 10);
    if (!end || *end != L'\0') return 0;
    return std::min<DWORD>(static_cast<DWORD>(value), maxValue);
}

void SyncTextFields()
{
    if (g_hHours) SetWindowTextW(g_hHours, std::to_wstring(g_intervalHours).c_str());
    if (g_hMinutes) SetWindowTextW(g_hMinutes, std::to_wstring(g_intervalMinutes).c_str());
    if (g_hDate) DateTime_SetSystemtime(g_hDate, GDT_VALID, &g_expirationLocal);
    if (g_hTime) DateTime_SetSystemtime(g_hTime, GDT_VALID, &g_expirationLocal);
}

void ApplyEditorValues()
{
    if (g_mode == Mode::Timed) {
        g_intervalHours = ReadEditNumber(g_hHours, 999);
        g_intervalMinutes = ReadEditNumber(g_hMinutes, 59);
        if (g_intervalHours == 0 && g_intervalMinutes == 0) g_intervalMinutes = 1;
        g_expireTick = 0;
        EnsureModeTimer();
    } else if (g_mode == Mode::Expiration) {
        SYSTEMTIME date{}, time{};
        if (DateTime_GetSystemtime(g_hDate, &date) == GDT_VALID &&
            DateTime_GetSystemtime(g_hTime, &time) == GDT_VALID) {
            date.wHour = time.wHour;
            date.wMinute = time.wMinute;
            date.wSecond = 0;
            date.wMilliseconds = 0;
            g_expirationLocal = date;
            FILETIME ft{};
            if (LocalToUtcFileTime(g_expirationLocal, ft) && FileTimeToUll(ft) <= CurrentUtcFileTime()) {
                MessageBoxW(g_hwnd, L"Choose an expiration date and time in the future.", kAppName, MB_OK | MB_ICONINFORMATION);
                return;
            }
        }
    }
    ApplyExecutionState();
    SaveSettings();
    UpdateTray();
    SyncTextFields();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void AdjustWindowHeight()
{
    if (!g_hwnd) return;
    const bool hasDetails = g_enabled && (g_mode == Mode::Timed || g_mode == Mode::Expiration);
    const int desiredHeight = hasDetails ? 690 : 610;
    RECT wr{};
    if (GetWindowRect(g_hwnd, &wr)) {
        const int width = wr.right - wr.left;
        const int currentHeight = wr.bottom - wr.top;
        if (currentHeight != desiredHeight) {
            SetWindowPos(g_hwnd, nullptr, 0, 0, width, desiredHeight,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

void PositionControls()
{
    RECT client{};
    GetClientRect(g_hwnd, &client);
    const int contentLeft = 40;
    const int right = client.right - 40;

    SetWindowPos(g_hMode, nullptr, right - 360, 301, 342, 40, SWP_NOZORDER);
    SetWindowPos(g_hHours, nullptr, contentLeft + 315, 487, 92, 34, SWP_NOZORDER);
    SetWindowPos(g_hMinutes, nullptr, contentLeft + 455, 487, 92, 34, SWP_NOZORDER);
    SetWindowPos(g_hDate, nullptr, contentLeft + 270, 487, 178, 34, SWP_NOZORDER);
    SetWindowPos(g_hTime, nullptr, contentLeft + 468, 487, 132, 34, SWP_NOZORDER);
    SetWindowPos(g_hApply, nullptr, right - 92, 486, 78, 36, SWP_NOZORDER);
}

void UpdateControls()
{
    if (g_hMode) EnableWindow(g_hMode, g_enabled ? TRUE : FALSE);
    const bool timed = g_enabled && g_mode == Mode::Timed;
    const bool expiration = g_enabled && g_mode == Mode::Expiration;
    ShowWindow(g_hHours, timed ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hMinutes, timed ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hDate, expiration ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hTime, expiration ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hApply, (timed || expiration) ? SW_SHOW : SW_HIDE);
    SyncTextFields();
    AdjustWindowHeight();
    PositionControls();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ShowMainWindow()
{
    ShowWindow(g_hwnd, SW_RESTORE);
    SetForegroundWindow(g_hwnd);
}

void ShowTrayMenu(HWND hwnd)
{
    POINT pt{};
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, ID_TRAY_OPEN, L"Open Awake");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (!g_enabled ? MF_CHECKED : 0), ID_TRAY_OFF, L"Off");
    AppendMenuW(menu, MF_STRING | (g_enabled && g_mode == Mode::Passive ? MF_CHECKED : 0), ID_TRAY_PASSIVE, L"Use selected power plan");
    AppendMenuW(menu, MF_STRING | (g_enabled && g_mode == Mode::Indefinite ? MF_CHECKED : 0), ID_TRAY_INDEFINITE, L"Keep awake indefinitely");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_30M, L"Keep awake for 30 minutes");
    AppendMenuW(menu, MF_STRING, ID_TRAY_1H, L"Keep awake for 1 hour");
    AppendMenuW(menu, MF_STRING, ID_TRAY_2H, L"Keep awake for 2 hours");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g_keepDisplayOn ? MF_CHECKED : 0) |
                      ((!g_enabled || g_mode == Mode::Passive) ? MF_GRAYED : 0),
                ID_TRAY_DISPLAY, L"Keep screen on");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

void ApplyModernWindowVisuals(HWND hwnd)
{
    // Windows 11: rounded corners + Mica base. Unsupported attributes fail harmlessly on Windows 10.
    const DWORD DWMWA_WINDOW_CORNER_PREFERENCE_ID = 33;
    const DWORD DWMWA_BORDER_COLOR_ID = 34;
    const DWORD DWMWA_CAPTION_COLOR_ID = 35;
    const DWORD DWMWA_TEXT_COLOR_ID = 36;
    const DWORD DWMWA_SYSTEMBACKDROP_TYPE_ID = 38;
    const int DWMWCP_ROUND = 2;
    const int DWMSBT_MAINWINDOW = 2;
    const COLORREF border = RGB(204, 216, 229);
    const COLORREF caption = RGB(242, 247, 252);
    const COLORREF captionText = kText;

    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE_ID, &DWMWCP_ROUND, sizeof(DWMWCP_ROUND));
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE_ID, &DWMSBT_MAINWINDOW, sizeof(DWMSBT_MAINWINDOW));
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR_ID, &border, sizeof(border));
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR_ID, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR_ID, &captionText, sizeof(captionText));
}

void ShowModeMenu(HWND hwnd)
{
    if (!g_enabled) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING | (g_mode == Mode::Passive ? MF_CHECKED : 0), 1, L"Selected power plan");
    AppendMenuW(menu, MF_STRING | (g_mode == Mode::Indefinite ? MF_CHECKED : 0), 2, L"Awake indefinitely");
    AppendMenuW(menu, MF_STRING | (g_mode == Mode::Timed ? MF_CHECKED : 0), 3, L"Time interval");
    AppendMenuW(menu, MF_STRING | (g_mode == Mode::Expiration ? MF_CHECKED : 0), 4, L"Until expiration");

    RECT modeRect{};
    GetWindowRect(g_hMode, &modeRect);
    POINT pt{modeRect.left, modeRect.bottom + 4};
    SetForegroundWindow(hwnd);
    const UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                                         pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (selected >= 1 && selected <= 4) {
        SetMode(static_cast<Mode>(selected - 1));
        UpdateControls();
    }
}

void DrawStatusPill(HDC dc, int left, int top, const std::wstring& text)
{
    SIZE size{};
    HGDIOBJ oldFont = SelectObject(dc, g_fontSmall);
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    SelectObject(dc, oldFont);
    const int width = std::min(360, std::max(160, static_cast<int>(size.cx) + 52));
    RECT pill{left, top, left + width, top + 30};
    DrawPill(dc, pill, g_enabled ? RGB(239, 248, 255) : RGB(244, 246, 249),
             g_enabled ? RGB(187, 217, 242) : RGB(215, 222, 230));

    HBRUSH dotBrush = CreateSolidBrush(g_enabled ? RGB(34, 153, 84) : RGB(145, 154, 166));
    HGDIOBJ oldBrush = SelectObject(dc, dotBrush);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, left + 12, top + 10, left + 22, top + 20);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(dotBrush);
    DrawTextLine(dc, text, MakeRect(left + 30, top, left + width - 12, top + 30), g_fontSmall, kMuted);
}

void DrawInterface(HWND hwnd, HDC dc)
{
    RECT client{};
    GetClientRect(hwnd, &client);
    DrawSoftBackground(dc, client);

    // Header sits directly on the material layer; cards provide the content hierarchy.
    RECT header{0, 0, client.right, 72};
    FillSolid(dc, header, RGB(247, 250, 253));
    HPEN separator = CreatePen(PS_SOLID, 1, RGB(214, 224, 234));
    HGDIOBJ oldPen = SelectObject(dc, separator);
    MoveToEx(dc, 0, 71, nullptr);
    LineTo(dc, client.right, 71);
    SelectObject(dc, oldPen);
    DeleteObject(separator);

    if (g_appIcon) DrawIconEx(dc, 34, 18, g_appIcon, 34, 34, 0, nullptr, DI_NORMAL);
    DrawTextLine(dc, L"Awake", MakeRect(80, 10, 250, 60), g_fontSection, kText);

    RECT versionPill{client.right - 112, 20, client.right - 32, 50};
    DrawPill(dc, versionPill, RGB(239, 245, 251), RGB(211, 222, 233));
    DrawTextLine(dc, L"v2.0.0", versionPill, g_fontSmall, kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    const int left = 40;
    const int right = client.right - 40;
    DrawTextLine(dc, L"Awake", MakeRect(left, 88, right, 130), g_fontTitle, kText);
    DrawTextLine(dc, L"Keep your PC awake when you need it.", MakeRect(left, 126, right, 154), g_fontBody, kMuted);

    RECT activation{left, 164, right, 234};
    DrawGlassCard(dc, activation, true);
    RECT iconTile{left + 18, 179, left + 58, 219};
    DrawPill(dc, iconTile, kAccentSoft, RGB(194, 220, 244));
    if (g_appIcon) DrawIconEx(dc, left + 24, 185, g_appIcon, 28, 28, 0, nullptr, DI_NORMAL);
    DrawTextLine(dc, L"Awake", MakeRect(left + 74, 171, left + 270, 202), g_fontSection, kText);
    DrawTextLine(dc, L"Override sleep behavior without changing your power plan.",
                 MakeRect(left + 74, 198, left + 535, 226), g_fontSmall, kMuted);
    DrawTextLine(dc, g_enabled ? L"On" : L"Off", MakeRect(right - 142, 170, right - 76, 226),
                 g_fontBody, g_enabled ? kText : kMuted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    g_enableToggleRect = MakeRect(right - 60, 185, right - 12, 213);
    DrawToggle(dc, g_enableToggleRect, g_enabled, true, g_hoverEnable);

    DrawTextLine(dc, L"Behavior", MakeRect(left, 248, right, 280), g_fontSection, kText);

    RECT modeCard{left, 286, right, 356};
    DrawGlassCard(dc, modeCard);
    DrawTextLine(dc, L"Mode", MakeRect(left + 24, 294, left + 235, 326), g_fontBody, g_enabled ? kText : kDisabled);
    DrawTextLine(dc, L"Choose how Awake should keep your device active", MakeRect(left + 24, 323, left + 475, 348),
                 g_fontSmall, g_enabled ? kMuted : kDisabled);
    g_modeSelectorRect = MakeRect(right - 360, 301, right - 18, 341);

    RECT screenCard{left, 368, right, 438};
    DrawGlassCard(dc, screenCard);
    const bool screenEnabled = g_enabled && g_mode != Mode::Passive;
    DrawTextLine(dc, L"Keep screen on", MakeRect(left + 24, 376, left + 300, 408), g_fontBody,
                 screenEnabled ? kText : kDisabled);
    DrawTextLine(dc, L"Prevent the display from turning off while Awake is active",
                 MakeRect(left + 24, 405, left + 520, 430), g_fontSmall, screenEnabled ? kMuted : kDisabled);
    g_screenToggleRect = MakeRect(right - 60, 389, right - 12, 417);
    DrawToggle(dc, g_screenToggleRect, g_keepDisplayOn, screenEnabled, g_hoverScreen);

    const bool hasDetails = g_enabled && (g_mode == Mode::Timed || g_mode == Mode::Expiration);
    if (hasDetails) {
        RECT detail{left, 450, right, 536};
        DrawGlassCard(dc, detail);
        if (g_mode == Mode::Timed) {
            DrawTextLine(dc, L"Duration", MakeRect(left + 24, 458, left + 200, 490), g_fontBody, kText);
            DrawTextLine(dc, L"Hours", MakeRect(left + 315, 458, left + 407, 484), g_fontSmall, kMuted);
            DrawTextLine(dc, L"Minutes", MakeRect(left + 455, 458, left + 547, 484), g_fontSmall, kMuted);
        } else {
            DrawTextLine(dc, L"Expiration", MakeRect(left + 24, 458, left + 200, 490), g_fontBody, kText);
            DrawTextLine(dc, L"Date", MakeRect(left + 270, 458, left + 448, 484), g_fontSmall, kMuted);
            DrawTextLine(dc, L"Time", MakeRect(left + 468, 458, left + 600, 484), g_fontSmall, kMuted);
        }
    }

    const int statusTop = hasDetails ? 552 : 464;
    const int noteTop = hasDetails ? 588 : 500;
    std::wstring shortStatus = StatusText();
    if (shortStatus.rfind(L"Awake - ", 0) == 0) shortStatus = shortStatus.substr(8);
    DrawStatusPill(dc, left, statusTop, shortStatus);
    DrawTextLine(dc, L"Settings save automatically. Closing this window keeps Awake running in the tray.",
                 MakeRect(left, noteTop, right, noteTop + 28), g_fontSmall, kMuted);
}

void HandleExpiration()
{
    if (!g_enabled) return;
    bool expired = false;
    if (g_mode == Mode::Timed && g_expireTick != 0 && GetTickCount64() >= g_expireTick) {
        expired = true;
    } else if (g_mode == Mode::Expiration) {
        FILETIME ft{};
        if (LocalToUtcFileTime(g_expirationLocal, ft) && FileTimeToUll(ft) <= CurrentUtcFileTime()) expired = true;
    }

    if (expired) {
        g_enabled = false;
        g_expireTick = 0;
        ApplyExecutionState();
        SaveSettings();
        UpdateControls();
        UpdateTray();
        InvalidateRect(g_hwnd, nullptr, FALSE);
    } else if (g_mode == Mode::Timed || g_mode == Mode::Expiration) {
        UpdateTray();
        InvalidateRect(g_hwnd, nullptr, FALSE);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == g_taskbarCreated && g_taskbarCreated != 0) {
        AddTrayIcon();
        return 0;
    }

    switch (msg) {
        case WM_CREATE:
            SetTimer(hwnd, TIMER_STATUS, 1000, nullptr);
            return 0;

        case WM_SIZE:
            PositionControls();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_TIMER:
            if (wParam == TIMER_STATUS) HandleExpiration();
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
            HGDIOBJ oldBitmap = SelectObject(mem, bitmap);
            DrawInterface(hwnd, mem);
            BitBlt(dc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(mem);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEMOVE: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const bool screenEnabled = g_enabled && g_mode != Mode::Passive;
            const bool newEnable = PtInRect(&g_enableToggleRect, pt) != FALSE;
            const bool newScreen = screenEnabled && PtInRect(&g_screenToggleRect, pt) != FALSE;
            if (newEnable != g_hoverEnable || newScreen != g_hoverScreen) {
                g_hoverEnable = newEnable;
                g_hoverScreen = newScreen;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (!g_trackingMouse) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                g_trackingMouse = true;
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            g_trackingMouse = false;
            if (g_hoverEnable || g_hoverMode || g_hoverScreen) {
                g_hoverEnable = g_hoverMode = g_hoverScreen = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                POINT pt{};
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                const bool screenEnabled = g_enabled && g_mode != Mode::Passive;
                if (PtInRect(&g_enableToggleRect, pt) ||
                    (screenEnabled && PtInRect(&g_screenToggleRect, pt))) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
            }
            break;

        case WM_LBUTTONUP: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (PtInRect(&g_enableToggleRect, pt)) {
                SetEnabled(!g_enabled);
                UpdateControls();
                return 0;
            }
            const bool screenEnabled = g_enabled && g_mode != Mode::Passive;
            if (screenEnabled && PtInRect(&g_screenToggleRect, pt)) {
                g_keepDisplayOn = !g_keepDisplayOn;
                ApplyExecutionState();
                SaveSettings();
                UpdateTray();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            return 0;
        }

        case WM_TRAYICON:
            if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
                ShowMainWindow();
            } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                ShowTrayMenu(hwnd);
            }
            return 0;

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (dis && dis->CtlID == IDC_MODE) {
                const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                DrawModeSelector(dis->hDC, dis->rcItem, g_enabled, pressed);
                if ((dis->itemState & ODS_FOCUS) != 0) {
                    RECT focus = dis->rcItem;
                    InflateRect(&focus, -3, -3);
                    DrawFocusRect(dis->hDC, &focus);
                }
                return TRUE;
            }
            if (dis && dis->CtlID == IDC_APPLY) {
                const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                RECT r = dis->rcItem;
                const COLORREF fill = pressed ? kAccentHover : kAccent;
                HBRUSH brush = CreateSolidBrush(fill);
                HPEN pen = CreatePen(PS_SOLID, 1, fill);
                HGDIOBJ oldBrush = SelectObject(dis->hDC, brush);
                HGDIOBJ oldPen = SelectObject(dis->hDC, pen);
                RoundRect(dis->hDC, r.left, r.top, r.right, r.bottom, 12, 12);
                SelectObject(dis->hDC, oldBrush);
                SelectObject(dis->hDC, oldPen);
                DeleteObject(brush);
                DeleteObject(pen);
                DrawTextLine(dis->hDC, L"Apply", r, g_fontBody, RGB(255, 255, 255),
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            const UINT id = LOWORD(wParam);
            const UINT code = HIWORD(wParam);
            if (id == IDC_MODE && code == BN_CLICKED) {
                ShowModeMenu(hwnd);
                return 0;
            }
            if (id == IDC_APPLY && code == BN_CLICKED) {
                ApplyEditorValues();
                return 0;
            }

            switch (id) {
                case ID_TRAY_OPEN:
                    ShowMainWindow();
                    break;
                case ID_TRAY_OFF:
                    SetEnabled(false);
                    UpdateControls();
                    break;
                case ID_TRAY_PASSIVE:
                    g_enabled = true;
                    SetMode(Mode::Passive);
                    UpdateControls();
                    break;
                case ID_TRAY_INDEFINITE:
                    g_enabled = true;
                    SetMode(Mode::Indefinite);
                    UpdateControls();
                    break;
                case ID_TRAY_30M:
                    ApplyTimedPreset(30);
                    UpdateControls();
                    break;
                case ID_TRAY_1H:
                    ApplyTimedPreset(60);
                    UpdateControls();
                    break;
                case ID_TRAY_2H:
                    ApplyTimedPreset(120);
                    UpdateControls();
                    break;
                case ID_TRAY_DISPLAY:
                    if (g_enabled && g_mode != Mode::Passive) {
                        g_keepDisplayOn = !g_keepDisplayOn;
                        ApplyExecutionState();
                        SaveSettings();
                        UpdateTray();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    break;
                case ID_TRAY_EXIT:
                    DestroyWindow(hwnd);
                    break;
            }
            return 0;
        }

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, TIMER_STATUS);
            SetThreadExecutionState(ES_CONTINUOUS);
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ParseBool(const std::wstring& value, bool& result)
{
    std::wstring v = value;
    std::transform(v.begin(), v.end(), v.begin(), ::towlower);
    if (v == L"true" || v == L"1" || v == L"yes" || v == L"on") { result = true; return true; }
    if (v == L"false" || v == L"0" || v == L"no" || v == L"off") { result = false; return true; }
    return false;
}

struct StartupOptions {
    bool showHelp = false;
    bool off = false;
    bool minimized = false;
    bool displayOnSet = false;
    bool displayOn = false;
    bool hasTimeLimit = false;
    ULONGLONG timeLimitSeconds = 0;
};

StartupOptions ParseArgs()
{
    StartupOptions opts{};
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return opts;

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--help" || arg == L"-h" || arg == L"-?") {
            opts.showHelp = true;
        } else if (arg == L"--off") {
            opts.off = true;
        } else if (arg == L"--minimized") {
            opts.minimized = true;
        } else if (arg == L"--display-on" || arg == L"-d") {
            if (i + 1 < argc) {
                bool value = false;
                if (ParseBool(argv[++i], value)) {
                    opts.displayOnSet = true;
                    opts.displayOn = value;
                }
            }
        } else if (arg.rfind(L"--display-on=", 0) == 0) {
            bool value = false;
            if (ParseBool(arg.substr(13), value)) {
                opts.displayOnSet = true;
                opts.displayOn = value;
            }
        } else if (arg == L"--time-limit" || arg == L"-t") {
            if (i + 1 < argc) {
                wchar_t* end = nullptr;
                unsigned long long value = wcstoull(argv[++i], &end, 10);
                if (end && *end == L'\0') {
                    opts.hasTimeLimit = true;
                    opts.timeLimitSeconds = value;
                }
            }
        } else if (arg.rfind(L"--time-limit=", 0) == 0) {
            std::wstring raw = arg.substr(13);
            wchar_t* end = nullptr;
            unsigned long long value = wcstoull(raw.c_str(), &end, 10);
            if (end && *end == L'\0') {
                opts.hasTimeLimit = true;
                opts.timeLimitSeconds = value;
            }
        }
    }
    LocalFree(argv);
    return opts;
}

void ShowHelp()
{
    MessageBoxW(nullptr,
        L"Awake v2.0\n\n"
        L"Usage:\n"
        L"  Awake.exe\n"
        L"  Awake.exe --minimized\n"
        L"  Awake.exe --display-on true\n"
        L"  Awake.exe --time-limit 3600\n"
        L"  Awake.exe --time-limit 3600 --display-on true\n"
        L"  Awake.exe --off\n\n"
        L"Settings can also be changed from the window or tray icon.",
        kAppName, MB_OK | MB_ICONINFORMATION);
}

void CreateFonts()
{
    g_fontTitle = CreateFontW(-32, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
    g_fontSection = CreateFontW(-19, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    g_fontBody = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
    g_fontSmall = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
}

void DeleteFonts()
{
    if (g_fontTitle) DeleteObject(g_fontTitle);
    if (g_fontSection) DeleteObject(g_fontSection);
    if (g_fontBody) DeleteObject(g_fontBody);
    if (g_fontSmall) DeleteObject(g_fontSmall);
}

void CreateChildControls(HWND hwnd, HINSTANCE instance)
{
    g_hMode = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MODE)), instance, nullptr);

    g_hHours = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1",
        WS_CHILD | ES_NUMBER | ES_CENTER | WS_TABSTOP,
        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_INTERVAL_HOURS)), instance, nullptr);
    g_hMinutes = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0",
        WS_CHILD | ES_NUMBER | ES_CENTER | WS_TABSTOP,
        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_INTERVAL_MINUTES)), instance, nullptr);
    SendMessageW(g_hHours, EM_SETLIMITTEXT, 3, 0);
    SendMessageW(g_hMinutes, EM_SETLIMITTEXT, 2, 0);

    g_hDate = CreateWindowExW(0, DATETIMEPICK_CLASSW, nullptr,
        WS_CHILD | DTS_SHORTDATEFORMAT | WS_TABSTOP,
        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EXPIRY_DATE)), instance, nullptr);
    g_hTime = CreateWindowExW(0, DATETIMEPICK_CLASSW, nullptr,
        WS_CHILD | DTS_TIMEFORMAT | WS_TABSTOP,
        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EXPIRY_TIME)), instance, nullptr);

    g_hApply = CreateWindowExW(0, L"BUTTON", L"Apply",
        WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_APPLY)), instance, nullptr);

    for (HWND control : {g_hMode, g_hHours, g_hMinutes, g_hDate, g_hTime, g_hApply}) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontBody), TRUE);
        SetWindowTheme(control, L"Explorer", nullptr);
    }
    PositionControls();
    UpdateControls();
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_DATE_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(kWindowClass, nullptr);
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(mutex);
        return 0;
    }

    LoadSettings();
    StartupOptions options = ParseArgs();
    if (options.showHelp) {
        ShowHelp();
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 0;
    }

    if (options.off) g_enabled = false;
    if (options.displayOnSet) g_keepDisplayOn = options.displayOn;
    if (options.hasTimeLimit) {
        g_enabled = true;
        g_mode = Mode::Timed;
        g_intervalHours = static_cast<DWORD>(std::min<ULONGLONG>(options.timeLimitSeconds / 3600ULL, 999ULL));
        g_intervalMinutes = static_cast<DWORD>((options.timeLimitSeconds % 3600ULL + 59ULL) / 60ULL);
        if (g_intervalMinutes >= 60) {
            if (g_intervalHours < 999) ++g_intervalHours;
            g_intervalMinutes = 0;
        }
    }
    EnsureModeTimer();

    g_appIcon = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    CreateFonts();
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kWindowClass;
    wc.hIcon = g_appIcon ? g_appIcon : LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = g_appIcon ? g_appIcon : LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    if (!RegisterClassExW(&wc)) {
        DeleteFonts();
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 2;
    }

    g_hwnd = CreateWindowExW(0, kWindowClass, L"Awake 2.0",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 940, 610,
        nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) {
        DeleteFonts();
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 3;
    }

    ApplyModernWindowVisuals(g_hwnd);
    CreateChildControls(g_hwnd, hInstance);
    AddTrayIcon();
    ApplyExecutionState();
    SaveSettings();

    if (options.minimized) {
        ShowWindow(g_hwnd, SW_HIDE);
    } else {
        ShowWindow(g_hwnd, SW_SHOW);
        UpdateWindow(g_hwnd);
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteFonts();
    if (g_appIcon) DestroyIcon(g_appIcon);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return static_cast<int>(msg.wParam);
}
