#include "recipe.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>

#include "hash.h"
#include "ops.h"

namespace forja {

Json new_behavior() {
    Json b = Json::obj();
    b.set("formato", kFormatoReceita);
    Json nos = Json::arr();
    Json n0 = Json::obj();
    n0.set("id", "n0");
    n0.set("op", "entrada.ativar");
    n0.set("v", 1);
    n0.set("p", Json::obj());
    nos.push(n0);
    b.set("nos", nos);
    b.set("fluxo", Json::arr());
    b.set("dados", Json::arr());
    b.set("expor", Json::arr());
    return b;
}

Json canonical_behavior(const Json& behavior) {
    Json c = Json::obj();
    c.set("formato", kFormatoReceita);
    std::vector<Json> nos;
    for (auto& n : behavior.get("nos").items()) {
        Json m = Json::obj();
        m.set("id", n.get("id"));
        m.set("op", n.get("op"));
        m.set("v", Json(n.get("v").as_int(1)));
        Json p = n.get("p").is_obj() ? n.get("p") : Json::obj();
        const OpDef* op = find_op(n.get("op").as_str(), static_cast<int>(n.get("v").as_int(1)));
        if (op && !op->planejada) {
            std::vector<std::string> errs;
            normalize_params(*op, p, errs);  // erros ficam para o validador; aqui só padrões
        }
        m.set("p", p);
        nos.push_back(m);
    }
    std::sort(nos.begin(), nos.end(), [](const Json& a, const Json& b) { return a.get("id").as_str() < b.get("id").as_str(); });
    Json na = Json::arr();
    for (auto& n : nos) na.push(n);
    c.set("nos", na);

    auto sorted = [&](const char* key, std::initializer_list<const char*> fields) {
        std::vector<Json> v;
        for (auto& e : behavior.get(key).items()) {
            Json m = Json::obj();
            for (auto* f : fields) m.set(f, e.get(f));
            v.push_back(m);
        }
        std::sort(v.begin(), v.end(), [](const Json& a, const Json& b) { return a.dump(true) < b.dump(true); });
        v.erase(std::unique(v.begin(), v.end()), v.end());
        Json arr = Json::arr();
        for (auto& x : v) arr.push(x);
        return arr;
    };
    c.set("fluxo", sorted("fluxo", {"de", "porta", "para"}));
    c.set("dados", sorted("dados", {"de", "porta", "para", "entrada"}));
    c.set("expor", sorted("expor", {"nome", "no", "param"}));
    return c;
}

std::string digest_of(const Json& canonical) { return "sha256:" + sha256_hex(canonical.dump(true)); }

std::vector<std::string> refs_of(const Json& behavior) {
    std::vector<std::string> r;
    for (auto& n : behavior.get("nos").items())
        if (n.get("op").as_str() == "sub.incorporar") {
            std::string ref = n.get("p").get("ref").as_str();
            if (!ref.empty()) r.push_back(ref);
        }
    return r;
}

std::string ObjectStore::put(const Json& behavior) {
    Json c = canonical_behavior(behavior);
    std::string d = digest_of(c);
    if (!objs_.count(d)) {
        objs_[d] = c;
        if (!dir_.empty()) write_file_atomic(dir_ + "/" + d.substr(7) + ".json", c.dump(true));
    }
    return d;
}

const Json* ObjectStore::get(const std::string& digest) const {
    auto it = objs_.find(digest);
    return it == objs_.end() ? nullptr : &it->second;
}

std::vector<std::string> ObjectStore::closure(const std::string& root, std::vector<std::string>* missing) const {
    std::vector<std::string> out;
    std::set<std::string> seen;
    std::vector<std::string> stack = {root};
    while (!stack.empty()) {
        std::string d = stack.back();
        stack.pop_back();
        if (seen.count(d)) continue;
        seen.insert(d);
        const Json* o = get(d);
        if (!o) { if (missing) missing->push_back(d); continue; }
        out.push_back(d);
        for (auto& r : refs_of(*o)) stack.push_back(r);
    }
    return out;
}

bool ObjectStore::attach_dir(const std::string& dir) {
    dir_ = dir;
    if (!make_dirs(dir)) return false;
    for (auto& f : list_dir(dir, ".json")) {
        std::string txt;
        if (!read_file(dir + "/" + f, txt)) continue;
        Json j;
        if (!Json::parse(txt, j)) continue;
        std::string d = digest_of(j);
        // Verificação de integridade: o nome do arquivo deve ser o digest do conteúdo.
        if (d.substr(7) + ".json" != f) {
            std::fprintf(stderr, "[forja] objeto corrompido ignorado: %s\n", f.c_str());
            continue;
        }
        objs_[d] = j;
    }
    return true;
}

bool read_file(const std::string& path, std::string& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool write_file_atomic(const std::string& path, const std::string& data) {
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (std::fclose(f) == 0) && ok;
    if (!ok) return false;
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    return !ec;
}

bool make_dirs(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec || std::filesystem::is_directory(path, ec);
}

std::vector<std::string> list_dir(const std::string& path, const std::string& suffix) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(path, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        std::string n = it->path().filename().string();
        if (n.size() >= suffix.size() && n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0) out.push_back(n);
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace forja
