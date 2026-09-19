#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <cwchar>
#include <cwctype>
#include <algorithm>

namespace {
constexpr wchar_t kWindowClass[] = L"AwakeStandaloneWindow";
constexpr wchar_t kAppName[] = L"Awake";
constexpr wchar_t kMutexName[] = L"Local\\devbloxstrap.Awake";
constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT_PTR TIMER_STATUS = 1;
constexpr UINT ID_TRAY_OFF = 1001;
constexpr UINT ID_TRAY_INDEFINITE = 1002;
constexpr UINT ID_TRAY_DISPLAY = 1003;
constexpr UINT ID_TRAY_30M = 1004;
constexpr UINT ID_TRAY_1H = 1005;
constexpr UINT ID_TRAY_2H = 1006;
constexpr UINT ID_TRAY_EXIT = 1099;

enum class Mode { Passive, Indefinite, Timed };

HWND g_hwnd = nullptr;
NOTIFYICONDATAW g_nid{};
Mode g_mode = Mode::Passive;
bool g_keepDisplayOn = false;
ULONGLONG g_expireTick = 0;

std::wstring FormatRemaining()
{
    if (g_mode != Mode::Timed || g_expireTick == 0) return L"";
    ULONGLONG now = GetTickCount64();
    if (now >= g_expireTick) return L"0m";
    ULONGLONG seconds = (g_expireTick - now + 999) / 1000;
    ULONGLONG hours = seconds / 3600;
    ULONGLONG minutes = (seconds % 3600 + 59) / 60;
    if (hours > 0) return std::to_wstring(hours) + L"h " + std::to_wstring(minutes) + L"m";
    return std::to_wstring(minutes) + L"m";
}

std::wstring StatusText()
{
    switch (g_mode) {
        case Mode::Passive:
            return L"Awake - Off";
        case Mode::Indefinite:
            return g_keepDisplayOn
                ? L"Awake - Awake + display on"
                : L"Awake - Awake indefinitely";
        case Mode::Timed:
            return L"Awake - " + FormatRemaining() +
                   (g_keepDisplayOn ? L" (display on)" : L" remaining");
    }
    return kAppName;
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
    if (g_mode != Mode::Passive) {
        state = static_cast<EXECUTION_STATE>(state | ES_SYSTEM_REQUIRED);
        if (g_keepDisplayOn) {
            state = static_cast<EXECUTION_STATE>(state | ES_DISPLAY_REQUIRED);
        }
    }
    return SetThreadExecutionState(state) != 0;
}

void SetPassive()
{
    g_mode = Mode::Passive;
    g_keepDisplayOn = false;
    g_expireTick = 0;
    ApplyExecutionState();
    UpdateTray();
}

void SetIndefinite(bool keepDisplayOn)
{
    g_mode = Mode::Indefinite;
    g_keepDisplayOn = keepDisplayOn;
    g_expireTick = 0;
    ApplyExecutionState();
    UpdateTray();
}

void SetTimed(ULONGLONG seconds, bool keepDisplayOn)
{
    g_mode = Mode::Timed;
    g_keepDisplayOn = keepDisplayOn;
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG maxAdd = ~static_cast<ULONGLONG>(0) - now;
    const ULONGLONG requestedMs = seconds > (maxAdd / 1000ULL) ? maxAdd : seconds * 1000ULL;
    g_expireTick = now + requestedMs;
    ApplyExecutionState();
    UpdateTray();
}

void ShowTrayMenu(HWND hwnd)
{
    POINT pt{};
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING | (g_mode == Mode::Passive ? MF_CHECKED : 0), ID_TRAY_OFF, L"Off");
    AppendMenuW(menu, MF_STRING | (g_mode == Mode::Indefinite && !g_keepDisplayOn ? MF_CHECKED : 0), ID_TRAY_INDEFINITE, L"Keep awake indefinitely");
    AppendMenuW(menu, MF_STRING | (g_mode == Mode::Indefinite && g_keepDisplayOn ? MF_CHECKED : 0), ID_TRAY_DISPLAY, L"Keep awake + display on");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_30M, L"Keep awake for 30 minutes");
    AppendMenuW(menu, MF_STRING, ID_TRAY_1H, L"Keep awake for 1 hour");
    AppendMenuW(menu, MF_STRING, ID_TRAY_2H, L"Keep awake for 2 hours");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_CREATE:
            SetTimer(hwnd, TIMER_STATUS, 1000, nullptr);
            return 0;
        case WM_TIMER:
            if (wParam == TIMER_STATUS) {
                if (g_mode == Mode::Timed && GetTickCount64() >= g_expireTick) {
                    SetPassive();
                } else if (g_mode == Mode::Timed) {
                    UpdateTray();
                }
            }
            return 0;
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP || lParam == WM_CONTEXTMENU) {
                ShowTrayMenu(hwnd);
            }
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_TRAY_OFF: SetPassive(); break;
                case ID_TRAY_INDEFINITE: SetIndefinite(false); break;
                case ID_TRAY_DISPLAY: SetIndefinite(true); break;
                case ID_TRAY_30M: SetTimed(30ULL * 60ULL, false); break;
                case ID_TRAY_1H: SetTimed(60ULL * 60ULL, false); break;
                case ID_TRAY_2H: SetTimed(2ULL * 60ULL * 60ULL, false); break;
                case ID_TRAY_EXIT: DestroyWindow(hwnd); break;
            }
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

void ShowHelp()
{
    MessageBoxW(nullptr,
        L"Awake\n\n"
        L"Usage:\n"
        L"  Awake.exe\n"
        L"  Awake.exe --display-on true\n"
        L"  Awake.exe --time-limit 3600\n"
        L"  Awake.exe --time-limit 3600 --display-on true\n"
        L"  Awake.exe --off\n\n"
        L"Without arguments the app keeps the PC awake indefinitely.\n"
        L"Use the tray icon to change mode or exit.",
        kAppName, MB_OK | MB_ICONINFORMATION);
}

struct StartupOptions {
    bool showHelp = false;
    bool off = false;
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
        } else if (arg == L"--display-on" || arg == L"-d") {
            if (i + 1 < argc) {
                bool value = false;
                if (ParseBool(argv[++i], value)) opts.displayOn = value;
            }
        } else if (arg.rfind(L"--display-on=", 0) == 0) {
            bool value = false;
            if (ParseBool(arg.substr(13), value)) opts.displayOn = value;
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
} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Awake is already running. Use its tray icon.", kAppName, MB_OK | MB_ICONINFORMATION);
        CloseHandle(mutex);
        return 0;
    }

    StartupOptions options = ParseArgs();
    if (options.showHelp) {
        ShowHelp();
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 0;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kWindowClass;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc)) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 2;
    }

    g_hwnd = CreateWindowExW(0, kWindowClass, kAppName, 0, 0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 3;
    }

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(g_nid.szTip, kAppName, _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    if (options.off) {
        SetPassive();
    } else if (options.hasTimeLimit) {
        SetTimed(options.timeLimitSeconds, options.displayOn);
    } else {
        SetIndefinite(options.displayOn);
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return static_cast<int>(msg.wParam);
}
