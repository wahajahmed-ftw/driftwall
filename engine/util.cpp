#include "engine.h"
#include <cstdarg>
#include <cstdio>

static FILE* g_log = nullptr;

std::wstring Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

bool ReadFileUtf8(const std::wstring& path, std::string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    GetFileSizeEx(f, &size);
    out.resize((size_t)size.QuadPart);
    DWORD read = 0;
    BOOL ok = out.empty() || ReadFile(f, &out[0], (DWORD)out.size(), &read, nullptr);
    CloseHandle(f);
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);  // UTF-8 BOM
    return ok;
}

void LogOpen(const std::wstring& path) {
    g_log = _wfopen(path.c_str(), L"w");
}

void Log(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a;
    va_start(a, fmt);
    vfprintf(g_log, fmt, a);
    va_end(a);
    fputc('\n', g_log);
    fflush(g_log);
}

double NowSeconds() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
}

bool LoadWallpaper(const std::wstring& libraryDir, const std::string& id, Wallpaper& out) {
    if (id.empty() || id.find_first_of("\\/:") != std::string::npos || id.find("..") != std::string::npos) return false;
    out = Wallpaper();
    out.id = id;
    out.dir = libraryDir + L"\\" + Widen(id);
    std::string text;
    if (!ReadFileUtf8(out.dir + L"\\wallpaper.json", text)) return false;
    Json j = Json::parse(text);
    out.title = j["title"].str(id);
    out.type = j["type"].str();
    out.scene = j["scene"].str();
    out.params = j["params"];
    std::string file = j["file"].str();
    if (!file.empty()) {
        if (file.find_first_of("\\/") != std::string::npos || file.find("..") != std::string::npos) return false;
        out.file = out.dir + L"\\" + Widen(file);
    }
    return !out.type.empty();
}
