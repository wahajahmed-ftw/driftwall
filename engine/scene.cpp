// Built-in scenes rendered natively with Direct3D 11.
// "hill": port of "The Hill" (GLSL Hills by Yoichi Kobayashi, https://codepen.io/ykob/pen/aBrjaR),
// including the Lively clock/date overlay, without a browser engine.
#include "engine.h"
#include <dxgi1_3.h>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

const char* kHillShader = R"(
cbuffer Scene : register(b0) {
    float4x4 mvp;
    float4 colorOpacity;
    float time;
    float3 pad;
};

// GLSL textureless classic 3D noise "cnoise" by Stefan Gustavson (MIT), ported to HLSL.
float3 mod289(float3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
float4 mod289(float4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
float4 permute(float4 x) { return mod289(((x * 34.0) + 1.0) * x); }
float4 taylorInvSqrt(float4 r) { return 1.79284291400159 - 0.85373472095314 * r; }
float3 fade(float3 t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

float cnoise(float3 P) {
    float3 Pi0 = floor(P);
    float3 Pi1 = Pi0 + 1.0;
    Pi0 = mod289(Pi0);
    Pi1 = mod289(Pi1);
    float3 Pf0 = P - floor(P);
    float3 Pf1 = Pf0 - 1.0;
    float4 ix = float4(Pi0.x, Pi1.x, Pi0.x, Pi1.x);
    float4 iy = float4(Pi0.yy, Pi1.yy);
    float4 iz0 = Pi0.zzzz;
    float4 iz1 = Pi1.zzzz;

    float4 ixy = permute(permute(ix) + iy);
    float4 ixy0 = permute(ixy + iz0);
    float4 ixy1 = permute(ixy + iz1);

    float4 gx0 = ixy0 * (1.0 / 7.0);
    float4 gy0 = (floor(gx0) * (1.0 / 7.0) - floor(floor(gx0) * (1.0 / 7.0))) - 0.5;
    gx0 = gx0 - floor(gx0);
    float4 gz0 = 0.5 - abs(gx0) - abs(gy0);
    float4 sz0 = step(gz0, 0.0);
    gx0 -= sz0 * (step(0.0, gx0) - 0.5);
    gy0 -= sz0 * (step(0.0, gy0) - 0.5);

    float4 gx1 = ixy1 * (1.0 / 7.0);
    float4 gy1 = (floor(gx1) * (1.0 / 7.0) - floor(floor(gx1) * (1.0 / 7.0))) - 0.5;
    gx1 = gx1 - floor(gx1);
    float4 gz1 = 0.5 - abs(gx1) - abs(gy1);
    float4 sz1 = step(gz1, 0.0);
    gx1 -= sz1 * (step(0.0, gx1) - 0.5);
    gy1 -= sz1 * (step(0.0, gy1) - 0.5);

    float3 g000 = float3(gx0.x, gy0.x, gz0.x);
    float3 g100 = float3(gx0.y, gy0.y, gz0.y);
    float3 g010 = float3(gx0.z, gy0.z, gz0.z);
    float3 g110 = float3(gx0.w, gy0.w, gz0.w);
    float3 g001 = float3(gx1.x, gy1.x, gz1.x);
    float3 g101 = float3(gx1.y, gy1.y, gz1.y);
    float3 g011 = float3(gx1.z, gy1.z, gz1.z);
    float3 g111 = float3(gx1.w, gy1.w, gz1.w);

    float4 norm0 = taylorInvSqrt(float4(dot(g000, g000), dot(g010, g010), dot(g100, g100), dot(g110, g110)));
    g000 *= norm0.x; g010 *= norm0.y; g100 *= norm0.z; g110 *= norm0.w;
    float4 norm1 = taylorInvSqrt(float4(dot(g001, g001), dot(g011, g011), dot(g101, g101), dot(g111, g111)));
    g001 *= norm1.x; g011 *= norm1.y; g101 *= norm1.z; g111 *= norm1.w;

    float n000 = dot(g000, Pf0);
    float n100 = dot(g100, float3(Pf1.x, Pf0.yz));
    float n010 = dot(g010, float3(Pf0.x, Pf1.y, Pf0.z));
    float n110 = dot(g110, float3(Pf1.xy, Pf0.z));
    float n001 = dot(g001, float3(Pf0.xy, Pf1.z));
    float n101 = dot(g101, float3(Pf1.x, Pf0.y, Pf1.z));
    float n011 = dot(g011, float3(Pf0.x, Pf1.yz));
    float n111 = dot(g111, Pf1);

    float3 fade_xyz = fade(Pf0);
    float4 n_z = lerp(float4(n000, n100, n010, n110), float4(n001, n101, n011, n111), fade_xyz.z);
    float2 n_yz = lerp(n_z.xy, n_z.zw, fade_xyz.y);
    float n_xyz = lerp(n_yz.x, n_yz.y, fade_xyz.x);
    return 2.2 * n_xyz;
}

struct VSOut { float4 pos : SV_Position; float3 vpos : TEXCOORD0; };

VSOut vs(float3 position : POSITION) {
    float r = radians(90.0);
    float3 p = float3(position.x, cos(r) * position.y + sin(r) * position.z,
                      -sin(r) * position.y + cos(r) * position.z);
    float sin1 = sin(radians(p.x / 128.0 * 90.0));
    float3 np = p + float3(0.0, 0.0, time * -30.0);
    float noise1 = cnoise(np * 0.08);
    float noise2 = cnoise(np * 0.06);
    float noise3 = cnoise(np * 0.4);
    float3 last = p + float3(0.0,
        noise1 * sin1 * 8.0
        + noise2 * sin1 * 8.0
        + noise3 * (abs(sin1) * 2.0 + 0.5)
        + sin1 * sin1 * 40.0, 0.0);
    VSOut o;
    o.vpos = last;
    o.pos = mul(mvp, float4(last, 1.0));
    return o;
}

float4 ps(VSOut i) : SV_Target {
    float opacity = (96.0 - length(i.vpos)) / 256.0 * 0.6;
    return float4(colorOpacity.rgb, saturate(opacity * colorOpacity.a));
}
)";

const char* kOverlayShader = R"(
cbuffer Overlay : register(b0) { float4 rect; };  // NDC: left, top, right, bottom
Texture2D tex : register(t0);
SamplerState smp : register(s0);
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut vs(uint id : SV_VertexID) {
    float2 uv = float2(id & 1, id >> 1);
    VSOut o;
    o.pos = float4(lerp(rect.x, rect.z, uv.x), lerp(rect.y, rect.w, uv.y), 0, 1);
    o.uv = uv;
    return o;
}
float4 ps(VSOut i) : SV_Target { return tex.Sample(smp, i.uv); }
)";

struct Mat4 {
    float m[16];  // column-major, m[col * 4 + row]
    float& at(int r, int c) { return m[c * 4 + r]; }
};

Mat4 Mul(Mat4 a, Mat4 b) {
    Mat4 r{};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.at(i, k) * b.at(k, j);
            r.at(i, j) = s;
        }
    return r;
}

// Right-handed perspective with D3D depth range [0, 1].
Mat4 Perspective(float fovyDeg, float aspect, float zn, float zf) {
    Mat4 p{};
    float f = 1.0f / tanf(fovyDeg * 3.14159265f / 360.0f);
    p.at(0, 0) = f / aspect;
    p.at(1, 1) = f;
    p.at(2, 2) = zf / (zn - zf);
    p.at(2, 3) = zn * zf / (zn - zf);
    p.at(3, 2) = -1;
    return p;
}

Mat4 LookAt(const float eye[3], const float target[3]) {
    float f[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (float& v : f) v /= fl;
    float s[3] = {-f[2], 0, f[0]};  // cross(f, up) with up = (0, 1, 0)
    float sl = sqrtf(s[0] * s[0] + s[2] * s[2]);
    s[0] /= sl; s[2] /= sl;
    float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    Mat4 v{};
    for (int c = 0; c < 3; c++) {
        v.at(0, c) = s[c];
        v.at(1, c) = u[c];
        v.at(2, c) = -f[c];
    }
    v.at(0, 3) = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    v.at(1, 3) = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    v.at(2, 3) = (f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]);
    v.at(3, 3) = 1;
    return v;
}

void ParseColor(const std::string& hex, float out[3], unsigned def) {
    unsigned v = def;
    if (hex.size() == 7 && hex[0] == '#') v = strtoul(hex.c_str() + 1, nullptr, 16);
    out[0] = ((v >> 16) & 255) / 255.0f;
    out[1] = ((v >> 8) & 255) / 255.0f;
    out[2] = (v & 255) / 255.0f;
}

void LoadSceneFonts(const std::wstring& dir) {
    static std::vector<std::wstring> loaded;
    for (auto& d : loaded)
        if (d == dir) return;
    loaded.push_back(dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\fonts\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring n = fd.cFileName;
        if (n.size() > 4 && (_wcsicmp(n.c_str() + n.size() - 4, L".ttf") == 0 || _wcsicmp(n.c_str() + n.size() - 4, L".otf") == 0))
            AddFontResourceExW((dir + L"\\fonts\\" + n).c_str(), FR_PRIVATE, nullptr);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

struct CB {
    Mat4 mvp;
    float colorOpacity[4];
    float time;
    float pad[3];
};

class HillScene {
    Gfx& g;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer *vb = nullptr, *ib = nullptr, *cb = nullptr;
    ID3D11BlendState* blend = nullptr;
    ID3D11DepthStencilState *depthOn = nullptr, *depthOff = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    UINT indexCount = 0;
    // overlay
    ID3D11VertexShader* ovs = nullptr;
    ID3D11PixelShader* ops = nullptr;
    ID3D11Buffer* ocb = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11Texture2D* textTex = nullptr;
    ID3D11ShaderResourceView* textSrv = nullptr;
    int textY0 = 0, textH = 0;
    std::wstring textKey;

    UINT w = 0, h = 0;
    float scale = 1;
    float bg[3], color[3], fontColor[3], opacity = 0.5f;
    bool showTime = true, showDate = true, hour12 = true, monthDay = true;

public:
    double time = 0;

    explicit HillScene(Gfx& gfx) : g(gfx) {}

    ~HillScene() {
        ID3D11DeviceChild* all[] = {vs, ps, layout, vb, ib, cb, blend, depthOn, depthOff, raster,
                                    ovs, ops, ocb, sampler, textTex, textSrv};
        for (auto* p : all)
            if (p) p->Release();
    }

    bool Init(const Wallpaper& wp, UINT width, UINT height, UINT dpi) {
        w = width;
        h = height;
        scale = dpi / 96.0f;
        const Json& p = wp.params;
        ParseColor(p["background"].str(), bg, 0x0e0e0e);
        ParseColor(p["color"].str(), color, 0xffffff);
        ParseColor(p["fontColor"].str(), fontColor, 0xffffff);
        opacity = (float)p["opacity"].num(50) / 100.0f;
        showTime = p["showTime"].boolean(true);
        showDate = p["showDate"].boolean(true);
        hour12 = p["hour12"].boolean(true);
        monthDay = p["monthDayFormat"].boolean(true);
        LoadSceneFonts(wp.dir);

        ID3DBlob* vsb = CompileShader(kHillShader, "vs", "vs_4_0");
        ID3DBlob* psb = CompileShader(kHillShader, "ps", "ps_4_0");
        ID3DBlob* ovsb = CompileShader(kOverlayShader, "vs", "vs_4_0");
        ID3DBlob* opsb = CompileShader(kOverlayShader, "ps", "ps_4_0");
        bool ok = vsb && psb && ovsb && opsb;
        if (ok) {
            g.device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);
            g.device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);
            g.device->CreateVertexShader(ovsb->GetBufferPointer(), ovsb->GetBufferSize(), nullptr, &ovs);
            g.device->CreatePixelShader(opsb->GetBufferPointer(), opsb->GetBufferSize(), nullptr, &ops);
            D3D11_INPUT_ELEMENT_DESC el{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
            g.device->CreateInputLayout(&el, 1, vsb->GetBufferPointer(), vsb->GetBufferSize(), &layout);
        }
        SafeRelease(vsb); SafeRelease(psb); SafeRelease(ovsb); SafeRelease(opsb);
        FreeShaderCompiler();
        if (!ok) return false;

        // THREE.PlaneGeometry(256, 256, 256, 256): 257 x 257 vertices in the XY plane.
        const int seg = 256;
        std::vector<float> verts;
        verts.reserve((seg + 1) * (seg + 1) * 3);
        for (int iy = 0; iy <= seg; iy++)
            for (int ix = 0; ix <= seg; ix++) {
                verts.push_back(-128.0f + ix);
                verts.push_back(128.0f - iy);
                verts.push_back(0);
            }
        std::vector<UINT> idx;
        idx.reserve(seg * seg * 6);
        for (int iy = 0; iy < seg; iy++)
            for (int ix = 0; ix < seg; ix++) {
                UINT a = ix + (seg + 1) * iy, b = ix + (seg + 1) * (iy + 1);
                UINT c = (ix + 1) + (seg + 1) * (iy + 1), d = (ix + 1) + (seg + 1) * iy;
                UINT tri[6] = {a, b, d, b, c, d};
                idx.insert(idx.end(), tri, tri + 6);
            }
        indexCount = (UINT)idx.size();

        D3D11_BUFFER_DESC bd{};
        D3D11_SUBRESOURCE_DATA sd{};
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.ByteWidth = (UINT)(verts.size() * sizeof(float));
        sd.pSysMem = verts.data();
        g.device->CreateBuffer(&bd, &sd, &vb);
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.ByteWidth = (UINT)(idx.size() * sizeof(UINT));
        sd.pSysMem = idx.data();
        g.device->CreateBuffer(&bd, &sd, &ib);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.ByteWidth = sizeof(CB);
        g.device->CreateBuffer(&bd, nullptr, &cb);
        bd.ByteWidth = 16;
        g.device->CreateBuffer(&bd, nullptr, &ocb);

        // three.js NormalBlending for a transparent material.
        D3D11_BLEND_DESC bl{};
        auto& rt = bl.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ONE;
        rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        g.device->CreateBlendState(&bl, &blend);

        D3D11_DEPTH_STENCIL_DESC ds{};
        ds.DepthEnable = TRUE;
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        g.device->CreateDepthStencilState(&ds, &depthOn);
        ds.DepthEnable = FALSE;
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        g.device->CreateDepthStencilState(&ds, &depthOff);

        // three.js FrontSide: counter-clockwise triangles are front faces, back faces culled.
        D3D11_RASTERIZER_DESC rs{};
        rs.FillMode = D3D11_FILL_SOLID;
        rs.CullMode = D3D11_CULL_BACK;
        rs.FrontCounterClockwise = TRUE;
        rs.DepthClipEnable = TRUE;
        g.device->CreateRasterizerState(&rs, &raster);

        D3D11_SAMPLER_DESC smp{};
        smp.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        g.device->CreateSamplerState(&smp, &sampler);
        return vb && ib && cb && blend && raster;
    }

    void Render(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, double dt) {
        time += dt;
        UpdateText();
        auto* c = g.context;
        float clear[4] = {bg[0], bg[1], bg[2], 1};
        c->ClearRenderTargetView(rtv, clear);
        c->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
        c->OMSetRenderTargets(1, &rtv, dsv);
        D3D11_VIEWPORT vp{0, 0, (float)w, (float)h, 0, 1};
        c->RSSetViewports(1, &vp);

        const float eye[3] = {0, 16, 128}, target[3] = {0, 28, 0};
        CB data{};
        data.mvp = Mul(Perspective(45, (float)w / h, 1, 10000), LookAt(eye, target));
        data.colorOpacity[0] = color[0];
        data.colorOpacity[1] = color[1];
        data.colorOpacity[2] = color[2];
        data.colorOpacity[3] = opacity;
        data.time = (float)time;
        c->UpdateSubresource(cb, 0, nullptr, &data, 0, 0);

        UINT stride = 12, offset = 0;
        c->IASetInputLayout(layout);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        c->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
        c->VSSetShader(vs, nullptr, 0);
        c->VSSetConstantBuffers(0, 1, &cb);
        c->PSSetShader(ps, nullptr, 0);
        c->PSSetConstantBuffers(0, 1, &cb);
        float bf[4] = {0, 0, 0, 0};
        c->OMSetBlendState(blend, bf, 0xffffffff);
        c->OMSetDepthStencilState(depthOn, 0);
        c->RSSetState(raster);
        c->DrawIndexed(indexCount, 0, 0);

        if (textSrv) {
            float rect[4] = {-1, 1 - 2.0f * textY0 / h, 1, 1 - 2.0f * (textY0 + textH) / h};
            c->UpdateSubresource(ocb, 0, nullptr, rect, 0, 0);
            c->IASetInputLayout(nullptr);
            c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            c->VSSetShader(ovs, nullptr, 0);
            c->VSSetConstantBuffers(0, 1, &ocb);
            c->PSSetShader(ops, nullptr, 0);
            c->PSSetShaderResources(0, 1, &textSrv);
            c->PSSetSamplers(0, 1, &sampler);
            c->OMSetDepthStencilState(depthOff, 0);
            c->RSSetState(nullptr);  // default state: the overlay quad is wound clockwise
            c->Draw(4, 0);
        }
    }

private:
    // Mirrors the wallpaper's Intl.DateTimeFormat output, e.g. "- 06:05 -", "Wednesday", "Nov 11 26".
    void UpdateText() {
        if (!showTime && !showDate) return;
        SYSTEMTIME t;
        GetLocalTime(&t);
        static const wchar_t* days[] = {L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday"};
        static const wchar_t* months[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
                                          L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
        wchar_t clock[32], date[32];
        int hr = hour12 ? (t.wHour % 12 == 0 ? 12 : t.wHour % 12) : t.wHour;
        swprintf(clock, 32, L"- %02d:%02d -", hr, t.wMinute);
        if (monthDay) swprintf(date, 32, L"%ls %02d %02d", months[t.wMonth - 1], t.wDay, t.wYear % 100);
        else swprintf(date, 32, L"%02d/%02d/%02d", t.wDay, t.wMonth, t.wYear % 100);
        std::wstring key = std::wstring(clock) + days[t.wDayOfWeek] + date;
        if (key == textKey) return;
        textKey = key;
        RenderText(clock, days[t.wDayOfWeek], date);
    }

    HFONT Font(const wchar_t* face, float cssPx) {
        return CreateFontW(-(int)lroundf(cssPx * scale), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                           CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
    }

    // Layout follows the wallpaper's CSS: block at 25% height, centred; clock 3em Roboto,
    // day 5em Dancing Script (line-height .3em, box .4em, rotated -4deg, 70% opacity), date 3em Cinzel.
    void RenderText(const wchar_t* clock, const wchar_t* day, const wchar_t* date) {
        HDC dc = CreateCompatibleDC(nullptr);
        HFONT fClock = Font(L"Roboto", 48), fDay = Font(L"Dancing Script OT", 80), fDate = Font(L"Cinzel", 48);
        TEXTMETRICW tm;
        SelectObject(dc, fClock);
        GetTextMetricsW(dc, &tm);
        int clockH = showTime ? tm.tmHeight : 0;
        SelectObject(dc, fDay);
        GetTextMetricsW(dc, &tm);
        int dayCell = tm.tmHeight;
        SelectObject(dc, fDate);
        GetTextMetricsW(dc, &tm);
        int dateH = showDate ? tm.tmHeight : 0;
        int dayBox = showDate ? (int)lroundf(32 * scale) : 0;

        int top = (int)(h * 0.25f);
        int y0 = std::max(0, top - (int)(48 * scale));
        int th = std::min((int)h - y0, (top - y0) + clockH + dayBox + dateH + (int)(64 * scale));

        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -th;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HBITMAP bmp = th > 0 ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
        if (!bmp) {
            DeleteObject(fClock);
            DeleteObject(fDay);
            DeleteObject(fDate);
            DeleteDC(dc);
            return;
        }
        HGDIOBJ oldBmp = SelectObject(dc, bmp);
        memset(bits, 0, (size_t)w * th * 4);
        SetBkMode(dc, TRANSPARENT);
        UINT flags = DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX;

        int y = top - y0;
        if (showTime) {
            SelectObject(dc, fClock);
            SetTextColor(dc, RGB(255, 255, 255));
            RECT r{0, y, (LONG)w, y + clockH};
            DrawTextW(dc, clock, -1, &r, flags);
            y += clockH;
        }
        if (showDate) {
            // Day: glyphs centred on the 24px line box, rotated about the centre of the 32px box.
            float cx = w / 2.0f, lineMid = y + 12 * scale, boxMid = y + 16 * scale;
            SetGraphicsMode(dc, GM_ADVANCED);
            float a = -4.0f * 3.14159265f / 180.0f;
            XFORM x{cosf(a), sinf(a), -sinf(a), cosf(a), 0, 0};
            x.eDx = cx - (cx * x.eM11 + boxMid * x.eM21);
            x.eDy = boxMid - (cx * x.eM12 + boxMid * x.eM22);
            SetWorldTransform(dc, &x);
            SelectObject(dc, fDay);
            SetTextColor(dc, RGB(179, 179, 179));  // 70% opacity once converted to alpha
            RECT r{0, (LONG)(lineMid - dayCell / 2.0f), (LONG)w, (LONG)(lineMid + dayCell / 2.0f)};
            DrawTextW(dc, day, -1, &r, flags);
            ModifyWorldTransform(dc, nullptr, MWT_IDENTITY);
            SetGraphicsMode(dc, GM_COMPATIBLE);
            y += dayBox;

            SelectObject(dc, fDate);
            SetTextColor(dc, RGB(255, 255, 255));
            RECT r2{0, y, (LONG)w, y + dateH};
            DrawTextW(dc, date, -1, &r2, flags);
        }
        GdiFlush();

        // White-on-black coverage becomes alpha; colour comes from the font colour setting.
        BYTE fb = (BYTE)(fontColor[2] * 255), fg = (BYTE)(fontColor[1] * 255), fr = (BYTE)(fontColor[0] * 255);
        BYTE* px = (BYTE*)bits;
        for (size_t i = 0; i < (size_t)w * th; i++, px += 4) {
            BYTE a = std::max(px[0], std::max(px[1], px[2]));
            px[0] = fb; px[1] = fg; px[2] = fr; px[3] = a;
        }

        if (!textTex || th != textH) {
            SafeRelease(textSrv);
            SafeRelease(textTex);
            D3D11_TEXTURE2D_DESC td{};
            td.Width = w;
            td.Height = th;
            td.MipLevels = td.ArraySize = 1;
            td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            g.device->CreateTexture2D(&td, nullptr, &textTex);
            if (textTex) g.device->CreateShaderResourceView(textTex, nullptr, &textSrv);
        }
        if (textTex) g.context->UpdateSubresource(textTex, 0, nullptr, bits, w * 4, 0);
        textY0 = y0;
        textH = th;

        SelectObject(dc, oldBmp);
        DeleteObject(bmp);
        DeleteObject(fClock);
        DeleteObject(fDay);
        DeleteObject(fDate);
        DeleteDC(dc);
    }
};

ID3D11DepthStencilView* CreateDepth(Gfx& g, UINT w, UINT h) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ID3D11Texture2D* tex = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    if (SUCCEEDED(g.device->CreateTexture2D(&td, nullptr, &tex))) g.device->CreateDepthStencilView(tex, nullptr, &dsv);
    SafeRelease(tex);
    return dsv;
}

class SceneRenderer : public Renderer {
    Gfx& g;
    HillScene scene;
    IDXGISwapChain1* swap = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    HANDLE frameWait = nullptr;  // set when the scene is paced by the display's vblank
    int fps;
    double interval = 0, last = 0;
    bool paused = false;
    // The achieved frame rate is logged once, a few seconds after start.
    double rateStart = 0;
    int rateFrames = 0;
    bool rateLogged = false;

public:
    SceneRenderer(Gfx& gfx, int fps) : g(gfx), scene(gfx), fps(fps) {}

    bool Init(HWND parent, const RECT& r, const Monitor& mon, const Wallpaper& wp) {
        UINT w = r.right - r.left, h = r.bottom - r.top;
        if (wp.scene != "hill") { Log("Unknown scene '%s'", wp.scene.c_str()); return false; }
        if (!scene.Init(wp, w, h, mon.dpi)) return false;
        hwnd = CreateSurfaceWindow(parent, r);
        if (!hwnd) return false;
        bool matchDisplay = fps <= 0;
        swap = CreateSwapChain(g, hwnd, w, h, matchDisplay);
        if (!swap) return false;
        DXGI_SWAP_CHAIN_DESC1 desc{};
        swap->GetDesc1(&desc);
        IDXGISwapChain2* swap2 = nullptr;
        if ((desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) &&
            SUCCEEDED(swap->QueryInterface(IID_PPV_ARGS(&swap2)))) {
            swap2->SetMaximumFrameLatency(1);
            frameWait = swap2->GetFrameLatencyWaitableObject();
            SafeRelease(swap2);
        }
        // Without a waitable (blt fallback) a matched rate falls back to a timer at the refresh rate.
        interval = 1.0 / std::max(1, std::min(matchDisplay ? (int)mon.refreshHz : fps, 240));
        ID3D11Texture2D* bb = nullptr;
        swap->GetBuffer(0, IID_PPV_ARGS(&bb));
        if (bb) g.device->CreateRenderTargetView(bb, nullptr, &rtv);
        SafeRelease(bb);
        dsv = CreateDepth(g, w, h);
        Log("Scene: %s (%ux%u, %.0f fps%s)", wp.scene.c_str(), w, h, 1.0 / interval,
            frameWait ? ", synced to the display" : "");
        return rtv && dsv;
    }

    ~SceneRenderer() override {
        if (g.context) g.context->OMSetRenderTargets(0, nullptr, nullptr);
        if (frameWait) CloseHandle(frameWait);
        SafeRelease(dsv);
        SafeRelease(rtv);
        SafeRelease(swap);
        if (hwnd) DestroyWindow(hwnd);
    }

    void SetPaused(bool p) override {
        if (p == paused) return;
        paused = p;
        last = 0;  // don't jump the animation forward after a pause
        rateStart = 0;
        rateFrames = 0;
    }

    double Interval() const override { return paused ? 0 : interval; }
    HANDLE FrameWaitable() const override { return paused ? nullptr : frameWait; }

    void Tick(double now) override {
        double dt = last ? std::min(now - last, 0.1) : interval;
        last = now;
        // The flip model unbinds the back buffer after Present; rebind each frame.
        scene.Render(rtv, dsv, dt);
        swap->Present(frameWait ? 1 : 0, 0);
        if (!rateLogged) {
            if (!rateStart) rateStart = now;
            else if (++rateFrames, now - rateStart >= 5) {
                Log("Scene: running at %.1f fps", rateFrames / (now - rateStart));
                rateLogged = true;
            }
        }
    }
};

}  // namespace

std::unique_ptr<Renderer> CreateSceneRenderer(Gfx& g, HWND parent, const RECT& r, const Monitor& mon,
                                              const Wallpaper& wp, const EngineSettings& settings) {
    std::unique_ptr<SceneRenderer> s(new SceneRenderer(g, settings.sceneFps));
    if (!s->Init(parent, r, mon, wp)) return nullptr;
    return std::move(s);
}

bool SnapshotScene(Gfx& g, const Wallpaper& wp, UINT w, UINT h, UINT dpi, double seconds, const std::wstring& out) {
    HillScene scene(g);
    if (!scene.Init(wp, w, h, dpi)) return false;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D *target = nullptr, *staging = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    g.device->CreateTexture2D(&td, nullptr, &target);
    g.device->CreateRenderTargetView(target, nullptr, &rtv);
    td.BindFlags = 0;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    g.device->CreateTexture2D(&td, nullptr, &staging);
    ID3D11DepthStencilView* dsv = CreateDepth(g, w, h);
    bool ok = false;
    if (rtv && dsv && staging) {
        scene.Render(rtv, dsv, seconds);
        g.context->CopyResource(staging, target);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g.context->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
            ok = SavePng(out, m.pData, w, h, m.RowPitch);
            g.context->Unmap(staging, 0);
        }
    }
    SafeRelease(dsv);
    SafeRelease(rtv);
    SafeRelease(staging);
    SafeRelease(target);
    return ok;
}
