// forja-core: regras de sala e partida (autoridade do servidor).
// Fases: saguao -> treino (compartilhado) -> contagem -> duelo <-> ponto -> fim -> (revanche) contagem ...
// Pausa: desconexão de um jogador durante o duelo; reconexão retoma, tempo esgotado encerra por abandono.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "compile.h"
#include "json.h"
#include "recipe.h"
#include "runtime.h"

namespace forja {

struct MatchConfig {
    std::string modo = "duelo";        // duelo | equipes | livre
    int pontos_para_vencer = 3;
    int contagem_ms = 3000;
    int pausa_ponto_ms = 1500;
    int espera_reconexao_ms = 15000;
    Rules regras = Rules::duelo();
    Json to_json() const;
    static MatchConfig for_mode(const std::string& modo);
};

struct Seat {
    std::string nome;
    std::string token;
    int team = 0;
    int ent = -1;
    bool pronto = false;
    bool conectado = true;
    bool espectador = false;
    bool quer_revanche = false;
    std::map<int, std::string> equip;  // espaço -> digest
};

class Match {
public:
    explicit Match(MatchConfig cfg);
    Runtime& rt() { return rt_; }
    const MatchConfig& config() const { return cfg_; }
    void set_mode(const std::string& modo);
    const std::string& fase() const { return fase_; }

    int join(const std::string& nome, const std::string& token, int64_t t);  // devolve assento
    int find_seat(const std::string& nome) const;
    Seat* seat(int i) { return (i >= 0 && static_cast<size_t>(i) < seats_.size()) ? &seats_[static_cast<size_t>(i)] : nullptr; }
    const std::vector<Seat>& seats() const { return seats_; }
    void disconnect(int seat, int64_t t);
    bool reconnect(int seat, const std::string& token, int64_t t);
    void set_ready(int seat, bool v, int64_t t);

    // Publicação de receita no cache da sala (por digest). Recalcula digests, valida e compila.
    Json publish(const Json& pacote);
    std::shared_ptr<const Plan> plan_of(const std::string& digest) const;
    Json equip(int seat, int slot, const std::string& digest, int64_t t);

    Json start_training(int64_t t);
    Json start_duel(int64_t t);
    Json request_rematch(int seat, int64_t t);

    // Entrada do jogador
    Runtime::Act press(int seat, int slot, bool soltar, bool secundario, int64_t t_julgado, int64_t t, double aim_yaw, Vec3 aim);
    void move(int seat, Vec3 dir, double yaw, int64_t t);
    bool cancel(int seat, int64_t t);

    // Treino
    Json training(const Json& cmd, int seat, int64_t t);

    void tick(int64_t t);
    std::vector<Json> drain();
    Json room_state() const;
    Json scores() const;
    Json late_join_snapshot() const;
    const std::vector<Json>& decisive_impacts() const { return decisivos_; }

private:
    void set_phase(const std::string& f, int64_t t, Json extra = Json::obj());
    void spawn_all(int64_t t, bool swap);
    void emit(const std::string& tipo, Json d, int64_t t);
    Vec3 spawn_pos(int team, int idx, bool swap, double& yaw) const;
    Json check_loadout(const Seat& s) const;

    MatchConfig cfg_;
    Runtime rt_;
    std::vector<Seat> seats_;
    ObjectStore store_;
    std::map<std::string, std::shared_ptr<const Plan>> cache_;
    std::map<std::string, Json> analises_;
    std::string fase_ = "saguao";
    int64_t fase_ate_ = 0;
    std::map<int, int> placar_;
    int alvo_pontos_ = 3;
    std::vector<Json> pend_impactos_;
    std::vector<Json> out_;
    std::vector<Json> decisivos_;
    int partida_ = 0;
    bool lados_trocados_ = false;
    std::string fase_antes_pausa_;
    int seat_pausa_ = -1;
};

}  // namespace forja
