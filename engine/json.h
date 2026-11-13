// Minimal JSON reader (UTF-8). Accepts // line comments, which Lively's files use.
#pragma once
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object };
    Type type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;

    const Json& operator[](const char* key) const {
        if (type == Object)
            for (auto& kv : obj)
                if (kv.first == key) return kv.second;
        return none();
    }
    const Json& operator[](size_t i) const { return (type == Array && i < arr.size()) ? arr[i] : none(); }

    std::string str(const std::string& def = "") const { return type == String ? s : def; }
    double num(double def = 0) const { return type == Number ? n : def; }
    bool boolean(bool def = false) const { return type == Bool ? b : def; }
    bool isNull() const { return type == Null; }

    static Json parse(const std::string& text) {
        size_t i = 0;
        Json j;
        if (!value(text, i, j)) return Json();
        return j;
    }

private:
    static const Json& none() {
        static Json n;
        return n;
    }

    static void ws(const std::string& t, size_t& i) {
        while (i < t.size()) {
            char c = t[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') i++;
            else if (c == '/' && i + 1 < t.size() && t[i + 1] == '/')
                while (i < t.size() && t[i] != '\n') i++;
            else break;
        }
    }

    static void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }

    static bool string(const std::string& t, size_t& i, std::string& out) {
        if (t[i] != '"') return false;
        i++;
        while (i < t.size() && t[i] != '"') {
            char c = t[i++];
            if (c != '\\') { out += c; continue; }
            if (i >= t.size()) return false;
            char e = t[i++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                if (i + 4 > t.size()) return false;
                unsigned cp = strtoul(t.substr(i, 4).c_str(), nullptr, 16);
                i += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= t.size() && t[i] == '\\' && t[i + 1] == 'u') {
                    unsigned lo = strtoul(t.substr(i + 2, 4).c_str(), nullptr, 16);
                    i += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(out, cp);
                break;
            }
            default: out += e; break;
            }
        }
        if (i >= t.size()) return false;
        i++;
        return true;
    }

    static bool value(const std::string& t, size_t& i, Json& j) {
        ws(t, i);
        if (i >= t.size()) return false;
        char c = t[i];
        if (c == '{') {
            j.type = Object;
            i++;
            ws(t, i);
            if (i < t.size() && t[i] == '}') { i++; return true; }
            for (;;) {
                ws(t, i);
                std::string key;
                if (i >= t.size() || !string(t, i, key)) return false;
                ws(t, i);
                if (i >= t.size() || t[i] != ':') return false;
                i++;
                Json v;
                if (!value(t, i, v)) return false;
                j.obj.emplace_back(std::move(key), std::move(v));
                ws(t, i);
                if (i < t.size() && t[i] == ',') { i++; continue; }
                if (i < t.size() && t[i] == '}') { i++; return true; }
                return false;
            }
        }
        if (c == '[') {
            j.type = Array;
            i++;
            ws(t, i);
            if (i < t.size() && t[i] == ']') { i++; return true; }
            for (;;) {
                Json v;
                if (!value(t, i, v)) return false;
                j.arr.push_back(std::move(v));
                ws(t, i);
                if (i < t.size() && t[i] == ',') { i++; continue; }
                if (i < t.size() && t[i] == ']') { i++; return true; }
                return false;
            }
        }
        if (c == '"') { j.type = String; return string(t, i, j.s); }
        if (t.compare(i, 4, "true") == 0) { j.type = Bool; j.b = true; i += 4; return true; }
        if (t.compare(i, 5, "false") == 0) { j.type = Bool; j.b = false; i += 5; return true; }
        if (t.compare(i, 4, "null") == 0) { j.type = Null; i += 4; return true; }
        char* end = nullptr;
        j.n = strtod(t.c_str() + i, &end);
        if (end == t.c_str() + i) return false;
        j.type = Number;
        i = end - t.c_str();
        return true;
    }
};
