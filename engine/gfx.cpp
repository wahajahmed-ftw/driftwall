#include "engine.h"
#include <d3dcompiler.h>
#include <mfapi.h>
#include <wincodec.h>

bool GfxInit(Gfx& g) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 4, D3D11_SDK_VERSION,
                                   &g.device, nullptr, &g.context);
    if (FAILED(hr)) {
        // Some drivers refuse VIDEO_SUPPORT; scenes still work without it.
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 4,
                               D3D11_SDK_VERSION, &g.device, nullptr, &g.context);
    }
    if (FAILED(hr)) { Log("D3D11CreateDevice failed 0x%08lx", hr); return false; }

    // Media Foundation decodes on its own threads using this device.
    ID3D10Multithread* mt = nullptr;
    if (SUCCEEDED(g.device->QueryInterface(__uuidof(ID3D10Multithread), (void**)&mt))) {
        mt->SetMultithreadProtected(TRUE);
        mt->Release();
    }

    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    if (SUCCEEDED(g.device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice)) &&
        SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))
        adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&g.factory);
    SafeRelease(adapter);
    SafeRelease(dxgiDevice);
    if (!g.factory) {
        Log("No IDXGIFactory2");
        GfxShutdown(g);
        return false;
    }

    if (SUCCEEDED(MFCreateDXGIDeviceManager(&g.resetToken, &g.dxgiManager)))
        g.dxgiManager->ResetDevice(g.device, g.resetToken);
    return true;
}

void GfxShutdown(Gfx& g) {
    SafeRelease(g.dxgiManager);
    SafeRelease(g.factory);
    if (g.context) g.context->ClearState();
    SafeRelease(g.context);
    SafeRelease(g.device);
}

static LRESULT CALLBACK SurfaceProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_PAINT: ValidateRect(h, nullptr); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

HWND CreateSurfaceWindow(HWND parent, const RECT& r) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = SurfaceProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"DriftwallSurface";
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassW(&wc);
        registered = true;
    }
    return CreateWindowExW(WS_EX_NOACTIVATE, L"DriftwallSurface", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                           r.left, r.top, r.right - r.left, r.bottom - r.top, parent, nullptr,
                           GetModuleHandleW(nullptr), nullptr);
}

IDXGISwapChain1* CreateSwapChain(Gfx& g, HWND hwnd, UINT w, UINT h, bool frameWaitable) {
    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = w;
    d.Height = h;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    if (frameWaitable) d.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    IDXGISwapChain1* sc = nullptr;
    HRESULT hr = g.factory->CreateSwapChainForHwnd(g.device, hwnd, &d, nullptr, nullptr, &sc);
    if (FAILED(hr)) {
        Log("Flip swap chain failed 0x%08lx, falling back to blt model", hr);
        d.BufferCount = 1;
        d.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        d.Flags = 0;
        hr = g.factory->CreateSwapChainForHwnd(g.device, hwnd, &d, nullptr, nullptr, &sc);
    }
    if (FAILED(hr)) { Log("CreateSwapChainForHwnd failed 0x%08lx", hr); return nullptr; }
    g.factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    return sc;
}

// d3dcompiler_47.dll ships with Windows; it is loaded only while scenes are being set up.
static HMODULE g_compiler = nullptr;

void FreeShaderCompiler() {
    if (g_compiler) { FreeLibrary(g_compiler); g_compiler = nullptr; }
}

ID3DBlob* CompileShader(const char* source, const char* entry, const char* target) {
    typedef HRESULT(WINAPI * PD3DCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR,
                                          LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
    if (!g_compiler) g_compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!g_compiler) { Log("d3dcompiler_47.dll not found"); return nullptr; }
    auto compile = (PD3DCompile)(void*)GetProcAddress(g_compiler, "D3DCompile");
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = compile ? compile(source, strlen(source), entry, nullptr, nullptr, entry, target,
                                   D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors)
                         : E_FAIL;
    if (FAILED(hr)) Log("Shader %s failed: %s", entry, errors ? (const char*)errors->GetBufferPointer() : "?");
    SafeRelease(errors);
    return code;
}

bool SavePng(const std::wstring& path, const void* bgra, UINT w, UINT h, UINT stride) {
    IWICImagingFactory* wic = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
        SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->SetSize(w, h))) {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        ok = SUCCEEDED(frame->SetPixelFormat(&fmt)) &&
             SUCCEEDED(frame->WritePixels(h, stride, stride * h, (BYTE*)bgra)) && SUCCEEDED(frame->Commit()) &&
             SUCCEEDED(enc->Commit());
    }
    SafeRelease(frame);
    SafeRelease(enc);
    SafeRelease(stream);
    SafeRelease(wic);
    return ok;
}
