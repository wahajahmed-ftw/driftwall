// Video wallpaper: Media Foundation's media engine decodes on the GPU (frame-server mode),
// and each new frame is copied straight into the swap chain's back buffer.
#include <initguid.h>
#include "engine.h"
#include <mfapi.h>
#include <mfmediaengine.h>
#include <algorithm>
#include <atomic>

namespace {

class Notify final : public IMFMediaEngineNotify {
    LONG ref = 1;

public:
    std::atomic<bool> failed{false};
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFMediaEngineNotify)) {
            *ppv = static_cast<IMFMediaEngineNotify*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = InterlockedDecrement(&ref);
        if (!r) delete this;
        return r;
    }
    STDMETHODIMP EventNotify(DWORD event, DWORD_PTR param1, DWORD param2) override {
        if (event == MF_MEDIA_ENGINE_EVENT_ERROR) {
            Log("Media engine error %lu (0x%08lx)", (unsigned long)param1, (unsigned long)param2);
            failed = true;
        }
        return S_OK;
    }
};

class VideoRenderer : public Renderer {
    Gfx& g;
    IDXGISwapChain1* swap = nullptr;
    IMFMediaEngine* engine = nullptr;
    Notify* notify = nullptr;
    UINT w = 0, h = 0;
    MFVideoNormalizedRect src{0, 0, 1, 1};
    bool sized = false, paused = false;
    double interval = 1.0 / 60;

public:
    explicit VideoRenderer(Gfx& gfx) : g(gfx) {}

    bool Init(HWND parent, const RECT& r, const Wallpaper& wp) {
        w = r.right - r.left;
        h = r.bottom - r.top;
        hwnd = CreateSurfaceWindow(parent, r);
        if (!hwnd) return false;
        swap = CreateSwapChain(g, hwnd, w, h);
        if (!swap || !g.dxgiManager) return false;

        IMFMediaEngineClassFactory* factory = nullptr;
        IMFAttributes* attr = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&factory));
        if (SUCCEEDED(hr)) hr = MFCreateAttributes(&attr, 3);
        notify = new Notify();
        if (SUCCEEDED(hr)) {
            attr->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, g.dxgiManager);
            attr->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify);
            attr->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);
            hr = factory->CreateInstance(MF_MEDIA_ENGINE_REAL_TIME_MODE, attr, &engine);
        }
        SafeRelease(attr);
        SafeRelease(factory);
        if (FAILED(hr)) { Log("Media engine create failed 0x%08lx", hr); return false; }

        engine->SetMuted(TRUE);
        engine->SetLoop(TRUE);
        engine->SetAutoPlay(TRUE);
        BSTR url = SysAllocString(wp.file.c_str());
        hr = engine->SetSource(url);
        SysFreeString(url);
        if (FAILED(hr)) { Log("SetSource failed 0x%08lx", hr); return false; }
        Log("Video: %s (%ux%u)", Narrow(wp.file).c_str(), w, h);
        return true;
    }

    ~VideoRenderer() override {
        if (engine) { engine->Shutdown(); engine->Release(); }
        SafeRelease(notify);
        SafeRelease(swap);
        if (hwnd) DestroyWindow(hwnd);
    }

    void SetPaused(bool p) override {
        if (p == paused || !engine) return;
        paused = p;
        if (p) engine->Pause();
        else engine->Play();
    }

    double Interval() const override { return (paused || !engine || notify->failed) ? 0 : interval; }

    void Tick(double) override {
        LONGLONG pts;
        if (engine->OnVideoStreamTick(&pts) != S_OK) return;
        if (!sized) FitCover();
        ID3D11Texture2D* bb = nullptr;
        if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&bb)))) return;
        RECT dst{0, 0, (LONG)w, (LONG)h};
        MFARGB black{0, 0, 0, 255};
        engine->TransferVideoFrame(bb, &src, &dst, &black);
        bb->Release();
        swap->Present(0, 0);
    }

private:
    // Crop the video so it fills the monitor without black bars (like CSS object-fit: cover).
    void FitCover() {
        DWORD vw = 0, vh = 0;
        if (FAILED(engine->GetNativeVideoSize(&vw, &vh)) || !vw || !vh) return;
        double va = (double)vw / vh, sa = (double)w / h;
        if (va > sa) {
            float f = (float)(sa / va);
            src = {(1 - f) / 2, 0, (1 + f) / 2, 1};
        } else {
            float f = (float)(va / sa);
            src = {0, (1 - f) / 2, 1, (1 + f) / 2};
        }
        // Poll at twice the video's frame rate (bounded) so frames are picked up promptly.
        IMFMediaEngineEx* ex = nullptr;
        if (SUCCEEDED(engine->QueryInterface(IID_PPV_ARGS(&ex)))) {
            DWORD streams = 0;
            ex->GetNumberOfStreams(&streams);
            for (DWORD i = 0; i < streams; i++) {
                PROPVARIANT v;
                PropVariantInit(&v);
                if (SUCCEEDED(ex->GetStreamAttribute(i, MF_MT_FRAME_RATE, &v)) && v.vt == VT_UI8) {
                    double fps = (double)(v.uhVal.QuadPart >> 32) / (double)(v.uhVal.QuadPart & 0xffffffff);
                    if (fps > 1 && fps < 241) interval = std::max(1.0 / 240, std::min(1.0 / 30, 0.5 / fps));
                }
                PropVariantClear(&v);
            }
            ex->Release();
        }
        Log("Video size %lux%lu, poll %.1f ms", vw, vh, interval * 1000);
        sized = true;
    }
};

}  // namespace

std::unique_ptr<Renderer> CreateVideoRenderer(Gfx& g, HWND parent, const RECT& r, const Wallpaper& wp) {
    std::unique_ptr<VideoRenderer> v(new VideoRenderer(g));
    if (!v->Init(parent, r, wp)) return nullptr;
    return std::move(v);
}
