// forja-core: mundo e interpretador autoritativo dos planos.
//
// Tempo: milissegundos inteiros do servidor. Simulação por eventos discretos: o mundo (andar e
// deslocamentos analíticos) avança exatamente até o instante de cada evento. Ordem de eventos no
// mesmo instante: (t, fase, execução, sequência). Fases: 0 entrada, 1 estado (marcas, deslocamentos,
// ativação de anteparos), 2 contato, 3 fim/decisões. Consequência documentada: um anteparo que
// fica ativo no mesmo milissegundo de um contato já protege (empate favorece a defesa).
#pragma once
#include <climits>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "compile.h"
#include "geom.h"
#include "json.h"

namespace forja {

enum class EntKind : uint8_t { Jogador, Alvo };

struct Guard {
    uint32_t id = 0;
    uint32_t exec = 0;
    int node = -1;
    int sinst = 0;
    int64_t t_on = 0, t_off = 0;
    int abertura = 90;
    double yaw_fixo = 0;
    int orient = 0;
    bool segue = true;
    int cargas = 1;
    bool destacada = false;  // concedida por amparo: não segura o escopo
    std::string gesto;
    int recup = 0;
};

struct Entity {
    int id = 0;
    EntKind kind = EntKind::Jogador;
    int team = 0;          // alvos de treino: -1 (atingíveis por todos)
    std::string nome;
    Vec3 pos;
    double yaw = 0;
    Vec3 move_dir;         // direção desejada (unitária ou zero)
    double speed = 450;    // cm/s
    double radius = 34, height = 180;
    bool ativo = true;
    int64_t recup_ate = 0;
    double folego = 100;
    std::vector<Guard> guards;
    // deslocamento em curso
    bool em_mov = false;
    uint32_t mov_inst = 0;
    Vec3 m_from, m_ctrl, m_to;
    int64_t m_t0 = 0, m_t1 = 0;
    int64_t last_t = 0;
    std::map<int, int64_t> recarga_ate;  // por espaço
};

struct SurfaceBox {
    Box box;
    Surface s = Surface::Concreto;
    std::string nome;
};

struct Arena {
    Box limites;
    std::vector<SurfaceBox> superficies;
    double lentidao_agua = 0.7;
    Surface surface_at(const Vec3& p) const;
    Json to_json() const;
    static Arena galeria();
    Vec3 clamp(const Vec3& p, double r) const;
};

struct TokCtx {
    bool tem_ponto = false;
    Vec3 ponto;
    int ent = -1;
};

class Runtime {
public:
    Runtime(Arena a, Rules r);

    int add_entity(EntKind k, int team, const std::string& nome, Vec3 pos, double yaw);
    Entity* ent(int id);
    const Entity* ent(int id) const;
    std::vector<Entity>& entities() { return ents_; }
    const Arena& arena() const { return arena_; }
    const Rules& rules() const { return rules_; }
    void set_rules(const Rules& r) { rules_ = r; }
    bool sem_recarga = false;   // treino

    void set_move(int ent, Vec3 dir, double yaw, int64_t t);
    void teleport(int ent, Vec3 pos, double yaw, int64_t t);
    // Motor externo (Unreal): a posição vem do CharacterMovement. Fora de deslocamento da receita, a entidade
    // assume a posição dada (com detecção de travessia de superfície no trecho); durante um deslocamento,
    // o runtime manda e o adaptador aplica a posição do runtime ao ator.
    void sync_external(int ent, Vec3 pos, double yaw, int64_t t);

    static constexpr int64_t kSemRelogio = INT64_MIN;
    struct Act { bool ok = false; uint32_t exec = 0; std::string motivo; bool foi_comando = false; int64_t t_julgado = 0; };
    // t_cliente: instante da entrada na linha do tempo do próprio jogador (qualquer relógio monotônico).
    Act activate(int ent, int slot, std::shared_ptr<const Plan> plan, int64_t t, double aim_yaw, Vec3 aim_point, int64_t t_cliente = kSemRelogio);
    // Comando durante uma execução (2º toque, soltar, secundário). O instante julgado é relativo à ativação na
    // linha do tempo do jogador: t0 + (t_cliente - t_cliente_ativacao), limitado a [agora - tolerância, agora].
    // Latência constante não muda o julgamento; variação de latência é absorvida até a tolerância.
    int64_t command(int ent, int slot, bool soltar, bool secundario, int64_t t_cliente, int64_t t_agora);
    bool cancel(uint32_t exec, const std::string& motivo, int64_t t, bool manter_comprometidas);
    void cancel_owner(int ent, const std::string& motivo, int64_t t, bool manter_comprometidas);
    void cancel_all(const std::string& motivo, int64_t t);
    bool exec_alive(uint32_t exec) const;
    bool has_alive_exec(int ent, int slot) const;
    uint32_t alive_exec_of(int ent, int slot) const;

    void advance(int64_t t);
    int64_t now() const { return now_; }

    // Eventos públicos desde a última drenagem (o servidor os replica; o registro guarda todos)
    std::vector<Json> drain();
    Json snapshot() const;           // estado das entidades e efeitos ativos (para entrada tardia)
    Json frame() const;              // quadro de depuração (simulação passo a passo)

    std::function<void(const Json& impacto)> on_impacto;

    // contabilidade da sala
    int execucoes_ativas() const;
    int trabalho_reservado() const;
    int areas_ativas() const;

private:
    struct ScopeInst { int scope = 0; int parent = -1; int embed = -1; int active = 0; TokCtx ctx; bool ended = false; };
    struct Exec {
        uint32_t id = 0;
        int owner = -1;
        int slot = 0;
        std::shared_ptr<const Plan> plan;
        int64_t t0 = 0, deadline = 0;
        int64_t t_cli0 = kSemRelogio;
        Vec3 origin;
        double yaw0 = 0, aim_yaw = 0;
        Vec3 aim_point;
        bool alive = true;
        int ativacoes = 0, eventos = 0;
        std::vector<ScopeInst> scopes;
        std::map<std::pair<int, int>, Vec3> marks;
        std::map<std::pair<int, int>, int> joins;
        std::vector<std::pair<int64_t, int>> presses;  // (t_julgado, tipo: 0 press 1 release 2 secundário)
    };
    struct Area {
        uint32_t id = 0;
        uint32_t exec = 0;
        int node = -1;
        int sinst = 0;
        int owner = -1, team = 0;
        Vec3 centro, desloc_mundo;
        int raio = 0, esp = 0, altura = 0, impacto = 0, recup = 0;
        Afeta afeta = Afeta::Inimigos;
        bool segue = false;
        int64_t t_spawn = 0, t_contato = 0, t_fim = 0;
        std::vector<int> atingidos;
        bool contato_iniciado = false;
        bool orfa = false;  // execução cancelada após o contato: resolve sem disparar saídas
        bool viva = true;
    };
    struct Window { uint32_t exec; int node; int sinst; int owner; int slot; int64_t t_abre, t_fecha; bool decidida = false; bool soltar, secundario, cedo_anula; int tol; TokCtx ctx; };
    struct Watch { uint32_t exec; int node; int sinst; int owner; Surface s; int64_t t_fim; int restantes; bool dentro; bool viva = true; TokCtx ctx; };
    struct Rep { uint32_t exec; int node; int sinst; int k = 0; TokCtx ctx; bool viva = true; };
    struct QItem {
        int64_t t; int fase; uint32_t exec; uint64_t seq;
        int kind;      // 0 token, 1 timer
        int node; int sinst; int tag; uint32_t inst; TokCtx ctx;
        bool operator>(const QItem& o) const {
            if (t != o.t) return t > o.t;
            if (fase != o.fase) return fase > o.fase;
            if (exec != o.exec) return exec > o.exec;
            return seq > o.seq;
        }
    };
    enum Tag : int { T_MOV_FIM = 1, T_AREA_CONTATO, T_AREA_TICK, T_AREA_FIM, T_GUARD_ON, T_GUARD_FIM, T_JANELA_ABRE, T_JANELA_DECIDE,
                     T_VIGIA_FIM, T_ESPERA, T_REPETE, T_AMPARO, T_PRAZO };

    void emit(const std::string& tipo, Json dados, uint32_t exec = 0);
    void push_token(Exec& ex, int node, int sinst, int64_t t, const TokCtx& c);
    void push_timer(uint32_t exec, int node, int sinst, int64_t t, int fase, int tag, uint32_t inst, const TokCtx& c = TokCtx());
    void fire_out(Exec& ex, int node, int port, int sinst, int64_t t, const TokCtx& c);
    void scope_dec(Exec& ex, int sinst, int64_t t);
    void finish_exec(Exec& ex, const std::string& motivo, int64_t t);
    void process(const QItem& q);
    void on_token(Exec& ex, const QItem& q);
    void on_timer(const QItem& q);
    void step_world(int64_t t);
    void vigiar_trecho(Entity& e, const Vec3& old, int64_t t_old, int64_t t);
    void area_check(Area& a, int64_t t, bool primeiro);
    Vec3 anchor_point(Exec& ex, const PNode& n, int sinst, const TokCtx& c);
    Exec* exec(uint32_t id);
    const Exec* exec(uint32_t id) const;
    Guard* find_guard(uint32_t gid, Entity** owner);
    void end_guard(Entity& e, uint32_t gid, const std::string& motivo, int64_t t);
    void start_motion(Exec& ex, int node, int sinst, Entity& e, Vec3 to, Vec3 ctrl, int dur, int64_t t);

    Arena arena_;
    Rules rules_;
    std::vector<Entity> ents_;
    std::map<uint32_t, Exec> execs_;
    std::map<uint32_t, Area> areas_;
    std::map<uint32_t, Window> windows_;
    std::map<uint32_t, Watch> watches_;
    std::map<uint32_t, Rep> reps_;
    std::map<uint32_t, std::pair<uint32_t, int>> motions_;  // inst -> (exec, node)
    std::map<uint32_t, int> mov_sinst_;
    std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> q_;
    std::vector<Json> out_;
    int64_t now_ = 0;
    uint64_t seq_ = 0;
    uint32_t next_exec_ = 1;
    uint32_t next_inst_ = 1;
};

}  // namespace forja
