#include "match.h"

#include <algorithm>
#include <set>

namespace forja {

Json MatchConfig::to_json() const {
    Json j = Json::obj();
    j.set("modo", modo); j.set("pontos_para_vencer", pontos_para_vencer); j.set("contagem_ms", contagem_ms);
    j.set("pausa_ponto_ms", pausa_ponto_ms); j.set("espera_reconexao_ms", espera_reconexao_ms); j.set("regras", regras.to_json());
    return j;
}

MatchConfig MatchConfig::for_mode(const std::string& modo) {
    MatchConfig c;
    c.modo = modo;
    if (modo == "equipes") c.regras = Rules::equipes_2v2();
    else if (modo == "livre") { c.regras = Rules::arena_livre(); c.pontos_para_vencer = 5; }
    else { c.modo = "duelo"; c.regras = Rules::duelo(); }
    return c;
}

static int max_players(const std::string& modo) { return modo == "equipes" ? 4 : (modo == "livre" ? 8 : 2); }

Match::Match(MatchConfig cfg) : cfg_(cfg), rt_(Arena::galeria(), cfg.regras) {
    rt_.on_impacto = [this](const Json& i) { pend_impactos_.push_back(i); };
    alvo_pontos_ = cfg_.pontos_para_vencer;
}

void Match::set_mode(const std::string& modo) {
    MatchConfig c = MatchConfig::for_mode(modo);
    cfg_ = c;
    rt_.set_rules(c.regras);
    alvo_pontos_ = c.pontos_para_vencer;
    for (size_t i = 0; i < seats_.size(); ++i) {
        Seat& s = seats_[i];
        if (s.espectador) continue;
        s.team = cfg_.modo == "livre" ? static_cast<int>(i) : static_cast<int>(i % 2);
        if (Entity* e = rt_.ent(s.ent)) e->team = s.team;
    }
    // recompila o cache com as novas regras (critérios competitivos mudam com o modo)
    for (auto& kv : cache_) {
        CompileOut co = compile_recipe(*store_.get(kv.first), [this](const std::string& d) { return store_.get(d); }, cfg_.regras);
        if (co.ok) kv.second = co.plan;
        analises_[kv.first] = co.analise;
    }
    Json d = Json::obj(); d.set("config", cfg_.to_json());
    emit("sala.modo", d, rt_.now());
}

void Match::emit(const std::string& tipo, Json d, int64_t t) {
    Json e = Json::obj();
    e.set("tipo", tipo);
    e.set("t", t);
    for (auto& kv : d.members()) if (kv.first != "tipo") e.set(kv.first, kv.second);
    out_.push_back(e);
}

std::vector<Json> Match::drain() {
    std::vector<Json> v = rt_.drain();
    for (auto& e : out_) v.push_back(e);
    out_.clear();
    std::stable_sort(v.begin(), v.end(), [](const Json& a, const Json& b) { return a.get("t").as_int() < b.get("t").as_int(); });
    return v;
}

int Match::find_seat(const std::string& nome) const {
    for (size_t i = 0; i < seats_.size(); ++i) if (seats_[i].nome == nome) return static_cast<int>(i);
    return -1;
}

Vec3 Match::spawn_pos(int team, int idx, bool swap, double& yaw) const {
    if (fase_ == "treino" || fase_ == "saguao") {
        yaw = 0;
        double y = (team % 2 == 0) ? -250 : 250;
        return Vec3(-600 - 300.0 * idx - (team >= 2 ? 300 : 0), y, 0);
    }
    bool west = (team % 2 == 0) != swap;
    yaw = west ? 0 : 180;
    double y = (idx % 2 == 0 ? -200 : 200) * (west ? 1 : -1);
    if (cfg_.modo == "livre") {
        double ang = 360.0 * team / std::max<size_t>(1, seats_.size());
        Vec3 p = forward_of(ang) * 600.0;
        p.y = std::max(-380.0, std::min(380.0, p.y));
        yaw = wrap_deg(ang + 180);
        return p;
    }
    return Vec3(west ? -600 : 600, y, 0);
}

void Match::spawn_all(int64_t t, bool swap) {
    std::map<int, int> count;
    for (auto& s : seats_) {
        if (s.espectador || s.ent < 0) continue;
        double yaw = 0;
        Vec3 p = spawn_pos(s.team, count[s.team]++, swap, yaw);
        rt_.teleport(s.ent, p, yaw, t);
        if (Entity* e = rt_.ent(s.ent)) { e->folego = 100; e->recarga_ate.clear(); e->recup_ate = 0; e->ativo = s.conectado; }
    }
}

int Match::join(const std::string& nome, const std::string& token, int64_t t) {
    rt_.advance(t);
    int existing = find_seat(nome);
    if (existing >= 0) {
        if (reconnect(existing, token, t)) return existing;
        return -1;
    }
    Seat s;
    s.nome = nome;
    s.token = token;
    int players = 0;
    for (auto& x : seats_) if (!x.espectador) ++players;
    bool can_play = (fase_ == "saguao" || fase_ == "treino") && players < max_players(cfg_.modo);
    s.espectador = !can_play;
    s.team = s.espectador ? -2 : (cfg_.modo == "livre" ? players : players % 2);
    if (!s.espectador) {
        double yaw = 0;
        Vec3 p = spawn_pos(s.team, players / 2, false, yaw);
        s.ent = rt_.add_entity(EntKind::Jogador, s.team, nome, p, yaw);
    }
    seats_.push_back(s);
    Json d = Json::obj();
    d.set("assento", static_cast<int64_t>(seats_.size() - 1)); d.set("nome", nome); d.set("time", s.team); d.set("ent", s.ent); d.set("espectador", s.espectador);
    emit("sala.entrada", d, t);
    return static_cast<int>(seats_.size() - 1);
}

void Match::disconnect(int si, int64_t t) {
    Seat* s = seat(si);
    if (!s || !s->conectado) return;
    rt_.advance(t);
    s->conectado = false;
    if (s->ent >= 0) {
        // Áreas já em contato resolvem (o adversário já reagiu a elas); o resto é cancelado e liberado.
        rt_.cancel_owner(s->ent, "desconexão do autor", t, true);
        rt_.set_move(s->ent, Vec3(), rt_.ent(s->ent) ? rt_.ent(s->ent)->yaw : 0, t);
        if (Entity* e = rt_.ent(s->ent)) e->ativo = false;
    }
    Json d = Json::obj(); d.set("assento", si); d.set("nome", s->nome);
    emit("sala.desconexao", d, t);
    if (!s->espectador && (fase_ == "duelo" || fase_ == "contagem" || fase_ == "ponto")) {
        fase_antes_pausa_ = fase_;
        seat_pausa_ = si;
        Json x = Json::obj(); x.set("aguardando", s->nome);
        set_phase("pausa", t, x);
        fase_ate_ = t + cfg_.espera_reconexao_ms;
    }
}

bool Match::reconnect(int si, const std::string& token, int64_t t) {
    Seat* s = seat(si);
    if (!s) return false;
    if (s->conectado) return token == s->token;
    if (!s->token.empty() && token != s->token) return false;
    s->conectado = true;
    if (Entity* e = rt_.ent(s->ent)) e->ativo = true;
    Json d = Json::obj(); d.set("assento", si); d.set("nome", s->nome);
    emit("sala.reconexao", d, t);
    if (fase_ == "pausa") {
        bool all = true;
        for (auto& x : seats_) if (!x.espectador && !x.conectado) all = false;
        if (all) {
            spawn_all(t, lados_trocados_);
            set_phase("contagem", t);
            fase_ate_ = t + cfg_.contagem_ms;
        }
    }
    return true;
}

void Match::set_ready(int si, bool v, int64_t t) {
    Seat* s = seat(si);
    if (!s) return;
    s->pronto = v;
    Json d = Json::obj(); d.set("assento", si); d.set("pronto", v);
    emit("sala.pronto", d, t);
}

void Match::set_phase(const std::string& f, int64_t t, Json extra) {
    fase_ = f;
    Json d = extra;
    d.set("fase", f);
    d.set("partida", partida_);
    emit("sala.fase", d, t);
}

Json Match::publish(const Json& b) {
    Json r = Json::obj();
    if (b.get("formato").as_str() != kFormatoPacote) { r.set("ok", false); r.set("erro", "pacote em formato desconhecido"); return r; }
    std::string root = b.get("raiz").as_str();
    for (auto& kv : b.get("objetos").members()) {
        if (digest_of(canonical_behavior(kv.second)) != kv.first) {
            r.set("ok", false); r.set("erro", "digest não confere para " + kv.first.substr(0, 19) + "… (conteúdo adulterado ou versão incompatível)");
            return r;
        }
    }
    for (auto& kv : b.get("objetos").members()) store_.put(kv.second);
    const Json* rootj = store_.get(root);
    if (!rootj) { r.set("ok", false); r.set("erro", "raiz ausente no pacote"); return r; }
    Naming nm;
    nm.rotulos = b.get("nomes").get(root).get("rotulos");
    Json nomes = b.get("nomes");
    nm.nome_sub = [nomes](const std::string& d) { const Json& x = nomes.get(d); return x.is_obj() ? x.get("nome").as_str() + " r" + std::to_string(x.get("revisao").as_int()) : std::string(); };
    nm.rotulos_sub = [nomes](const std::string& d) { return nomes.get(d).get("rotulos"); };
    CompileOut co = compile_recipe(*rootj, [this](const std::string& d) { return store_.get(d); }, cfg_.regras, &nm);
    analises_[root] = co.analise;
    r.set("ok", co.ok);
    r.set("digest", root);
    r.set("competitiva_ok", co.competitiva_ok);
    r.set("analise", co.analise);
    if (co.ok) cache_[root] = co.plan;
    else r.set("erro", "receita rejeitada pelo servidor");
    return r;
}

std::shared_ptr<const Plan> Match::plan_of(const std::string& d) const {
    auto it = cache_.find(d);
    return it == cache_.end() ? nullptr : it->second;
}

Json Match::equip(int si, int slot, const std::string& digest, int64_t t) {
    Json r = Json::obj();
    Seat* s = seat(si);
    if (!s) { r.set("ok", false); r.set("erro", "assento inválido"); return r; }
    if (fase_ != "saguao" && fase_ != "treino" && fase_ != "fim") { r.set("ok", false); r.set("erro", "só é possível equipar no saguão, no treino ou entre partidas"); return r; }
    if (slot < 0 || slot > 3) { r.set("ok", false); r.set("erro", "espaços válidos: 0..3"); return r; }
    if (digest.empty()) { s->equip.erase(slot); r.set("ok", true); return r; }
    auto p = plan_of(digest);
    if (!p) { r.set("ok", false); r.set("erro", "receita não publicada nesta sala"); return r; }
    s->equip[slot] = digest;
    r.set("ok", true);
    r.set("digest", digest); r.set("custo", p->custo); r.set("recarga_ms", p->recarga_ms); r.set("competitiva_ok", p->competitiva_ok);
    Json d = r;
    d.set("assento", si); d.set("slot", slot);
    emit("equipado", d, t);
    return r;
}

Json Match::check_loadout(const Seat& s) const {
    Json r = Json::obj();
    Json probs = Json::arr();
    double total = 0;
    if (s.equip.empty()) probs.push(s.nome + " não equipou nenhuma criação");
    for (auto& kv : s.equip) {
        auto p = plan_of(kv.second);
        if (!p) { probs.push("espaço " + std::to_string(kv.first) + ": receita ausente do cache"); continue; }
        total += p->custo;
        if (cfg_.regras.competitiva && !p->competitiva_ok) {
            std::string why;
            auto it = analises_.find(kv.second);
            if (it != analises_.end())
                for (auto& i : it->second.get("problemas").items())
                    if (i.get("nivel").as_str() == "competitivo") why += (why.empty() ? "" : "; ") + i.get("msg").as_str();
            probs.push(s.nome + ", espaço " + std::to_string(kv.first) + ": não atende aos critérios competitivos (" + why + ")");
        }
    }
    if (cfg_.regras.competitiva && total > cfg_.regras.custo_equipamento_max)
        probs.push(s.nome + ": custo total do equipamento " + std::to_string(static_cast<int>(total)) + " excede " + std::to_string(static_cast<int>(cfg_.regras.custo_equipamento_max)));
    r.set("ok", probs.size() == 0);
    r.set("problemas", probs);
    r.set("custo_total", total);
    return r;
}

Json Match::start_training(int64_t t) {
    rt_.advance(t);
    Json r = Json::obj();
    if (fase_ != "saguao" && fase_ != "fim" && fase_ != "treino") { r.set("ok", false); r.set("erro", "a partida está em andamento"); return r; }
    rt_.cancel_all("início do treino", t);
    set_phase("treino", t);
    spawn_all(t, false);
    r.set("ok", true);
    return r;
}

Json Match::start_duel(int64_t t) {
    rt_.advance(t);
    Json r = Json::obj();
    if (fase_ != "saguao" && fase_ != "treino" && fase_ != "fim") { r.set("ok", false); r.set("erro", "a partida já está em andamento"); return r; }
    std::set<int> teams;
    Json probs = Json::arr();
    int players = 0;
    for (auto& s : seats_) {
        if (s.espectador) continue;
        ++players;
        if (!s.conectado) probs.push(s.nome + " está desconectado");
        if (!s.pronto) probs.push(s.nome + " não está pronto");
        teams.insert(s.team);
        Json lc = check_loadout(s);
        for (auto& p : lc.get("problemas").items()) probs.push(p);
    }
    if (teams.size() < 2) probs.push("são necessários jogadores em pelo menos dois times");
    if (cfg_.modo == "equipes" && players != 4) probs.push("o modo equipes pede 4 jogadores (2v2)");
    if (probs.size()) { r.set("ok", false); r.set("problemas", probs); return r; }
    partida_++;
    placar_.clear();
    for (int tm : teams) placar_[tm] = 0;
    alvo_pontos_ = cfg_.pontos_para_vencer;
    decisivos_.clear();
    rt_.cancel_all("início do duelo", t);
    for (auto& e : rt_.entities()) if (e.kind == EntKind::Alvo) e.ativo = false;
    rt_.sem_recarga = false;
    set_phase("contagem", t);
    spawn_all(t, lados_trocados_);
    fase_ate_ = t + cfg_.contagem_ms;
    Json eq = Json::arr();
    for (size_t i = 0; i < seats_.size(); ++i) {
        const Seat& s = seats_[i];
        if (s.espectador) continue;
        Json x = Json::obj();
        x.set("assento", static_cast<int64_t>(i)); x.set("nome", s.nome); x.set("time", s.team);
        Json sl = Json::obj();
        for (auto& kv : s.equip) sl.set(std::to_string(kv.first), kv.second);
        x.set("equipamento", sl);
        eq.push(x);
    }
    Json d = Json::obj(); d.set("partida", partida_); d.set("equipamentos", eq); d.set("pontos_para_vencer", alvo_pontos_); d.set("lados_trocados", lados_trocados_);
    emit("duelo.inicio", d, t);
    r.set("ok", true);
    r.set("partida", partida_);
    return r;
}

Json Match::request_rematch(int si, int64_t t) {
    Json r = Json::obj();
    Seat* s = seat(si);
    if (!s || fase_ != "fim") { r.set("ok", false); r.set("erro", "a revanche só pode ser pedida no fim da partida"); return r; }
    s->quer_revanche = true;
    Json d = Json::obj(); d.set("assento", si); d.set("nome", s->nome);
    emit("revanche.pedido", d, t);
    bool all = true;
    for (auto& x : seats_) if (!x.espectador && x.conectado && !x.quer_revanche) all = false;
    r.set("ok", true);
    if (all) {
        for (auto& x : seats_) { x.quer_revanche = false; x.pronto = true; }
        lados_trocados_ = !lados_trocados_;
        Json st = start_duel(t);
        r.set("iniciada", st.get("ok").as_bool());
        if (!st.get("ok").as_bool()) r.set("problemas", st.get("problemas"));
    }
    return r;
}

Runtime::Act Match::press(int si, int slot, bool soltar, bool sec, int64_t tj, int64_t t, double aim_yaw, Vec3 aim) {
    Runtime::Act a;
    Seat* s = seat(si);
    if (!s || s->ent < 0 || s->espectador) { a.motivo = "espectador"; return a; }
    if (fase_ != "treino" && fase_ != "duelo") { a.motivo = "fora de jogo (fase " + fase_ + ")"; return a; }
    rt_.advance(t);
    if (soltar || sec || rt_.has_alive_exec(s->ent, slot)) {
        rt_.command(s->ent, slot, soltar, sec, tj, t);
        a.ok = true;
        a.foi_comando = true;
        a.exec = rt_.alive_exec_of(s->ent, slot);
        return a;
    }
    auto it = s->equip.find(slot);
    if (it == s->equip.end()) { a.motivo = "nenhuma criação equipada no espaço " + std::to_string(slot + 1); return a; }
    return rt_.activate(s->ent, slot, plan_of(it->second), t, aim_yaw, aim);
}

void Match::move(int si, Vec3 dir, double yaw, int64_t t) {
    Seat* s = seat(si);
    if (!s || s->ent < 0) return;
    bool can_walk = fase_ == "treino" || fase_ == "duelo" || fase_ == "saguao";
    rt_.set_move(s->ent, can_walk ? dir : Vec3(), yaw, t);
}

bool Match::cancel(int si, int64_t t) {
    Seat* s = seat(si);
    if (!s || s->ent < 0) return false;
    rt_.advance(t);
    bool any = false;
    for (int slot = 0; slot < 4; ++slot) if (rt_.has_alive_exec(s->ent, slot)) any = true;
    rt_.cancel_owner(s->ent, "cancelada pelo jogador", t, true);
    return any;
}

Json Match::training(const Json& cmd, int si, int64_t t) {
    Json r = Json::obj();
    rt_.advance(t);
    if (fase_ != "treino") { r.set("ok", false); r.set("erro", "comandos de treino só valem no treino"); return r; }
    std::string k = cmd.get("cmd").as_str();
    auto V = [](const Json& a) { return Vec3(a.at(0).as_num(), a.at(1).as_num(), a.at(2).as_num()); };
    if (k == "treino.alvo") {
        int id = rt_.add_entity(EntKind::Alvo, -1, cmd.get("nome").str_or("alvo"), V(cmd.get("pos")), cmd.get("yaw").as_num(0));
        r.set("ok", true); r.set("ent", id);
    } else if (k == "treino.mover_alvo") {
        Entity* e = rt_.ent(static_cast<int>(cmd.get("ent").as_int()));
        if (!e || e->kind != EntKind::Alvo) { r.set("ok", false); r.set("erro", "alvo inexistente"); return r; }
        e->pos = V(cmd.get("pos"));
        r.set("ok", true);
    } else if (k == "treino.limpar") {
        for (auto& e : rt_.entities()) if (e.kind == EntKind::Alvo) e.ativo = false;
        r.set("ok", true);
    } else if (k == "treino.posicionar") {
        Seat* s = seat(si);
        if (!s || s->ent < 0) { r.set("ok", false); return r; }
        rt_.teleport(s->ent, V(cmd.get("pos")), cmd.get("yaw").as_num(0), t);
        r.set("ok", true);
    } else if (k == "treino.reiniciar") {
        rt_.cancel_all("cenário reiniciado", t);
        spawn_all(t, false);
        r.set("ok", true);
    } else if (k == "treino.sem_recarga") {
        rt_.sem_recarga = cmd.get("valor").as_bool(true);
        r.set("ok", true);
    } else {
        r.set("ok", false); r.set("erro", "comando de treino desconhecido");
        return r;
    }
    Json d = cmd;
    d.set("assento", si);
    d.erase("cmd");
    d.set("comando", k);
    if (r.has("ent")) d.set("ent", r.get("ent"));
    emit("treino.config", d, t);
    return r;
}

void Match::tick(int64_t t) {
    rt_.advance(t);
    if (!pend_impactos_.empty()) {
        std::vector<Json> imps;
        imps.swap(pend_impactos_);
        bool scored = false;
        for (auto& imp : imps) {
            const Entity* a = rt_.ent(static_cast<int>(imp.get("autor").as_int()));
            const Entity* b = rt_.ent(static_cast<int>(imp.get("alvo").as_int()));
            if (fase_ == "treino" || fase_ == "saguao") { emit("treino.acerto", imp, imp.get("t").as_int()); continue; }
            if (fase_ != "duelo" || !a || !b) continue;
            if (b->kind != EntKind::Jogador || a->team == b->team) continue;
            placar_[a->team] += static_cast<int>(imp.get("valor").as_int(1));
            Json d = Json::obj();
            d.set("time", a->team); d.set("autor_nome", a->nome); d.set("alvo_nome", b->nome); d.set("impacto", imp);
            Json pl = Json::obj(); for (auto& kv : placar_) pl.set(std::to_string(kv.first), kv.second); d.set("placar", pl);
            emit("ponto", d, imp.get("t").as_int());
            decisivos_.push_back(d);
            scored = true;
        }
        if (scored) {
            rt_.cancel_all("ponto marcado", t);
            std::vector<int> winners;
            for (auto& kv : placar_) if (kv.second >= alvo_pontos_) winners.push_back(kv.first);
            if (winners.size() > 1) {
                alvo_pontos_++;
                Json d = Json::obj(); d.set("alvo", alvo_pontos_); d.set("motivo", "empate na pontuação final: morte súbita");
                emit("placar.alvo", d, t);
                winners.clear();
            }
            if (winners.size() == 1) {
                Json d = Json::obj(); d.set("vencedor", winners[0]); d.set("motivo", "placar");
                Json pl = Json::obj(); for (auto& kv : placar_) pl.set(std::to_string(kv.first), kv.second); d.set("placar", pl);
                set_phase("fim", t, d);
                for (auto& s : seats_) if (s.ent >= 0) rt_.set_move(s.ent, Vec3(), rt_.ent(s.ent)->yaw, t);
            } else {
                set_phase("ponto", t);
                fase_ate_ = t + cfg_.pausa_ponto_ms;
                for (auto& s : seats_) if (s.ent >= 0) rt_.set_move(s.ent, Vec3(), rt_.ent(s.ent)->yaw, t);
            }
        }
    }
    if (fase_ == "contagem" && t >= fase_ate_) set_phase("duelo", t);
    else if (fase_ == "ponto" && t >= fase_ate_) {
        spawn_all(t, lados_trocados_);
        set_phase("contagem", t);
        fase_ate_ = t + 1000;
    } else if (fase_ == "pausa" && t >= fase_ate_) {
        const Seat* gone = seat(seat_pausa_);
        int win = -1;
        for (auto& s : seats_) if (!s.espectador && s.conectado && gone && s.team != gone->team) win = s.team;
        Json d = Json::obj(); d.set("vencedor", win); d.set("motivo", "abandono");
        Json pl = Json::obj(); for (auto& kv : placar_) pl.set(std::to_string(kv.first), kv.second); d.set("placar", pl);
        set_phase("fim", t, d);
    }
}

Json Match::scores() const {
    Json j = Json::obj();
    Json pl = Json::obj(); for (auto& kv : placar_) pl.set(std::to_string(kv.first), kv.second);
    j.set("placar", pl);
    j.set("alvo", alvo_pontos_);
    return j;
}

Json Match::room_state() const {
    Json j = Json::obj();
    j.set("fase", fase_); j.set("fase_ate", fase_ate_); j.set("partida", partida_); j.set("config", cfg_.to_json());
    j.set("placar", scores()); j.set("lados_trocados", lados_trocados_); j.set("sem_recarga", rt_.sem_recarga);
    Json as = Json::arr();
    for (size_t i = 0; i < seats_.size(); ++i) {
        const Seat& s = seats_[i];
        Json x = Json::obj();
        x.set("assento", static_cast<int64_t>(i)); x.set("nome", s.nome); x.set("time", s.team); x.set("ent", s.ent);
        x.set("pronto", s.pronto); x.set("conectado", s.conectado); x.set("espectador", s.espectador); x.set("quer_revanche", s.quer_revanche);
        Json eq = Json::obj();
        for (auto& kv : s.equip) {
            Json e = Json::obj();
            e.set("digest", kv.second);
            if (auto p = plan_of(kv.second)) { e.set("custo", p->custo); e.set("recarga_ms", p->recarga_ms); e.set("competitiva_ok", p->competitiva_ok); e.set("folego", p->folego); }
            eq.set(std::to_string(kv.first), e);
        }
        x.set("equipamento", eq);
        Json lc = check_loadout(s);
        x.set("equipamento_ok", lc.get("ok"));
        x.set("equipamento_problemas", lc.get("problemas"));
        as.push(x);
    }
    j.set("assentos", as);
    return j;
}

Json Match::late_join_snapshot() const {
    Json j = Json::obj();
    j.set("sala", room_state());
    j.set("mundo", rt_.snapshot());
    j.set("arena", rt_.arena().to_json());
    return j;
}

}  // namespace forja
