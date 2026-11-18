// forja-core: registro de operações tipadas (as "peças" da linguagem do jogador).
// Cada operação tem nome estável, versão, fundamento (classe), portas de fluxo, portas de dados e
// parâmetros com unidade e faixa. Acrescentar uma operação = uma entrada nesta tabela + um caso no
// interpretador + um caso na análise. Ver docs/LINGUAGEM.md, seção "Acrescentar uma operação".
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "json.h"

namespace forja {

enum class OpKind : uint8_t {
    Ativar, Deslocar, Retornar, Marca, Anel, Sondar, Vigiar, Guarda, Amparo,
    Janela, Esperar, Repetir, Reunir, Encerrar, Incorporar, Planejada
};

enum class PType : uint8_t { Int, Enum, Vec, Bool, Ref, Obj };

struct ParamDef {
    std::string nome;
    std::string rotulo;
    std::string unidade;   // "cm", "ms", "graus", "" ...
    PType tipo = PType::Int;
    int64_t min = 0, max = 0, def = 0;
    std::vector<std::string> opcoes;  // Enum
    std::string def_s;                // Enum/Ref
    int64_t def_v[3] = {0, 0, 0};     // Vec
};

struct OutPort {
    std::string nome;
    std::string rotulo;   // linguagem de jogo: "ao chegar", "se tocar um alvo"...
    bool condicional = false;  // pode não acontecer
    bool manual = false;       // depende de comando do jogador durante a execução
};

struct DataPort {
    std::string nome;
    std::string tipo;  // "ponto"
    std::string rotulo;
};

struct OpDef {
    std::string nome;
    int versao = 1;
    OpKind kind = OpKind::Planejada;
    std::string fundamento;  // Vetor, Limiar, Lastro, Cadência, Entrada, Subcriação
    std::string rotulo;
    std::string descricao;
    bool planejada = false;   // operação nomeada mas ainda não implementada
    bool obsoleta = false;    // versão antiga: executa via adaptador, editor oferece migração
    bool tem_entrada = true;
    std::vector<OutPort> saidas;
    std::vector<DataPort> din, dout;
    std::vector<ParamDef> params;

    int out_index(const std::string& port) const;
    int din_index(const std::string& port) const;
    int dout_index(const std::string& port) const;
    const ParamDef* param(const std::string& n) const;
};

const OpDef* find_op(const std::string& nome, int versao);
const OpDef* latest_op(const std::string& nome);
std::vector<int> op_versions(const std::string& nome);
const std::vector<OpDef>& all_ops();

// Preenche padrões ausentes; devolve erros de tipo/faixa/desconhecidos. Não altera valores válidos.
bool normalize_params(const OpDef& op, Json& params, std::vector<std::string>& errs);

// Adapta (em memória) um nó de versão antiga para a versão atual, preservando a semântica.
// Usado pelo compilador (a receita salva não muda) e pela migração explícita do editor.
bool adapt_to_latest(Json& node, std::string& err);

// Texto em linguagem de jogo
std::string describe_node(const OpDef& op, const Json& params);
std::string fmt_m(int64_t cm);       // "3,00 m"
std::string fmt_ms(int64_t ms);      // "300 ms"

// Catálogo para o editor (paleta), inclui planejadas
Json ops_catalog();

}  // namespace forja
