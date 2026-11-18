#include "compile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

#include "recipe.h"

namespace forja {

const char* surface_name(Surface s) {
    switch (s) {
        case Surface::Agua: return "agua";
        case Surface::Metal: return "metal";
        default: return "concreto";
    }
}

static Surface parse_surface(const std::string& s) {
    if (s == "agua") return Surface::Agua;
    if (s == "metal") return Surface::Metal;
    return Surface::Concreto;
}

int area_work_units(int raio_cm) {
    double m = raio_cm / 100.0;
    double m2 = kPi * m * m;
    return 4 + static_cast<int>(std::ceil(m2 - 1e-9));
}

Json Rules::to_json() const {
    Json j = Json::obj();
    j.set("tick_hz", tick_hz); j.set("tolerancia_ms", tolerancia_ms); j.set("max_alvos", max_alvos);
    j.set("exec_pico_trabalho", exec_pico_trabalho); j.set("exec_pico_areas", exec_pico_areas);
    j.set("exec_max_ativacoes", exec_max_ativacoes); j.set("exec_max_eventos", exec_max_eventos);
    j.set("exec_max_duracao_ms", exec_max_duracao_ms); j.set("exec_max_memoria", exec_max_memoria);
    j.set("sala_trabalho", sala_trabalho); j.set("sala_areas", sala_areas); j.set("sala_execucoes", sala_execucoes);
    j.set("competitiva", competitiva); j.set("max_inimigos", max_inimigos); j.set("max_aliados", max_aliados);
    j.set("preparo_minimo_ms", preparo_minimo_ms); j.set("alcance_max_cm", alcance_max_cm);
    j.set("custo_max", custo_max); j.set("custo_equipamento_max", custo_equipamento_max);
    j.set("abertura_max_graus", abertura_max_graus); j.set("cargas_max", cargas_max);
    j.set("duracao_max_ms", duracao_max_ms); j.set("impacto_max", impacto_max);
    return j;
}

Rules Rules::from_json(const Json& j) {
    Rules r;
    auto I = [&](const char* k, int& v) { if (j.has(k)) v = static_cast<int>(j.get(k).as_int(v)); };
    auto D = [&](const char* k, double& v) { if (j.has(k)) v = j.get(k).as_num(v); };
    I("tick_hz", r.tick_hz); I("tolerancia_ms", r.tolerancia_ms); I("max_alvos", r.max_alvos);
    I("exec_pico_trabalho", r.exec_pico_trabalho); I("exec_pico_areas", r.exec_pico_areas);
    I("exec_max_ativacoes", r.exec_max_ativacoes); I("exec_max_eventos", r.exec_max_eventos);
    I("exec_max_duracao_ms", r.exec_max_duracao_ms); I("exec_max_memoria", r.exec_max_memoria);
    I("sala_trabalho", r.sala_trabalho); I("sala_areas", r.sala_areas); I("sala_execucoes", r.sala_execucoes);
    if (j.has("competitiva")) r.competitiva = j.get("competitiva").as_bool(true);
    I("max_inimigos", r.max_inimigos); I("max_aliados", r.max_aliados);
    I("preparo_minimo_ms", r.preparo_minimo_ms); I("alcance_max_cm", r.alcance_max_cm);
    D("custo_max", r.custo_max); D("custo_equipamento_max", r.custo_equipamento_max);
    I("abertura_max_graus", r.abertura_max_graus); I("cargas_max", r.cargas_max);
    I("duracao_max_ms", r.duracao_max_ms); I("impacto_max", r.impacto_max);
    return r;
}

Rules Rules::duelo() { return Rules(); }
Rules Rules::equipes_2v2() { Rules r; r.max_inimigos = 2; r.max_aliados = 1; return r; }
Rules Rules::arena_livre() { Rules r; r.competitiva = false; r.max_inimigos = 7; r.max_aliados = 7; return r; }

Json Issue::to_json() const {
    Json j = Json::obj();
    j.set("nivel", nivel); j.set("codigo", codigo); j.set("no", no); j.set("caminho", caminho); j.set("msg", msg);
    return j;
}

namespace {

struct LNode {
    std::string id;
    Json node;
    const OpDef* op = nullptr;
};
struct FEdge { int from, port, to; };
struct DEdge { int from, dport, to, din; };
struct Graph {
    std::vector<LNode> nodes;
    std::map<std::string, int> idx;
    std::vector<FEdge> flow;
    std::vector<DEdge> data;
    int entry = -1;
    Json expor;
    Json behavior;
    std::string digest;
};

struct Ctx {
    const Resolver& res;
    const Rules& rules;
    const Naming* naming;
    std::vector<Issue> issues;
    Plan plan;
    bool fatal = false;
    std::vector<Json> suggestions;
    Ctx(const Resolver& r, const Rules& ru, const Naming* n) : res(r), rules(ru), naming(n) {}
    void add(const char* nivel, const std::string& codigo, const std::string& no, const std::string& caminho, const std::string& msg) {
        Issue i; i.nivel = nivel; i.codigo = codigo; i.no = no; i.caminho = caminho; i.msg = msg;
        issues.push_back(i);
        if (std::string(nivel) == "erro") fatal = true;
    }
};

std::string top_of(const std::string& prefix_top, const std::string& id) { return prefix_top.empty() ? id : prefix_top; }

bool load_graph(Ctx& cx, const Json& behavior_in, const std::string& prefix, const std::string& top, const Json& overrides,
                const std::string& digest, Graph& g) {
    Json behavior = canonical_behavior(behavior_in);
    g.behavior = behavior;
    g.digest = digest;
    const std::string where = prefix.empty() ? "" : " (na subcriação " + prefix + ")";
    if (behavior_in.get("formato").as_str() != kFormatoReceita) {
        cx.add("erro", "formato", top, prefix, "formato desconhecido '" + behavior_in.get("formato").as_str() + "'" + where);
        return false;
    }
    g.expor = behavior.get("expor");
    // Sobreposições: nome exposto -> (nó, parâmetro)
    std::map<std::string, std::pair<std::string, std::string>> exposed;
    for (auto& e : g.expor.items()) exposed[e.get("nome").as_str()] = {e.get("no").as_str(), e.get("param").as_str()};
    for (auto& kv : overrides.members()) {
        if (!exposed.count(kv.first))
            cx.add("erro", "sobreposicao_invalida", top, prefix, "a subcriação não expõe o parâmetro '" + kv.first + "'");
    }
    for (auto& n : behavior.get("nos").items()) {
        LNode ln;
        ln.id = n.get("id").as_str();
        ln.node = n;
        std::string tid = top_of(top, ln.id);
        if (ln.id.empty() || g.idx.count(ln.id)) {
            cx.add("erro", "id_invalido", tid, prefix + ln.id, "identificador de nó vazio ou repetido '" + ln.id + "'" + where);
            continue;
        }
        std::string opn = n.get("op").as_str();
        int v = static_cast<int>(n.get("v").as_int(1));
        const OpDef* od = find_op(opn, v);
        if (od && od->planejada) {
            cx.add("erro", "operacao_ausente", tid, prefix + ln.id,
                   "a intenção usa a operação '" + opn + "' (" + od->rotulo + ": " + od->descricao + "), que ainda não existe. A receita foi preservada." + where);
            ln.op = od;
        } else if (!od || od->obsoleta) {
            std::string err;
            if (!adapt_to_latest(ln.node, err)) {
                cx.add("erro", op_versions(opn).empty() ? "operacao_desconhecida" : "versao_incompativel", tid, prefix + ln.id, err + where);
            } else {
                od = find_op(ln.node.get("op").as_str(), static_cast<int>(ln.node.get("v").as_int()));
                cx.add("aviso", "versao_obsoleta", tid, prefix + ln.id,
                       opn + "@" + std::to_string(v) + " executa por adaptador; migrar cria uma revisão nova e preserva a original" + where);
            }
            ln.op = od;
        } else {
            ln.op = od;
        }
        if (ln.op && !ln.op->planejada) {
            Json p = ln.node.get("p");
            for (auto& kv : overrides.members()) {
                auto it = exposed.find(kv.first);
                if (it != exposed.end() && it->second.first == ln.id) p.set(it->second.second, kv.second);
            }
            std::vector<std::string> errs;
            if (!normalize_params(*ln.op, p, errs))
                for (auto& e : errs) cx.add("erro", "parametro_invalido", tid, prefix + ln.id, ln.op->rotulo + ": " + e + where);
            ln.node.set("p", p);
        }
        g.idx[ln.id] = static_cast<int>(g.nodes.size());
        g.nodes.push_back(ln);
    }
    int entries = 0;
    for (size_t i = 0; i < g.nodes.size(); ++i)
        if (g.nodes[i].op && g.nodes[i].op->kind == OpKind::Ativar) { ++entries; g.entry = static_cast<int>(i); }
    if (entries != 1) {
        cx.add("erro", "entrada", top, prefix, "a criação precisa de exatamente um nó 'Ativar' (tem " + std::to_string(entries) + ")" + where);
        return false;
    }
    for (auto& e : behavior.get("fluxo").items()) {
        std::string a = e.get("de").as_str(), port = e.get("porta").as_str(), b = e.get("para").as_str();
        if (!g.idx.count(a) || !g.idx.count(b)) {
            cx.add("erro", "aresta_invalida", top_of(top, a), prefix + a, "ligação de fluxo entre nós inexistentes " + a + " → " + b + where);
            continue;
        }
        int ia = g.idx[a], ib = g.idx[b];
        const OpDef* oa = g.nodes[static_cast<size_t>(ia)].op;
        const OpDef* ob = g.nodes[static_cast<size_t>(ib)].op;
        if (!oa || !ob) continue;
        int pi = oa->out_index(port);
        if (pi < 0) { cx.add("erro", "porta_invalida", top_of(top, a), prefix + a, oa->rotulo + " não tem a saída '" + port + "'" + where); continue; }
        if (!ob->tem_entrada) { cx.add("erro", "porta_invalida", top_of(top, b), prefix + b, ob->rotulo + " não recebe fluxo" + where); continue; }
        g.flow.push_back({ia, pi, ib});
    }
    for (auto& e : behavior.get("dados").items()) {
        std::string a = e.get("de").as_str(), port = e.get("porta").as_str(), b = e.get("para").as_str(), in = e.get("entrada").as_str();
        if (!g.idx.count(a) || !g.idx.count(b)) {
            cx.add("erro", "aresta_invalida", top_of(top, b), prefix + b, "ligação de dados entre nós inexistentes" + where);
            continue;
        }
        int ia = g.idx[a], ib = g.idx[b];
        const OpDef* oa = g.nodes[static_cast<size_t>(ia)].op;
        const OpDef* ob = g.nodes[static_cast<size_t>(ib)].op;
        if (!oa || !ob) continue;
        int dp = oa->dout_index(port), di = ob->din_index(in);
        if (dp < 0 || di < 0) { cx.add("erro", "porta_invalida", top_of(top, b), prefix + b, "porta de dados inexistente " + a + "." + port + " → " + b + "." + in + where); continue; }
        if (oa->dout[static_cast<size_t>(dp)].tipo != ob->din[static_cast<size_t>(di)].tipo) {
            cx.add("erro", "tipo", top_of(top, b), prefix + b, "tipos incompatíveis: " + oa->dout[static_cast<size_t>(dp)].tipo + " → " + ob->din[static_cast<size_t>(di)].tipo + where);
            continue;
        }
        for (auto& d : g.data)
            if (d.to == ib && d.din == di) cx.add("erro", "tipo", top_of(top, b), prefix + b, "entrada de dados ligada duas vezes" + where);
        g.data.push_back({ia, dp, ib, di});
    }
    // Ciclos de fluxo: proibidos (a repetição é feita por cadencia.repetir, com intervalo mínimo)
    size_t N = g.nodes.size();
    std::vector<std::vector<int>> adj(N), radj(N);
    for (auto& f : g.flow) { adj[static_cast<size_t>(f.from)].push_back(f.to); radj[static_cast<size_t>(f.to)].push_back(f.from); }
    std::vector<int> color(N, 0);
    std::vector<int> stack;
    bool cyc = false;
    std::function<void(int)> dfs = [&](int u) {
        color[static_cast<size_t>(u)] = 1;
        stack.push_back(u);
        for (int w : adj[static_cast<size_t>(u)]) {
            if (cyc) return;
            if (color[static_cast<size_t>(w)] == 1) {
                std::string path;
                bool on = false;
                for (int s : stack) { if (s == w) on = true; if (on) path += g.nodes[static_cast<size_t>(s)].id + " → "; }
                path += g.nodes[static_cast<size_t>(w)].id;
                cx.add("erro", "ciclo_instantaneo", top_of(top, g.nodes[static_cast<size_t>(w)].id), prefix + g.nodes[static_cast<size_t>(w)].id,
                       "ciclo de fluxo " + path + ": geraria eventos sem avanço de tempo. Use 'Repetir' para repetição controlada." + where);
                cyc = true;
                return;
            }
            if (color[static_cast<size_t>(w)] == 0) dfs(w);
        }
        stack.pop_back();
        color[static_cast<size_t>(u)] = 2;
    };
    for (size_t i = 0; i < N && !cyc; ++i) if (color[i] == 0) dfs(static_cast<int>(i));
    if (cyc) return false;
    // Alcançabilidade
    std::vector<bool> reach(N, false);
    std::vector<int> q = {g.entry};
    reach[static_cast<size_t>(g.entry)] = true;
    while (!q.empty()) {
        int u = q.back(); q.pop_back();
        for (int w : adj[static_cast<size_t>(u)]) if (!reach[static_cast<size_t>(w)]) { reach[static_cast<size_t>(w)] = true; q.push_back(w); }
    }
    for (size_t i = 0; i < N; ++i)
        if (!reach[i]) cx.add("aviso", "no_solto", top_of(top, g.nodes[i].id), prefix + g.nodes[i].id, "o nó '" + g.nodes[i].id + "' não é alcançado a partir de Ativar e será ignorado" + where);
    // Dados: a origem deve ser ancestral de fluxo do consumidor
    for (auto& d : g.data) {
        std::vector<bool> anc(N, false);
        std::vector<int> st = {d.to};
        while (!st.empty()) {
            int u = st.back(); st.pop_back();
            for (int w : radj[static_cast<size_t>(u)]) if (!anc[static_cast<size_t>(w)]) { anc[static_cast<size_t>(w)] = true; st.push_back(w); }
        }
        if (!anc[static_cast<size_t>(d.from)])
            cx.add("erro", "dado_sem_origem", top_of(top, g.nodes[static_cast<size_t>(d.to)].id), prefix + g.nodes[static_cast<size_t>(d.to)].id,
                   "o dado vem de '" + g.nodes[static_cast<size_t>(d.from)].id + "', que não acontece antes deste nó" + where);
    }
    for (size_t i = 0; i < N; ++i) {
        const OpDef* od = g.nodes[i].op;
        if (!od || od->planejada) continue;
        const Json& p = g.nodes[i].node.get("p");
        bool needs_point = (od->kind == OpKind::Retornar) || (p.get("ancora").as_str() == "marca");
        if (!needs_point) continue;
        bool has = false;
        for (auto& d : g.data) if (d.to == static_cast<int>(i)) has = true;
        if (!has)
            cx.add("erro", "dado_ausente", top_of(top, g.nodes[i].id), prefix + g.nodes[i].id,
                   od->rotulo + " precisa receber um ponto (ligue a saída 'ponto' de uma marca)" + where);
    }
    for (auto& e : g.expor.items()) {
        std::string no = e.get("no").as_str(), pa = e.get("param").as_str();
        if (!g.idx.count(no) || !g.nodes[static_cast<size_t>(g.idx[no])].op || !g.nodes[static_cast<size_t>(g.idx[no])].op->param(pa))
            cx.add("erro", "expor_invalido", top, prefix, "parâmetro exposto '" + e.get("nome").as_str() + "' aponta para " + no + "." + pa + ", que não existe" + where);
    }
    return true;
}

void fill_params(PNode& pn, const OpDef& od, const Json& p) {
    auto I = [&](const char* k) { return static_cast<int>(p.get(k).as_int()); };
    auto S = [&](const char* k) { return p.get(k).as_str(); };
    auto V = [&](const char* k) { const Json& a = p.get(k); return Vec3(static_cast<double>(a.at(0).as_int()), static_cast<double>(a.at(1).as_int()), static_cast<double>(a.at(2).as_int())); };
    auto A = [&](const std::string& s) {
        if (s == "ativacao") return Anchor::Ativacao;
        if (s == "evento") return Anchor::Evento;
        if (s == "marca") return Anchor::Marca;
        return Anchor::Autor;
    };
    pn.kind = od.kind;
    switch (od.kind) {
        case OpKind::Deslocar:
            pn.vec = V("vetor_cm");
            pn.espaco = S("espaco") == "ativacao" ? Espaco::Ativacao : (S("espaco") == "mira" ? Espaco::Mira : Espaco::Autor);
            pn.curva = I("curva_cm"); pn.dur = I("duracao_ms"); pn.recup = I("recuperacao_ms");
            break;
        case OpKind::Retornar: pn.dur = I("duracao_ms"); pn.alcance = I("alcance_max_cm"); pn.recup = I("recuperacao_ms"); break;
        case OpKind::Marca: pn.ancora = A(S("ancora")); break;
        case OpKind::Anel:
            pn.ancora = A(S("ancora")); pn.desloc = V("deslocamento_cm"); pn.raio = I("raio_cm"); pn.esp = I("espessura_cm");
            pn.altura = I("altura_cm"); pn.prep = I("preparo_ms"); pn.contato = I("contato_ms"); pn.impacto = I("impacto");
            pn.afeta = S("afeta") == "aliados" ? Afeta::Aliados : (S("afeta") == "todos" ? Afeta::Todos : Afeta::Inimigos);
            pn.segue = p.get("segue_autor").as_bool(); pn.recup = I("recuperacao_ms");
            break;
        case OpKind::Sondar: pn.ancora = A(S("ancora")); pn.sup = parse_surface(S("superficie")); break;
        case OpKind::Vigiar: pn.sup = parse_surface(S("superficie")); pn.dur = I("duracao_ms"); pn.disparos = I("disparos"); break;
        case OpKind::Guarda:
            pn.abertura = I("abertura_graus"); pn.orient = I("orientacao_graus"); pn.segue = p.get("segue_autor").as_bool();
            pn.gesto = S("gesto"); pn.prep = I("preparo_ms"); pn.dur = I("duracao_ms"); pn.cargas = I("cargas"); pn.recup = I("recuperacao_ms");
            break;
        case OpKind::Amparo:
            pn.raio = I("raio_cm"); pn.prep = I("preparo_ms"); pn.dur = I("duracao_ms"); pn.cargas = I("cargas");
            pn.folego = I("folego"); pn.inclui_autor = p.get("inclui_autor").as_bool();
            break;
        case OpKind::Janela:
            pn.abre = I("abre_ms"); pn.dura = I("dura_ms"); pn.soltar = S("gatilho") == "soltar";
            pn.secundario = S("comando") == "secundario"; pn.cedo_anula = S("cedo") == "anular"; pn.tol_cedo = I("tolerancia_cedo_ms");
            break;
        case OpKind::Esperar: pn.ms = I("ms"); break;
        case OpKind::Repetir: pn.vezes = I("vezes"); pn.intervalo = I("intervalo_ms"); break;
        case OpKind::Reunir: pn.n = I("n"); break;
        default: break;
    }
}

std::string label_for(Ctx& cx, const std::string& digest_scope, bool root, const LNode& ln) {
    if (ln.op && ln.op->kind == OpKind::Incorporar && cx.naming && cx.naming->nome_sub) {
        std::string n = cx.naming->nome_sub(ln.node.get("p").get("ref").as_str());
        if (!n.empty()) return n;
    }
    if (cx.naming) {
        if (root) {
            std::string r = cx.naming->rotulos.get(ln.id).as_str();
            if (!r.empty()) return r;
        } else if (cx.naming->rotulos_sub) {
            Json rs = cx.naming->rotulos_sub(digest_scope);
            std::string r = rs.get(ln.id).as_str();
            if (!r.empty()) return r;
        }
    }
    return ln.op ? ln.op->rotulo : ln.id;
}

bool inline_graph(Ctx& cx, Graph& g, int scope, const std::string& prefix, const std::string& top, int depth, std::set<std::string>& chain) {
    std::vector<int> map(g.nodes.size(), -1);
    for (size_t i = 0; i < g.nodes.size(); ++i) {
        LNode& ln = g.nodes[i];
        if (!ln.op || ln.op->planejada) continue;
        PNode pn;
        pn.id = prefix + ln.id;
        pn.top = top_of(top, ln.id);
        pn.scope = scope;
        pn.rotulo = label_for(cx, g.digest, prefix.empty(), ln);
        fill_params(pn, *ln.op, ln.node.get("p"));
        pn.out.resize(ln.op->saidas.size());
        map[i] = static_cast<int>(cx.plan.nodes.size());
        cx.plan.nodes.push_back(pn);
        if (ln.op->kind == OpKind::Incorporar) {
            std::string ref = ln.node.get("p").get("ref").as_str();
            int me = map[i];
            if (depth >= 8) { cx.add("erro", "profundidade", pn.top, pn.id, "subcriações aninhadas além de 8 níveis"); continue; }
            if (chain.count(ref)) { cx.add("erro", "recursao", pn.top, pn.id, "a subcriação inclui a si mesma"); continue; }
            const Json* sub = cx.res ? cx.res(ref) : nullptr;
            if (!sub) { cx.add("erro", "dependencia_ausente", pn.top, pn.id, "revisão " + ref.substr(0, 19) + "… não encontrada no acervo"); continue; }
            if (digest_of(canonical_behavior(*sub)) != ref) { cx.add("erro", "dependencia_corrompida", pn.top, pn.id, "o conteúdo da revisão não confere com o digest"); continue; }
            Graph cg;
            if (!load_graph(cx, *sub, pn.id + "/", pn.top, ln.node.get("p").get("sobrepor"), ref, cg)) continue;
            PScope sc;
            sc.parent = scope;
            sc.embed = me;
            sc.digest = ref;
            sc.nome = pn.rotulo;
            int sid = static_cast<int>(cx.plan.scopes.size());
            cx.plan.scopes.push_back(sc);
            cx.plan.nodes[static_cast<size_t>(me)].child_scope = sid;
            chain.insert(ref);
            inline_graph(cx, cg, sid, pn.id + "/", pn.top, depth + 1, chain);
            chain.erase(ref);
        }
    }
    for (auto& f : g.flow) {
        int a = map[static_cast<size_t>(f.from)], b = map[static_cast<size_t>(f.to)];
        if (a < 0 || b < 0) continue;
        cx.plan.nodes[static_cast<size_t>(a)].out[static_cast<size_t>(f.port)].push_back(b);
    }
    for (auto& d : g.data) {
        int a = map[static_cast<size_t>(d.from)], b = map[static_cast<size_t>(d.to)];
        if (a < 0 || b < 0) continue;
        cx.plan.nodes[static_cast<size_t>(b)].data_src = a;
    }
    if (g.entry >= 0 && map[static_cast<size_t>(g.entry)] >= 0) {
        int e = map[static_cast<size_t>(g.entry)];
        if (scope == 0) cx.plan.entry = e;
        else {
            cx.plan.scopes[static_cast<size_t>(scope)].entry = e;
            cx.plan.nodes[static_cast<size_t>(cx.plan.scopes[static_cast<size_t>(scope)].embed)].child_entry = e;
        }
    }
    return true;
}

// ---------------- análise estática ----------------
struct PortInfo { int64_t tmin = 0, tmax = 0; int count = 1; bool cond = false, manual = false; double pb = 0; };

struct Analyzer {
    Plan& P;
    const Rules& R;
    int K;        // limite por saída "por alvo"
    int allies;   // aliados para amparo
    size_t N;
    std::vector<int> st;
    std::vector<std::vector<std::pair<int, int>>> in;
    std::vector<int64_t> smin, smax;
    std::vector<long long> mult;
    std::vector<bool> certo, manual, ok;
    std::vector<double> db, pb;
    std::vector<int> sstate;

    Analyzer(Plan& p, const Rules& r, int k, int al) : P(p), R(r), K(k), allies(al), N(p.nodes.size()) {
        st.assign(N, 0); in.assign(N, {}); smin.assign(N, 0); smax.assign(N, 0); mult.assign(N, 0);
        certo.assign(N, false); manual.assign(N, false); ok.assign(N, false); db.assign(N, 0); pb.assign(N, 0);
        sstate.assign(P.scopes.size(), 0);
        for (size_t i = 0; i < N; ++i)
            for (size_t k2 = 0; k2 < P.nodes[i].out.size(); ++k2)
                for (int t : P.nodes[i].out[k2]) in[static_cast<size_t>(t)].push_back({static_cast<int>(i), static_cast<int>(k2)});
    }

    static void dur_range(const PNode& n, const Rules& R, int64_t& dmin, int64_t& dmax) {
        dmin = dmax = 0;
        switch (n.kind) {
            case OpKind::Deslocar: case OpKind::Retornar: case OpKind::Vigiar: dmin = dmax = n.dur; break;
            case OpKind::Anel: dmin = dmax = n.prep + n.contato; break;
            case OpKind::Guarda: dmin = n.prep; dmax = n.prep + n.dur; break;
            case OpKind::Amparo: dmin = dmax = n.prep; break;
            case OpKind::Janela: dmin = n.abre; dmax = n.abre + n.dura + R.tolerancia_ms; break;
            case OpKind::Esperar: dmin = dmax = n.ms; break;
            case OpKind::Repetir: dmin = dmax = static_cast<int64_t>(n.vezes - 1) * n.intervalo; break;
            default: break;
        }
    }

    double center_bound(int i) {
        const PNode& n = P.nodes[static_cast<size_t>(i)];
        double off = n.desloc.len();
        switch (n.ancora) {
            case Anchor::Autor: return db[static_cast<size_t>(i)] + off;
            case Anchor::Ativacao: return off;
            case Anchor::Evento: return pb[static_cast<size_t>(i)] + off;
            case Anchor::Marca: return (n.data_src >= 0 ? center_bound(n.data_src) : db[static_cast<size_t>(i)]) + off;
        }
        return db[static_cast<size_t>(i)] + off;
    }

    void scope_end(int s) {
        if (sstate[static_cast<size_t>(s)] == 2) return;
        sstate[static_cast<size_t>(s)] = 2;
        PScope& sc = P.scopes[static_cast<size_t>(s)];
        int64_t emin = 0, emax = 0;
        bool any = false;
        if (sc.entry >= 0) { node(sc.entry); emin = smin[static_cast<size_t>(sc.entry)]; emax = smax[static_cast<size_t>(sc.entry)]; any = true; }
        for (size_t i = 0; i < N; ++i) {
            if (P.nodes[i].scope != s) continue;
            node(static_cast<int>(i));
            if (!ok[i]) continue;
            int64_t a, b;
            if (P.nodes[i].kind == OpKind::Incorporar && P.nodes[i].child_scope >= 0) {
                scope_end(P.nodes[i].child_scope);
                a = P.scopes[static_cast<size_t>(P.nodes[i].child_scope)].end_min;
                b = P.scopes[static_cast<size_t>(P.nodes[i].child_scope)].end_max;
            } else {
                int64_t dmin, dmax;
                dur_range(P.nodes[i], R, dmin, dmax);
                a = smin[i] + dmin;
                b = smax[i] + dmax;
            }
            if (!any) { emin = a; emax = b; any = true; }
            if (certo[i] && a > emin) emin = a;
            if (b > emax) emax = b;
        }
        sc.end_min = emin;
        sc.end_max = emax;
    }

    PortInfo port(int j, int k) {
        const PNode& n = P.nodes[static_cast<size_t>(j)];
        PortInfo pi;
        int64_t a = smin[static_cast<size_t>(j)], b = smax[static_cast<size_t>(j)];
        pi.pb = pb[static_cast<size_t>(j)];
        auto set = [&](int64_t lo, int64_t hi, int cnt, bool cond, bool man = false) { pi.tmin = a + lo; pi.tmax = b + hi; pi.count = cnt; pi.cond = cond; pi.manual = man; };
        switch (n.kind) {
            case OpKind::Ativar: set(0, 0, 1, false); break;
            case OpKind::Deslocar: case OpKind::Retornar: set(n.dur, n.dur, 1, false); break;
            case OpKind::Marca: set(0, 0, 1, false); break;
            case OpKind::Anel:
                if (k == 2) set(n.prep + n.contato, n.prep + n.contato, 1, false);
                else { set(n.prep, n.prep + n.contato, K, true); pi.pb = center_bound(j) + n.raio; }
                break;
            case OpKind::Sondar: set(0, 0, 1, true); break;
            case OpKind::Vigiar:
                if (k == 0) set(0, n.dur, n.disparos, true);
                else set(n.dur, n.dur, 1, false);
                break;
            case OpKind::Guarda:
                if (k == 0) { set(n.prep, n.prep + n.dur, n.cargas, true); pi.pb = db[static_cast<size_t>(j)] + R.alcance_max_cm; }
                else if (k == 1) set(n.prep, n.prep + n.dur, 1, true);
                else if (k == 2) set(n.prep + n.dur, n.prep + n.dur, 1, true);
                else set(n.prep, n.prep + n.dur, 1, false);
                break;
            case OpKind::Amparo:
                if (k == 0) { set(n.prep, n.prep, std::max(1, K), true); pi.pb = db[static_cast<size_t>(j)] + n.raio; }
                else set(n.prep, n.prep, 1, false);
                break;
            case OpKind::Janela:
                if (k == 0) set(n.abre, n.abre + n.dura + R.tolerancia_ms, 1, true, true);
                else set(n.abre + n.dura + R.tolerancia_ms, n.abre + n.dura + R.tolerancia_ms, 1, true);
                break;
            case OpKind::Esperar: set(n.ms, n.ms, 1, false); break;
            case OpKind::Repetir:
                if (k == 0) set(0, static_cast<int64_t>(n.vezes - 1) * n.intervalo, n.vezes, false);
                else set(static_cast<int64_t>(n.vezes - 1) * n.intervalo, static_cast<int64_t>(n.vezes - 1) * n.intervalo, 1, false);
                break;
            case OpKind::Reunir: set(0, 0, 1, true); break;
            case OpKind::Incorporar:
                if (n.child_scope >= 0) {
                    scope_end(n.child_scope);
                    pi.tmin = P.scopes[static_cast<size_t>(n.child_scope)].end_min;
                    pi.tmax = P.scopes[static_cast<size_t>(n.child_scope)].end_max;
                } else { pi.tmin = a; pi.tmax = b; }
                pi.count = 1;
                break;
            default: break;
        }
        return pi;
    }

    double db_out(int j) {
        const PNode& n = P.nodes[static_cast<size_t>(j)];
        if (n.kind == OpKind::Deslocar) return db[static_cast<size_t>(j)] + n.vec.len() + std::fabs(static_cast<double>(n.curva));
        if (n.kind == OpKind::Incorporar && n.child_scope >= 0) {
            double m = db[static_cast<size_t>(j)];
            for (size_t i = 0; i < N; ++i)
                if (ok[i] && in_scope_tree(static_cast<int>(i), n.child_scope)) m = std::max(m, db_out(static_cast<int>(i)));
            return m;
        }
        return db[static_cast<size_t>(j)];
    }

    bool in_scope_tree(int i, int s) {
        int sc = P.nodes[static_cast<size_t>(i)].scope;
        while (sc > 0) { if (sc == s) return true; sc = P.scopes[static_cast<size_t>(sc)].parent; }
        return s == 0;
    }

    void node(int i) {
        size_t u = static_cast<size_t>(i);
        if (st[u] == 2) return;
        if (st[u] == 1) { ok[u] = false; return; }
        st[u] = 1;
        const PNode& n = P.nodes[u];
        if (i == P.entry) {
            ok[u] = true; smin[u] = smax[u] = 0; mult[u] = 1; certo[u] = true; manual[u] = false; db[u] = 0; pb[u] = 0;
        } else if (n.kind == OpKind::Ativar && n.scope > 0 && P.scopes[static_cast<size_t>(n.scope)].entry == i) {
            int e = P.scopes[static_cast<size_t>(n.scope)].embed;
            node(e);
            size_t eu = static_cast<size_t>(e);
            ok[u] = ok[eu]; smin[u] = smin[eu]; smax[u] = smax[eu]; mult[u] = mult[eu]; certo[u] = certo[eu]; manual[u] = manual[eu]; db[u] = db[eu]; pb[u] = pb[eu];
        } else {
            bool first = true, all_manual = true;
            long long m = 0;
            for (auto& e : in[u]) {
                node(e.first);
                size_t ju = static_cast<size_t>(e.first);
                if (!ok[ju]) continue;
                PortInfo pi = port(e.first, e.second);
                if (first) { smin[u] = pi.tmin; smax[u] = pi.tmax; first = false; }
                smin[u] = std::min(smin[u], pi.tmin);
                smax[u] = std::max(smax[u], pi.tmax);
                m += mult[ju] * pi.count;
                if (certo[ju] && !pi.cond) certo[u] = true;
                if (!(manual[ju] || pi.manual)) all_manual = false;
                db[u] = std::max(db[u], db_out(e.first));
                pb[u] = std::max(pb[u], pi.pb);
            }
            if (!first) {
                ok[u] = true;
                manual[u] = all_manual;
                if (n.kind == OpKind::Reunir) { m = m / std::max(1, n.n); if (m < 1) m = 1; }
                mult[u] = std::min<long long>(m, 1000000);
            }
        }
        st[u] = 2;
    }

    void run() {
        for (size_t i = 0; i < N; ++i) node(static_cast<int>(i));
        for (size_t s = 0; s < P.scopes.size(); ++s) scope_end(static_cast<int>(s));
    }
};

struct Phase { int64_t a, b; int work, areas, mem; };

std::vector<Phase> phases_of(const PNode& n, const Rules& R, int64_t exec_dur, int64_t embed_dur) {
    std::vector<Phase> v;
    switch (n.kind) {
        case OpKind::Anel:
            v.push_back({0, n.prep, 1, 1, 64});
            v.push_back({n.prep, n.prep + n.contato, area_work_units(n.raio), 1, 64});
            break;
        case OpKind::Deslocar: case OpKind::Retornar: v.push_back({0, n.dur, 2, 0, 48}); break;
        case OpKind::Vigiar: v.push_back({0, n.dur, 2, 0, 32}); break;
        case OpKind::Guarda: v.push_back({0, n.prep + n.dur, 1, 0, 48}); break;
        case OpKind::Amparo:
            v.push_back({0, n.prep, 1, 1, 64});
            v.push_back({n.prep, n.prep + 17, area_work_units(n.raio), 1, 64});
            v.push_back({n.prep, n.prep + n.dur, R.max_alvos, 0, 48 * R.max_alvos});
            break;
        case OpKind::Janela: v.push_back({n.abre, n.abre + n.dura + R.tolerancia_ms, 1, 0, 32}); break;
        case OpKind::Marca: v.push_back({0, 1, 1, 0, 0}); v.push_back({0, std::max<int64_t>(exec_dur, 1), 0, 0, 32}); break;
        case OpKind::Sondar: v.push_back({0, 1, 1, 0, 0}); break;
        case OpKind::Esperar: v.push_back({0, n.ms, 0, 0, 16}); break;
        case OpKind::Repetir: v.push_back({0, static_cast<int64_t>(n.vezes - 1) * n.intervalo + 1, 0, 0, 16}); break;
        case OpKind::Incorporar: v.push_back({0, std::max<int64_t>(embed_dur, 1), 0, 0, 16}); break;
        default: break;
    }
    return v;
}

void events_of(const PNode& n, int K, int& ev, int& bytes) {
    switch (n.kind) {
        case OpKind::Ativar: ev = 1; bytes = 24; break;
        case OpKind::Deslocar: case OpKind::Retornar: ev = 2; bytes = 56; break;
        case OpKind::Marca: ev = 1; bytes = 20; break;
        case OpKind::Anel: ev = 3; bytes = 64 + 12 * K; break;
        case OpKind::Sondar: ev = 1; bytes = 12; break;
        case OpKind::Vigiar: ev = 1 + n.disparos; bytes = 12 + 20 * n.disparos; break;
        case OpKind::Guarda: ev = 2 + n.cargas; bytes = 40 + 24 * n.cargas; break;
        case OpKind::Amparo: ev = 2 + K; bytes = 24 + 20 * K; break;
        case OpKind::Janela: ev = 2; bytes = 28; break;
        case OpKind::Encerrar: ev = 1; bytes = 8; break;
        default: ev = 0; bytes = 0; break;
    }
}

const char* fundamento_of(OpKind k) {
    switch (k) {
        case OpKind::Deslocar: case OpKind::Retornar: case OpKind::Marca: return "Vetor";
        case OpKind::Anel: case OpKind::Sondar: case OpKind::Vigiar: return "Limiar";
        case OpKind::Guarda: case OpKind::Amparo: return "Lastro";
        case OpKind::Janela: case OpKind::Esperar: case OpKind::Repetir: case OpKind::Reunir: case OpKind::Encerrar: return "Cadência";
        case OpKind::Incorporar: return "Subcriação";
        default: return "Entrada";
    }
}

Json edit_param(const std::string& no, const char* nome, Json valor) {
    Json e = Json::obj();
    e.set("cmd", "no.param"); e.set("no", no); e.set("nome", nome); e.set("valor", std::move(valor));
    return e;
}

std::string fmt1(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.1f", v);
    std::string s = b;
    for (auto& c : s) if (c == '.') c = ',';
    return s;
}

}  // namespace

CompileOut compile_recipe(const Json& behavior, const Resolver& res, const Rules& rules, const Naming* naming) {
    CompileOut out;
    Ctx cx(res, rules, naming);
    Json canon = canonical_behavior(behavior);
    cx.plan.digest = digest_of(canon);
    Graph g;
    PScope root;
    root.nome = "raiz";
    root.digest = cx.plan.digest;
    cx.plan.scopes.push_back(root);
    std::set<std::string> chain = {cx.plan.digest};
    bool loaded = load_graph(cx, behavior, "", "", Json::obj(), cx.plan.digest, g);
    if (loaded) inline_graph(cx, g, 0, "", "", 0, chain);
    Plan& P = cx.plan;
    Json an = Json::obj();
    an.set("digest", P.digest);

    // Regras em linguagem de jogo (receita raiz)
    Json regras = Json::arr();
    if (loaded) {
        std::vector<int> order;
        std::vector<bool> seen(g.nodes.size(), false);
        std::vector<int> q = {g.entry};
        seen[static_cast<size_t>(g.entry)] = true;
        for (size_t h = 0; h < q.size(); ++h) {
            int u = q[h];
            order.push_back(u);
            for (auto& f : g.flow) if (f.from == u && !seen[static_cast<size_t>(f.to)]) { seen[static_cast<size_t>(f.to)] = true; q.push_back(f.to); }
        }
        auto lbl = [&](int i) { return label_for(cx, g.digest, true, g.nodes[static_cast<size_t>(i)]); };
        for (int u : order) {
            for (auto& f : g.flow) {
                if (f.from != u) continue;
                const LNode& a = g.nodes[static_cast<size_t>(f.from)];
                const LNode& b = g.nodes[static_cast<size_t>(f.to)];
                if (!a.op || !b.op) continue;
                std::string cond = a.op->saidas[static_cast<size_t>(f.port)].rotulo;
                const Json& ap = a.node.get("p");
                if (a.op->kind == OpKind::Sondar) cond = std::string(f.port == 0 ? "se tocar " : "se não tocar ") + (ap.get("superficie").as_str() == "agua" ? "água" : ap.get("superficie").as_str());
                if (a.op->kind == OpKind::Vigiar && f.port == 0) cond = "ao cruzar para " + std::string(ap.get("superficie").as_str() == "agua" ? "água" : ap.get("superficie").as_str());
                if (a.op->kind == OpKind::Janela && f.port == 0 && ap.get("gatilho").as_str() == "soltar") cond = "se soltar dentro da janela";
                if (a.op->kind == OpKind::Incorporar) cond = "quando " + lbl(f.from) + " terminar";
                std::string desc = b.op->planejada ? "[falta a operação " + b.op->nome + "]" : describe_node(*b.op, b.node.get("p"));
                if (b.op->kind == OpKind::Incorporar && naming && naming->nome_sub) desc = "executa " + lbl(f.to);
                Json r = Json::obj();
                std::string txt = cond;
                if (!txt.empty()) txt[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(txt[0])));
                if (u != g.entry) txt = lbl(f.from) + ": " + cond;
                txt += " → " + lbl(f.to) + " (" + desc + ")";
                r.set("texto", txt);
                r.set("de", a.id); r.set("porta", a.op->saidas[static_cast<size_t>(f.port)].nome); r.set("para", b.id);
                Json alts = Json::arr();
                for (auto& s : a.op->saidas) { Json x = Json::obj(); x.set("nome", s.nome); x.set("rotulo", s.rotulo); alts.push(x); }
                r.set("alternativas", alts);
                regras.push(r);
            }
        }
        for (auto& d : g.data) {
            Json r = Json::obj();
            r.set("texto", lbl(d.to) + " usa o ponto guardado por " + lbl(d.from));
            r.set("de", g.nodes[static_cast<size_t>(d.from)].id); r.set("para", g.nodes[static_cast<size_t>(d.to)].id); r.set("dado", true);
            regras.push(r);
        }
    }
    an.set("regras", regras);

    if (cx.fatal || P.nodes.empty()) {
        Json probs = Json::arr();
        for (auto& i : cx.issues) probs.push(i.to_json());
        an.set("problemas", probs);
        an.set("executavel", false);
        an.set("competitiva_ok", false);
        an.set("sugestoes", Json::arr());
        an.set("linha_do_tempo", Json::arr());
        out.issues = cx.issues;
        out.analise = an;
        return out;
    }

    // --- tempo e multiplicidade (limite estático computacional) ---
    Analyzer A(P, rules, rules.max_alvos, rules.max_alvos);
    A.run();
    for (size_t i = 0; i < P.nodes.size(); ++i) {
        P.nodes[i].alcancavel = A.ok[i]; P.nodes[i].smin = A.smin[i]; P.nodes[i].smax = A.smax[i];
        P.nodes[i].mult = static_cast<int>(std::min<long long>(A.mult[i], 1 << 30));
        P.nodes[i].certo = A.certo[i]; P.nodes[i].manual = A.manual[i];
    }
    P.max_dur_ms = P.scopes[0].end_max;

    // Pico de trabalho, áreas e memória (varredura de envelopes)
    struct Ev { int64_t t; int dw, da, dm; int node; bool start; };
    std::vector<Ev> evs;
    long long activ = 0, events = 0, bytes = 0;
    std::map<std::string, int> work_by_top;
    for (size_t i = 0; i < P.nodes.size(); ++i) {
        const PNode& n = P.nodes[i];
        if (!n.alcancavel) continue;
        activ += n.mult;
        int ev = 0, by = 0;
        events_of(n, rules.max_alvos, ev, by);
        events += static_cast<long long>(ev) * n.mult;
        bytes += static_cast<long long>(by) * n.mult;
        int64_t embed_dur = 0;
        if (n.kind == OpKind::Incorporar && n.child_scope >= 0) embed_dur = P.scopes[static_cast<size_t>(n.child_scope)].end_max - n.smin;
        for (auto& ph : phases_of(n, rules, P.max_dur_ms, embed_dur)) {
            int64_t a = n.smin + ph.a, b = n.smax + ph.b;
            if (b <= a) b = a + 1;
            long long m = n.mult;
            evs.push_back({a, static_cast<int>(ph.work * m), static_cast<int>(ph.areas * m), static_cast<int>(ph.mem * m), static_cast<int>(i), true});
            evs.push_back({b, -static_cast<int>(ph.work * m), -static_cast<int>(ph.areas * m), -static_cast<int>(ph.mem * m), static_cast<int>(i), false});
            work_by_top[n.top] = std::max(work_by_top[n.top], static_cast<int>(ph.work * m));
        }
    }
    evs.push_back({0, 0, 0, 96, -1, true});
    evs.push_back({std::max<int64_t>(P.max_dur_ms, 1), 0, 0, -96, -1, false});
    std::sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { if (a.t != b.t) return a.t < b.t; return !a.start && b.start; });
    int cw = 0, ca = 0, cm = 0, pw = 0, pa = 0, pm = 0;
    int64_t pw_t = 0;
    std::map<int, int> active_w;
    std::vector<int> resp;
    for (auto& e : evs) {
        cw += e.dw; ca += e.da; cm += e.dm;
        if (e.node >= 0) active_w[e.node] += e.dw;
        if (cw > pw) {
            pw = cw; pw_t = e.t;
            resp.clear();
            for (auto& kv : active_w) if (kv.second > 0) resp.push_back(kv.first);
        }
        pa = std::max(pa, ca);
        pm = std::max(pm, cm);
    }
    P.pico_trabalho = pw; P.pico_areas = pa; P.pico_memoria = pm;
    P.max_ativacoes = static_cast<int>(std::min<long long>(activ, 1 << 30));
    P.max_eventos = static_cast<int>(std::min<long long>(events, 1 << 30));
    P.trafego_bytes = static_cast<int>(std::min<long long>(bytes, 1 << 30));
    {
        int st = 0;
        for (auto& s : P.scopes) {
            const Json* o = s.digest == P.digest ? nullptr : (res ? res(s.digest) : nullptr);
            st += static_cast<int>(o ? o->dump(true).size() : 0);
        }
        st += static_cast<int>(canon.dump(true).size());
        P.armazenamento = st;
    }

    // --- custo competitivo (multiplicidade pelo número real de oponentes do modo) ---
    Analyzer C(P, rules, std::max(1, rules.max_inimigos), rules.max_aliados);
    C.run();
    double total = 0;
    std::map<std::string, double> by_top;
    Json detalhes = Json::arr();
    int min_prep = -1, impacto_total = 0;
    double alcance = 0;
    double folego_concedido = 0;
    struct Hit { int node; double reach; };
    std::vector<Hit> hits;
    auto addc = [&](const PNode& n, double pts, const std::string& why) {
        total += pts;
        by_top[n.top] += pts;
        Json d = Json::obj();
        d.set("no", n.top); d.set("caminho", n.id); d.set("rotulo", n.rotulo); d.set("pontos", std::round(pts * 10) / 10); d.set("motivo", why);
        detalhes.push(d);
    };
    for (size_t i = 0; i < P.nodes.size(); ++i) {
        const PNode& n = P.nodes[i];
        if (!C.ok[i]) continue;
        double m = static_cast<double>(C.mult[i]);
        switch (n.kind) {
            case OpKind::Anel: {
                if (n.impacto <= 0) { addc(n, 0.5 * m, "área só de detecção"); break; }
                double R = n.raio / 100.0, e = n.esp / 100.0;
                double area = n.esp > 0 && n.esp < n.raio ? kPi * (R * R - (R - e) * (R - e)) : kPi * R * R;
                double reach = C.center_bound(static_cast<int>(i)) + n.raio;
                double pts = m * (4.0 * n.impacto + 3.0 * area + n.contato / 100.0 + reach / 100.0 - 0.5 * std::min(n.prep, 600) / 100.0);
                addc(n, pts, "impacto " + std::to_string(n.impacto) + ", área " + fmt1(area) + " m², alcance " + fmt_m(static_cast<int64_t>(reach)) + ", preparo " + fmt_ms(n.prep));
                hits.push_back({static_cast<int>(i), reach});
                impacto_total += static_cast<int>(n.impacto * C.mult[i]);
                alcance = std::max(alcance, reach);
                if (min_prep < 0 || n.prep < min_prep) min_prep = n.prep;
                break;
            }
            case OpKind::Deslocar: case OpKind::Retornar: {
                double dist = n.kind == OpKind::Deslocar ? n.vec.len() + 0.5 * std::fabs(static_cast<double>(n.curva))
                                                         : std::min(static_cast<double>(n.alcance), C.db[i]);
                double speed = dist / std::max(1, n.dur) * 1000.0;
                double pts = m * (2.0 * dist / 100.0 + 2.0 * std::max(0.0, speed - 1000.0) / 1000.0);
                addc(n, pts, "mobilidade " + fmt_m(static_cast<int64_t>(dist)) + " a " + std::to_string(static_cast<int>(speed)) + " cm/s");
                break;
            }
            case OpKind::Guarda: {
                double pts = m * std::max(1.0, 2.0 + 2.0 * (n.abertura / 90.0) * n.cargas + n.dur / 500.0 - n.prep / 200.0);
                addc(n, pts, "proteção " + std::to_string(n.abertura) + "° × " + std::to_string(n.cargas) + " por " + fmt_ms(n.dur));
                break;
            }
            case OpKind::Amparo: {
                double al = rules.max_aliados + (n.inclui_autor ? 1 : 0);
                double pts = m * (1.0 + al * (1.0 + 2.0 * n.cargas + n.dur / 1000.0 + n.folego * 0.5));
                addc(n, pts, "suporte a " + std::to_string(static_cast<int>(al)) + " aliado(s)");
                folego_concedido += static_cast<double>(A.mult[i]) * rules.max_alvos * n.folego;
                break;
            }
            case OpKind::Janela: addc(n, 1.5 * m, "janela de comando (mistura de tempo)"); break;
            case OpKind::Vigiar: case OpKind::Sondar: addc(n, 0.5 * m, "condição de superfície"); break;
            default: break;
        }
        if (n.recup > 0 && (n.kind == OpKind::Deslocar || n.kind == OpKind::Retornar || n.kind == OpKind::Anel || n.kind == OpKind::Guarda))
            addc(n, -m * n.recup / 200.0, "recuperação " + fmt_ms(n.recup) + " (abertura para reação)");
    }
    if (total < 1) total = 1;
    P.custo = std::round(total * 10) / 10;
    P.recarga_ms = 300 + static_cast<int>(std::round(60.0 * P.custo));
    P.folego = static_cast<int>(std::round(P.custo / 2.0));
    P.alcance_cm = static_cast<int>(alcance);
    P.preparo_min_ms = min_prep;

    // --- limites computacionais (bloqueiam execução) ---
    auto topstr = [&](int i) { return P.nodes[static_cast<size_t>(i)].top; };
    if (P.pico_trabalho > rules.exec_pico_trabalho) {
        std::string names;
        std::vector<int> areas;
        int other = 0;
        for (int r : resp) {
            names += (names.empty() ? "" : ", ") + P.nodes[static_cast<size_t>(r)].rotulo;
            const PNode& n = P.nodes[static_cast<size_t>(r)];
            if (n.kind == OpKind::Anel && pw_t >= n.smin + n.prep) areas.push_back(r);
            else other += 0;
        }
        cx.add("erro", "pico_excedido", resp.empty() ? "" : topstr(resp[0]), resp.empty() ? "" : P.nodes[static_cast<size_t>(resp[0])].id,
               "pico de " + std::to_string(P.pico_trabalho) + " unidades/tick em t=" + std::to_string(pw_t) + " ms excede o limite de " +
               std::to_string(rules.exec_pico_trabalho) + " (simultâneos: " + names + ")");
        // Sugestão 1: encadear (sequência) quando duas áreas de topo disparam juntas
        if (areas.size() >= 2) {
            const PNode& a = P.nodes[static_cast<size_t>(areas[0])];
            const PNode& b = P.nodes[static_cast<size_t>(areas[1])];
            if (a.id == a.top && b.id == b.top) {
                Json alt = Json::arr();
                for (auto& f : g.flow) {
                    if (g.nodes[static_cast<size_t>(f.to)].id != b.id) continue;
                    Json d = Json::obj();
                    d.set("cmd", "fluxo.desligar"); d.set("de", g.nodes[static_cast<size_t>(f.from)].id);
                    d.set("porta", g.nodes[static_cast<size_t>(f.from)].op->saidas[static_cast<size_t>(f.port)].nome); d.set("para", b.id);
                    alt.push(d);
                }
                Json l = Json::obj();
                l.set("cmd", "fluxo.ligar"); l.set("de", a.id); l.set("porta", "ao_terminar"); l.set("para", b.id);
                alt.push(l);
                Json s = Json::obj();
                s.set("codigo", "pico_excedido"); s.set("no", b.id);
                s.set("texto", "Encadear " + b.rotulo + " para começar quando " + a.rotulo + " terminar (mesmas áreas, em sequência; pico cai para " +
                                   std::to_string(std::max(area_work_units(a.raio), area_work_units(b.raio))) + ")");
                s.set("preserva", "as duas áreas, raios e impactos");
                s.set("alteracoes", alt);
                cx.suggestions.push_back(s);
            }
            // Sugestão 2: reduzir raios para caber em paralelo
            int cnt = static_cast<int>(areas.size());
            int budget = rules.exec_pico_trabalho - (P.pico_trabalho - [&] { int s2 = 0; for (int r : areas) s2 += area_work_units(P.nodes[static_cast<size_t>(r)].raio) * P.nodes[static_cast<size_t>(r)].mult; return s2; }());
            int per = budget / std::max(1, cnt);
            int cells = per - 4;
            if (cells >= 1) {
                int rmax = static_cast<int>(std::floor(100.0 * std::sqrt(cells / kPi)));
                while (rmax > 10 && area_work_units(rmax) > per) --rmax;
                Json alt = Json::arr();
                bool all_top = true;
                for (int r : areas) {
                    const PNode& n = P.nodes[static_cast<size_t>(r)];
                    if (n.id != n.top) all_top = false;
                    if (n.raio > rmax) alt.push(edit_param(n.id, "raio_cm", Json(rmax)));
                }
                if (all_top && alt.size()) {
                    Json s = Json::obj();
                    s.set("codigo", "pico_excedido"); s.set("no", P.nodes[static_cast<size_t>(areas[0])].id);
                    s.set("texto", "Manter as áreas em paralelo com raio de até " + fmt_m(rmax) + " (" + std::to_string(area_work_units(rmax)) + " unidades cada)");
                    s.set("preserva", "simultaneidade e centros");
                    s.set("alteracoes", alt);
                    cx.suggestions.push_back(s);
                }
            }
        }
    }
    if (P.pico_areas > rules.exec_pico_areas)
        cx.add("erro", "pico_areas", resp.empty() ? "" : topstr(resp[0]), "", "até " + std::to_string(P.pico_areas) + " áreas simultâneas; limite " + std::to_string(rules.exec_pico_areas));
    if (P.max_ativacoes > rules.exec_max_ativacoes || P.max_eventos > rules.exec_max_eventos) {
        int worst = -1;
        for (size_t i = 0; i < P.nodes.size(); ++i) if (P.nodes[i].alcancavel && (worst < 0 || P.nodes[i].mult > P.nodes[static_cast<size_t>(worst)].mult)) worst = static_cast<int>(i);
        cx.add("erro", "multiplicacao_eventos", worst >= 0 ? topstr(worst) : "", worst >= 0 ? P.nodes[static_cast<size_t>(worst)].id : "",
               "até " + std::to_string(P.max_ativacoes) + " ativações de nós e " + std::to_string(P.max_eventos) + " eventos por execução (limites " +
               std::to_string(rules.exec_max_ativacoes) + " / " + std::to_string(rules.exec_max_eventos) + "); maior multiplicação em '" +
               (worst >= 0 ? P.nodes[static_cast<size_t>(worst)].rotulo : std::string("?")) + "' ×" + (worst >= 0 ? std::to_string(P.nodes[static_cast<size_t>(worst)].mult) : "0"));
    }
    if (P.max_dur_ms > rules.exec_max_duracao_ms)
        cx.add("erro", "duracao", "", "", "a execução pode durar " + fmt_ms(P.max_dur_ms) + "; limite " + fmt_ms(rules.exec_max_duracao_ms));
    if (P.pico_memoria > rules.exec_max_memoria)
        cx.add("erro", "memoria", "", "", "pico de memória " + std::to_string(P.pico_memoria) + " B; limite " + std::to_string(rules.exec_max_memoria) + " B");
    if (folego_concedido > P.folego) {
        std::string who;
        for (auto& n : P.nodes) if (n.alcancavel && n.kind == OpKind::Amparo && n.folego > 0) { who = n.top; break; }
        cx.add("erro", "geracao_recursos", who, "", "pode conceder até " + std::to_string(static_cast<int>(folego_concedido)) +
               " de fôlego e consome só " + std::to_string(P.folego) + ": geração líquida de recursos não é permitida");
    }

    // --- critérios competitivos (aplicados à receita completa, inclusive subcriações) ---
    bool comp_ok = true;
    if (rules.competitiva) {
        auto comp = [&](const std::string& code, const std::string& no, const std::string& cam, const std::string& msg) {
            cx.add("competitivo", code, no, cam, msg);
            comp_ok = false;
        };
        for (auto& h : hits) {
            const PNode& n = P.nodes[static_cast<size_t>(h.node)];
            if (n.prep < rules.preparo_minimo_ms) {
                comp("preparo_curto", n.top, n.id, n.rotulo + ": preparo de " + fmt_ms(n.prep) + " não dá ao oponente o mínimo de " + fmt_ms(rules.preparo_minimo_ms) + " para ler e reagir");
                if (n.id == n.top) {
                    Json s = Json::obj();
                    s.set("codigo", "preparo_curto"); s.set("no", n.id);
                    s.set("texto", "Aumentar o preparo de " + n.rotulo + " para " + fmt_ms(rules.preparo_minimo_ms));
                    s.set("preserva", "forma, alcance e impacto");
                    Json alt = Json::arr(); alt.push(edit_param(n.id, "preparo_ms", Json(rules.preparo_minimo_ms)));
                    s.set("alteracoes", alt);
                    cx.suggestions.push_back(s);
                }
            }
        }
        if (alcance > rules.alcance_max_cm) comp("alcance", hits.empty() ? "" : P.nodes[static_cast<size_t>(hits[0].node)].top, "", "alcance de " + fmt_m(static_cast<int64_t>(alcance)) + " excede " + fmt_m(rules.alcance_max_cm));
        for (auto& n : P.nodes) {
            if (!n.alcancavel || n.kind != OpKind::Guarda) continue;
            if (n.abertura > rules.abertura_max_graus) comp("abertura", n.top, n.id, n.rotulo + ": abertura de " + std::to_string(n.abertura) + "° excede " + std::to_string(rules.abertura_max_graus) + "° (o oponente precisa de um flanco)");
            if (n.cargas > rules.cargas_max) comp("cargas", n.top, n.id, n.rotulo + ": " + std::to_string(n.cargas) + " cargas excedem " + std::to_string(rules.cargas_max));
        }
        if (P.max_dur_ms > rules.duracao_max_ms) comp("duracao", "", "", "duração máxima " + fmt_ms(P.max_dur_ms) + " excede " + fmt_ms(rules.duracao_max_ms));
        if (impacto_total > rules.impacto_max) comp("impacto", "", "", "impacto total possível " + std::to_string(impacto_total) + " excede " + std::to_string(rules.impacto_max));
        if (P.custo > rules.custo_max) {
            std::string worst;
            double wv = 0;
            for (auto& kv : by_top) if (kv.second > wv) { wv = kv.second; worst = kv.first; }
            comp("custo_alto", worst, "", "custo competitivo " + fmt1(P.custo) + " excede " + fmt1(rules.custo_max) + " (maior parcela: " + worst + ")");
            for (auto& n : P.nodes) {
                if (!n.alcancavel || n.top != worst || n.id != n.top) continue;
                if (n.kind == OpKind::Anel || n.kind == OpKind::Deslocar || n.kind == OpKind::Guarda || n.kind == OpKind::Retornar) {
                    int need = static_cast<int>(std::ceil((P.custo - rules.custo_max) * 200.0 / std::max(1, n.mult)));
                    if (n.recup + need <= 2000) {
                        Json s = Json::obj();
                        s.set("codigo", "custo_alto"); s.set("no", n.id);
                        s.set("texto", "Acrescentar " + fmt_ms(need) + " de recuperação em " + n.rotulo + " (abre uma janela de punição proporcional)");
                        s.set("preserva", "forma, alcance, tempo de contato");
                        Json alt = Json::arr(); alt.push(edit_param(n.id, "recuperacao_ms", Json(n.recup + need)));
                        s.set("alteracoes", alt);
                        cx.suggestions.push_back(s);
                    }
                    break;
                }
            }
        }
    }
    P.competitiva_ok = comp_ok;

    // --- linha do tempo ---
    Json tl = Json::arr();
    auto seg = [&](const PNode& n, const char* tipo, int64_t a, int64_t b) {
        Json s = Json::obj();
        s.set("no", n.id); s.set("top", n.top); s.set("rotulo", n.rotulo); s.set("fundamento", fundamento_of(n.kind));
        s.set("escopo", P.scopes[static_cast<size_t>(n.scope)].nome); s.set("tipo", tipo);
        s.set("ini", n.smin + a); s.set("fim", n.smin + b); s.set("ini_max", n.smax + a); s.set("fim_max", n.smax + b);
        s.set("manual", n.manual); s.set("condicional", !n.certo); s.set("mult", n.mult);
        tl.push(s);
    };
    for (auto& n : P.nodes) {
        if (!n.alcancavel) continue;
        switch (n.kind) {
            case OpKind::Deslocar: case OpKind::Retornar: seg(n, "deslocamento", 0, n.dur); if (n.recup) seg(n, "recuperacao", n.dur, n.dur + n.recup); break;
            case OpKind::Anel: seg(n, "preparo", 0, n.prep); seg(n, "contato", n.prep, n.prep + n.contato); if (n.recup) seg(n, "recuperacao", n.prep + n.contato, n.prep + n.contato + n.recup); break;
            case OpKind::Guarda: seg(n, "preparo", 0, n.prep); seg(n, "persistencia", n.prep, n.prep + n.dur); if (n.recup) seg(n, "recuperacao", n.prep + n.dur, n.prep + n.dur + n.recup); break;
            case OpKind::Amparo: seg(n, "preparo", 0, n.prep); seg(n, "persistencia", n.prep, n.prep + n.dur); break;
            case OpKind::Janela: seg(n, "janela", n.abre, n.abre + n.dura); seg(n, "confirmacao", n.abre + n.dura, n.abre + n.dura + rules.tolerancia_ms); break;
            case OpKind::Vigiar: seg(n, "vigia", 0, n.dur); break;
            case OpKind::Esperar: seg(n, "espera", 0, n.ms); break;
            case OpKind::Repetir: seg(n, "repeticao", 0, static_cast<int64_t>(n.vezes - 1) * n.intervalo); break;
            case OpKind::Marca: case OpKind::Sondar: case OpKind::Encerrar: seg(n, "instante", 0, 0); break;
            default: break;
        }
    }
    an.set("linha_do_tempo", tl);

    Json probs = Json::arr();
    for (auto& i : cx.issues) probs.push(i.to_json());
    an.set("problemas", probs);
    Json sugs = Json::arr();
    int sid = 1;
    for (auto& s : cx.suggestions) { Json x = s; x.set("id", "s" + std::to_string(sid++)); sugs.push(x); }
    an.set("sugestoes", sugs);
    an.set("executavel", !cx.fatal);
    an.set("competitiva_ok", comp_ok && !cx.fatal);

    Json cc = Json::obj();
    cc.set("pico_trabalho", P.pico_trabalho); cc.set("limite_trabalho", rules.exec_pico_trabalho); cc.set("pico_em_ms", pw_t);
    Json rs = Json::arr(); for (int r : resp) rs.push(P.nodes[static_cast<size_t>(r)].id); cc.set("responsaveis_pico", rs);
    cc.set("pico_areas", P.pico_areas); cc.set("limite_areas", rules.exec_pico_areas);
    cc.set("ativacoes", P.max_ativacoes); cc.set("limite_ativacoes", rules.exec_max_ativacoes);
    cc.set("eventos", P.max_eventos); cc.set("limite_eventos", rules.exec_max_eventos);
    cc.set("duracao_ms", P.max_dur_ms); cc.set("limite_duracao_ms", rules.exec_max_duracao_ms);
    cc.set("memoria_bytes", P.pico_memoria); cc.set("limite_memoria", rules.exec_max_memoria);
    cc.set("armazenamento_bytes", P.armazenamento);
    cc.set("trafego_bytes_binario", P.trafego_bytes);
    cc.set("trafego_bytes_json_estimado", P.trafego_bytes * 4);
    Json wb = Json::obj(); for (auto& kv : work_by_top) wb.set(kv.first, kv.second); cc.set("trabalho_por_no", wb);
    an.set("custo_computacional", cc);
    Json cp = Json::obj();
    cp.set("pontos", P.custo); cp.set("limite", rules.custo_max); cp.set("recarga_ms", P.recarga_ms); cp.set("folego", P.folego);
    cp.set("alcance_cm", P.alcance_cm); cp.set("preparo_min_ms", P.preparo_min_ms); cp.set("impacto_total", impacto_total);
    cp.set("criterios_ativos", rules.competitiva);
    Json bt = Json::obj(); for (auto& kv : by_top) bt.set(kv.first, std::round(kv.second * 10) / 10); cp.set("por_no", bt);
    cp.set("detalhes", detalhes);
    an.set("custo_competitivo", cp);
    Json esc = Json::arr();
    for (auto& s : P.scopes) { Json x = Json::obj(); x.set("nome", s.nome); x.set("digest", s.digest); x.set("fim_min", s.end_min); x.set("fim_max", s.end_max); esc.push(x); }
    an.set("escopos", esc);

    out.ok = !cx.fatal;
    out.competitiva_ok = comp_ok && !cx.fatal;
    out.issues = cx.issues;
    out.analise = an;
    if (out.ok) out.plan = std::make_shared<Plan>(std::move(cx.plan));
    return out;
}

}  // namespace forja
