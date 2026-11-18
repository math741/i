#include "workshop.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>

#include "runtime.h"

namespace forja {

// ---------------- registros ----------------
Json RevRecord::to_json() const {
    Json j = Json::obj();
    j.set("rev", rev); j.set("digest", digest); j.set("pai", pai); j.set("nome", nome); j.set("nota", nota);
    j.set("rotulos", rotulos.is_obj() ? rotulos : Json::obj()); j.set("deps", deps.is_obj() ? deps : Json::obj());
    j.set("aparencia", aparencia.is_obj() ? aparencia : Json::obj()); j.set("quando", quando);
    return j;
}
RevRecord RevRecord::from_json(const Json& j) {
    RevRecord r;
    r.rev = static_cast<int>(j.get("rev").as_int()); r.digest = j.get("digest").as_str(); r.pai = j.get("pai").as_str();
    r.nome = j.get("nome").as_str(); r.nota = j.get("nota").as_str(); r.rotulos = j.get("rotulos"); r.deps = j.get("deps");
    r.aparencia = j.get("aparencia"); r.quando = j.get("quando").as_int();
    return r;
}
Json Creation::to_json() const {
    Json j = Json::obj();
    j.set("formato", "forja.criacao/1");
    j.set("id", id); j.set("nome", nome); j.set("autor", autor); j.set("origem", origem);
    j.set("rascunho", rascunho);
    Json rs = Json::arr(); for (auto& r : revs) rs.push(r.to_json()); j.set("revisoes", rs);
    Json u = Json::arr();
    size_t from = undo.size() > 50 ? undo.size() - 50 : 0;
    for (size_t i = from; i < undo.size(); ++i) u.push(undo[i]);
    j.set("desfazer", u);
    Json rd = Json::arr(); for (auto& x : redo) rd.push(x); j.set("refazer", rd);
    j.set("atualizacoes", atualizacoes);
    return j;
}
Creation Creation::from_json(const Json& j) {
    Creation c;
    c.id = j.get("id").as_str(); c.nome = j.get("nome").as_str(); c.autor = j.get("autor").as_str(); c.origem = j.get("origem").as_str();
    c.rascunho = j.get("rascunho");
    for (auto& r : j.get("revisoes").items()) c.revs.push_back(RevRecord::from_json(r));
    for (auto& u : j.get("desfazer").items()) c.undo.push_back(u);
    for (auto& r : j.get("refazer").items()) c.redo.push_back(r);
    c.atualizacoes = j.get("atualizacoes").is_arr() ? j.get("atualizacoes") : Json::arr();
    return c;
}

static Json empty_draft() {
    Json d = Json::obj();
    d.set("comportamento", new_behavior());
    d.set("rotulos", Json::obj());
    d.set("deps", Json::obj());
    d.set("aparencia", Json::obj());
    return d;
}

// ---------------- acervo ----------------
bool Library::open(const std::string& root) {
    root_ = root;
    if (root.empty()) return true;
    if (!make_dirs(root + "/criacoes")) return false;
    if (!store.attach_dir(root + "/objetos")) return false;
    for (auto& f : list_dir(root + "/criacoes", ".json")) {
        std::string txt;
        if (!read_file(root + "/criacoes/" + f, txt)) continue;
        Json j;
        if (!Json::parse(txt, j)) { std::fprintf(stderr, "[forja] criação ilegível: %s\n", f.c_str()); continue; }
        Creation c = Creation::from_json(j);
        if (!c.id.empty()) creations[c.id] = c;
    }
    return true;
}

Creation* Library::get(const std::string& id) {
    auto it = creations.find(id);
    return it == creations.end() ? nullptr : &it->second;
}

const Creation* Library::find_by_digest(const std::string& digest, const RevRecord** rec) const {
    for (auto& kv : creations)
        for (auto& r : kv.second.revs)
            if (r.digest == digest) { if (rec) *rec = &r; return &kv.second; }
    return nullptr;
}

bool Library::persist(const Creation& c) {
    if (root_.empty()) return true;
    return write_file_atomic(root_ + "/criacoes/" + c.id + ".json", c.to_json().pretty());
}

std::string Library::new_id(const std::string& nome) {
    std::string slug;
    for (unsigned char ch : nome) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) slug.push_back(static_cast<char>(ch));
        else if (ch >= 'A' && ch <= 'Z') slug.push_back(static_cast<char>(ch - 'A' + 'a'));
        else if (ch == ' ' || ch == '-' || ch == '_') { if (!slug.empty() && slug.back() != '-') slug.push_back('-'); }
        else if (ch >= 0x80) { /* acentos: descartados no id; o nome mantém */ }
    }
    if (slug.empty()) slug = "criacao";
    std::string id = "cr-" + slug;
    int n = 2;
    while (creations.count(id)) id = "cr-" + slug + "-" + std::to_string(n++);
    return id;
}

Resolver Library::resolver() const {
    return [this](const std::string& d) { return store.get(d); };
}

Naming Library::naming(const Creation* c) const {
    Naming n;
    n.rotulos = c ? c->rascunho.get("rotulos") : Json::obj();
    n.nome_sub = [this](const std::string& d) -> std::string {
        const RevRecord* r = nullptr;
        const Creation* cr = find_by_digest(d, &r);
        return cr && r ? r->nome + " r" + std::to_string(r->rev) : std::string();
    };
    n.rotulos_sub = [this](const std::string& d) -> Json {
        const RevRecord* r = nullptr;
        const Creation* cr = find_by_digest(d, &r);
        return cr && r ? r->rotulos : Json::obj();
    };
    return n;
}

Json Library::bundle(const std::string& digest) const {
    Json b = Json::obj();
    b.set("formato", kFormatoPacote);
    b.set("raiz", digest);
    std::vector<std::string> missing;
    Json objs = Json::obj(), nomes = Json::obj();
    for (auto& d : store.closure(digest, &missing)) {
        objs.set(d, *store.get(d));
        const RevRecord* r = nullptr;
        const Creation* c = find_by_digest(d, &r);
        if (c && r) {
            Json x = Json::obj();
            x.set("criacao", c->id); x.set("nome", r->nome); x.set("revisao", r->rev); x.set("autor", c->autor);
            x.set("rotulos", r->rotulos); x.set("deps", r->deps); x.set("aparencia", r->aparencia);
            nomes.set(d, x);
        }
    }
    b.set("objetos", objs);
    b.set("nomes", nomes);
    if (!missing.empty()) {
        Json m = Json::arr();
        for (auto& d : missing) m.push(d);
        b.set("faltando", m);
    }
    return b;
}

// ---------------- oficina ----------------
Workshop::Workshop(Library& lib, Rules rules, std::string autor) : lib_(lib), rules_(rules), autor_(std::move(autor)) {}

int64_t Workshop::now_ms() const {
    if (clock) return clock();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

Json Workshop::fail(const std::string& msg) const {
    Json j = Json::obj();
    j.set("ok", false);
    j.set("erro", msg);
    return j;
}

std::string Workshop::alloc_node_id(const Json& behavior) const {
    std::set<std::string> used;
    for (auto& n : behavior.get("nos").items()) used.insert(n.get("id").as_str());
    for (int i = 1;; ++i) {
        std::string id = "n" + std::to_string(i);
        if (!used.count(id)) return id;
    }
}

static Json* find_node(Json& beh, const std::string& id) {
    for (auto& n : beh["nos"].items()) if (n.get("id").as_str() == id) return &n;
    return nullptr;
}

static const char* completion_port(OpKind k) {
    switch (k) {
        case OpKind::Deslocar: case OpKind::Retornar: return "ao_chegar";
        case OpKind::Anel: case OpKind::Incorporar: case OpKind::Anteparo: case OpKind::Vigiar: case OpKind::Repetir: case OpKind::Amparo: return "ao_terminar";
        case OpKind::Marca: case OpKind::Esperar: return "depois";
        default: return nullptr;
    }
}

Json Workshop::analyze(Creation& c, const Json* behavior) {
    Naming nm = lib_.naming(&c);
    CompileOut co = compile_recipe(behavior ? *behavior : c.rascunho.get("comportamento"), lib_.resolver(), rules_, &nm);
    return co.analise;
}

bool Workshop::mutate(Creation& c, const std::function<bool(Json&, std::string&)>& fn, std::string& err) {
    Json before = c.rascunho;
    Json d = before;
    if (!fn(d, err)) return false;
    if (d == before) return true;
    c.undo.push_back(before);
    if (c.undo.size() > 200) c.undo.erase(c.undo.begin());
    c.redo.clear();
    c.rascunho = d;
    lib_.persist(c);
    return true;
}

// Aplica um comando de edição ao rascunho (sem histórico). Devolve {ok,...}.
Json Workshop::apply_edit(Creation& c, const Json& e, bool) {
    std::string cmd = e.get("cmd").as_str();
    Json res = Json::obj();
    res.set("ok", true);
    std::string err;
    bool ok = mutate(c, [&](Json& d, std::string& er) -> bool {
        Json& beh = d["comportamento"];
        if (!d.get("rotulos").is_obj()) d.set("rotulos", Json::obj());
        if (!d.get("deps").is_obj()) d.set("deps", Json::obj());
        if (cmd == "no.adicionar") {
            std::string opn = e.get("op").as_str();
            const OpDef* od = e.has("v") ? find_op(opn, static_cast<int>(e.get("v").as_int())) : latest_op(opn);
            if (!od) { er = "operação desconhecida '" + opn + "'"; return false; }
            std::string id = e.get("id").str_or(alloc_node_id(beh));
            if (find_node(beh, id)) { er = "já existe um nó '" + id + "'"; return false; }
            Json p = e.get("p").is_obj() ? e.get("p") : Json::obj();
            if (!od->planejada) {
                std::vector<std::string> errs;
                if (!normalize_params(*od, p, errs)) { er = od->rotulo + ": " + errs[0]; return false; }
            }
            Json n = Json::obj();
            n.set("id", id); n.set("op", od->nome); n.set("v", od->versao); n.set("p", p);
            beh["nos"].push(n);
            if (e.has("rotulo")) d["rotulos"].set(id, e.get("rotulo"));
            res.set("no", id);
            if (od->planejada) res.set("aviso", "operação planejada '" + od->nome + "': a receita fica preservada, mas só executa quando a operação existir");
            if (e.has("ligar_de")) {
                Json f = Json::obj();
                f.set("de", e.get("ligar_de")); f.set("porta", e.get("porta")); f.set("para", id);
                beh["fluxo"].push(f);
            }
            return true;
        }
        if (cmd == "no.remover") {
            std::string id = e.get("no").as_str();
            Json* n = find_node(beh, id);
            if (!n) { er = "nó inexistente '" + id + "'"; return false; }
            if (n->get("op").as_str() == "entrada.ativar") { er = "o nó Ativar é a raiz e não pode ser removido"; return false; }
            auto& nos = beh["nos"].items();
            nos.erase(std::remove_if(nos.begin(), nos.end(), [&](const Json& x) { return x.get("id").as_str() == id; }), nos.end());
            auto& fl = beh["fluxo"].items();
            fl.erase(std::remove_if(fl.begin(), fl.end(), [&](const Json& x) { return x.get("de").as_str() == id || x.get("para").as_str() == id; }), fl.end());
            auto& da = beh["dados"].items();
            da.erase(std::remove_if(da.begin(), da.end(), [&](const Json& x) { return x.get("de").as_str() == id || x.get("para").as_str() == id; }), da.end());
            auto& ex = beh["expor"].items();
            ex.erase(std::remove_if(ex.begin(), ex.end(), [&](const Json& x) { return x.get("no").as_str() == id; }), ex.end());
            d["rotulos"].erase(id);
            return true;
        }
        if (cmd == "no.param") {
            std::string id = e.get("no").as_str(), nome = e.get("nome").as_str();
            Json* n = find_node(beh, id);
            if (!n) { er = "nó inexistente '" + id + "'"; return false; }
            const OpDef* od = find_op(n->get("op").as_str(), static_cast<int>(n->get("v").as_int(1)));
            if (!od || !od->param(nome)) { er = "parâmetro inexistente '" + nome + "'"; return false; }
            Json p = n->get("p");
            p.set(nome, e.get("valor"));
            std::vector<std::string> errs;
            if (!normalize_params(*od, p, errs)) { er = od->rotulo + ": " + errs[0]; return false; }
            (*n)["p"] = p;
            return true;
        }
        if (cmd == "no.sobrepor") {
            Json* n = find_node(beh, e.get("no").as_str());
            if (!n || n->get("op").as_str() != "sub.incorporar") { er = "o nó não é uma subcriação"; return false; }
            Json& sob = (*n)["p"]["sobrepor"];
            if (!sob.is_obj()) sob = Json::obj();
            if (e.get("valor").is_null()) sob.erase(e.get("nome").as_str());
            else sob.set(e.get("nome").as_str(), e.get("valor"));
            return true;
        }
        if (cmd == "no.rotulo") {
            if (!find_node(beh, e.get("no").as_str())) { er = "nó inexistente"; return false; }
            d["rotulos"].set(e.get("no").as_str(), e.get("rotulo"));
            return true;
        }
        if (cmd == "fluxo.ligar" || cmd == "fluxo.desligar" || cmd == "fluxo.trocar_porta") {
            std::string a = e.get("de").as_str(), port = e.get("porta").as_str(), b = e.get("para").as_str();
            Json* na = find_node(beh, a);
            Json* nb = find_node(beh, b);
            if (!na || !nb) { er = "nó inexistente na ligação"; return false; }
            auto& fl = beh["fluxo"].items();
            auto match = [&](const Json& x) { return x.get("de").as_str() == a && x.get("porta").as_str() == port && x.get("para").as_str() == b; };
            if (cmd == "fluxo.ligar") {
                const OpDef* oa = find_op(na->get("op").as_str(), static_cast<int>(na->get("v").as_int(1)));
                if (oa && oa->out_index(port) < 0) { er = (oa ? oa->rotulo : a) + " não tem a saída '" + port + "'"; return false; }
                for (auto& x : fl) if (match(x)) return true;
                Json f = Json::obj(); f.set("de", a); f.set("porta", port); f.set("para", b);
                fl.push_back(f);
                return true;
            }
            size_t before = fl.size();
            if (cmd == "fluxo.desligar") {
                fl.erase(std::remove_if(fl.begin(), fl.end(), match), fl.end());
                if (fl.size() == before) { er = "ligação inexistente"; return false; }
                return true;
            }
            std::string np = e.get("nova_porta").as_str();
            const OpDef* oa = find_op(na->get("op").as_str(), static_cast<int>(na->get("v").as_int(1)));
            if (oa && oa->out_index(np) < 0) { er = "saída inexistente '" + np + "'"; return false; }
            for (auto& x : fl) if (match(x)) { x.set("porta", np); return true; }
            er = "ligação inexistente";
            return false;
        }
        if (cmd == "dados.ligar") {
            auto& da = beh["dados"].items();
            std::string b = e.get("para").as_str(), in = e.get("entrada").as_str();
            if (!find_node(beh, e.get("de").as_str()) || !find_node(beh, b)) { er = "nó inexistente"; return false; }
            da.erase(std::remove_if(da.begin(), da.end(), [&](const Json& x) { return x.get("para").as_str() == b && x.get("entrada").as_str() == in; }), da.end());
            Json x = Json::obj(); x.set("de", e.get("de")); x.set("porta", e.get("porta")); x.set("para", b); x.set("entrada", in);
            da.push_back(x);
            return true;
        }
        if (cmd == "dados.desligar") {
            auto& da = beh["dados"].items();
            std::string b = e.get("para").as_str(), in = e.get("entrada").as_str();
            da.erase(std::remove_if(da.begin(), da.end(), [&](const Json& x) { return x.get("para").as_str() == b && x.get("entrada").as_str() == in; }), da.end());
            return true;
        }
        if (cmd == "expor") {
            auto& ex = beh["expor"].items();
            std::string nome = e.get("nome").as_str();
            ex.erase(std::remove_if(ex.begin(), ex.end(), [&](const Json& x) { return x.get("nome").as_str() == nome; }), ex.end());
            Json x = Json::obj(); x.set("nome", nome); x.set("no", e.get("no")); x.set("param", e.get("param"));
            ex.push_back(x);
            return true;
        }
        if (cmd == "expor.remover") {
            auto& ex = beh["expor"].items();
            std::string nome = e.get("nome").as_str();
            ex.erase(std::remove_if(ex.begin(), ex.end(), [&](const Json& x) { return x.get("nome").as_str() == nome; }), ex.end());
            return true;
        }
        if (cmd == "sequencia.inverter") {
            std::string entry;
            for (auto& n : beh.get("nos").items()) if (n.get("op").as_str() == "entrada.ativar") entry = n.get("id").as_str();
            auto outs = [&](const std::string& id) { std::vector<Json> v; for (auto& f : beh.get("fluxo").items()) if (f.get("de").as_str() == id) v.push_back(f); return v; };
            auto ins = [&](const std::string& id) { int k = 0; for (auto& f : beh.get("fluxo").items()) if (f.get("para").as_str() == id) ++k; return k; };
            std::vector<std::string> chain;
            std::string cur = entry;
            while (true) {
                auto o = outs(cur);
                if (o.empty()) break;
                if (o.size() > 1) { er = "inverter exige uma sequência linear (o nó '" + cur + "' tem ramificações)"; return false; }
                std::string nx = o[0].get("para").as_str();
                if (ins(nx) != 1) { er = "inverter exige uma sequência linear ('" + nx + "' tem mais de uma entrada)"; return false; }
                chain.push_back(nx);
                cur = nx;
                if (chain.size() > 64) { er = "sequência longa demais"; return false; }
            }
            if (chain.size() < 2) { er = "nada a inverter: a sequência tem menos de duas etapas"; return false; }
            for (auto& id : chain) {
                Json* n = find_node(beh, id);
                const OpDef* od = n ? find_op(n->get("op").as_str(), static_cast<int>(n->get("v").as_int(1))) : nullptr;
                if (!od || !completion_port(od->kind)) { er = "a etapa '" + id + "' não tem um fim incondicional; não dá para inverter através dela"; return false; }
            }
            std::set<std::string> in_chain(chain.begin(), chain.end());
            in_chain.insert(entry);
            auto& fl = beh["fluxo"].items();
            fl.erase(std::remove_if(fl.begin(), fl.end(), [&](const Json& x) { return in_chain.count(x.get("de").as_str()) && in_chain.count(x.get("para").as_str()); }), fl.end());
            auto add = [&](const std::string& a, const std::string& port, const std::string& b) {
                Json f = Json::obj(); f.set("de", a); f.set("porta", port); f.set("para", b); fl.push_back(f);
            };
            add(entry, "ao_ativar", chain.back());
            for (size_t i = chain.size() - 1; i > 0; --i) {
                Json* n = find_node(beh, chain[i]);
                const OpDef* od = find_op(n->get("op").as_str(), static_cast<int>(n->get("v").as_int(1)));
                add(chain[i], completion_port(od->kind), chain[i - 1]);
            }
            Json ord = Json::arr();
            for (size_t i = chain.size(); i > 0; --i) ord.push(chain[i - 1]);
            res.set("nova_ordem", ord);
            return true;
        }
        if (cmd == "incorporar") {
            Creation* t = lib_.get(e.get("alvo").as_str());
            if (!t) { er = "criação inexistente '" + e.get("alvo").as_str() + "'"; return false; }
            if (t->revs.empty()) { er = "'" + t->nome + "' ainda não tem revisão salva; salve antes de incorporar"; return false; }
            const RevRecord* r = &t->revs.back();
            if (e.has("revisao")) {
                r = nullptr;
                for (auto& x : t->revs) if (x.rev == e.get("revisao").as_int()) r = &x;
                if (!r) { er = "revisão inexistente"; return false; }
            }
            std::string id = e.get("id").str_or(alloc_node_id(beh));
            Json p = Json::obj();
            p.set("ref", r->digest);
            p.set("sobrepor", e.get("sobrepor").is_obj() ? e.get("sobrepor") : Json::obj());
            Json n = Json::obj();
            n.set("id", id); n.set("op", "sub.incorporar"); n.set("v", 1); n.set("p", p);
            beh["nos"].push(n);
            Json dep = Json::obj(); dep.set("criacao", t->id); dep.set("revisao", r->rev); dep.set("nome", r->nome);
            d["deps"].set(r->digest, dep);
            d["rotulos"].set(id, e.get("rotulo").str_or(r->nome));
            if (e.has("ligar_de")) {
                Json f = Json::obj(); f.set("de", e.get("ligar_de")); f.set("porta", e.get("porta")); f.set("para", id);
                beh["fluxo"].push(f);
            }
            res.set("no", id); res.set("digest", r->digest); res.set("revisao", r->rev);
            return true;
        }
        if (cmd == "aparencia") {
            Json& ap = d["aparencia"];
            if (!ap.is_obj()) ap = Json::obj();
            for (const char* k : {"cor", "material", "ritmo"}) if (e.has(k)) ap.set(k, e.get(k));
            return true;
        }
        er = "comando de edição desconhecido '" + cmd + "'";
        return false;
    }, err);
    if (!ok) return fail(err);
    return res;
}

Json Workshop::diff(const Json& a, const Json& b, const Json& da, const Json& db) const {
    Json ca = canonical_behavior(a), cb = canonical_behavior(b);
    Json out = Json::obj();
    out.set("digest_a", digest_of(ca));
    out.set("digest_b", digest_of(cb));
    out.set("iguais", digest_of(ca) == digest_of(cb));
    std::map<std::string, Json> na, nb;
    for (auto& n : ca.get("nos").items()) na[n.get("id").as_str()] = n;
    for (auto& n : cb.get("nos").items()) nb[n.get("id").as_str()] = n;
    Json add = Json::arr(), rem = Json::arr(), chg = Json::arr(), deps = Json::arr();
    for (auto& kv : nb) if (!na.count(kv.first)) { Json x = Json::obj(); x.set("no", kv.first); x.set("op", kv.second.get("op")); add.push(x); }
    for (auto& kv : na) if (!nb.count(kv.first)) { Json x = Json::obj(); x.set("no", kv.first); x.set("op", kv.second.get("op")); rem.push(x); }
    for (auto& kv : na) {
        auto it = nb.find(kv.first);
        if (it == nb.end()) continue;
        const Json& pa = kv.second.get("p");
        const Json& pb = it->second.get("p");
        if (kv.second.get("op").as_str() == "sub.incorporar") {
            std::string ra = pa.get("ref").as_str(), rb = pb.get("ref").as_str();
            if (ra != rb) {
                Json x = Json::obj();
                x.set("no", kv.first);
                Json fa = da.get(ra), fb = db.get(rb);
                x.set("de", fa.is_obj() ? fa : Json::obj()); x.set("para", fb.is_obj() ? fb : Json::obj());
                x.set("digest_de", ra); x.set("digest_para", rb);
                deps.push(x);
            }
            if (pa.get("sobrepor") != pb.get("sobrepor")) {
                Json x = Json::obj(); x.set("no", kv.first); x.set("nome", "sobrepor"); x.set("de", pa.get("sobrepor")); x.set("para", pb.get("sobrepor"));
                chg.push(x);
            }
            continue;
        }
        if (kv.second.get("v") != it->second.get("v")) {
            Json x = Json::obj(); x.set("no", kv.first); x.set("nome", "versão da operação"); x.set("de", kv.second.get("v")); x.set("para", it->second.get("v"));
            chg.push(x);
        }
        std::set<std::string> keys;
        for (auto& m : pa.members()) keys.insert(m.first);
        for (auto& m : pb.members()) keys.insert(m.first);
        for (auto& k : keys) {
            if (pa.get(k) != pb.get(k)) {
                Json x = Json::obj(); x.set("no", kv.first); x.set("nome", k); x.set("de", pa.get(k)); x.set("para", pb.get(k));
                chg.push(x);
            }
        }
    }
    auto edges = [](const Json& c, const char* key) { std::set<std::string> s; for (auto& e : c.get(key).items()) s.insert(e.dump(true)); return s; };
    Json fa = Json::arr(), fr = Json::arr();
    auto ea = edges(ca, "fluxo"), eb = edges(cb, "fluxo");
    for (auto& s : eb) if (!ea.count(s)) { Json j; Json::parse(s, j); fa.push(j); }
    for (auto& s : ea) if (!eb.count(s)) { Json j; Json::parse(s, j); fr.push(j); }
    out.set("nos_adicionados", add); out.set("nos_removidos", rem); out.set("parametros", chg);
    out.set("fluxo_adicionado", fa); out.set("fluxo_removido", fr); out.set("dependencias", deps);
    return out;
}

Json Workshop::state_of(Creation& c) {
    Json s = Json::obj();
    s.set("ok", true);
    s.set("criacao", c.id); s.set("nome", c.nome); s.set("autor", c.autor); s.set("origem", c.origem);
    s.set("rascunho", c.rascunho);
    Json rs = Json::arr(); for (auto& r : c.revs) rs.push(r.to_json()); s.set("revisoes", rs);
    s.set("analise", analyze(c));
    s.set("pode_desfazer", !c.undo.empty());
    s.set("pode_refazer", !c.redo.empty());
    // dependências e atualizações disponíveis
    Json deps = Json::arr();
    for (auto& n : c.rascunho.get("comportamento").get("nos").items()) {
        if (n.get("op").as_str() != "sub.incorporar") continue;
        std::string ref = n.get("p").get("ref").as_str();
        const RevRecord* r = nullptr;
        const Creation* dc = lib_.find_by_digest(ref, &r);
        Json x = Json::obj();
        x.set("no", n.get("id"));
        x.set("digest", ref);
        if (dc && r) {
            x.set("criacao", dc->id); x.set("nome", r->nome); x.set("revisao", r->rev);
            const RevRecord& last = dc->revs.back();
            x.set("ultima_revisao", last.rev); x.set("ultima_digest", last.digest);
            x.set("desatualizada", last.digest != ref);
        } else {
            x.set("ausente", !lib_.store.has(ref));
        }
        deps.push(x);
    }
    s.set("dependencias", deps);
    s.set("atualizacoes", c.atualizacoes);
    return s;
}

Json Workshop::exec(const Json& cmd) {
    std::string k = cmd.get("cmd").as_str();
    if (k == "catalogo") { Json r = Json::obj(); r.set("ok", true); r.set("operacoes", ops_catalog()); r.set("regras", rules_.to_json()); return r; }
    if (k == "criar") {
        std::string nome = cmd.get("nome").str_or("Sem nome");
        Creation c;
        c.id = lib_.new_id(nome);
        c.nome = nome;
        c.autor = autor_;
        c.origem = "nova";
        c.rascunho = empty_draft();
        lib_.creations[c.id] = c;
        lib_.persist(lib_.creations[c.id]);
        return state_of(lib_.creations[c.id]);
    }
    if (k == "listar") {
        Json r = Json::obj();
        r.set("ok", true);
        Json arr = Json::arr();
        for (auto& kv : lib_.creations) {
            Json x = Json::obj();
            x.set("criacao", kv.first); x.set("nome", kv.second.nome); x.set("autor", kv.second.autor); x.set("origem", kv.second.origem);
            x.set("revisoes", static_cast<int64_t>(kv.second.revs.size()));
            if (!kv.second.revs.empty()) { x.set("ultima_revisao", kv.second.revs.back().rev); x.set("digest", kv.second.revs.back().digest); }
            arr.push(x);
        }
        r.set("criacoes", arr);
        return r;
    }
    if (k == "importar") {
        const Json& b = cmd.get("pacote");
        if (b.get("formato").as_str() != kFormatoPacote) return fail("pacote em formato desconhecido");
        std::string root = b.get("raiz").as_str();
        for (auto& kv : b.get("objetos").members())
            if (digest_of(canonical_behavior(kv.second)) != kv.first) return fail("objeto adulterado: o conteúdo não confere com " + kv.first.substr(0, 19) + "…");
        if (!b.get("objetos").has(root)) return fail("o pacote não contém a raiz");
        for (auto& kv : b.get("objetos").members())
            for (auto& r : refs_of(kv.second))
                if (!b.get("objetos").has(r) && !lib_.store.has(r)) return fail("dependência ausente no pacote: " + r.substr(0, 19) + "…");
        for (auto& kv : b.get("objetos").members()) lib_.store.put(kv.second);
        Json made = Json::arr();
        for (auto& kv : b.get("nomes").members()) {
            if (lib_.find_by_digest(kv.first)) continue;
            const Json& nm = kv.second;
            Creation c;
            c.nome = nm.get("nome").str_or("Importada");
            c.id = lib_.new_id(c.nome);
            c.autor = nm.get("autor").as_str();
            c.origem = "importada de " + c.autor + " (" + nm.get("criacao").as_str() + " r" + std::to_string(nm.get("revisao").as_int()) + ")";
            RevRecord r;
            r.rev = static_cast<int>(nm.get("revisao").as_int(1)); r.digest = kv.first; r.nome = c.nome; r.rotulos = nm.get("rotulos");
            r.deps = nm.get("deps"); r.aparencia = nm.get("aparencia"); r.quando = now_ms(); r.nota = "importada";
            c.revs.push_back(r);
            c.rascunho = Json::obj();
            c.rascunho.set("comportamento", *lib_.store.get(kv.first));
            c.rascunho.set("rotulos", r.rotulos.is_obj() ? r.rotulos : Json::obj());
            c.rascunho.set("deps", r.deps.is_obj() ? r.deps : Json::obj());
            c.rascunho.set("aparencia", r.aparencia.is_obj() ? r.aparencia : Json::obj());
            lib_.creations[c.id] = c;
            lib_.persist(c);
            Json x = Json::obj(); x.set("criacao", c.id); x.set("nome", c.nome); x.set("digest", kv.first); x.set("raiz", kv.first == root);
            made.push(x);
        }
        Json r = Json::obj(); r.set("ok", true); r.set("criacoes", made); r.set("raiz", root);
        return r;
    }

    Creation* c = lib_.get(cmd.get("criacao").as_str());
    if (!c) return fail("criação inexistente '" + cmd.get("criacao").as_str() + "'");

    if (k == "abrir") return state_of(*c);
    if (k == "analisar") { Json r = Json::obj(); r.set("ok", true); r.set("analise", analyze(*c)); return r; }
    if (k == "renomear") { c->nome = cmd.get("nome").str_or(c->nome); lib_.persist(*c); return state_of(*c); }
    if (k == "duplicar") {
        Creation d;
        d.nome = cmd.get("nome").str_or(c->nome + " (cópia)");
        d.id = lib_.new_id(d.nome);
        d.autor = autor_;
        d.origem = "duplicada de " + c->nome + (c->revs.empty() ? " (rascunho)" : " r" + std::to_string(c->revs.back().rev));
        d.rascunho = c->rascunho;
        lib_.creations[d.id] = d;
        lib_.persist(lib_.creations[d.id]);
        return state_of(lib_.creations[d.id]);
    }
    if (k == "desfazer" || k == "refazer") {
        auto& from = k == "desfazer" ? c->undo : c->redo;
        auto& to = k == "desfazer" ? c->redo : c->undo;
        if (from.empty()) return fail(k == "desfazer" ? "nada para desfazer" : "nada para refazer");
        to.push_back(c->rascunho);
        c->rascunho = from.back();
        from.pop_back();
        lib_.persist(*c);
        return state_of(*c);
    }
    if (k == "salvar") {
        Naming nm = lib_.naming(c);
        const Json& beh = c->rascunho.get("comportamento");
        CompileOut co = compile_recipe(beh, lib_.resolver(), rules_, &nm);
        if (!co.ok) {
            Json r = fail("a receita tem erros e não pode virar revisão; o rascunho foi preservado");
            r.set("analise", co.analise);
            return r;
        }
        std::string d = lib_.store.put(beh);
        Json r = Json::obj();
        r.set("ok", true);
        if (!c->revs.empty() && c->revs.back().digest == d) {
            r.set("revisao", c->revs.back().rev); r.set("digest", d); r.set("inalterada", true); r.set("analise", co.analise);
            return r;
        }
        RevRecord rec;
        rec.rev = c->revs.empty() ? 1 : c->revs.back().rev + 1;
        rec.digest = d;
        rec.pai = c->revs.empty() ? "" : c->revs.back().digest;
        rec.nome = c->nome;
        rec.nota = cmd.get("nota").as_str();
        rec.rotulos = c->rascunho.get("rotulos");
        rec.deps = c->rascunho.get("deps");
        rec.aparencia = c->rascunho.get("aparencia");
        rec.quando = now_ms();
        c->revs.push_back(rec);
        lib_.persist(*c);
        r.set("revisao", rec.rev); r.set("digest", d); r.set("analise", co.analise);
        return r;
    }
    if (k == "historico") {
        Json r = Json::obj(); r.set("ok", true);
        Json rs = Json::arr(); for (auto& x : c->revs) rs.push(x.to_json()); r.set("revisoes", rs);
        r.set("atualizacoes", c->atualizacoes);
        return r;
    }
    auto rev_of = [&](const Json& sel, Json& beh, Json& deps, std::string& err) -> bool {
        if (sel.is_str() && sel.as_str() == "rascunho") { beh = c->rascunho.get("comportamento"); deps = c->rascunho.get("deps"); return true; }
        for (auto& r : c->revs)
            if (r.rev == sel.as_int()) {
                const Json* o = lib_.store.get(r.digest);
                if (!o) { err = "objeto da revisão ausente"; return false; }
                beh = *o; deps = r.deps;
                return true;
            }
        err = "revisão inexistente";
        return false;
    };
    if (k == "revisao.abrir") {
        Json beh, deps;
        std::string err;
        if (!rev_of(cmd.get("revisao"), beh, deps, err)) return fail(err);
        const RevRecord* rr = nullptr;
        for (auto& r : c->revs) if (r.rev == cmd.get("revisao").as_int()) rr = &r;
        if (!mutate(*c, [&](Json& d, std::string&) {
                d.set("comportamento", beh); d.set("deps", deps);
                if (rr) d.set("rotulos", rr->rotulos);
                return true; }, err)) return fail(err);
        return state_of(*c);
    }
    if (k == "comparar") {
        Json ba, da, bb, db;
        std::string err;
        if (!rev_of(cmd.get("a"), ba, da, err) || !rev_of(cmd.get("b"), bb, db, err)) return fail(err);
        Json r = diff(ba, bb, da, db);
        r.set("ok", true);
        return r;
    }
    if (k == "dependencias") { Json s = state_of(*c); Json r = Json::obj(); r.set("ok", true); r.set("dependencias", s.get("dependencias")); return r; }
    if (k == "dependencias.atualizar" || k == "dependencias.reverter") {
        std::string no = cmd.get("no").as_str();
        Json* n = find_node(c->rascunho["comportamento"], no);
        if (!n || n->get("op").as_str() != "sub.incorporar") return fail("o nó não é uma subcriação");
        std::string cur = n->get("p").get("ref").as_str();
        std::string target;
        Json depinfo = Json::obj();
        if (k == "dependencias.atualizar") {
            const RevRecord* r = nullptr;
            const Creation* dc = lib_.find_by_digest(cur, &r);
            if (!dc) return fail("não sei de qual criação esta revisão veio");
            const RevRecord* t = &dc->revs.back();
            if (cmd.has("revisao")) { t = nullptr; for (auto& x : dc->revs) if (x.rev == cmd.get("revisao").as_int()) t = &x; }
            if (!t) return fail("revisão inexistente");
            if (t->digest == cur) return fail("já está na revisão " + std::to_string(t->rev));
            target = t->digest;
            depinfo.set("criacao", dc->id); depinfo.set("revisao", t->rev); depinfo.set("nome", t->nome);
        } else {
            for (size_t i = c->atualizacoes.size(); i > 0; --i) {
                const Json& u = c->atualizacoes.at(i - 1);
                if (u.get("no").as_str() == no && u.get("tipo").as_str() == "atualizacao" && u.get("para").get("digest").as_str() == cur) {
                    target = u.get("de").get("digest").as_str();
                    depinfo = u.get("de");
                    break;
                }
            }
            if (target.empty()) return fail("não há atualização para desfazer neste nó");
        }
        const RevRecord* rold = nullptr;
        const Creation* cold = lib_.find_by_digest(cur, &rold);
        std::string err;
        if (!mutate(*c, [&](Json& d, std::string&) {
                Json* nn = find_node(d["comportamento"], no);
                (*nn)["p"].set("ref", target);
                Json di = depinfo; di.erase("digest");
                d["deps"].set(target, di);
                return true; }, err)) return fail(err);
        Json u = Json::obj();
        u.set("tipo", k == "dependencias.atualizar" ? "atualizacao" : "reversao");
        u.set("no", no);
        Json de = Json::obj(); de.set("digest", cur); if (cold && rold) { de.set("criacao", cold->id); de.set("revisao", rold->rev); de.set("nome", rold->nome); }
        Json pa = depinfo; pa.set("digest", target);
        u.set("de", de); u.set("para", pa); u.set("quando", now_ms());
        c->atualizacoes.push(u);
        lib_.persist(*c);
        const Json* ob = lib_.store.get(cur);
        const Json* nb = lib_.store.get(target);
        Json r = state_of(*c);
        if (ob && nb) r.set("diferenca", diff(*ob, *nb, rold ? rold->deps : Json::obj(), Json::obj()));
        return r;
    }
    if (k == "sugestao.aplicar") {
        Json an = analyze(*c);
        Json sug;
        for (auto& s : an.get("sugestoes").items()) if (s.get("id").as_str() == cmd.get("id").as_str()) sug = s;
        if (sug.is_null()) return fail("sugestão inexistente ou desatualizada; analise de novo");
        // O jogador pode editar a sugestão antes de aprovar: valem as alterações enviadas.
        Json alts = cmd.get("alteracoes").is_arr() ? cmd.get("alteracoes") : sug.get("alteracoes");
        Json before = c->rascunho;
        auto saved_undo = c->undo;
        for (auto& e : alts.items()) {
            Json r = apply_edit(*c, e, false);
            if (!r.get("ok").as_bool()) {
                c->rascunho = before;
                c->undo = saved_undo;
                lib_.persist(*c);
                return fail("a sugestão não pôde ser aplicada: " + r.get("erro").as_str());
            }
        }
        // Uma única entrada de desfazer para a aprovação inteira
        c->undo = saved_undo;
        if (c->rascunho != before) c->undo.push_back(before);
        c->redo.clear();
        lib_.persist(*c);
        Json r = state_of(*c);
        r.set("aplicada", sug.get("id"));
        return r;
    }
    if (k == "exportar") {
        std::string d;
        if (cmd.has("revisao")) { for (auto& r : c->revs) if (r.rev == cmd.get("revisao").as_int()) d = r.digest; }
        else if (!c->revs.empty()) d = c->revs.back().digest;
        if (d.empty()) return fail("salve uma revisão antes de exportar");
        Json r = Json::obj(); r.set("ok", true); r.set("pacote", lib_.bundle(d));
        return r;
    }
    if (k == "migrar") {
        Json migr = Json::arr();
        std::string err;
        if (!mutate(*c, [&](Json& d, std::string& er) {
                for (auto& n : d["comportamento"]["nos"].items()) {
                    const OpDef* od = find_op(n.get("op").as_str(), static_cast<int>(n.get("v").as_int(1)));
                    if (od && !od->obsoleta) continue;
                    Json before = n;
                    std::string e2;
                    if (!adapt_to_latest(n, e2)) { er = e2; return false; }
                    Json x = Json::obj(); x.set("no", n.get("id")); x.set("de", before.get("op").as_str() + "@" + std::to_string(before.get("v").as_int()));
                    x.set("para", n.get("op").as_str() + "@" + std::to_string(n.get("v").as_int()));
                    migr.push(x);
                }
                return true; }, err)) return fail(err);
        Json r = state_of(*c);
        r.set("migrados", migr);
        return r;
    }
    if (k == "simular") return simulate(*c, cmd.get("cenario"));
    // demais: comandos de edição do rascunho
    Json r = apply_edit(*c, cmd, true);
    if (!r.get("ok").as_bool()) return r;
    Json s = state_of(*c);
    for (auto& kv : r.members()) if (kv.first != "ok") s.set(kv.first, kv.second);
    return s;
}

Json Workshop::simulate(Creation& c, const Json& cenario) {
    Naming nm = lib_.naming(&c);
    CompileOut co = compile_recipe(c.rascunho.get("comportamento"), lib_.resolver(), rules_, &nm);
    if (!co.ok) { Json r = fail("a receita tem erros"); r.set("analise", co.analise); return r; }
    Json r = simulate_plan(co.plan, rules_, cenario);
    r.set("analise", co.analise);
    return r;
}

static Vec3 vec_or(const Json& a, Vec3 def) {
    if (!a.is_arr() || a.size() < 2) return def;
    return Vec3(a.at(0).as_num(), a.at(1).as_num(), a.size() > 2 ? a.at(2).as_num() : 0.0);
}

Json simulate_plan(std::shared_ptr<const Plan> plan, const Rules& rules, const Json& cen) {
    Runtime rt(Arena::galeria(), rules);
    rt.sem_recarga = true;
    const Json& au = cen.get("autor");
    Vec3 pos = vec_or(au.get("pos"), Vec3(-600, -250, 0));
    double yaw = au.get("yaw").as_num(0);
    int me = rt.add_entity(EntKind::Jogador, 0, au.get("nome").str_or("autor"), pos, yaw);
    for (auto& a : cen.get("alvos").items())
        rt.add_entity(EntKind::Alvo, -1, a.get("nome").str_or("alvo"), vec_or(a.get("pos"), Vec3()), a.get("yaw").as_num(0));
    const int64_t t0 = 1000;
    rt.advance(t0);
    std::vector<Json> evs = rt.drain();
    struct Cmd { int64_t t; int tipo; };
    std::vector<Cmd> cmds;
    for (auto& c : cen.get("comandos").items()) {
        std::string tp = c.get("tipo").str_or("pressionar");
        cmds.push_back({t0 + c.get("t").as_int(), tp == "soltar" ? 1 : (tp == "secundario" ? 2 : 0)});
    }
    std::sort(cmds.begin(), cmds.end(), [](const Cmd& a, const Cmd& b) { return a.t < b.t; });
    Runtime::Act act = rt.activate(me, 0, plan, t0, yaw, pos);
    Json quadros = Json::arr();
    auto frame = [&]() {
        Json f = Json::obj();
        Json s = rt.snapshot();
        f.set("t", rt.now() - t0);
        f.set("entidades", s.get("entidades"));
        f.set("areas", s.get("areas"));
        f.set("janelas", s.get("janelas"));
        quadros.push(f);
    };
    for (auto& e : rt.drain()) evs.push_back(e);
    frame();
    size_t ci = 0;
    int64_t end = t0 + std::min<int64_t>(plan->max_dur_ms + 400, 12000);
    int64_t step = 1000 / std::max(1, rules.tick_hz);
    for (int64_t t = t0 + step; t <= end; t += step) {
        while (ci < cmds.size() && cmds[ci].t <= t) {
            rt.advance(cmds[ci].t);
            rt.command(me, 0, cmds[ci].tipo == 1, cmds[ci].tipo == 2, cmds[ci].t, cmds[ci].t);
            ++ci;
        }
        rt.advance(t);
        for (auto& e : rt.drain()) evs.push_back(e);
        frame();
        if (!rt.exec_alive(act.exec) && ci >= cmds.size() && t > t0 + 200) {
            // dois quadros extras para a recuperação ficar visível
            for (int k = 0; k < 6; ++k) { t += step; rt.advance(t); frame(); }
            break;
        }
    }
    Json r = Json::obj();
    r.set("ok", act.ok);
    if (!act.ok) r.set("erro", act.motivo);
    r.set("t0", t0);
    Json ev = Json::arr();
    Json imp = Json::arr();
    for (auto& e : evs) {
        Json x = e;
        if (x.has("t")) x.set("t_rel", x.get("t").as_int() - t0);
        if (x.get("tipo").as_str() == "impacto") imp.push(x);
        ev.push(x);
    }
    r.set("eventos", ev);
    r.set("impactos", imp);
    r.set("quadros", quadros);
    return r;
}

}  // namespace forja
