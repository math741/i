#include "runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace forja {

namespace {
double r1(double v) { return std::round(v * 10.0) / 10.0; }
Json jv(const Vec3& v) {
    Json a = Json::arr();
    a.push(Json(r1(v.x))); a.push(Json(r1(v.y))); a.push(Json(r1(v.z)));
    return a;
}
std::string fmt_cm(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.1f", v);
    std::string s = b;
    for (auto& c : s) if (c == '.') c = ',';
    return s + " cm";
}
}  // namespace

// ---------------- Arena ----------------
Surface Arena::surface_at(const Vec3& p) const {
    for (auto& s : superficies) if (s.box.contains2d(p)) return s.s;
    return Surface::Concreto;
}

Vec3 Arena::clamp(const Vec3& p, double r) const {
    Vec3 o = p;
    o.x = std::max(limites.mn.x + r, std::min(limites.mx.x - r, o.x));
    o.y = std::max(limites.mn.y + r, std::min(limites.mx.y - r, o.y));
    return o;
}

Arena Arena::galeria() {
    // Galeria de drenagem: 28 m × 9 m, pé-direito 5 m. Canal raso de 1,40 m ao longo do eixo X,
    // juntas metálicas de dilatação a ±7 m. Mesmas medidas no cliente e no nível da Unreal.
    Arena a;
    a.limites = {{-1400, -450, -100}, {1400, 450, 600}};
    a.superficies.push_back({{{-1400, -70, -30}, {1400, 70, 10}}, Surface::Agua, "canal"});
    a.superficies.push_back({{{-710, -450, -5}, {-690, 450, 5}}, Surface::Metal, "junta_oeste"});
    a.superficies.push_back({{{690, -450, -5}, {710, 450, 5}}, Surface::Metal, "junta_leste"});
    return a;
}

Json Arena::to_json() const {
    Json j = Json::obj();
    j.set("min", jv(limites.mn));
    j.set("max", jv(limites.mx));
    Json s = Json::arr();
    for (auto& x : superficies) {
        Json o = Json::obj();
        o.set("nome", x.nome); o.set("tipo", surface_name(x.s)); o.set("min", jv(x.box.mn)); o.set("max", jv(x.box.mx));
        s.push(o);
    }
    j.set("superficies", s);
    j.set("lentidao_agua", lentidao_agua);
    return j;
}

// ---------------- Runtime ----------------
Runtime::Runtime(Arena a, Rules r) : arena_(std::move(a)), rules_(r) {}

int Runtime::add_entity(EntKind k, int team, const std::string& nome, Vec3 pos, double yaw) {
    Entity e;
    e.id = ents_.empty() ? 1 : ents_.back().id + 1;
    e.kind = k; e.team = team; e.nome = nome; e.pos = pos; e.yaw = yaw; e.last_t = now_;
    if (k == EntKind::Alvo) { e.speed = 0; }
    ents_.push_back(e);
    Json d = Json::obj();
    d.set("ent", e.id); d.set("nome", nome); d.set("tipo", k == EntKind::Alvo ? "alvo" : "jogador"); d.set("time", team); d.set("pos", jv(pos)); d.set("yaw", yaw);
    emit("entidade.nova", d);
    return e.id;
}

Entity* Runtime::ent(int id) { for (auto& e : ents_) if (e.id == id) return &e; return nullptr; }
const Entity* Runtime::ent(int id) const { for (auto& e : ents_) if (e.id == id) return &e; return nullptr; }
Runtime::Exec* Runtime::exec(uint32_t id) { auto it = execs_.find(id); return it == execs_.end() ? nullptr : &it->second; }
const Runtime::Exec* Runtime::exec(uint32_t id) const { auto it = execs_.find(id); return it == execs_.end() ? nullptr : &it->second; }

void Runtime::set_move(int id, Vec3 dir, double yaw, int64_t t) {
    advance(t);
    Entity* e = ent(id);
    if (!e) return;
    double l = dir.len2d();
    e->move_dir = l > 1e-6 ? Vec3(dir.x / l, dir.y / l, 0) : Vec3();
    e->yaw = wrap_deg(yaw);
}

void Runtime::teleport(int id, Vec3 pos, double yaw, int64_t t) {
    advance(t);
    Entity* e = ent(id);
    if (!e) return;
    e->pos = arena_.clamp(pos, e->radius);
    e->yaw = wrap_deg(yaw);
    e->em_mov = false;
    e->move_dir = Vec3();
}

void Runtime::emit(const std::string& tipo, Json dados, uint32_t ex) {
    Json e = Json::obj();
    e.set("tipo", tipo);
    if (!dados.has("t")) e.set("t", now_);
    for (auto& kv : dados.members()) e.set(kv.first, kv.second);
    if (ex) { e.set("exec", ex); if (Exec* x = exec(ex)) x->eventos++; }
    out_.push_back(e);
}

std::vector<Json> Runtime::drain() {
    std::vector<Json> v;
    v.swap(out_);
    return v;
}

void Runtime::push_token(Exec& ex, int node, int sinst, int64_t t, const TokCtx& c) {
    ex.scopes[static_cast<size_t>(sinst)].active++;
    QItem q{t, 1, ex.id, seq_++, 0, node, sinst, 0, 0, c};
    q_.push(q);
}

void Runtime::push_timer(uint32_t ex, int node, int sinst, int64_t t, int fase, int tag, uint32_t inst, const TokCtx& c) {
    QItem q{t, fase, ex, seq_++, 1, node, sinst, tag, inst, c};
    q_.push(q);
}

void Runtime::fire_out(Exec& ex, int node, int port, int sinst, int64_t t, const TokCtx& c) {
    const PNode& n = ex.plan->nodes[static_cast<size_t>(node)];
    if (port < 0 || static_cast<size_t>(port) >= n.out.size()) return;
    for (int tgt : n.out[static_cast<size_t>(port)]) push_token(ex, tgt, sinst, t, c);
}

void Runtime::scope_dec(Exec& ex, int sinst, int64_t t) {
    if (!ex.alive) return;
    ScopeInst& s = ex.scopes[static_cast<size_t>(sinst)];
    s.active--;
    if (s.active > 0 || s.ended) return;
    s.ended = true;
    if (sinst == 0) { finish_exec(ex, "concluida", t); return; }
    int parent = s.parent, embed = s.embed;
    TokCtx ctx = s.ctx;
    Json d = Json::obj();
    d.set("no", ex.plan->nodes[static_cast<size_t>(embed)].id);
    d.set("rotulo", ex.plan->nodes[static_cast<size_t>(embed)].rotulo);
    d.set("t", t);
    emit("escopo.fim", d, ex.id);
    fire_out(ex, embed, 0, parent, t, ctx);
    scope_dec(ex, parent, t);
}

void Runtime::finish_exec(Exec& ex, const std::string& motivo, int64_t t) {
    if (!ex.alive) return;
    ex.alive = false;
    Json d = Json::obj();
    d.set("motivo", motivo); d.set("t", t); d.set("autor", ex.owner); d.set("slot", ex.slot);
    d.set("duracao_ms", t - ex.t0);
    emit("exec.fim", d, ex.id);
}

bool Runtime::exec_alive(uint32_t id) const { const Exec* e = exec(id); return e && e->alive; }
bool Runtime::has_alive_exec(int entid, int slot) const { return alive_exec_of(entid, slot) != 0; }
uint32_t Runtime::alive_exec_of(int entid, int slot) const {
    for (auto& kv : execs_) if (kv.second.alive && kv.second.owner == entid && kv.second.slot == slot) return kv.first;
    return 0;
}

int Runtime::execucoes_ativas() const { int n = 0; for (auto& kv : execs_) if (kv.second.alive) ++n; return n; }
int Runtime::trabalho_reservado() const {
    int w = 0;
    for (auto& kv : execs_) if (kv.second.alive) w += kv.second.plan->pico_trabalho;
    for (auto& kv : areas_) if (kv.second.viva && kv.second.orfa) w += area_work_units(kv.second.raio);
    return w;
}
int Runtime::areas_ativas() const {
    int n = 0;
    for (auto& kv : execs_) if (kv.second.alive) n += kv.second.plan->pico_areas;
    for (auto& kv : areas_) if (kv.second.viva && kv.second.orfa) ++n;
    return n;
}

Runtime::Act Runtime::activate(int entid, int slot, std::shared_ptr<const Plan> plan, int64_t t, double aim_yaw, Vec3 aim_point) {
    advance(t);
    Act r;
    Entity* e = ent(entid);
    if (!e || !e->ativo) { r.motivo = "entidade inativa"; return r; }
    if (uint32_t live = alive_exec_of(entid, slot)) {
        command(entid, slot, false, false, t, t);
        r.ok = true; r.exec = live; r.foi_comando = true;
        return r;
    }
    if (!plan) { r.motivo = "nenhuma criação equipada neste espaço"; return r; }
    if (t < e->recup_ate) { r.motivo = "em recuperação por mais " + std::to_string(e->recup_ate - t) + " ms"; return r; }
    if (e->em_mov) { r.motivo = "em deslocamento"; return r; }
    if (!sem_recarga) {
        auto it = e->recarga_ate.find(slot);
        if (it != e->recarga_ate.end() && t < it->second) { r.motivo = "recarga: faltam " + std::to_string(it->second - t) + " ms"; return r; }
        if (e->folego + 1e-9 < plan->folego) { r.motivo = "fôlego insuficiente (" + std::to_string(static_cast<int>(e->folego)) + "/" + std::to_string(plan->folego) + ")"; return r; }
    }
    if (execucoes_ativas() >= rules_.sala_execucoes) { r.motivo = "sala no limite de " + std::to_string(rules_.sala_execucoes) + " execuções simultâneas"; return r; }
    if (trabalho_reservado() + plan->pico_trabalho > rules_.sala_trabalho) {
        r.motivo = "sala no limite de trabalho (" + std::to_string(trabalho_reservado()) + " + " + std::to_string(plan->pico_trabalho) + " > " + std::to_string(rules_.sala_trabalho) + " unidades/tick)";
        return r;
    }
    if (areas_ativas() + plan->pico_areas > rules_.sala_areas) {
        r.motivo = "sala no limite de áreas (" + std::to_string(areas_ativas()) + " + " + std::to_string(plan->pico_areas) + " > " + std::to_string(rules_.sala_areas) + ")";
        return r;
    }
    Exec ex;
    ex.id = next_exec_++;
    ex.owner = entid; ex.slot = slot; ex.plan = plan; ex.t0 = t;
    ex.deadline = t + plan->max_dur_ms + 2 * rules_.tolerancia_ms + 100;
    ex.origin = e->pos; ex.yaw0 = e->yaw; ex.aim_yaw = aim_yaw; ex.aim_point = aim_point;
    ex.scopes.push_back(ScopeInst());
    execs_[ex.id] = ex;
    if (!sem_recarga) {
        e->recarga_ate[slot] = t + plan->recarga_ms;
        e->folego -= plan->folego;
    }
    Json d = Json::obj();
    d.set("autor", entid); d.set("slot", slot); d.set("digest", plan->digest); d.set("t", t);
    d.set("origem", jv(e->pos)); d.set("yaw", r1(e->yaw)); d.set("mira_yaw", r1(aim_yaw));
    d.set("recarga_ate", sem_recarga ? t : t + plan->recarga_ms); d.set("folego", r1(e->folego));
    d.set("duracao_max_ms", plan->max_dur_ms);
    emit("exec.inicio", d, ex.id);
    push_token(execs_[ex.id], plan->entry, 0, t, TokCtx());
    push_timer(ex.id, -1, 0, ex.deadline, 3, T_PRAZO, 0);
    advance(t);
    r.ok = true;
    r.exec = ex.id;
    return r;
}

void Runtime::command(int entid, int slot, bool soltar, bool secundario, int64_t tj, int64_t tnow) {
    advance(tnow);
    int type = secundario ? 2 : (soltar ? 1 : 0);
    bool any = false;
    for (auto& kv : execs_) {
        Exec& ex = kv.second;
        if (!ex.alive || ex.owner != entid || ex.slot != slot) continue;
        any = true;
        ex.presses.push_back({tj, type});
        Json rec = Json::obj();
        rec.set("t", tnow); rec.set("t_julgado", tj); rec.set("comando", type == 0 ? "pressionar" : (type == 1 ? "soltar" : "secundario"));
        emit("cmd.recebido", rec, ex.id);
        std::vector<uint32_t> ws;
        for (auto& wk : windows_) if (wk.second.exec == ex.id && !wk.second.decidida) ws.push_back(wk.first);
        for (uint32_t wid : ws) {
            Window& w = windows_[wid];
            int want = w.secundario ? 2 : (w.soltar ? 1 : 0);
            if (want != type) continue;
            const PNode& n = ex.plan->nodes[static_cast<size_t>(w.node)];
            Json d = Json::obj();
            d.set("no", n.id); d.set("rotulo", n.rotulo); d.set("t_comando", tj); d.set("t_abre", w.t_abre); d.set("t_fecha", w.t_fecha); d.set("t", tnow);
            if (tj >= w.t_abre && tj < w.t_fecha) {
                w.decidida = true;
                d.set("resultado", "comando");
                d.set("t_decisao", tnow);
                emit("janela.resultado", d, ex.id);
                int sinst = w.sinst;
                int node = w.node;
                TokCtx c = w.ctx;
                windows_.erase(wid);
                fire_out(ex, node, 0, sinst, tnow, c);
                scope_dec(ex, sinst, tnow);
            } else if (tj < w.t_abre) {
                d.set("motivo", "comando cedo: " + std::to_string(w.t_abre - tj) + " ms antes de abrir");
                emit("cmd.cedo", d, ex.id);
            } else {
                d.set("motivo", "comando tarde: " + std::to_string(tj - w.t_fecha) + " ms depois de fechar");
                emit("cmd.tarde", d, ex.id);
            }
        }
    }
    if (!any) {
        Json d = Json::obj();
        d.set("ent", entid); d.set("slot", slot); d.set("motivo", "nenhuma execução ativa neste espaço");
        emit("cmd.ignorado", d);
    }
    advance(tnow);
}

Guard* Runtime::find_guard(uint32_t gid, Entity** owner) {
    for (auto& e : ents_)
        for (auto& g : e.guards)
            if (g.id == gid) { if (owner) *owner = &e; return &g; }
    return nullptr;
}

void Runtime::end_guard(Entity& e, uint32_t gid, const std::string& motivo, int64_t t) {
    Guard g;
    bool found = false;
    for (size_t i = 0; i < e.guards.size(); ++i)
        if (e.guards[i].id == gid) { g = e.guards[i]; e.guards.erase(e.guards.begin() + static_cast<long>(i)); found = true; break; }
    if (!found) return;
    Json d = Json::obj();
    d.set("id", gid); d.set("ent", e.id); d.set("motivo", motivo); d.set("t", t);
    emit("guarda.fim", d, g.exec);
    if (g.destacada) return;
    Exec* ex = exec(g.exec);
    if (!ex || !ex->alive) return;
    if (motivo == "rompida") fire_out(*ex, g.node, 1, g.sinst, t, TokCtx());
    if (motivo == "expirou") fire_out(*ex, g.node, 2, g.sinst, t, TokCtx());
    fire_out(*ex, g.node, 3, g.sinst, t, TokCtx());
    if (g.recup) e.recup_ate = std::max(e.recup_ate, t + g.recup);
    scope_dec(*ex, g.sinst, t);
}

void Runtime::start_motion(Exec& ex, int node, int sinst, Entity& e, Vec3 to, Vec3 ctrl, int dur, int64_t t) {
    if (e.em_mov) {
        uint32_t old = e.mov_inst;
        auto it = motions_.find(old);
        if (it != motions_.end()) {
            Json d = Json::obj();
            d.set("ent", e.id); d.set("pos", jv(e.pos)); d.set("interrompido", "substituído por outro deslocamento"); d.set("t", t);
            emit("mov.fim", d, it->second.first);
            Exec* oe = exec(it->second.first);
            int os = mov_sinst_[old];
            motions_.erase(it);
            mov_sinst_.erase(old);
            if (oe && oe->alive) scope_dec(*oe, os, t);
        }
    }
    Vec3 lt = arena_.clamp(to, e.radius);
    Vec3 lc = arena_.clamp(ctrl, e.radius);
    bool parede = (lt - to).len2d() > 0.5;
    e.em_mov = true;
    e.m_from = e.pos; e.m_ctrl = lc; e.m_to = lt; e.m_t0 = t; e.m_t1 = t + dur;
    uint32_t inst = next_inst_++;
    e.mov_inst = inst;
    motions_[inst] = {ex.id, node};
    mov_sinst_[inst] = sinst;
    ex.scopes[static_cast<size_t>(sinst)].active++;
    const PNode& n = ex.plan->nodes[static_cast<size_t>(node)];
    Json d = Json::obj();
    d.set("no", n.id); d.set("rotulo", n.rotulo); d.set("ent", e.id);
    d.set("de", jv(e.m_from)); d.set("ctrl", jv(lc)); d.set("para", jv(lt)); d.set("t0", t); d.set("t1", t + dur); d.set("t", t);
    if (parede) d.set("parede", true);
    emit("mov.inicio", d, ex.id);
    push_timer(ex.id, node, sinst, t + dur, 1, T_MOV_FIM, inst);
}

Vec3 Runtime::anchor_point(Exec& ex, const PNode& n, int sinst, const TokCtx& c) {
    Entity* me = ent(ex.owner);
    Vec3 cur = me ? me->pos : ex.origin;
    switch (n.ancora) {
        case Anchor::Autor: return cur;
        case Anchor::Ativacao: return ex.origin;
        case Anchor::Evento: return c.tem_ponto ? c.ponto : cur;
        case Anchor::Marca: {
            auto it = ex.marks.find({sinst, n.data_src});
            return it == ex.marks.end() ? cur : it->second;
        }
    }
    return cur;
}

void Runtime::on_token(Exec& ex, const QItem& q) {
    const PNode& n = ex.plan->nodes[static_cast<size_t>(q.node)];
    Entity* me = ent(ex.owner);
    if (!me) return;
    ex.ativacoes++;
    if (ex.ativacoes > rules_.exec_max_ativacoes || ex.eventos > rules_.exec_max_eventos) {
        cancel(ex.id, "limite de ativações/eventos por execução", q.t, false);
        return;
    }
    TokCtx here;
    here.tem_ponto = true;
    here.ponto = me->pos;
    switch (n.kind) {
        case OpKind::Ativar: fire_out(ex, q.node, 0, q.sinst, q.t, q.ctx); break;
        case OpKind::Deslocar: {
            double yaw = n.espaco == Espaco::Ativacao ? ex.yaw0 : (n.espaco == Espaco::Mira ? ex.aim_yaw : me->yaw);
            Vec3 d = local_to_world(n.vec, yaw);
            Vec3 to = me->pos + d;
            Vec3 ctrl = me->pos + d * 0.5 + right_of(yaw) * static_cast<double>(n.curva) * 2.0;  // curva = desvio no meio do trajeto
            start_motion(ex, q.node, q.sinst, *me, to, ctrl, n.dur, q.t);
            break;
        }
        case OpKind::Retornar: {
            auto it = ex.marks.find({q.sinst, n.data_src});
            if (it == ex.marks.end()) {
                Json d = Json::obj(); d.set("no", n.id); d.set("motivo", "marca indefinida");
                emit("erro", d, ex.id);
                break;
            }
            Vec3 dest = it->second;
            Vec3 dv = dest - me->pos;
            double L = dv.len2d();
            if (L > n.alcance) dest = me->pos + dv * (n.alcance / L);
            Vec3 mid = me->pos + (dest - me->pos) * 0.5;
            start_motion(ex, q.node, q.sinst, *me, dest, mid, n.dur, q.t);
            break;
        }
        case OpKind::Marca: {
            Vec3 p = anchor_point(ex, n, q.sinst, q.ctx);
            ex.marks[{q.sinst, q.node}] = p;
            Json d = Json::obj(); d.set("no", n.id); d.set("rotulo", n.rotulo); d.set("ponto", jv(p)); d.set("t", q.t);
            emit("marca", d, ex.id);
            TokCtx c; c.tem_ponto = true; c.ponto = p;
            fire_out(ex, q.node, 0, q.sinst, q.t, c);
            break;
        }
        case OpKind::Anel: {
            Area a;
            a.id = next_inst_++;
            a.exec = ex.id; a.node = q.node; a.sinst = q.sinst; a.owner = ex.owner; a.team = me->team;
            a.desloc_mundo = local_to_world(n.desloc, me->yaw);
            a.centro = anchor_point(ex, n, q.sinst, q.ctx) + a.desloc_mundo;
            a.raio = n.raio; a.esp = n.esp; a.altura = n.altura; a.impacto = n.impacto; a.recup = n.recup;
            a.afeta = n.afeta; a.segue = n.segue;
            a.t_spawn = q.t; a.t_contato = q.t + n.prep; a.t_fim = a.t_contato + n.contato;
            areas_[a.id] = a;
            ex.scopes[static_cast<size_t>(q.sinst)].active++;
            Json d = Json::obj();
            d.set("id", a.id); d.set("no", n.id); d.set("rotulo", n.rotulo); d.set("autor", ex.owner);
            d.set("centro", jv(a.centro)); d.set("raio", a.raio); d.set("espessura", a.esp); d.set("altura", a.altura);
            d.set("t", q.t); d.set("t_contato", a.t_contato); d.set("t_fim", a.t_fim); d.set("impacto", a.impacto); d.set("segue", a.segue);
            d.set("superficie", surface_name(arena_.surface_at(a.centro)));
            emit("area.anuncio", d, ex.id);
            push_timer(ex.id, q.node, q.sinst, a.t_contato, 2, T_AREA_CONTATO, a.id);
            break;
        }
        case OpKind::Sondar: {
            Vec3 p = anchor_point(ex, n, q.sinst, q.ctx);
            Surface s = arena_.surface_at(p);
            bool yes = s == n.sup;
            Json d = Json::obj();
            d.set("no", n.id); d.set("rotulo", n.rotulo); d.set("ponto", jv(p)); d.set("superficie", surface_name(s));
            d.set("procurada", surface_name(n.sup)); d.set("resultado", yes); d.set("t", q.t);
            emit("sondar", d, ex.id);
            TokCtx c; c.tem_ponto = true; c.ponto = p;
            fire_out(ex, q.node, yes ? 0 : 1, q.sinst, q.t, c);
            break;
        }
        case OpKind::Vigiar: {
            Watch w{ex.id, q.node, q.sinst, ex.owner, n.sup, q.t + n.dur, n.disparos, arena_.surface_at(me->pos) == n.sup, true, q.ctx};
            uint32_t id = next_inst_++;
            watches_[id] = w;
            ex.scopes[static_cast<size_t>(q.sinst)].active++;
            Json d = Json::obj(); d.set("no", n.id); d.set("rotulo", n.rotulo); d.set("superficie", surface_name(n.sup)); d.set("t_fim", w.t_fim); d.set("t", q.t);
            emit("vigia.inicio", d, ex.id);
            push_timer(ex.id, q.node, q.sinst, w.t_fim, 3, T_VIGIA_FIM, id);
            break;
        }
        case OpKind::Anteparo: {
            Guard g;
            g.id = next_inst_++;
            g.exec = ex.id; g.node = q.node; g.sinst = q.sinst;
            g.t_on = q.t + n.prep; g.t_off = g.t_on + n.dur;
            g.abertura = n.abertura; g.yaw_fixo = me->yaw; g.orient = n.orient; g.segue = n.segue;
            g.cargas = n.cargas; g.gesto = n.gesto; g.recup = n.recup;
            me->guards.push_back(g);
            ex.scopes[static_cast<size_t>(q.sinst)].active++;
            Json d = Json::obj();
            d.set("id", g.id); d.set("no", n.id); d.set("rotulo", n.rotulo); d.set("ent", me->id);
            d.set("t", q.t); d.set("t_ativa", g.t_on); d.set("t_fim", g.t_off); d.set("abertura", g.abertura);
            d.set("eixo_yaw", r1(wrap_deg(me->yaw + g.orient))); d.set("orientacao", g.orient); d.set("segue", g.segue);
            d.set("cargas", g.cargas); d.set("gesto", g.gesto);
            emit("guarda.inicio", d, ex.id);
            push_timer(ex.id, q.node, q.sinst, g.t_off, 3, T_GUARD_FIM, g.id);
            break;
        }
        case OpKind::Amparo: {
            uint32_t id = next_inst_++;
            ex.scopes[static_cast<size_t>(q.sinst)].active++;
            Json d = Json::obj();
            d.set("id", id); d.set("no", n.id); d.set("rotulo", n.rotulo); d.set("centro", jv(me->pos)); d.set("raio", n.raio);
            d.set("t", q.t); d.set("t_efeito", q.t + n.prep);
            emit("amparo.anuncio", d, ex.id);
            push_timer(ex.id, q.node, q.sinst, q.t + n.prep, 1, T_AMPARO, id, q.ctx);
            break;
        }
        case OpKind::Janela: {
            Window w{ex.id, q.node, q.sinst, ex.owner, ex.slot, q.t + n.abre, q.t + n.abre + n.dura, false, n.soltar, n.secundario, n.cedo_anula, n.tol_cedo, q.ctx};
            uint32_t id = next_inst_++;
            windows_[id] = w;
            ex.scopes[static_cast<size_t>(q.sinst)].active++;
            Json d = Json::obj();
            d.set("id", id); d.set("no", n.id); d.set("rotulo", n.rotulo); d.set("t", q.t);
            d.set("t_abre", w.t_abre); d.set("t_fecha", w.t_fecha); d.set("t_decide", w.t_fecha + rules_.tolerancia_ms);
            d.set("gatilho", n.soltar ? "soltar" : "pressionar"); d.set("comando", n.secundario ? "secundario" : "mesmo_botao");
            emit("janela.abre", d, ex.id);
            if (n.cedo_anula) push_timer(ex.id, q.node, q.sinst, w.t_abre, 1, T_JANELA_ABRE, id);
            push_timer(ex.id, q.node, q.sinst, w.t_fecha + rules_.tolerancia_ms, 3, T_JANELA_DECIDE, id);
            break;
        }
        case OpKind::Esperar:
            ex.scopes[static_cast<size_t>(q.sinst)].active++;
            push_timer(ex.id, q.node, q.sinst, q.t + n.ms, 1, T_ESPERA, 0, q.ctx);
            break;
        case OpKind::Repetir: {
            fire_out(ex, q.node, 0, q.sinst, q.t, q.ctx);
            if (n.vezes <= 1) { fire_out(ex, q.node, 1, q.sinst, q.t, q.ctx); break; }
            uint32_t id = next_inst_++;
            Rep r; r.exec = ex.id; r.node = q.node; r.sinst = q.sinst; r.k = 1; r.ctx = q.ctx;
            reps_[id] = r;
            ex.scopes[static_cast<size_t>(q.sinst)].active++;
            push_timer(ex.id, q.node, q.sinst, q.t + n.intervalo, 1, T_REPETE, id);
            break;
        }
        case OpKind::Reunir: {
            int& c = ex.joins[{q.sinst, q.node}];
            if (++c >= n.n) { c = 0; fire_out(ex, q.node, 0, q.sinst, q.t, q.ctx); }
            break;
        }
        case OpKind::Encerrar:
            cancel(ex.id, "interrompida pela própria criação (" + n.rotulo + ")", q.t, true);
            return;
        case OpKind::Incorporar: {
            ex.scopes[static_cast<size_t>(q.sinst)].active++;
            ScopeInst c;
            c.scope = n.child_scope; c.parent = q.sinst; c.embed = q.node; c.ctx = q.ctx;
            int ci = static_cast<int>(ex.scopes.size());
            ex.scopes.push_back(c);
            Json d = Json::obj(); d.set("no", n.id); d.set("rotulo", n.rotulo); d.set("digest", ex.plan->scopes[static_cast<size_t>(n.child_scope)].digest); d.set("t", q.t);
            emit("escopo.inicio", d, ex.id);
            push_token(ex, n.child_entry, ci, q.t, q.ctx);
            break;
        }
        case OpKind::Planejada: break;
    }
}

void Runtime::area_check(Area& a, int64_t t, bool primeiro) {
    struct Dec { int ent; double d, lim; int res; double ang; uint32_t guard; std::string motivo; };
    std::vector<Dec> decs;
    Entity* owner = ent(a.owner);
    for (auto& e : ents_) {
        if (!e.ativo || e.id == a.owner) continue;
        if (std::find(a.atingidos.begin(), a.atingidos.end(), e.id) != a.atingidos.end()) continue;
        bool aliado = e.team == a.team && e.team >= 0;
        bool afeta = a.afeta == Afeta::Todos || (a.afeta == Afeta::Inimigos && !aliado) || (a.afeta == Afeta::Aliados && aliado);
        if (!afeta) continue;
        double d = dist2d(e.pos, a.centro);
        double lim = a.raio + e.radius;
        double inner = a.esp > 0 ? a.raio - a.esp - e.radius : -1e9;
        bool vert = (e.pos.z <= a.centro.z + a.altura) && (e.pos.z + e.height >= a.centro.z - 20);
        Dec dc{e.id, d, lim, 0, 0, 0, ""};
        if (!(d <= lim + 1e-9 && d >= inner && vert)) {
            if (!primeiro) continue;
            if (!vert) dc.motivo = "fora da altura do anel";
            else if (d > lim) dc.motivo = "distância " + fmt_cm(d) + " > raio " + std::to_string(a.raio) + " + cápsula " + std::to_string(static_cast<int>(e.radius)) + " = " + fmt_cm(lim);
            else dc.motivo = "dentro do vazio do anel (" + fmt_cm(d) + " < " + fmt_cm(inner) + ")";
            decs.push_back(dc);
            continue;
        }
        Vec3 src = a.centro;
        if (dist2d(src, e.pos) < 1.0 && owner) src = owner->pos;
        Guard* best = nullptr;
        std::string guard_note;
        for (auto& g : e.guards) {
            if (t < g.t_on || t >= g.t_off || g.cargas <= 0) {
                if (t < g.t_on) guard_note = "anteparo ainda no preparo (ativa em t=" + std::to_string(g.t_on) + ")";
                continue;
            }
            double axis = (g.segue ? e.yaw : g.yaw_fixo) + g.orient;
            double ang = angle_from_axis(axis, src - e.pos);
            if (g.abertura >= 360 || ang <= g.abertura / 2.0 + 1e-6) { best = &g; dc.ang = ang; break; }
            char b[160];
            std::snprintf(b, sizeof(b), "anteparo não cobre: impacto a %.1f° do eixo > %.1f° (metade de %d°)", ang, g.abertura / 2.0, g.abertura);
            guard_note = b;
            dc.ang = ang;
        }
        a.atingidos.push_back(e.id);
        if (best) {
            dc.res = 2;
            dc.guard = best->id;
            char b[160];
            std::snprintf(b, sizeof(b), "absorvido: impacto a %.1f° do eixo ≤ %.1f° (metade de %d°)", dc.ang, best->abertura / 2.0, best->abertura);
            dc.motivo = b;
        } else {
            dc.res = 1;
            dc.motivo = "distância " + fmt_cm(d) + " ≤ " + fmt_cm(lim) + (guard_note.empty() ? "" : "; " + guard_note);
        }
        decs.push_back(dc);
    }
    Json res = Json::arr();
    for (auto& dc : decs) {
        Json r = Json::obj();
        const Entity* e = ent(dc.ent);
        r.set("ent", dc.ent); r.set("nome", e ? e->nome : ""); r.set("pos", jv(e ? e->pos : Vec3()));
        r.set("dist", r1(dc.d)); r.set("limite", r1(dc.lim));
        r.set("resultado", dc.res == 0 ? "fora" : (dc.res == 1 ? "atingido" : "absorvido"));
        if (dc.res != 0 && dc.ang > 0) r.set("angulo", r1(dc.ang));
        r.set("motivo", dc.motivo);
        res.push(r);
    }
    if (primeiro || res.size()) {
        const Exec* ex0 = exec(a.exec);
        Json d = Json::obj();
        d.set("id", a.id); d.set("no", ex0 ? ex0->plan->nodes[static_cast<size_t>(a.node)].id : "");
        d.set("rotulo", ex0 ? ex0->plan->nodes[static_cast<size_t>(a.node)].rotulo : "");
        d.set("autor", a.owner); d.set("centro", jv(a.centro)); d.set("raio", a.raio); d.set("t", t); d.set("primeiro", primeiro);
        d.set("resultados", res);
        emit("area.contato", d, a.exec);
    }
    Exec* ex = exec(a.exec);
    bool outputs = ex && ex->alive && !a.orfa;
    for (auto& dc : decs) {
        if (dc.res == 0) continue;
        Entity* e = ent(dc.ent);
        if (!e) continue;
        TokCtx c; c.tem_ponto = true; c.ponto = e->pos; c.ent = e->id;
        if (dc.res == 2) {
            Entity* go = nullptr;
            Guard* g = find_guard(dc.guard, &go);
            if (!g || !go) continue;
            g->cargas--;
            Json d = Json::obj();
            d.set("id", g->id); d.set("ent", e->id); d.set("area", a.id); d.set("angulo", r1(dc.ang)); d.set("fonte", jv(a.centro));
            d.set("cargas", g->cargas); d.set("t", t);
            uint32_t gexec = g->exec;
            int gnode = g->node, gs = g->sinst;
            bool gdest = g->destacada;
            int gc = g->cargas;
            emit("guarda.absorve", d, gexec);
            Exec* gx = exec(gexec);
            TokCtx src; src.tem_ponto = true; src.ponto = a.centro; src.ent = a.owner;
            if (!gdest && gx && gx->alive) fire_out(*gx, gnode, 0, gs, t, src);
            if (gc <= 0) end_guard(*go, dc.guard, "rompida", t);
            if (outputs) fire_out(*ex, a.node, 1, a.sinst, t, c);
        } else {
            if (a.impacto > 0) {
                Json imp = Json::obj();
                imp.set("autor", a.owner); imp.set("alvo", e->id); imp.set("alvo_nome", e->nome); imp.set("alvo_tipo", e->kind == EntKind::Alvo ? "alvo" : "jogador");
                imp.set("valor", a.impacto); imp.set("area", a.id); imp.set("centro", jv(a.centro)); imp.set("raio", a.raio);
                imp.set("pos_alvo", jv(e->pos)); imp.set("dist", r1(dc.d)); imp.set("t", t);
                if (ex) imp.set("digest", ex->plan->digest);
                imp.set("exec", a.exec);
                emit("impacto", imp, a.exec);
                if (on_impacto) on_impacto(imp);
            }
            if (outputs) fire_out(*ex, a.node, 0, a.sinst, t, c);
        }
    }
}

void Runtime::on_timer(const QItem& q) {
    Exec* ex = exec(q.exec);
    bool live = ex && ex->alive;
    switch (q.tag) {
        case T_MOV_FIM: {
            auto it = motions_.find(q.inst);
            if (it == motions_.end()) return;
            motions_.erase(it);
            mov_sinst_.erase(q.inst);
            Entity* e = ex ? ent(ex->owner) : nullptr;
            if (e && e->em_mov && e->mov_inst == q.inst) {
                e->pos = e->m_to;
                e->em_mov = false;
                const PNode& n = ex->plan->nodes[static_cast<size_t>(q.node)];
                if (n.recup) e->recup_ate = std::max(e->recup_ate, q.t + n.recup);
                Json d = Json::obj(); d.set("no", n.id); d.set("rotulo", n.rotulo); d.set("ent", e->id); d.set("pos", jv(e->pos)); d.set("t", q.t);
                if (n.recup) d.set("recup_ate", q.t + n.recup);
                emit("mov.fim", d, ex->id);
            }
            if (live) {
                TokCtx c; c.tem_ponto = true; c.ponto = e ? e->pos : Vec3();
                fire_out(*ex, q.node, 0, q.sinst, q.t, c);
                scope_dec(*ex, q.sinst, q.t);
            }
            break;
        }
        case T_AREA_CONTATO: case T_AREA_TICK: {
            auto it = areas_.find(q.inst);
            if (it == areas_.end() || !it->second.viva) return;
            Area& a = it->second;
            if (q.tag == T_AREA_CONTATO) {
                if (a.segue) { if (Entity* o = ent(a.owner)) a.centro = o->pos + a.desloc_mundo; }
                a.contato_iniciado = true;
                area_check(a, q.t, true);
                push_timer(q.exec, q.node, q.sinst, a.t_fim, 3, T_AREA_FIM, a.id);
            } else {
                area_check(a, q.t, false);
            }
            auto it2 = areas_.find(q.inst);
            if (it2 == areas_.end()) return;
            int64_t tick = 1000 / std::max(1, rules_.tick_hz);
            if (q.t + tick < it2->second.t_fim) push_timer(q.exec, q.node, q.sinst, q.t + tick, 2, T_AREA_TICK, q.inst);
            break;
        }
        case T_AREA_FIM: {
            auto it = areas_.find(q.inst);
            if (it == areas_.end() || !it->second.viva) return;
            Area a = it->second;
            areas_.erase(it);
            Json d = Json::obj(); d.set("id", a.id); d.set("t", q.t); d.set("atingidos", static_cast<int64_t>(a.atingidos.size()));
            emit("area.fim", d, a.exec);
            if (live && !a.orfa) {
                if (a.recup) if (Entity* o = ent(a.owner)) o->recup_ate = std::max(o->recup_ate, q.t + a.recup);
                TokCtx c; c.tem_ponto = true; c.ponto = a.centro;
                fire_out(*ex, a.node, 2, a.sinst, q.t, c);
                scope_dec(*ex, a.sinst, q.t);
            }
            break;
        }
        case T_GUARD_FIM: {
            Entity* owner = nullptr;
            if (find_guard(q.inst, &owner) && owner) end_guard(*owner, q.inst, "expirou", q.t);
            break;
        }
        case T_JANELA_ABRE: {
            auto it = windows_.find(q.inst);
            if (it == windows_.end() || it->second.decidida || !live) return;
            Window& w = it->second;
            int want = w.secundario ? 2 : (w.soltar ? 1 : 0);
            for (auto& p : ex->presses) {
                if (p.second == want && p.first >= w.t_abre - w.tol && p.first < w.t_abre) {
                    w.decidida = true;
                    const PNode& n = ex->plan->nodes[static_cast<size_t>(w.node)];
                    Json d = Json::obj(); d.set("no", n.id); d.set("rotulo", n.rotulo); d.set("resultado", "anulada"); d.set("t_comando", p.first);
                    d.set("motivo", "comando cedo dentro da tolerância de " + std::to_string(w.tol) + " ms anula a janela"); d.set("t_decisao", q.t); d.set("t", q.t);
                    emit("janela.resultado", d, ex->id);
                    int sinst = w.sinst, node = w.node;
                    TokCtx c = w.ctx;
                    windows_.erase(it);
                    fire_out(*ex, node, 1, sinst, q.t, c);
                    scope_dec(*ex, sinst, q.t);
                    return;
                }
            }
            break;
        }
        case T_JANELA_DECIDE: {
            auto it = windows_.find(q.inst);
            if (it == windows_.end() || it->second.decidida || !live) return;
            Window w = it->second;
            windows_.erase(it);
            const PNode& n = ex->plan->nodes[static_cast<size_t>(w.node)];
            Json d = Json::obj(); d.set("no", n.id); d.set("rotulo", n.rotulo); d.set("resultado", "expirou");
            d.set("t_abre", w.t_abre); d.set("t_fecha", w.t_fecha); d.set("t_decisao", q.t); d.set("t", q.t);
            emit("janela.resultado", d, ex->id);
            fire_out(*ex, w.node, 1, w.sinst, q.t, w.ctx);
            scope_dec(*ex, w.sinst, q.t);
            break;
        }
        case T_VIGIA_FIM: {
            auto it = watches_.find(q.inst);
            if (it == watches_.end()) return;
            Watch w = it->second;
            watches_.erase(it);
            if (!live) return;
            fire_out(*ex, w.node, 1, w.sinst, q.t, w.ctx);
            scope_dec(*ex, w.sinst, q.t);
            break;
        }
        case T_ESPERA:
            if (!live) return;
            fire_out(*ex, q.node, 0, q.sinst, q.t, q.ctx);
            scope_dec(*ex, q.sinst, q.t);
            break;
        case T_REPETE: {
            auto it = reps_.find(q.inst);
            if (it == reps_.end() || !live) return;
            Rep& r = it->second;
            const PNode& n = ex->plan->nodes[static_cast<size_t>(r.node)];
            fire_out(*ex, r.node, 0, r.sinst, q.t, r.ctx);
            r.k++;
            if (r.k >= n.vezes) {
                Rep done = r;
                reps_.erase(it);
                fire_out(*ex, done.node, 1, done.sinst, q.t, done.ctx);
                scope_dec(*ex, done.sinst, q.t);
            } else {
                push_timer(q.exec, q.node, q.sinst, q.t + n.intervalo, 1, T_REPETE, q.inst);
            }
            break;
        }
        case T_AMPARO: {
            if (!live) return;
            const PNode& n = ex->plan->nodes[static_cast<size_t>(q.node)];
            Entity* me = ent(ex->owner);
            if (me) {
                std::vector<int> ids;
                for (auto& e : ents_) {
                    if (!e.ativo || e.kind != EntKind::Jogador || e.team != me->team) continue;
                    if (e.id == me->id && !n.inclui_autor) continue;
                    if (dist2d(e.pos, me->pos) > n.raio + e.radius) continue;
                    ids.push_back(e.id);
                }
                for (int id : ids) {
                    Entity* e = ent(id);
                    Guard g;
                    g.id = next_inst_++; g.exec = ex->id; g.node = q.node; g.sinst = q.sinst; g.t_on = q.t; g.t_off = q.t + n.dur;
                    g.abertura = 360; g.cargas = n.cargas; g.destacada = true; g.gesto = "amparo"; g.yaw_fixo = e->yaw;
                    e->guards.push_back(g);
                    e->folego = std::min(100.0, e->folego + n.folego);
                    Json d = Json::obj();
                    d.set("id", g.id); d.set("no", n.id); d.set("rotulo", n.rotulo); d.set("ent", e->id); d.set("t", q.t); d.set("t_ativa", g.t_on); d.set("t_fim", g.t_off);
                    d.set("abertura", 360); d.set("eixo_yaw", r1(e->yaw)); d.set("cargas", g.cargas); d.set("gesto", "amparo"); d.set("destacada", true);
                    d.set("folego_concedido", n.folego);
                    emit("guarda.inicio", d, ex->id);
                    push_timer(ex->id, q.node, q.sinst, g.t_off, 3, T_GUARD_FIM, g.id);
                    TokCtx c; c.tem_ponto = true; c.ponto = e->pos; c.ent = e->id;
                    fire_out(*ex, q.node, 0, q.sinst, q.t, c);
                }
            }
            fire_out(*ex, q.node, 1, q.sinst, q.t, q.ctx);
            scope_dec(*ex, q.sinst, q.t);
            break;
        }
        case T_PRAZO:
            if (live) cancel(q.exec, "prazo de segurança esgotado", q.t, false);
            break;
        default: break;
    }
}

void Runtime::process(const QItem& q) {
    if (q.kind == 0) {
        Exec* ex = exec(q.exec);
        if (!ex || !ex->alive) return;
        on_token(*ex, q);
        Exec* ex2 = exec(q.exec);
        if (ex2 && ex2->alive) scope_dec(*ex2, q.sinst, q.t);
    } else {
        on_timer(q);
    }
}

void Runtime::step_world(int64_t t) {
    if (t < now_) return;
    for (auto& e : ents_) {
        if (!e.ativo) { e.last_t = t; continue; }
        Vec3 old = e.pos;
        double dt = static_cast<double>(t - e.last_t) / 1000.0;
        if (e.em_mov) {
            double span = static_cast<double>(std::max<int64_t>(1, e.m_t1 - e.m_t0));
            double u = std::max(0.0, std::min(1.0, static_cast<double>(t - e.m_t0) / span));
            e.pos = bezier2(e.m_from, e.m_ctrl, e.m_to, u);
        } else if (e.move_dir.len2d() > 0 && e.speed > 0 && dt > 0) {
            double sp = e.speed;
            if (arena_.surface_at(e.pos) == Surface::Agua) sp *= arena_.lentidao_agua;
            if (t <= e.recup_ate) sp *= 0.5;
            e.pos = arena_.clamp(e.pos + e.move_dir * (sp * dt), e.radius);
        }
        if (dt > 0) e.folego = std::min(100.0, e.folego + 12.0 * dt);
        for (auto& wk : watches_) {
            Watch& w = wk.second;
            if (!w.viva || w.owner != e.id || w.restantes <= 0) continue;
            if (!w.dentro) {
                double best = -1;
                for (auto& sb : arena_.superficies) {
                    if (sb.s != w.s) continue;
                    double u = segment_enter_t(old, e.pos, sb.box);
                    if (u >= 0 && (best < 0 || u < best)) best = u;
                }
                if (best >= 0) {
                    Vec3 p = old + (e.pos - old) * best;
                    int64_t tc = e.last_t + static_cast<int64_t>(std::llround(best * static_cast<double>(t - e.last_t)));
                    w.dentro = true;
                    w.restantes--;
                    Exec* ex = exec(w.exec);
                    if (ex && ex->alive) {
                        const PNode& n = ex->plan->nodes[static_cast<size_t>(w.node)];
                        Json d = Json::obj();
                        d.set("no", n.id); d.set("rotulo", n.rotulo); d.set("ponto", jv(p)); d.set("t_cruzamento", tc); d.set("t", t);
                        d.set("superficie", surface_name(w.s));
                        emit("vigia.cruzou", d, ex->id);
                        TokCtx c; c.tem_ponto = true; c.ponto = p;
                        fire_out(*ex, w.node, 0, w.sinst, t, c);
                    }
                    continue;
                }
            }
            w.dentro = arena_.surface_at(e.pos) == w.s;
        }
        e.last_t = t;
    }
    now_ = t;
}

void Runtime::advance(int64_t t) {
    if (t < now_) t = now_;
    int64_t tick = 1000 / std::max(1, rules_.tick_hz);
    while (true) {
        int64_t tn = t;
        if (!q_.empty() && q_.top().t < tn) tn = std::max(now_, q_.top().t);
        if (now_ + tick < tn) tn = now_ + tick;
        step_world(tn);
        while (!q_.empty() && q_.top().t <= tn) {
            QItem it = q_.top();
            q_.pop();
            process(it);
        }
        if (tn >= t) break;
    }
    // coleta de execuções encerradas
    for (auto it = execs_.begin(); it != execs_.end();) {
        bool has_orphan = false;
        for (auto& a : areas_) if (a.second.exec == it->first) has_orphan = true;
        if (!it->second.alive && !has_orphan) {
            bool guard_ref = false;
            for (auto& e : ents_) for (auto& g : e.guards) if (g.exec == it->first) guard_ref = true;
            if (!guard_ref) { it = execs_.erase(it); continue; }
        }
        ++it;
    }
}

bool Runtime::cancel(uint32_t id, const std::string& motivo, int64_t t, bool manter) {
    Exec* ex = exec(id);
    if (!ex || !ex->alive) return false;
    ex->alive = false;
    std::vector<uint32_t> dead;
    for (auto& kv : areas_) {
        Area& a = kv.second;
        if (a.exec != id || !a.viva) continue;
        if (manter && a.contato_iniciado) a.orfa = true;
        else {
            a.viva = false;
            dead.push_back(kv.first);
            Json d = Json::obj(); d.set("id", a.id); d.set("t", t); d.set("motivo", motivo);
            emit("area.cancelada", d, id);
        }
    }
    for (uint32_t d : dead) areas_.erase(d);
    for (auto& e : ents_) {
        std::vector<uint32_t> gs;
        for (auto& g : e.guards) if (g.exec == id && !g.destacada) gs.push_back(g.id);
        for (uint32_t g : gs) end_guard(e, g, "cancelada", t);
        if (e.em_mov) {
            auto it = motions_.find(e.mov_inst);
            if (it != motions_.end() && it->second.first == id) {
                e.em_mov = false;
                Json d = Json::obj(); d.set("ent", e.id); d.set("pos", jv(e.pos)); d.set("interrompido", motivo); d.set("t", t);
                emit("mov.fim", d, id);
                mov_sinst_.erase(it->first);
                motions_.erase(it);
            }
        }
    }
    for (auto it = windows_.begin(); it != windows_.end();) it = it->second.exec == id ? windows_.erase(it) : std::next(it);
    for (auto it = watches_.begin(); it != watches_.end();) it = it->second.exec == id ? watches_.erase(it) : std::next(it);
    for (auto it = reps_.begin(); it != reps_.end();) it = it->second.exec == id ? reps_.erase(it) : std::next(it);
    Json d = Json::obj();
    d.set("motivo", motivo); d.set("t", t); d.set("cancelada", true); d.set("autor", ex->owner); d.set("slot", ex->slot);
    d.set("comprometidas_mantidas", manter);
    emit("exec.fim", d, id);
    return true;
}

void Runtime::cancel_owner(int entid, const std::string& motivo, int64_t t, bool manter) {
    std::vector<uint32_t> ids;
    for (auto& kv : execs_) if (kv.second.alive && kv.second.owner == entid) ids.push_back(kv.first);
    for (uint32_t id : ids) cancel(id, motivo, t, manter);
}

void Runtime::cancel_all(const std::string& motivo, int64_t t) {
    std::vector<uint32_t> ids;
    for (auto& kv : execs_) if (kv.second.alive) ids.push_back(kv.first);
    for (uint32_t id : ids) cancel(id, motivo, t, false);
    for (auto& kv : areas_) {
        Json d = Json::obj(); d.set("id", kv.first); d.set("t", t); d.set("motivo", motivo);
        emit("area.cancelada", d, kv.second.exec);
    }
    areas_.clear();
    for (auto& e : ents_) {
        std::vector<uint32_t> gs;
        for (auto& g : e.guards) gs.push_back(g.id);
        for (uint32_t g : gs) end_guard(e, g, "cancelada", t);
        e.em_mov = false;
        e.recup_ate = 0;
    }
    motions_.clear();
    mov_sinst_.clear();
}

Json Runtime::snapshot() const {
    Json s = Json::obj();
    s.set("t", now_);
    Json es = Json::arr();
    for (auto& e : ents_) {
        Json j = Json::obj();
        j.set("id", e.id); j.set("nome", e.nome); j.set("tipo", e.kind == EntKind::Alvo ? "alvo" : "jogador"); j.set("time", e.team);
        j.set("pos", jv(e.pos)); j.set("yaw", r1(e.yaw)); j.set("ativo", e.ativo); j.set("folego", r1(e.folego));
        j.set("recup_ate", e.recup_ate);
        if (e.em_mov) {
            Json m = Json::obj(); m.set("de", jv(e.m_from)); m.set("ctrl", jv(e.m_ctrl)); m.set("para", jv(e.m_to)); m.set("t0", e.m_t0); m.set("t1", e.m_t1);
            j.set("mov", m);
        }
        Json gs = Json::arr();
        for (auto& g : e.guards) {
            Json x = Json::obj();
            x.set("id", g.id); x.set("t_ativa", g.t_on); x.set("t_fim", g.t_off); x.set("abertura", g.abertura);
            x.set("eixo_yaw", r1(wrap_deg((g.segue ? e.yaw : g.yaw_fixo) + g.orient))); x.set("cargas", g.cargas); x.set("gesto", g.gesto);
            gs.push(x);
        }
        j.set("guardas", gs);
        Json rc = Json::obj();
        for (auto& kv : e.recarga_ate) rc.set(std::to_string(kv.first), kv.second);
        j.set("recarga_ate", rc);
        es.push(j);
    }
    s.set("entidades", es);
    Json as = Json::arr();
    for (auto& kv : areas_) {
        const Area& a = kv.second;
        if (!a.viva) continue;
        Json x = Json::obj();
        x.set("id", a.id); x.set("exec", a.exec); x.set("autor", a.owner); x.set("centro", jv(a.centro)); x.set("raio", a.raio);
        x.set("espessura", a.esp); x.set("altura", a.altura); x.set("t_contato", a.t_contato); x.set("t_fim", a.t_fim); x.set("impacto", a.impacto);
        x.set("fase", now_ < a.t_contato ? "preparo" : "contato");
        as.push(x);
    }
    s.set("areas", as);
    Json ws = Json::arr();
    for (auto& kv : windows_) {
        Json x = Json::obj();
        x.set("id", kv.first); x.set("exec", kv.second.exec); x.set("t_abre", kv.second.t_abre); x.set("t_fecha", kv.second.t_fecha); x.set("dono", kv.second.owner);
        ws.push(x);
    }
    s.set("janelas", ws);
    Json xs = Json::arr();
    for (auto& kv : execs_) {
        if (!kv.second.alive) continue;
        Json x = Json::obj(); x.set("id", kv.first); x.set("autor", kv.second.owner); x.set("slot", kv.second.slot); x.set("digest", kv.second.plan->digest); x.set("t0", kv.second.t0);
        xs.push(x);
    }
    s.set("execucoes", xs);
    return s;
}

Json Runtime::frame() const { return snapshot(); }

}  // namespace forja
