// forja-core: formato salvo da receita, forma canônica, digest e armazenamento por conteúdo.
//
// Formato salvo (comportamento, entra no digest):
//   {"formato":"forja.receita/1",
//    "nos":[{"id":"n0","op":"entrada.ativar","v":1,"p":{}}, ...],
//    "fluxo":[{"de":"n0","porta":"ao_ativar","para":"n1"}, ...],
//    "dados":[{"de":"n1","porta":"ponto","para":"n4","entrada":"destino"}, ...],
//    "expor":[{"nome":"raio","no":"n2","param":"raio_cm"}, ...]}
// Subcriações entram como nós sub.incorporar com p.ref = "sha256:<digest da revisão>". Como o digest de
// uma receita cobre o digest de cada dependência, ele cobre transitivamente toda a árvore (Merkle),
// além da versão de cada operação (campo "v" de cada nó).
// Metadados visuais (nome, rótulos, cor, material) ficam fora do digest, no registro de revisão.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "json.h"

namespace forja {

constexpr const char* kFormatoReceita = "forja.receita/1";
constexpr const char* kFormatoPacote = "forja.pacote/1";

Json new_behavior();

// Normaliza parâmetros (preenche padrões das operações conhecidas) e ordena coleções.
// Operações desconhecidas/planejadas são preservadas como estão (a receita não se perde).
Json canonical_behavior(const Json& behavior);
std::string digest_of(const Json& canonical);  // "sha256:<hex>"

class ObjectStore {
public:
    // Insere conteúdo canônico; devolve o digest. Imutável: reinserir é no-op.
    std::string put(const Json& behavior);
    const Json* get(const std::string& digest) const;
    bool has(const std::string& digest) const { return objs_.count(digest) != 0; }
    // Fecho transitivo de dependências (inclui a raiz). Faltantes vão para `missing`.
    std::vector<std::string> closure(const std::string& root, std::vector<std::string>* missing = nullptr) const;
    const std::map<std::string, Json>& all() const { return objs_; }

    bool attach_dir(const std::string& dir);  // carrega objetos existentes e passa a gravar novos ali
    const std::string& dir() const { return dir_; }

private:
    std::map<std::string, Json> objs_;
    std::string dir_;
};

std::vector<std::string> refs_of(const Json& behavior);

// Arquivos
bool read_file(const std::string& path, std::string& out);
bool write_file_atomic(const std::string& path, const std::string& data);
bool make_dirs(const std::string& path);
std::vector<std::string> list_dir(const std::string& path, const std::string& suffix);

}  // namespace forja
