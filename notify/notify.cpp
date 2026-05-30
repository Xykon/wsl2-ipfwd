// wsl2ipfwd-notify.exe — minimal notification helper
//
// Usage (new port):
//   wsl2ipfwd-notify.exe <port> "<gui_exe_path>"
//
// Usage (update available):
//   wsl2ipfwd-notify.exe --update <version> "<gui_exe_path>"
//
// Shows a Windows tray balloon notification.
// Clicking the balloon launches the GUI and exits.
// On timeout (balloon dismissed) it exits cleanly.
// Pure Win32 — no external libraries.

#include <windows.h>
#include <shellapi.h>
#include <string>

#define WM_TRAY   (WM_USER + 1)
#define IDT_EXIT   1
#define TIMEOUT_MS 12000

static std::wstring       s_guiPath;
static NOTIFYICONDATAW    s_nid = {};
static bool               s_iconAdded = false;

static void Cleanup(HWND hwnd) {
    if (s_iconAdded) {
        Shell_NotifyIconW(NIM_DELETE, &s_nid);
        s_iconAdded = false;
    }
    KillTimer(hwnd, IDT_EXIT);
    PostQuitMessage(0);
}

static void LaunchGui() {
    if (!s_guiPath.empty())
        ShellExecuteW(nullptr, L"open", s_guiPath.c_str(),
                      nullptr, nullptr, SW_SHOWNORMAL);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TRAY:
        switch (LOWORD(lp)) {
        case NIN_BALLOONUSERCLICK:
            LaunchGui();
            Cleanup(hwnd);
            break;
        case NIN_BALLOONTIMEOUT:
        case NIN_BALLOONHIDE:
            Cleanup(hwnd);
            break;
        }
        break;
    case WM_TIMER:
        if (wp == IDT_EXIT) Cleanup(hwnd);
        break;
    case WM_DESTROY:
        Cleanup(hwnd);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;

    bool         isUpdate = false;
    std::wstring notifyParam;  // port number (new-port mode) or version string (update mode)

    if (argc >= 4 && std::wstring(argv[1]) == L"--update") {
        isUpdate    = true;
        notifyParam = argv[2];
        s_guiPath   = argv[3];
    } else if (argc >= 3) {
        isUpdate    = false;
        notifyParam = argv[1];
        s_guiPath   = argv[2];
    } else {
        LocalFree(argv);
        return 1;
    }
    LocalFree(argv);

    // Message-only window (no taskbar entry, no visible frame)
    WNDCLASSW wc      = {};
    wc.lpfnWndProc    = WndProc;
    wc.hInstance      = hInst;
    wc.lpszClassName  = L"wsl2ipfwdNotifyWnd";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, L"wsl2ipfwdNotifyWnd", L"",
                                 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    // Build balloon body text
    std::wstring body;
    if (isUpdate) {
        body = L"Version " + notifyParam + L" is available.\n"
               L"Click here to open WSL2 IP Forwarder and install the update.";
    } else {
        body = L"New port " + notifyParam +
               L" is listening in WSL2 but has no forwarding rule.\n"
               L"Click here to open WSL2 IP Forwarder and configure it.";
    }
    if (body.size() > 255) body.resize(255);

    s_nid.cbSize           = sizeof(s_nid);
    s_nid.hWnd             = hwnd;
    s_nid.uID              = 1;
    s_nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_INFO;
    s_nid.uCallbackMessage = WM_TRAY;
    s_nid.dwInfoFlags      = isUpdate ? NIIF_INFO : NIIF_INFO;
    s_nid.hIcon            = LoadIconW(nullptr, IDI_INFORMATION);
    wcscpy_s(s_nid.szTip,       L"WSL2 IP Forwarder");
    wcscpy_s(s_nid.szInfoTitle, L"WSL2 IP Forwarder");
    wcscpy_s(s_nid.szInfo,      body.c_str());

    if (!Shell_NotifyIconW(NIM_ADD, &s_nid)) {
        DestroyWindow(hwnd);
        return 1;
    }
    s_iconAdded = true;

    // NOTIFYICON_VERSION_4 delivers NIN_BALLOON* messages reliably
    s_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &s_nid);

    SetTimer(hwnd, IDT_EXIT, TIMEOUT_MS + 2000, nullptr);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
