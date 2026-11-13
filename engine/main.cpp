// Driftwall Engine: draws live wallpapers behind the desktop icons, one renderer per monitor.
// Runs in the tray with no UI; the Gallery app edits config.json and posts WM_APP_RELOAD.
#include "engine.h"
#include <dwmapi.h>
#include <mfapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shobjidl.h>
#include <wtsapi32.h>
#include <algorithm>
#include <vector>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace {

const UINT WM_APP_RELOAD = WM_APP + 1;  // sent by the Gallery after writing config.json
const UINT WM_APP_EXIT = WM_APP + 2;
const UINT WM_TRAY = WM_APP + 10;
const UINT_PTR TIMER_COVER = 1, TIMER_REBUILD = 2;
const UINT ID_OPEN = 1, ID_PAUSE = 2, ID_EXIT = 3;

const CLSID CLSID_DesktopWallpaperObj = {0xC2CF3110, 0x460E, 0x4fc1, {0xB9, 0xD0, 0x8A, 0x1C, 0x0C, 0x9C, 0xC4, 0xBD}};

struct Slot {
    Monitor mon;
    std::string wallpaperId;
    std::string imageApplied;  // image wallpapers are handed to Windows itself (no window, no memory)
    std::unique_ptr<Renderer> renderer;
    HWND parent = nullptr;
    bool covered = false, paused = false;
};

std::wstring g_dataDir, g_libraryDir;
Gfx g_gfx;
HWND g_msgWnd = nullptr, g_parent = nullptr;
std::vector<Slot> g_slots;
EngineSettings g_settings;
Json g_config;
bool g_pauseWhenCovered = true, g_userPaused = false, g_locked = false, g_displayOff = false;
UINT g_taskbarCreated = 0;
NOTIFYICONDATAW g_nid{};

// ---------- desktop ----------

// The wallpaper layer: a WorkerW window behind the desktop icons. Progman creates it on message 0x052C.
HWND FindWallpaperParent() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) return nullptr;
    DWORD_PTR result;
    SendMessageTimeoutW(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, &result);
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &result);
    // Windows 11 24H2+: the WorkerW is a child of Progman.
    HWND worker = FindWindowExW(progman, nullptr, L"WorkerW", nullptr);
    if (worker) return worker;
    // Older builds: it is the top-level WorkerW right after the window hosting SHELLDLL_DefView.
    HWND found = nullptr;
    EnumWindows(
        [](HWND top, LPARAM lp) -> BOOL {
            if (FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr))
                *(HWND*)lp = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
            return TRUE;
        },
        (LPARAM)&found);
    return found;
}

std::vector<Monitor> EnumerateMonitors() {
    std::vector<Monitor> list;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR hm, HDC, LPRECT, LPARAM lp) -> BOOL {
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            if (!GetMonitorInfoW(hm, &mi)) return TRUE;
            Monitor m;
            m.name = mi.szDevice;
            m.rect = mi.rcMonitor;
            m.work = mi.rcWork;
            UINT dx = 96, dy = 96;
            if (SUCCEEDED(GetDpiForMonitor(hm, MDT_EFFECTIVE_DPI, &dx, &dy))) m.dpi = dx;
            DEVMODEW dm{};
            dm.dmSize = sizeof(dm);
            // 0 and 1 mean "hardware default"
            if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
                m.refreshHz = dm.dmDisplayFrequency;
            DISPLAY_DEVICEW dd{};
            dd.cb = sizeof(dd);
            if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, EDD_GET_DEVICE_INTERFACE_NAME)) m.id = dd.DeviceID;
            if (m.id.empty()) m.id = m.name;
            ((std::vector<Monitor>*)lp)->push_back(m);
            return TRUE;
        },
        (LPARAM)&list);
    return list;
}

// ---------- config ----------

void LoadConfig() {
    std::string text;
    g_config = ReadFileUtf8(g_dataDir + L"\\config.json", text) ? Json::parse(text) : Json();
    g_settings.sceneFps = (int)g_config["sceneFps"].num(30);
    g_pauseWhenCovered = g_config["pauseWhenCovered"].boolean(true);
}

std::string WallpaperFor(const Monitor& m) {
    return g_config["monitors"][Narrow(m.id).c_str()].str();
}

void SetImageWallpaper(const Monitor& m, const std::wstring& path) {
    IDesktopWallpaper* dw = nullptr;
    if (FAILED(CoCreateInstance(CLSID_DesktopWallpaperObj, nullptr, CLSCTX_LOCAL_SERVER, __uuidof(IDesktopWallpaper),
                                (void**)&dw)))
        return;
    dw->SetPosition(DWPOS_FILL);
    HRESULT hr = dw->SetWallpaper(m.id.c_str(), path.c_str());
    Log("Image wallpaper on %s: 0x%08lx", Narrow(m.name).c_str(), hr);
    dw->Release();
}

// Re-applying each monitor's Windows wallpaper repaints the areas our windows covered.
void RefreshWindowsWallpaper() {
    IDesktopWallpaper* dw = nullptr;
    if (FAILED(CoCreateInstance(CLSID_DesktopWallpaperObj, nullptr, CLSCTX_LOCAL_SERVER, __uuidof(IDesktopWallpaper),
                                (void**)&dw)))
        return;
    for (auto& m : EnumerateMonitors()) {
        LPWSTR path = nullptr;
        if (SUCCEEDED(dw->GetWallpaper(m.id.c_str(), &path)) && path) {
            dw->SetWallpaper(m.id.c_str(), path);
            CoTaskMemFree(path);
        }
    }
    dw->Release();
}

// ---------- slots ----------

void UpdatePause() {
    for (auto& s : g_slots) {
        bool p = g_userPaused || g_locked || g_displayOff || (g_pauseWhenCovered && s.covered);
        if (s.renderer && p != s.paused) {
            Log("%s %s", Narrow(s.mon.name).c_str(), p ? "paused" : "resumed");
            s.renderer->SetPaused(p);
        }
        s.paused = p;
    }
}

void DestroyRenderers() {
    bool any = false;
    for (auto& s : g_slots)
        if (s.renderer) { s.renderer.reset(); any = true; }
    if (any) RefreshWindowsWallpaper();
}

void Apply(bool force) {
    // A failed start or a lost GPU is retried on every rebuild (display change, resume, Explorer restart).
    if (!g_gfx.device && !GfxInit(g_gfx)) Log("Graphics unavailable; only image wallpapers will be shown");
    HWND parent = FindWallpaperParent();
    if (!parent) Log("Wallpaper layer (WorkerW) not found");
    if (force || parent != g_parent) {
        DestroyRenderers();
        g_slots.clear();
    }
    g_parent = parent;

    std::vector<Slot> next;
    for (auto& m : EnumerateMonitors()) {
        Slot s;
        s.mon = m;
        s.wallpaperId = WallpaperFor(m);
        for (auto& old : g_slots)
            if (old.mon.id == m.id && EqualRect(&old.mon.rect, &m.rect) && old.mon.dpi == m.dpi &&
                old.wallpaperId == s.wallpaperId && old.parent == parent) {
                s.renderer = std::move(old.renderer);
                s.imageApplied = old.imageApplied;
                s.parent = old.parent;
                s.covered = old.covered;
                s.paused = old.paused;
            }
        next.push_back(std::move(s));
    }
    bool removedAny = false;
    for (auto& old : g_slots)
        if (old.renderer) { old.renderer.reset(); removedAny = true; }
    if (removedAny) RefreshWindowsWallpaper();
    g_slots = std::move(next);

    for (auto& s : g_slots) {
        if (s.renderer || s.wallpaperId.empty() || !parent) continue;
        Wallpaper wp;
        if (!LoadWallpaper(g_libraryDir, s.wallpaperId, wp)) {
            Log("Wallpaper '%s' missing or invalid", s.wallpaperId.c_str());
            continue;
        }
        if (wp.type == "image") {
            if (s.imageApplied != s.wallpaperId) SetImageWallpaper(s.mon, wp.file);
            s.imageApplied = s.wallpaperId;
            continue;
        }
        if (!g_gfx.device) continue;
        RECT r = s.mon.rect;
        MapWindowPoints(nullptr, parent, (POINT*)&r, 2);
        if (wp.type == "video") s.renderer = CreateVideoRenderer(g_gfx, parent, r, wp);
        else if (wp.type == "scene") s.renderer = CreateSceneRenderer(g_gfx, parent, r, s.mon, wp, g_settings);
        if (s.renderer) {
            s.parent = parent;
            s.paused = false;
            Log("%s -> %s ('%s')", Narrow(s.mon.name).c_str(), s.wallpaperId.c_str(), wp.title.c_str());
        } else {
            Log("Could not start '%s' on %s", s.wallpaperId.c_str(), Narrow(s.mon.name).c_str());
        }
    }
    UpdatePause();
}

// A monitor counts as covered when a visible window spans its whole work area (maximized or fullscreen).
void UpdateCoverage() {
    for (auto& s : g_slots) s.covered = false;
    EnumWindows(
        [](HWND w, LPARAM) -> BOOL {
            if (!IsWindowVisible(w) || IsIconic(w)) return TRUE;
            BOOL cloaked = FALSE;
            DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
            if (cloaked) return TRUE;
            wchar_t cls[64];
            GetClassNameW(w, cls, 64);
            if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd") ||
                !wcscmp(cls, L"Shell_SecondaryTrayWnd"))
                return TRUE;
            LONG ex = GetWindowLongW(w, GWL_EXSTYLE);
            if ((ex & WS_EX_LAYERED) && (ex & WS_EX_TRANSPARENT)) return TRUE;  // click-through overlays
            RECT r;
            if (FAILED(DwmGetWindowAttribute(w, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(w, &r);
            for (auto& s : g_slots) {
                const RECT& a = s.mon.work;
                if (r.left <= a.left && r.top <= a.top && r.right >= a.right && r.bottom >= a.bottom) s.covered = true;
            }
            return TRUE;
        },
        0);
    UpdatePause();
}

// ---------- tray ----------

void AddTrayIcon() {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_msgWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wcscpy(g_nid.szTip, L"Driftwall");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void OpenGallery() {
    ShellExecuteW(nullptr, L"open", (g_dataDir + L"\\Driftwall.exe").c_str(), nullptr, g_dataDir.c_str(), SW_SHOWNORMAL);
}

void ShowTrayMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_OPEN, L"Open Driftwall");
    AppendMenuW(m, MF_STRING, ID_PAUSE, g_userPaused ? L"Resume wallpapers" : L"Pause wallpapers");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"Exit Driftwall");
    SetMenuDefaultItem(m, ID_OPEN, FALSE);
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_msgWnd);
    UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_msgWnd, nullptr);
    DestroyMenu(m);
    if (cmd == ID_OPEN) OpenGallery();
    else if (cmd == ID_PAUSE) { g_userPaused = !g_userPaused; UpdatePause(); }
    else if (cmd == ID_EXIT) DestroyWindow(g_msgWnd);
}

LRESULT CALLBACK MsgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == g_taskbarCreated && m) {  // Explorer restarted: new tray and a new WorkerW
        AddTrayIcon();
        Apply(true);
        return 0;
    }
    switch (m) {
    case WM_APP_RELOAD: {
        int fps = g_settings.sceneFps;
        LoadConfig();
        Apply(fps != g_settings.sceneFps);  // scenes pick up a new frame rate only when recreated
        return 0;
    }
    case WM_APP_EXIT:
        DestroyWindow(h);
        return 0;
    case WM_TRAY:
        if (l == WM_RBUTTONUP) ShowTrayMenu();
        else if (l == WM_LBUTTONDBLCLK) OpenGallery();
        return 0;
    case WM_TIMER:
        if (w == TIMER_REBUILD) {
            KillTimer(h, TIMER_REBUILD);
            Apply(true);
        } else if (w == TIMER_COVER) {
            if (g_gfx.device && g_gfx.device->GetDeviceRemovedReason() != S_OK) {
                Log("GPU device lost, recreating");
                DestroyRenderers();
                g_slots.clear();
                GfxShutdown(g_gfx);
                Apply(true);
            } else if (g_parent && !IsWindow(g_parent)) {
                Apply(true);
            }
            UpdateCoverage();
        }
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
        SetTimer(h, TIMER_REBUILD, 1500, nullptr);
        return 0;
    case WM_SETTINGCHANGE:
        if (w == SPI_SETWORKAREA) SetTimer(h, TIMER_REBUILD, 1500, nullptr);
        return 0;
    case WM_WTSSESSION_CHANGE:
        if (w == WTS_SESSION_LOCK) g_locked = true;
        else if (w == WTS_SESSION_UNLOCK) g_locked = false;
        UpdatePause();
        return 0;
    case WM_POWERBROADCAST:
        if (w == PBT_POWERSETTINGCHANGE) {
            auto* s = (POWERBROADCAST_SETTING*)l;
            if (IsEqualGUID(s->PowerSetting, GUID_CONSOLE_DISPLAY_STATE)) {
                g_displayOff = s->Data[0] == 0;
                UpdatePause();
            }
        } else if (w == PBT_APMRESUMEAUTOMATIC) {
            SetTimer(h, TIMER_REBUILD, 3000, nullptr);
        }
        return TRUE;
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* e) {
    void* addr = e->ExceptionRecord->ExceptionAddress;
    HMODULE mod = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)addr, &mod))
        GetModuleFileNameA(mod, name, MAX_PATH);
    Log("CRASH 0x%08lx at %s+0x%llx", e->ExceptionRecord->ExceptionCode, name,
        (unsigned long long)((char*)addr - (char*)mod));
    return EXCEPTION_CONTINUE_SEARCH;
}

int Snapshot(int argc, wchar_t** argv) {
    // --snapshot <wallpaperId> <width> <height> <dpi> <seconds> <out.png>
    if (argc < 8) return 2;
    LogOpen(g_dataDir + L"\\snapshot.log");
    Wallpaper wp;
    if (!LoadWallpaper(g_libraryDir, Narrow(argv[2]), wp) || wp.type != "scene") return 3;
    Log("Snapshot of '%s'", wp.title.c_str());
    if (!GfxInit(g_gfx)) return 4;
    Log("Graphics ready");
    bool ok = SnapshotScene(g_gfx, wp, _wtoi(argv[3]), _wtoi(argv[4]), _wtoi(argv[5]), _wtof(argv[6]), argv[7]);
    Log("Snapshot %s", ok ? "saved" : "failed");
    GfxShutdown(g_gfx);
    return ok ? 0 : 5;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_dataDir = exe;
    g_dataDir = g_dataDir.substr(0, g_dataDir.find_last_of(L'\\'));
    g_libraryDir = g_dataDir + L"\\Library";

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_LITE);

    if (argc > 1 && !wcscmp(argv[1], L"--snapshot")) {
        int rc = Snapshot(argc, argv);
        MFShutdown();
        CoUninitialize();
        return rc;
    }

    // One engine at a time; a second launch just forwards the request.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\DriftwallEngineInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(L"DriftwallEngine", nullptr);
        if (other) PostMessageW(other, (argc > 1 && !wcscmp(argv[1], L"--exit")) ? WM_APP_EXIT : WM_APP_RELOAD, 0, 0);
        return 0;
    }
    if (argc > 1 && !wcscmp(argv[1], L"--exit")) return 0;

    LogOpen(g_dataDir + L"\\engine.log");
    Log("Driftwall Engine starting");
    SetUnhandledExceptionFilter(CrashFilter);

    WNDCLASSW wc{};
    wc.lpfnWndProc = MsgProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"DriftwallEngine";
    RegisterClassW(&wc);
    // A hidden top-level window (not message-only) so it receives the TaskbarCreated broadcast.
    g_msgWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"DriftwallEngine", L"Driftwall Engine", WS_POPUP, 0, 0, 0, 0,
                               nullptr, nullptr, inst, nullptr);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(g_msgWnd, g_taskbarCreated, MSGFLT_ALLOW, nullptr);
    WTSRegisterSessionNotification(g_msgWnd, NOTIFY_FOR_THIS_SESSION);
    HPOWERNOTIFY power = RegisterPowerSettingNotification(g_msgWnd, &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_WINDOW_HANDLE);
    AddTrayIcon();

    LoadConfig();
    Apply(true);
    SetTimer(g_msgWnd, TIMER_COVER, 500, nullptr);

    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);

    MSG msg;
    std::vector<HANDLE> waits;
    std::vector<Renderer*> paced;  // renderers ticked by their display, in the same order as waits
    for (;;) {
        double now = NowSeconds(), next = 1e300;
        waits.clear();
        paced.clear();
        for (auto& s : g_slots) {
            Renderer* r = s.renderer.get();
            if (!r || r->Interval() <= 0) continue;
            if (HANDLE frame = r->FrameWaitable()) {
                waits.push_back(frame);
                paced.push_back(r);
                continue;
            }
            if (r->nextTick < now - 1) r->nextTick = now;  // resumed after a pause
            next = std::min(next, r->nextTick);
        }
        if (next < 1e300) {
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)(std::max(0.0, next - now) * 1e7);
            if (due.QuadPart == 0) due.QuadPart = -1;
            SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
            waits.push_back(timer);
        }
        // With nothing animating (all paused) this waits for messages only: zero CPU.
        DWORD ret = MsgWaitForMultipleObjectsEx((DWORD)waits.size(), waits.data(), INFINITE, QS_ALLINPUT,
                                                MWMO_INPUTAVAILABLE);

        now = NowSeconds();
        // Only the signaled waitable is consumed; any other ready display is picked up on the next pass.
        if (ret - WAIT_OBJECT_0 < paced.size()) paced[ret - WAIT_OBJECT_0]->Tick(now);
        for (auto& s : g_slots) {
            Renderer* r = s.renderer.get();
            double iv = r ? r->Interval() : 0;
            if (iv <= 0 || r->FrameWaitable() || now < r->nextTick - 0.0005) continue;
            r->Tick(now);
            r->nextTick += iv;
            if (r->nextTick < now) r->nextTick = now + iv;
        }

        bool quit = false;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { quit = true; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (quit) break;
    }

    Log("Driftwall Engine exiting");
    DestroyRenderers();
    g_slots.clear();
    if (power) UnregisterPowerSettingNotification(power);
    GfxShutdown(g_gfx);
    MFShutdown();
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return 0;
}
