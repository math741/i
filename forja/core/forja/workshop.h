// forja-core: acervo do jogador (criações, revisões imutáveis, histórico) e a oficina.
//
// A oficina expõe UM ponto de entrada, Workshop::exec(comando JSON). A interface (menus, gizmos na cena,
// linha do tempo) e os testes automatizados mandam exatamente os mesmos comandos. Nenhuma criação de
// exemplo existe no código: todas nascem destes comandos.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "compile.h"
#include "json.h"
#include "recipe.h"

namespace forja {

struct RevRecord {
    int rev = 0;
    std::string digest, pai, nome, nota;
    Json rotulos, deps, aparencia;
    int64_t quando = 0;
    Json to_json() const;
    static RevRecord from_json(const Json& j);
};

struct Creation {
    std::string id, nome, autor, origem;
    Json rascunho;  // {comportamento, rotulos, deps, aparencia}
    std::vector<RevRecord> revs;
    std::vector<Json> undo, redo;
    Json atualizacoes = Json::arr();
    Json to_json() const;
    static Creation from_json(const Json& j);
};

class Library {
public:
    bool open(const std::string& root);  // "" = só memória
    const std::string& root() const { return root_; }
    ObjectStore store;
    std::map<std::string, Creation> creations;

    Creation* get(const std::string& id);
    const Creation* find_by_digest(const std::string& digest, const RevRecord** rec = nullptr) const;
    bool persist(const Creation& c);
    std::string new_id(const std::string& nome);
    Resolver resolver() const;
    Naming naming(const Creation* c) const;
    // pacote com o fecho transitivo (para publicar numa sala ou exportar)
    Json bundle(const std::string& digest) const;

private:
    std::string root_;
    uint64_t counter_ = 0;
};

class Workshop {
public:
    Workshop(Library& lib, Rules rules, std::string autor);
    Json exec(const Json& cmd);
    void set_rules(const Rules& r) { rules_ = r; }
    const Rules& rules() const { return rules_; }
    int64_t (*clock)() = nullptr;  // relógio de parede para metadados (opcional)

private:
    Json fail(const std::string& msg) const;
    Json state_of(Creation& c);
    Json analyze(Creation& c, const Json* behavior = nullptr);
    bool mutate(Creation& c, const std::function<bool(Json& draft, std::string& err)>& fn, std::string& err);
    std::string alloc_node_id(const Json& behavior) const;
    Json diff(const Json& a_beh, const Json& b_beh, const Json& a_deps, const Json& b_deps) const;
    Json simulate(Creation& c, const Json& cenario);
    Json apply_edit(Creation& c, const Json& e, bool record);
    int64_t now_ms() const;

    Library& lib_;
    Rules rules_;
    std::string autor_;
};

// Simulação isolada (treino/oficina): roda um plano num mundo próprio e devolve eventos e quadros.
Json simulate_plan(std::shared_ptr<const Plan> plan, const Rules& rules, const Json& cenario);

}  // namespace forja
