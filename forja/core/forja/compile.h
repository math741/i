// forja-core: validação, compilação para plano executável e análise estática (tempo, custos, limites).
//
// Formato executado (Plan): vetor plano de nós com parâmetros resolvidos, subcriações já incorporadas
// em escopos aninhados, arestas resolvidas para índices e limites estáticos conhecidos. O servidor
// interpreta o plano; o cliente usa o mesmo plano só para previsão cosmética e para a linha do tempo.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "geom.h"
#include "json.h"
#include "ops.h"

namespace forja {

enum class Anchor : uint8_t { Autor, Ativacao, Evento, Marca };
enum class Espaco : uint8_t { Autor, Ativacao, Mira };
enum class Afeta : uint8_t { Inimigos, Aliados, Todos };
enum class Surface : uint8_t { Concreto = 0, Agua = 1, Metal = 2 };

const char* surface_name(Surface s);

// Regras da sala. Valores padrão = modo competitivo 1v1.
struct Rules {
    int tick_hz = 60;
    int tolerancia_ms = 120;     // atraso de decisão das janelas (compensação de latência)
    int max_alvos = 8;           // limite estático de entidades atingíveis por área
    // custo computacional por execução
    int exec_pico_trabalho = 64; // unidades de trabalho por tick
    int exec_pico_areas = 4;
    int exec_max_ativacoes = 128;
    int exec_max_eventos = 512;
    int exec_max_duracao_ms = 8000;
    int exec_max_memoria = 16384;  // bytes
    // custo computacional por sala
    int sala_trabalho = 512;
    int sala_areas = 24;
    int sala_execucoes = 32;
    // custo competitivo
    bool competitiva = true;
    int max_inimigos = 1;
    int max_aliados = 0;
    int preparo_minimo_ms = 150;
    int alcance_max_cm = 1200;
    double custo_max = 40.0;
    double custo_equipamento_max = 100.0;
    int abertura_max_graus = 180;
    int cargas_max = 2;
    int duracao_max_ms = 6000;
    int impacto_max = 3;

    Json to_json() const;
    static Rules from_json(const Json& j);
    static Rules duelo();
    static Rules equipes_2v2();
    static Rules arena_livre();
};

struct PNode {
    OpKind kind = OpKind::Ativar;
    std::string id;       // caminho com escopo, ex.: "n3/n1"
    std::string top;      // nó da receita raiz que contém este (para destacar no editor)
    std::string rotulo;
    int scope = 0;
    // parâmetros resolvidos (subconjunto usado conforme o tipo)
    Vec3 vec, desloc;
    Espaco espaco = Espaco::Autor;
    Anchor ancora = Anchor::Autor;
    Afeta afeta = Afeta::Inimigos;
    Surface sup = Surface::Agua;
    int curva = 0, dur = 0, recup = 0, alcance = 0;
    int raio = 0, esp = 0, altura = 0, prep = 0, contato = 0, impacto = 0;
    bool segue = false;
    int disparos = 1, abertura = 90, orient = 0, cargas = 1, folego = 0;
    bool inclui_autor = false;
    std::string gesto;
    int abre = 0, dura = 0, tol_cedo = 0;
    bool soltar = false, secundario = false, cedo_anula = false;
    int ms = 0, vezes = 1, intervalo = 0, n = 2;
    int child_scope = -1, child_entry = -1;
    std::vector<std::vector<int>> out;  // por porta de saída: índices dos nós alvo
    int data_src = -1;                  // nó que fornece o ponto (marca)
    // análise estática
    bool alcancavel = false;
    int64_t smin = 0, smax = 0;
    int mult = 0;
    bool certo = false, manual = false;
};

struct PScope {
    int parent = -1;
    int embed = -1;   // índice do nó sub.incorporar no escopo pai
    int entry = -1;
    std::string digest;
    std::string nome;
    int64_t end_min = 0, end_max = 0;
};

struct Plan {
    std::string digest;
    std::vector<PNode> nodes;
    std::vector<PScope> scopes;
    int entry = 0;
    // limites e custos estáticos
    int64_t max_dur_ms = 0;
    int max_ativacoes = 0;
    int max_eventos = 0;
    int pico_trabalho = 0;
    int pico_areas = 0;
    int pico_memoria = 0;
    int armazenamento = 0;
    int trafego_bytes = 0;
    double custo = 0;     // competitivo, pontos
    int recarga_ms = 0;
    int folego = 0;
    int alcance_cm = 0;
    int preparo_min_ms = -1;  // menor anúncio antes de um impacto (-1 = sem impacto)
    bool competitiva_ok = true;
};

struct Issue {
    std::string nivel;   // erro | aviso | competitivo
    std::string codigo;
    std::string no;      // nó da receita raiz responsável
    std::string caminho; // caminho completo no plano
    std::string msg;
    Json to_json() const;
};

using Resolver = std::function<const Json*(const std::string& digest)>;

struct Naming {
    Json rotulos;  // id do nó -> nome mostrado (receita raiz)
    std::function<std::string(const std::string& digest)> nome_sub;
    std::function<Json(const std::string& digest)> rotulos_sub;
};

struct CompileOut {
    bool ok = false;           // executável (sem erros; violações competitivas não bloqueiam o treino)
    bool competitiva_ok = false;
    std::shared_ptr<const Plan> plan;
    std::vector<Issue> issues;
    Json analise;              // linha do tempo, regras, custos, sugestões
};

CompileOut compile_recipe(const Json& behavior, const Resolver& res, const Rules& rules, const Naming* naming = nullptr);

// Trabalho por tick de uma área (unidades): 4 + células de 1 m² cobertas pelo círculo envolvente.
int area_work_units(int raio_cm);

}  // namespace forja
