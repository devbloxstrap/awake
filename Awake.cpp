#define UNICODE
#define _UNICODE

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <string>
#include <cwchar>
#include <cwctype>
#include <algorithm>
#include <iterator>
#include <limits>

#include "resource.h"

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
HWND g_hMode = nullptr;
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
UINT g_taskbarCreated = 0;

COLORREF kBackground = RGB(247, 249, 252);
COLORREF kSidebar = RGB(242, 246, 250);
COLORREF kCard = RGB(255, 255, 255);
COLORREF kBorder = RGB(218, 224, 232);
COLORREF kText = RGB(30, 35, 44);
COLORREF kMuted = RGB(105, 113, 126);
COLORREF kAccent = RGB(0, 120, 212);
COLORREF kDisabled = RGB(170, 176, 186);

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

void DrawRoundedCard(HDC dc, const RECT& r)
{
    HBRUSH brush = CreateSolidBrush(kCard);
    HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, 12, 12);
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

void DrawToggle(HDC dc, const RECT& r, bool on, bool enabled)
{
    // Compact Windows-style switch with a subtle outline in the off state.
    const COLORREF trackFill = !enabled ? RGB(224, 228, 234)
                              : on ? kAccent
                                   : RGB(244, 246, 249);
    const COLORREF trackBorder = !enabled ? RGB(201, 206, 214)
                                : on ? kAccent
                                     : RGB(145, 153, 165);

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
    const COLORREF knobFill = enabled ? RGB(255, 255, 255) : RGB(246, 247, 249);
    const COLORREF knobBorder = enabled ? RGB(205, 210, 217) : RGB(218, 222, 227);
    HBRUSH knobBrush = CreateSolidBrush(knobFill);
    HPEN knobPen = CreatePen(PS_SOLID, 1, knobBorder);
    oldBrush = SelectObject(dc, knobBrush);
    oldPen = SelectObject(dc, knobPen);
    Ellipse(dc, knob.left, knob.top, knob.right, knob.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(knobBrush);
    DeleteObject(knobPen);
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

void PositionControls()
{
    RECT client{};
    GetClientRect(g_hwnd, &client);
    const int contentLeft = 48;
    const int right = client.right - 48;
    const int comboWidth = 360;
    const int comboX = std::max(contentLeft + 420, right - comboWidth - 18);
    SetWindowPos(g_hMode, nullptr, comboX, 326, comboWidth, 220, SWP_NOZORDER);

    SetWindowPos(g_hHours, nullptr, contentLeft + 330, 518, 90, 30, SWP_NOZORDER);
    SetWindowPos(g_hMinutes, nullptr, contentLeft + 485, 518, 90, 30, SWP_NOZORDER);
    SetWindowPos(g_hDate, nullptr, contentLeft + 275, 518, 180, 30, SWP_NOZORDER);
    SetWindowPos(g_hTime, nullptr, contentLeft + 475, 518, 135, 30, SWP_NOZORDER);
    SetWindowPos(g_hApply, nullptr, right - 96, 516, 78, 32, SWP_NOZORDER);
}

void UpdateControls()
{
    if (!g_hMode) return;
    SendMessageW(g_hMode, CB_SETCURSEL, static_cast<WPARAM>(g_mode), 0);
    EnableWindow(g_hMode, g_enabled ? TRUE : FALSE);

    const bool timed = g_enabled && g_mode == Mode::Timed;
    const bool expiration = g_enabled && g_mode == Mode::Expiration;
    ShowWindow(g_hHours, timed ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hMinutes, timed ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hDate, expiration ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hTime, expiration ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hApply, (timed || expiration) ? SW_SHOW : SW_HIDE);
    SyncTextFields();
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

void DrawInterface(HWND hwnd, HDC dc)
{
    RECT client{};
    GetClientRect(hwnd, &client);
    FillSolid(dc, client, kBackground);

    // Simple single-page header. Non-functional navigation items were removed.
    RECT header{0, 0, client.right, 78};
    FillSolid(dc, header, RGB(255, 255, 255));
    HPEN separator = CreatePen(PS_SOLID, 1, kBorder);
    HGDIOBJ oldPen = SelectObject(dc, separator);
    MoveToEx(dc, 0, 77, nullptr);
    LineTo(dc, client.right, 77);
    SelectObject(dc, oldPen);
    DeleteObject(separator);

    if (g_appIcon) DrawIconEx(dc, 38, 22, g_appIcon, 34, 34, 0, nullptr, DI_NORMAL);
    DrawTextLine(dc, L"Awake", MakeRect(86, 14, 250, 62), g_fontSection, kText);
    DrawTextLine(dc, L"v2.0.0", MakeRect(client.right - 130, 18, client.right - 42, 58), g_fontSmall, kMuted,
                 DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    const int left = 48;
    const int right = client.right - 48;
    DrawTextLine(dc, L"Awake", MakeRect(left, 96, right, 142), g_fontTitle, kText);
    DrawTextLine(dc, L"Keep your PC awake when you need it.", MakeRect(left, 140, right, 170), g_fontBody, kMuted);

    RECT activation{left, 182, right, 258};
    DrawRoundedCard(dc, activation);
    if (g_appIcon) DrawIconEx(dc, left + 22, 201, g_appIcon, 36, 36, 0, nullptr, DI_NORMAL);
    DrawTextLine(dc, L"Awake", MakeRect(left + 74, 192, left + 245, 246), g_fontSection, kText);
    DrawTextLine(dc, g_enabled ? L"On" : L"Off", MakeRect(right - 150, 192, right - 78, 246),
                 g_fontBody, g_enabled ? kText : kMuted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    g_enableToggleRect = MakeRect(right - 62, 206, right - 14, 234);
    DrawToggle(dc, g_enableToggleRect, g_enabled, true);

    DrawTextLine(dc, L"Behavior", MakeRect(left, 274, right, 310), g_fontSection, kText);

    RECT modeCard{left, 310, right, 386};
    DrawRoundedCard(dc, modeCard);
    DrawTextLine(dc, L"Mode", MakeRect(left + 28, 316, left + 220, 350), g_fontBody, g_enabled ? kText : kDisabled);
    DrawTextLine(dc, L"Choose how Awake should keep your device active", MakeRect(left + 28, 346, left + 500, 374),
                 g_fontSmall, g_enabled ? kMuted : kDisabled);

    RECT screenCard{left, 398, right, 474};
    DrawRoundedCard(dc, screenCard);
    const bool screenEnabled = g_enabled && g_mode != Mode::Passive;
    DrawTextLine(dc, L"Keep screen on", MakeRect(left + 28, 404, left + 300, 438), g_fontBody,
                 screenEnabled ? kText : kDisabled);
    DrawTextLine(dc, L"Prevent the display from turning off while Awake is active", MakeRect(left + 28, 434, left + 520, 462),
                 g_fontSmall, screenEnabled ? kMuted : kDisabled);
    g_screenToggleRect = MakeRect(right - 62, 422, right - 14, 450);
    DrawToggle(dc, g_screenToggleRect, g_keepDisplayOn, screenEnabled);

    if (g_enabled && (g_mode == Mode::Timed || g_mode == Mode::Expiration)) {
        RECT detail{left, 486, right, 568};
        DrawRoundedCard(dc, detail);
        if (g_mode == Mode::Timed) {
            DrawTextLine(dc, L"Duration", MakeRect(left + 28, 494, left + 200, 524), g_fontBody, kText);
            DrawTextLine(dc, L"Hours", MakeRect(left + 330, 488, left + 420, 516), g_fontSmall, kMuted);
            DrawTextLine(dc, L"Minutes", MakeRect(left + 485, 488, left + 575, 516), g_fontSmall, kMuted);
        } else {
            DrawTextLine(dc, L"Expiration", MakeRect(left + 28, 494, left + 200, 524), g_fontBody, kText);
            DrawTextLine(dc, L"Date", MakeRect(left + 275, 488, left + 455, 516), g_fontSmall, kMuted);
            DrawTextLine(dc, L"Time", MakeRect(left + 475, 488, left + 610, 516), g_fontSmall, kMuted);
        }
    }

    std::wstring status = L"Status: " + StatusText().substr(8);
    DrawTextLine(dc, status, MakeRect(left, 590, right, 620), g_fontSmall, kMuted);
    DrawTextLine(dc, L"Settings are saved automatically. Closing this window keeps Awake running in the tray.",
                 MakeRect(left, 620, right, 650), g_fontSmall, kMuted);
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

        case WM_COMMAND: {
            const UINT id = LOWORD(wParam);
            const UINT code = HIWORD(wParam);
            if (id == IDC_MODE && code == CBN_SELCHANGE) {
                int index = static_cast<int>(SendMessageW(g_hMode, CB_GETCURSEL, 0, 0));
                if (index >= 0 && index <= static_cast<int>(Mode::Expiration)) {
                    SetMode(static_cast<Mode>(index));
                    UpdateControls();
                }
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
    g_fontTitle = CreateFontW(-34, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontSection = CreateFontW(-19, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontBody = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontSmall = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
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
    g_hMode = CreateWindowExW(0, WC_COMBOBOXW, nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MODE)), instance, nullptr);
    SendMessageW(g_hMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Keep using the selected power plan"));
    SendMessageW(g_hMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Keep awake indefinitely"));
    SendMessageW(g_hMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Keep awake for a time interval"));
    SendMessageW(g_hMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Keep awake until expiration"));

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
        WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_APPLY)), instance, nullptr);

    for (HWND control : {g_hMode, g_hHours, g_hMinutes, g_hDate, g_hTime, g_hApply}) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontBody), TRUE);
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
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassExW(&wc)) {
        DeleteFonts();
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 2;
    }

    g_hwnd = CreateWindowExW(0, kWindowClass, L"Awake 2.0",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 980, 700,
        nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) {
        DeleteFonts();
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 3;
    }

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
