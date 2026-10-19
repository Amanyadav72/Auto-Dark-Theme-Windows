// AutoDarkTheme.cpp - Windows 10/11, C++17, native Win32.
#define UNICODE
#define _UNICODE
#define NOMINMAX
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <cwchar>
#include <initializer_list>
#include <string>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

constexpr wchar_t Name[] = L"AutoDarkTheme";
constexpr wchar_t HostClass[] = L"AutoDarkTheme.Host";
constexpr wchar_t UIClass[] = L"AutoDarkTheme.Settings";

constexpr wchar_t AppKey[] = L"Software\\AutoDarkTheme";
constexpr wchar_t RunKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t ThemeKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT RefreshMessage = WM_APP + 2;
constexpr UINT OpenMessage = WM_APP + 3;
constexpr UINT_PTR ScheduleTimer = 1;

enum {
    LightTime = 100, DarkTime, Enable, Save, Light, Dark,
    Resume, Startup, Open, Close, Quit, Status
};

struct Settings {
    DWORD version = 1;
    DWORD light = 420;   // 07:00
    DWORD dark = 1140;   // 19:00
    DWORD enabled = 0;
};

Settings config;
HINSTANCE instance;
HWND host = nullptr;
HWND ui = nullptr;
HFONT font = nullptr;
HICON icon = nullptr;
UINT taskbarCreated = 0;
int dpi = 96;
bool trayPresent = false;
NOTIFYICONDATAW tray{};
std::wstring errorText;
std::wstring launchCommand;

// Works for both normal and overnight light schedules.
constexpr bool LightAt(int minute, int light, int dark) {
    return (minute - light + 1440) % 1440 <
           (dark - light + 1440) % 1440;
}

constexpr int NextDelay(int second, int light, int dark) {
    int a = (light * 60 - second + 86400) % 86400;
    int b = (dark * 60 - second + 86400) % 86400;

    if (!a) a = 86400;
    if (!b) b = 86400;

    return a < b ? a : b;
}

static_assert(LightAt(420, 420, 1140));
static_assert(!LightAt(1140, 420, 1140));
static_assert(LightAt(0, 1140, 420));
static_assert(!LightAt(420, 1140, 420));
static_assert(NextDelay(419 * 60 + 59, 420, 1140) == 1);
static_assert(NextDelay(420 * 60, 420, 1140) == 43200);

int Scale(int n) {
    return MulDiv(n, dpi, 96);
}

void ShowSettings();
void Refresh();
void Command(int id);

std::wstring Error(DWORD code) {
    wchar_t text[512]{};

    if (!FormatMessageW(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, code, 0, text, 512, nullptr)) {
        return L"Windows error " + std::to_wstring(code);
    }

    return text;
}

void Alert(const std::wstring& text) {
    MessageBoxW(
        ui ? ui : host, text.c_str(), Name, MB_OK | MB_ICONERROR
    );
}

std::wstring Clock(DWORD minute) {
    wchar_t text[6]{};

    swprintf_s(
        text, L"%02u:%02u",
        static_cast<unsigned>(minute / 60),
        static_cast<unsigned>(minute % 60)
    );

    return text;
}

DWORD ReadDWORD(const wchar_t* name) {
    DWORD value = 2;
    DWORD bytes = sizeof(value);

    if (RegGetValueW(
            HKEY_CURRENT_USER, ThemeKey, name, RRF_RT_REG_DWORD,
            nullptr, &value, &bytes) != ERROR_SUCCESS) {
        return 2;
    }

    return value;
}

std::wstring ThemeName() {
    DWORD a = ReadDWORD(L"AppsUseLightTheme");
    DWORD b = ReadDWORD(L"SystemUsesLightTheme");

    if (a == 0 && b == 0) return L"Dark";
    if (a == 1 && b == 1) return L"Light";

    return L"Custom / unknown";
}

bool StartupEnabled() {
    wchar_t value[32768]{};
    DWORD bytes = sizeof(value);

    return RegGetValueW(
        HKEY_CURRENT_USER, RunKey, Name, RRF_RT_REG_SZ,
        nullptr, value, &bytes
    ) == ERROR_SUCCESS && launchCommand == value;
}

bool SetStartup(bool enabled) {
    if (enabled && launchCommand.size() > 260) {
        Alert(
            L"The executable path is too long for Windows startup. "
            L"Move the app to a shorter folder path first."
        );
        return false;
    }

    HKEY key{};

    LSTATUS result = RegCreateKeyExW(
        HKEY_CURRENT_USER, RunKey, 0, nullptr,
        0, KEY_SET_VALUE, nullptr, &key, nullptr
    );

    if (result == ERROR_SUCCESS) {
        if (enabled) {
            result = RegSetValueExW(
                key, Name, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(launchCommand.c_str()),
                static_cast<DWORD>(
                    (launchCommand.size() + 1) * sizeof(wchar_t)
                )
            );
        } else {
            result = RegDeleteValueW(key, Name);
        }

        RegCloseKey(key);

        if (!enabled && result == ERROR_FILE_NOT_FOUND) {
            result = ERROR_SUCCESS;
        }
    }

    if (result != ERROR_SUCCESS) Alert(Error(result));

    return result == ERROR_SUCCESS;
}

void LoadSettings() {
    Settings saved;
    DWORD bytes = sizeof(saved);

    LSTATUS result = RegGetValueW(
        HKEY_CURRENT_USER, AppKey, L"Settings",
        RRF_RT_REG_BINARY, nullptr, &saved, &bytes
    );

    if (result == ERROR_FILE_NOT_FOUND) return;

    if (result == ERROR_SUCCESS &&
        bytes == sizeof(saved) &&
        saved.version == 1 &&
        saved.light < 1440 &&
        saved.dark < 1440 &&
        saved.light != saved.dark &&
        saved.enabled <= 1) {
        config = saved;
    } else {
        MessageBoxW(
            nullptr,
            L"Saved settings could not be loaded. "
            L"Automatic switching is off.",
            Name, MB_OK | MB_ICONWARNING
        );
    }
}

bool StoreSettings(const Settings& next) {
    HKEY key{};

    LSTATUS result = RegCreateKeyExW(
        HKEY_CURRENT_USER, AppKey, 0, nullptr,
        0, KEY_SET_VALUE, nullptr, &key, nullptr
    );

    if (result == ERROR_SUCCESS) {
        result = RegSetValueExW(
            key, L"Settings", 0, REG_BINARY,
            reinterpret_cast<const BYTE*>(&next),
            static_cast<DWORD>(sizeof(next))
        );
        RegCloseKey(key);
    }

    if (result != ERROR_SUCCESS) {
        Alert(Error(result));
        return false;
    }

    config = next;

    if (ui) {
        CheckDlgButton(
            ui, Enable,
            config.enabled ? BST_CHECKED : BST_UNCHECKED
        );
    }

    return true;
}

bool ApplyTheme(bool light) {
    DWORD value = light ? 1 : 0;

    if (ReadDWORD(L"AppsUseLightTheme") == value &&
        ReadDWORD(L"SystemUsesLightTheme") == value) {
        return true;
    }

    HKEY key{};

    LSTATUS result = RegCreateKeyExW(
        HKEY_CURRENT_USER, ThemeKey, 0, nullptr,
        0, KEY_SET_VALUE, nullptr, &key, nullptr
    );

    if (result == ERROR_SUCCESS) {
        for (auto name : {
            L"AppsUseLightTheme",
            L"SystemUsesLightTheme"
        }) {
            result = RegSetValueExW(
                key, name, 0, REG_DWORD,
                reinterpret_cast<const BYTE*>(&value), sizeof(value)
            );

            if (result != ERROR_SUCCESS) break;
        }

        RegCloseKey(key);
    }

    // Refresh any preference that was successfully written.
    DWORD_PTR ignored{};

    SendMessageTimeoutW(
        HWND_BROADCAST, WM_SETTINGCHANGE, 0,
        reinterpret_cast<LPARAM>(L"ImmersiveColorSet"),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &ignored
    );

    if (result != ERROR_SUCCESS) {
        errorText = L"Could not change theme: " + Error(result);
        return false;
    }

    return true;
}

void UpdateStatus() {
    std::wstring text;

    if (!errorText.empty()) {
        text = errorText;
    } else {
        text = L"Windows theme: " + ThemeName();

        if (config.enabled) {
            SYSTEMTIME now{};
            GetLocalTime(&now);

            bool light = LightAt(
                now.wHour * 60 + now.wMinute,
                config.light, config.dark
            );

            text += L"\r\nAutomatic | Next: ";
            text += light ? L"Dark at " : L"Light at ";
            text += Clock(light ? config.dark : config.light);
        } else {
            text += L"\r\nSchedule paused";
        }
    }

    if (ui) SetDlgItemTextW(ui, Status, text.c_str());

    if (trayPresent) {
        wcsncpy_s(tray.szTip, text.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &tray);
    }
}

void Refresh() {
    KillTimer(host, ScheduleTimer);
    errorText.clear();

    if (config.enabled) {
        SYSTEMTIME now{};
        GetLocalTime(&now);

        bool target = LightAt(
            now.wHour * 60 + now.wMinute,
            config.light, config.dark
        );

        ApplyTheme(target);

        // Broadcasting can take time. Recheck if a boundary passed.
        GetLocalTime(&now);

        if (target != LightAt(
                now.wHour * 60 + now.wMinute,
                config.light, config.dark)) {
            PostMessageW(host, RefreshMessage, 0, 0);
            UpdateStatus();
            return;
        }

        int seconds =
            now.wHour * 3600 + now.wMinute * 60 + now.wSecond;

        UINT delay = static_cast<UINT>(
            NextDelay(seconds, config.light, config.dark) * 1000 -
            now.wMilliseconds
        );

        if (!SetTimer(host, ScheduleTimer, delay, nullptr)) {
            errorText =
                L"Could not start schedule timer: " + Error(GetLastError());
        }
    }

    UpdateStatus();
}

HICON MakeIcon() {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 32;
    info.bmiHeader.biHeight = -32;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;

    DWORD* pixels = nullptr;

    HBITMAP color = CreateDIBSection(
        nullptr, &info, DIB_RGB_COLORS,
        reinterpret_cast<void**>(&pixels), nullptr, 0
    );

    if (!color) return nullptr;

    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            bool disc =
                (x - 16) * (x - 16) + (y - 16) * (y - 16) <= 169;
            bool cut =
                (x - 23) * (x - 23) + (y - 9) * (y - 9) <= 121;

            pixels[y * 32 + x] = disc && !cut ? 0xFFA5B4FC : 0;
        }
    }

    BYTE maskBits[128]{};
    HBITMAP mask = CreateBitmap(32, 32, 1, 1, maskBits);

    ICONINFO iconInfo{};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmMask = mask;
    iconInfo.hbmColor = color;

    HICON result = mask ? CreateIconIndirect(&iconInfo) : nullptr;

    if (mask) DeleteObject(mask);
    DeleteObject(color);

    return result;
}

bool AddTray() {
    tray.cbSize = sizeof(tray);
    tray.hWnd = host;
    tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    tray.uCallbackMessage = TrayMessage;
    tray.hIcon = icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);

    wcscpy_s(tray.szTip, Name);

    trayPresent = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;

    if (trayPresent) {
        tray.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &tray);
        UpdateStatus();
    }

    return trayPresent;
}

void TrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, Open, L"Open settings");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, Light, L"Use light (pause schedule)");
    AppendMenuW(menu, MF_STRING, Dark, L"Use dark (pause schedule)");
    AppendMenuW(menu, MF_STRING, Resume, L"Resume saved schedule");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, Quit, L"Quit");
    SetMenuDefaultItem(menu, Open, FALSE);

    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(host);

    int choice = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        point.x, point.y, 0, host, nullptr
    );

    DestroyMenu(menu);
    PostMessageW(host, WM_NULL, 0, 0);

    if (choice) Command(choice);
}

HWND Control(
    const wchar_t* cls, const wchar_t* text, DWORD style,
    int x, int y, int w, int h, int id = 0
) {
    HWND control = CreateWindowExW(
        0, cls, text, WS_CHILD | WS_VISIBLE | style,
        Scale(x), Scale(y), Scale(w), Scale(h), ui,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        instance, nullptr
    );

    SendMessageW(
        control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE
    );

    return control;
}

void SetTimeControl(int id, DWORD minute) {
    SYSTEMTIME time{};
    GetLocalTime(&time);

    time.wHour = static_cast<WORD>(minute / 60);
    time.wMinute = static_cast<WORD>(minute % 60);
    time.wSecond = time.wMilliseconds = 0;

    HWND control = GetDlgItem(ui, id);
    DateTime_SetFormat(control, L"HH:mm");
    DateTime_SetSystemtime(control, GDT_VALID, &time);
}

void ShowSettings() {
    if (!ui) {
        DWORD style =
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

        RECT rect{0, 0, Scale(480), Scale(440)};
        AdjustWindowRectEx(&rect, style, FALSE, WS_EX_APPWINDOW);

        ui = CreateWindowExW(
            WS_EX_APPWINDOW, UIClass, Name, style,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left, rect.bottom - rect.top,
            nullptr, nullptr, instance, nullptr
        );

        if (!ui) {
            Alert(Error(GetLastError()));
            return;
        }

        font = CreateFontW(
            -Scale(14), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"
        );

        Control(
            L"STATIC", L"Automatic light and dark themes",
            0, 24, 18, 430, 24
        );
        Control(L"STATIC", L"", 0, 24, 52, 430, 56, Status);

        Control(L"STATIC", L"Light starts", 0, 24, 116, 180, 22);
        Control(L"STATIC", L"Dark starts", 0, 252, 116, 180, 22);

        Control(
            DATETIMEPICK_CLASSW, L"",
            DTS_TIMEFORMAT | DTS_UPDOWN | WS_TABSTOP,
            24, 140, 190, 28, LightTime
        );
        Control(
            DATETIMEPICK_CLASSW, L"",
            DTS_TIMEFORMAT | DTS_UPDOWN | WS_TABSTOP,
            252, 140, 190, 28, DarkTime
        );

        SetTimeControl(LightTime, config.light);
        SetTimeControl(DarkTime, config.dark);

        Control(
            L"BUTTON", L"Enable automatic switching",
            BS_AUTOCHECKBOX | WS_TABSTOP,
            24, 184, 300, 24, Enable
        );
        Control(
            L"BUTTON", L"Save schedule",
            BS_DEFPUSHBUTTON | WS_TABSTOP,
            24, 218, 418, 34, Save
        );
        Control(
            L"BUTTON", L"Use light now",
            WS_TABSTOP, 24, 264, 200, 34, Light
        );
        Control(
            L"BUTTON", L"Use dark now",
            WS_TABSTOP, 242, 264, 200, 34, Dark
        );
        Control(
            L"STATIC", L"Manual switching pauses the schedule.",
            0, 24, 308, 420, 24
        );
        Control(
            L"BUTTON", L"Start when I sign in to Windows",
            BS_AUTOCHECKBOX | WS_TABSTOP,
            24, 340, 420, 24, Startup
        );
        Control(
            L"STATIC",
            L"Local time. Close keeps the app in the system tray.",
            0, 24, 372, 430, 24
        );
        Control(
            L"BUTTON", L"Quit app",
            WS_TABSTOP, 24, 402, 200, 28, Quit
        );
        Control(
            L"BUTTON", L"Close",
            WS_TABSTOP, 242, 402, 200, 28, Close
        );

        CheckDlgButton(
            ui, Enable,
            config.enabled ? BST_CHECKED : BST_UNCHECKED
        );
    }

    CheckDlgButton(
        ui, Startup,
        StartupEnabled() ? BST_CHECKED : BST_UNCHECKED
    );

    UpdateStatus();
    ShowWindow(ui, SW_RESTORE);
    SetForegroundWindow(ui);
}

void Command(int id) {
    if (id == Open) {
        ShowSettings();
        return;
    }

    if (id == Close) {
        SendMessageW(ui, WM_CLOSE, 0, 0);
        return;
    }

    if (id == Quit) {
        DestroyWindow(host);
        return;
    }

    if (id == Startup) {
        SetStartup(IsDlgButtonChecked(ui, Startup) == BST_CHECKED);

        CheckDlgButton(
            ui, Startup,
            StartupEnabled() ? BST_CHECKED : BST_UNCHECKED
        );
        return;
    }

    Settings next = config;

    if (id == Save) {
        SYSTEMTIME light{}, dark{};

        if (DateTime_GetSystemtime(
                GetDlgItem(ui, LightTime), &light) != GDT_VALID ||
            DateTime_GetSystemtime(
                GetDlgItem(ui, DarkTime), &dark) != GDT_VALID) {
            Alert(L"Enter valid start times.");
            return;
        }

        next.light = light.wHour * 60 + light.wMinute;
        next.dark = dark.wHour * 60 + dark.wMinute;

        if (next.light == next.dark) {
            Alert(L"Light and dark start times must be different.");
            return;
        }

        next.enabled =
            IsDlgButtonChecked(ui, Enable) == BST_CHECKED;
    } else if (id == Resume) {
        next.enabled = 1;
    } else if (id == Light || id == Dark) {
        next.enabled = 0;
    } else {
        return;
    }

    if (!StoreSettings(next)) return;

    Refresh();

    if (id == Light || id == Dark) {
        ApplyTheme(id == Light);
        UpdateStatus();
    }

    if (!errorText.empty()) Alert(errorText);
}

LRESULT CALLBACK SettingsProc(
    HWND window, UINT msg, WPARAM wp, LPARAM lp
) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            Command(Save);
        } else if (LOWORD(wp) == IDCANCEL) {
            SendMessageW(window, WM_CLOSE, 0, 0);
        } else if (HIWORD(wp) == BN_CLICKED) {
            Command(LOWORD(wp));
        }
        return 0;

    case WM_CLOSE:
        if (!trayPresent && !AddTray()) {
            if (MessageBoxW(
                    window,
                    L"The tray is unavailable. Quit the app?",
                    Name, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                DestroyWindow(host);
            }
        } else {
            DestroyWindow(window);
        }
        return 0;

    case WM_NCDESTROY:
        ui = nullptr;
        if (font) {
            DeleteObject(font);
            font = nullptr;
        }
        break;
    }

    return DefWindowProcW(window, msg, wp, lp);
}

LRESULT CALLBACK HostProc(
    HWND window, UINT msg, WPARAM wp, LPARAM lp
) {
    if (taskbarCreated && msg == taskbarCreated) {
        trayPresent = false;
        if (!AddTray()) ShowSettings();
        return 0;
    }

    switch (msg) {
    case TrayMessage: {
        UINT event = LOWORD(lp);

        if (event == WM_CONTEXTMENU) {
            TrayMenu();
        } else if (event == NIN_SELECT || event == NIN_KEYSELECT) {
            ShowSettings();
        }
        return 0;
    }

    case OpenMessage:
        ShowSettings();
        return 0;

    case RefreshMessage:
        Refresh();
        return 0;

    case WM_TIMER:
        if (wp == ScheduleTimer) Refresh();
        return 0;

    case WM_TIMECHANGE:
        PostMessageW(window, RefreshMessage, 0, 0);
        return 0;

    case WM_SETTINGCHANGE:
        if (lp &&
            (!wcscmp(reinterpret_cast<LPCWSTR>(lp), L"intl") ||
             !wcscmp(
                 reinterpret_cast<LPCWSTR>(lp),
                 L"TimeZoneInformation"))) {
            PostMessageW(window, RefreshMessage, 0, 0);
        }
        return 0;

    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC ||
            wp == PBT_APMRESUMESUSPEND) {
            PostMessageW(window, RefreshMessage, 0, 0);
        }
        return TRUE;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (wp) DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        KillTimer(window, ScheduleTimer);

        if (trayPresent) {
            Shell_NotifyIconW(NIM_DELETE, &tray);
        }

        if (ui) DestroyWindow(ui);

        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE app, HINSTANCE, PWSTR, int) {
    instance = app;
    SetProcessDPIAware();
    dpi = static_cast<int>(GetDpiForSystem());

    HANDLE mutex = CreateMutexW(
        nullptr, FALSE, L"Local\\AutoDarkTheme.Aman.v1"
    );
    DWORD mutexError = GetLastError();

    if (!mutex) {
        Alert(Error(mutexError));
        return 1;
    }

    if (mutexError == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(HostClass, nullptr);

        if (existing) {
            PostMessageW(existing, OpenMessage, 0, 0);
        } else {
            MessageBoxW(
                nullptr,
                L"AutoDarkTheme is already running. "
                L"Open it from the system tray.",
                Name, MB_OK
            );
        }

        CloseHandle(mutex);
        return 0;
    }

    wchar_t path[32768]{};
    DWORD length = GetModuleFileNameW(nullptr, path, 32768);

    if (!length || length >= 32768) {
        Alert(L"Could not find the executable path.");
        CloseHandle(mutex);
        return 1;
    }

    launchCommand = L"\"" + std::wstring(path) + L"\" --tray";

    LoadSettings();

    INITCOMMONCONTROLSEX controls{
        sizeof(controls), ICC_DATE_CLASSES
    };
    InitCommonControlsEx(&controls);

    icon = MakeIcon();

    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = HostClass;
    wc.lpfnWndProc = HostProc;

    if (!RegisterClassW(&wc)) {
        Alert(Error(GetLastError()));
        if (icon) DestroyIcon(icon);
        CloseHandle(mutex);
        return 1;
    }

    wc.lpszClassName = UIClass;
    wc.lpfnWndProc = SettingsProc;

    if (!RegisterClassW(&wc)) {
        Alert(Error(GetLastError()));
        if (icon) DestroyIcon(icon);
        CloseHandle(mutex);
        return 1;
    }

    host = CreateWindowExW(
        0, HostClass, Name, WS_OVERLAPPED,
        0, 0, 0, 0,
        nullptr, nullptr, instance, nullptr
    );

    if (!host) {
        Alert(Error(GetLastError()));
        if (icon) DestroyIcon(icon);
        CloseHandle(mutex);
        return 1;
    }

    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    AddTray();
    Refresh();

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    bool quiet =
        argc == 2 && argv && !wcscmp(argv[1], L"--tray");

    if (argv) LocalFree(argv);

    if (!quiet || !trayPresent || !errorText.empty()) {
        ShowSettings();
    }

    MSG message{};
    BOOL received;

    while ((received = GetMessageW(
                &message, nullptr, 0, 0)) > 0) {
        if (!ui || !IsDialogMessageW(ui, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (icon) DestroyIcon(icon);
    CloseHandle(mutex);

    return received == -1 ? 1 : static_cast<int>(message.wParam);
}