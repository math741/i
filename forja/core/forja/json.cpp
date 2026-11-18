#include "json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace forja {

bool Json::has(const std::string& k) const {
    if (t_ != Type::Obj) return false;
    for (auto& kv : o_) if (kv.first == k) return true;
    return false;
}

const Json& Json::get(const std::string& k) const {
    static const Json n;
    if (t_ != Type::Obj) return n;
    for (auto& kv : o_) if (kv.first == k) return kv.second;
    return n;
}

Json* Json::find(const std::string& k) {
    if (t_ != Type::Obj) return nullptr;
    for (auto& kv : o_) if (kv.first == k) return &kv.second;
    return nullptr;
}

Json& Json::set(const std::string& k, Json v) {
    if (t_ != Type::Obj) *this = obj();
    for (auto& kv : o_) if (kv.first == k) { kv.second = std::move(v); return kv.second; }
    o_.emplace_back(k, std::move(v));
    return o_.back().second;
}

Json& Json::operator[](const std::string& k) {
    if (t_ != Type::Obj) *this = obj();
    for (auto& kv : o_) if (kv.first == k) return kv.second;
    o_.emplace_back(k, Json());
    return o_.back().second;
}

bool Json::erase(const std::string& k) {
    if (t_ != Type::Obj) return false;
    for (size_t i = 0; i < o_.size(); ++i)
        if (o_[i].first == k) { o_.erase(o_.begin() + static_cast<long>(i)); return true; }
    return false;
}

bool Json::operator==(const Json& o) const {
    if (is_num() && o.is_num()) {
        if (t_ == Type::Int && o.t_ == Type::Int) return i_ == o.i_;
        return as_num() == o.as_num();
    }
    if (t_ != o.t_) return false;
    switch (t_) {
        case Type::Null: return true;
        case Type::Bool: return b_ == o.b_;
        case Type::Str: return s_ == o.s_;
        case Type::Arr: return a_ == o.a_;
        case Type::Obj: {
            if (o_.size() != o.o_.size()) return false;
            for (auto& kv : o_) {
                if (!o.has(kv.first)) return false;
                if (!(kv.second == o.get(kv.first))) return false;
            }
            return true;
        }
        default: return false;
    }
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
    return out;
}

static void dump_num(std::string& out, double d) {
    if (!std::isfinite(d)) { out += "null"; return; }
    if (d == std::floor(d) && std::fabs(d) < 1e15) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
        out += buf;
        return;
    }
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.6g", d);
    out += buf;
}

void Json::dump_to(std::string& out, bool canonical) const {
    switch (t_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += b_ ? "true" : "false"; break;
        case Type::Int: {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(i_));
            out += buf;
            break;
        }
        case Type::Num: dump_num(out, d_); break;
        case Type::Str: out += json_escape(s_); break;
        case Type::Arr: {
            out.push_back('[');
            for (size_t i = 0; i < a_.size(); ++i) {
                if (i) out.push_back(',');
                a_[i].dump_to(out, canonical);
            }
            out.push_back(']');
            break;
        }
        case Type::Obj: {
            out.push_back('{');
            if (canonical) {
                std::vector<const std::pair<std::string, Json>*> v;
                v.reserve(o_.size());
                for (auto& kv : o_) v.push_back(&kv);
                std::sort(v.begin(), v.end(), [](auto* a, auto* b) { return a->first < b->first; });
                for (size_t i = 0; i < v.size(); ++i) {
                    if (i) out.push_back(',');
                    out += json_escape(v[i]->first);
                    out.push_back(':');
                    v[i]->second.dump_to(out, canonical);
                }
            } else {
                for (size_t i = 0; i < o_.size(); ++i) {
                    if (i) out.push_back(',');
                    out += json_escape(o_[i].first);
                    out.push_back(':');
                    o_[i].second.dump_to(out, canonical);
                }
            }
            out.push_back('}');
            break;
        }
    }
}

std::string Json::dump(bool canonical) const {
    std::string out;
    dump_to(out, canonical);
    return out;
}

static bool is_scalar_array(const std::vector<Json>& a) {
    for (auto& x : a) if (x.is_arr() || x.is_obj()) return false;
    return true;
}

void Json::pretty_to(std::string& out, int indent, int depth) const {
    auto nl = [&](int d) { out.push_back('\n'); out.append(static_cast<size_t>(d * indent), ' '); };
    if (t_ == Type::Arr) {
        if (a_.empty()) { out += "[]"; return; }
        if (is_scalar_array(a_) && a_.size() <= 8) { dump_to(out, false); return; }
        out.push_back('[');
        for (size_t i = 0; i < a_.size(); ++i) {
            if (i) out.push_back(',');
            nl(depth + 1);
            a_[i].pretty_to(out, indent, depth + 1);
        }
        nl(depth);
        out.push_back(']');
    } else if (t_ == Type::Obj) {
        if (o_.empty()) { out += "{}"; return; }
        out.push_back('{');
        for (size_t i = 0; i < o_.size(); ++i) {
            if (i) out.push_back(',');
            nl(depth + 1);
            out += json_escape(o_[i].first);
            out += ": ";
            o_[i].second.pretty_to(out, indent, depth + 1);
        }
        nl(depth);
        out.push_back('}');
    } else {
        dump_to(out, false);
    }
}

std::string Json::pretty(int indent) const {
    std::string out;
    pretty_to(out, indent, 0);
    out.push_back('\n');
    return out;
}

namespace {
struct Parser {
    const std::string& s;
    size_t p = 0;
    std::string err;
    explicit Parser(const std::string& in) : s(in) {}

    void ws() { while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t')) ++p; }
    bool fail(const char* m) { if (err.empty()) { err = m; err += " na posição " + std::to_string(p); } return false; }

    static void utf8(std::string& o, uint32_t cp) {
        if (cp < 0x80) o.push_back(static_cast<char>(cp));
        else if (cp < 0x800) { o.push_back(static_cast<char>(0xC0 | (cp >> 6))); o.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { o.push_back(static_cast<char>(0xE0 | (cp >> 12))); o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); o.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else { o.push_back(static_cast<char>(0xF0 | (cp >> 18))); o.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); o.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    }
    bool hex4(uint32_t& v) {
        if (p + 4 > s.size()) return fail("escape \\u incompleto");
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s[p++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else return fail("hex inválido");
        }
        return true;
    }
    bool str(std::string& o) {
        if (s[p] != '"') return fail("esperava string");
        ++p;
        while (p < s.size()) {
            char c = s[p++];
            if (c == '"') return true;
            if (c == '\\') {
                if (p >= s.size()) return fail("escape incompleto");
                char e = s[p++];
                switch (e) {
                    case '"': o.push_back('"'); break;
                    case '\\': o.push_back('\\'); break;
                    case '/': o.push_back('/'); break;
                    case 'b': o.push_back('\b'); break;
                    case 'f': o.push_back('\f'); break;
                    case 'n': o.push_back('\n'); break;
                    case 'r': o.push_back('\r'); break;
                    case 't': o.push_back('\t'); break;
                    case 'u': {
                        uint32_t cp = 0;
                        if (!hex4(cp)) return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            if (p + 6 <= s.size() && s[p] == '\\' && s[p + 1] == 'u') {
                                p += 2;
                                uint32_t lo = 0;
                                if (!hex4(lo)) return false;
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            }
                        }
                        utf8(o, cp);
                        break;
                    }
                    default: return fail("escape desconhecido");
                }
            } else {
                o.push_back(c);
            }
        }
        return fail("string sem fim");
    }
    bool value(Json& out, int depth) {
        if (depth > 64) return fail("profundidade excessiva");
        ws();
        if (p >= s.size()) return fail("fim inesperado");
        char c = s[p];
        if (c == '{') {
            ++p;
            out = Json::obj();
            ws();
            if (p < s.size() && s[p] == '}') { ++p; return true; }
            while (true) {
                ws();
                std::string k;
                if (p >= s.size() || !str(k)) return fail("chave inválida");
                ws();
                if (p >= s.size() || s[p] != ':') return fail("esperava ':'");
                ++p;
                Json v;
                if (!value(v, depth + 1)) return false;
                out.set(k, std::move(v));
                ws();
                if (p < s.size() && s[p] == ',') { ++p; continue; }
                if (p < s.size() && s[p] == '}') { ++p; return true; }
                return fail("esperava ',' ou '}'");
            }
        }
        if (c == '[') {
            ++p;
            out = Json::arr();
            ws();
            if (p < s.size() && s[p] == ']') { ++p; return true; }
            while (true) {
                Json v;
                if (!value(v, depth + 1)) return false;
                out.push(std::move(v));
                ws();
                if (p < s.size() && s[p] == ',') { ++p; continue; }
                if (p < s.size() && s[p] == ']') { ++p; return true; }
                return fail("esperava ',' ou ']'");
            }
        }
        if (c == '"') { std::string v; if (!str(v)) return false; out = Json(std::move(v)); return true; }
        if (s.compare(p, 4, "true") == 0) { p += 4; out = Json(true); return true; }
        if (s.compare(p, 5, "false") == 0) { p += 5; out = Json(false); return true; }
        if (s.compare(p, 4, "null") == 0) { p += 4; out = Json(); return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            size_t st = p;
            bool flt = false;
            if (s[p] == '-') ++p;
            while (p < s.size() && ((s[p] >= '0' && s[p] <= '9') || s[p] == '.' || s[p] == 'e' || s[p] == 'E' || s[p] == '+' || s[p] == '-')) {
                if (s[p] == '.' || s[p] == 'e' || s[p] == 'E') flt = true;
                ++p;
            }
            std::string num = s.substr(st, p - st);
            if (flt) {
                char* end = nullptr;
                double d = std::strtod(num.c_str(), &end);
                if (!end || *end) return fail("número inválido");
                out = Json(d);
            } else {
                char* end = nullptr;
                long long v = std::strtoll(num.c_str(), &end, 10);
                if (!end || *end) return fail("número inválido");
                out = Json(static_cast<int64_t>(v));
            }
            return true;
        }
        return fail("valor inválido");
    }
};
}  // namespace

bool Json::parse(const std::string& text, Json& out, std::string* err) {
    Parser ps(text);
    Json v;
    if (!ps.value(v, 0)) { if (err) *err = ps.err; return false; }
    ps.ws();
    if (ps.p != text.size()) { if (err) *err = "lixo após o JSON na posição " + std::to_string(ps.p); return false; }
    out = std::move(v);
    return true;
}

}  // namespace forja
