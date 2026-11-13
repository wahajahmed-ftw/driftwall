// Shared types for the Driftwall engine.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <memory>
#include <string>
#include "json.h"

// ---- utilities (util.cpp) ----
std::wstring Widen(const std::string& s);
std::string Narrow(const std::wstring& s);
bool ReadFileUtf8(const std::wstring& path, std::string& out);
void Log(const char* fmt, ...);
void LogOpen(const std::wstring& path);
double NowSeconds();

template <class T> void SafeRelease(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

// ---- library ----
struct Wallpaper {
    std::string id;     // library folder name
    std::wstring dir;   // full path of the library folder
    std::string title;
    std::string type;   // "video" | "image" | "scene"
    std::wstring file;  // full path of the media file (video/image)
    std::string scene;  // built-in scene name for type "scene" (e.g. "hill")
    Json params;        // scene settings
};
bool LoadWallpaper(const std::wstring& libraryDir, const std::string& id, Wallpaper& out);

struct Monitor {
    std::wstring id;    // device interface path, matches IDesktopWallpaper monitor IDs
    std::wstring name;  // \\.\DISPLAYn
    RECT rect{};        // physical pixels, virtual-screen coordinates
    RECT work{};
    UINT dpi = 96;
};

// ---- graphics (gfx.cpp) ----
// One D3D11 device is shared by every renderer and by Media Foundation.
struct Gfx {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGIFactory2* factory = nullptr;
    struct IMFDXGIDeviceManager* dxgiManager = nullptr;
    UINT resetToken = 0;
};
bool GfxInit(Gfx& g);
void GfxShutdown(Gfx& g);
HWND CreateSurfaceWindow(HWND parent, const RECT& rectInParent);
IDXGISwapChain1* CreateSwapChain(Gfx& g, HWND hwnd, UINT w, UINT h);
ID3DBlob* CompileShader(const char* source, const char* entry, const char* target);
void FreeShaderCompiler();  // call once every blob from CompileShader has been released
bool SavePng(const std::wstring& path, const void* bgra, UINT w, UINT h, UINT stride);

// ---- renderers ----
class Renderer {
public:
    virtual ~Renderer() {}
    virtual void SetPaused(bool paused) = 0;
    virtual void Tick(double now) = 0;
    // Seconds between ticks while running; <= 0 means "no ticks needed".
    virtual double Interval() const = 0;
    double nextTick = 0;
    HWND hwnd = nullptr;
};

struct EngineSettings {
    int sceneFps = 30;
};

std::unique_ptr<Renderer> CreateVideoRenderer(Gfx& g, HWND parent, const RECT& rectInParent, const Wallpaper& wp);
std::unique_ptr<Renderer> CreateSceneRenderer(Gfx& g, HWND parent, const RECT& rectInParent, const Monitor& mon,
                                              const Wallpaper& wp, const EngineSettings& settings);
// Renders one frame of a scene to a PNG (used to check scenes without touching the desktop).
bool SnapshotScene(Gfx& g, const Wallpaper& wp, UINT w, UINT h, UINT dpi, double seconds, const std::wstring& outPng);
