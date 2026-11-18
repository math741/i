#include "ops.h"

#include <cstdio>
#include <cstdlib>

namespace forja {

namespace {

ParamDef pint(const char* n, const char* rot, const char* un, int64_t mn, int64_t mx, int64_t def) {
    ParamDef p; p.nome = n; p.rotulo = rot; p.unidade = un; p.tipo = PType::Int; p.min = mn; p.max = mx; p.def = def; return p;
}
ParamDef penum(const char* n, const char* rot, std::vector<std::string> opts, const char* def) {
    ParamDef p; p.nome = n; p.rotulo = rot; p.tipo = PType::Enum; p.opcoes = std::move(opts); p.def_s = def; return p;
}
ParamDef pvec(const char* n, const char* rot, int64_t lim, int64_t x, int64_t y, int64_t z) {
    ParamDef p; p.nome = n; p.rotulo = rot; p.unidade = "cm"; p.tipo = PType::Vec; p.min = -lim; p.max = lim;
    p.def_v[0] = x; p.def_v[1] = y; p.def_v[2] = z; return p;
}
ParamDef pbool(const char* n, const char* rot, bool def) {
    ParamDef p; p.nome = n; p.rotulo = rot; p.tipo = PType::Bool; p.def = def ? 1 : 0; return p;
}
ParamDef pref(const char* n, const char* rot) { ParamDef p; p.nome = n; p.rotulo = rot; p.tipo = PType::Ref; return p; }
ParamDef pobj(const char* n, const char* rot) { ParamDef p; p.nome = n; p.rotulo = rot; p.tipo = PType::Obj; return p; }
OutPort out(const char* n, const char* rot, bool cond = false, bool manual = false) {
    OutPort o; o.nome = n; o.rotulo = rot; o.condicional = cond; o.manual = manual; return o;
}

const std::vector<std::string> kAncoras = {"autor", "ativacao", "evento", "marca"};
const std::vector<std::string> kSuperficies = {"agua", "metal", "concreto"};

std::vector<OpDef> build() {
    std::vector<OpDef> v;
    {
        OpDef o; o.nome = "entrada.ativar"; o.kind = OpKind::Ativar; o.fundamento = "Entrada";
        o.rotulo = "Ativar"; o.descricao = "Raiz da criação: dispara quando o jogador pressiona o botão do espaço equipado.";
        o.tem_entrada = false;
        o.saidas = {out("ao_ativar", "ao ativar")};
        v.push_back(o);
    }
    {
        OpDef o; o.nome = "vetor.deslocar"; o.kind = OpKind::Deslocar; o.fundamento = "Vetor";
        o.rotulo = "Deslocar"; o.descricao = "Move o autor por um vetor no referencial escolhido, com curva opcional. Paredes interrompem o trajeto.";
        o.saidas = {out("ao_chegar", "ao chegar")};
        o.params = {pvec("vetor_cm", "vetor (frente, direita, cima)", 1500, 300, 0, 0),
                    penum("espaco", "referencial", {"autor", "ativacao", "mira"}, "autor"),
                    pint("curva_cm", "curva lateral no meio do trajeto", "cm", -600, 600, 0),
                    pint("duracao_ms", "duração", "ms", 30, 3000, 150),
                    pint("recuperacao_ms", "recuperação", "ms", 0, 2000, 0)};
        v.push_back(o);
    }
    {
        OpDef o; o.nome = "vetor.retornar"; o.kind = OpKind::Retornar; o.fundamento = "Vetor";
        o.rotulo = "Retornar"; o.descricao = "Leva o autor até um ponto recebido por dado (ex.: marca de saída).";
        o.saidas = {out("ao_chegar", "ao chegar")};
        o.din = {{"destino", "ponto", "destino"}};
        o.params = {pint("duracao_ms", "duração", "ms", 30, 3000, 120),
                    pint("alcance_max_cm", "alcance máximo", "cm", 50, 2000, 1000),
                    pint("recuperacao_ms", "recuperação", "ms", 0, 2000, 0)};
        v.push_back(o);
    }
    {
        OpDef o; o.nome = "marca.guardar"; o.kind = OpKind::Marca; o.fundamento = "Vetor";
        o.rotulo = "Guardar marca"; o.descricao = "Guarda uma posição para uso posterior (dado do tipo ponto).";
        o.saidas = {out("depois", "depois de marcar")};
        o.dout = {{"ponto", "ponto", "posição marcada"}};
        o.params = {penum("ancora", "o que marcar", {"autor", "ativacao", "evento"}, "autor")};
        v.push_back(o);
    }
    {
        OpDef o; o.nome = "limiar.anel"; o.kind = OpKind::Anel; o.fundamento = "Limiar";
        o.rotulo = "Anel"; o.descricao = "Área circular (disco ou anel) com preparo público, janela de contato e impacto.";
        o.saidas = {out("ao_tocar", "se tocar um alvo", true), out("ao_absorvido", "se for absorvido", true),
                    out("ao_terminar", "quando o anel terminar")};
        o.din = {{"ponto", "ponto", "centro (âncora marca)"}};
        o.params = {penum("ancora", "centro", kAncoras, "autor"),
                    pvec("deslocamento_cm", "deslocamento do centro", 1200, 0, 0, 0),
                    pint("raio_cm", "raio", "cm", 10, 800, 100),
                    pint("espessura_cm", "espessura (0 = disco cheio)", "cm", 0, 400, 0),
                    pint("altura_cm", "altura", "cm", 20, 600, 200),
                    pint("preparo_ms", "preparo (anúncio)", "ms", 0, 3000, 200),
                    pint("contato_ms", "contato", "ms", 16, 2000, 100),
                    pint("impacto", "impacto", "", 0, 3, 1),
                    penum("afeta", "afeta", {"inimigos", "aliados", "todos"}, "inimigos"),
                    pbool("segue_autor", "segue o autor durante o preparo", false),
                    pint("recuperacao_ms", "recuperação", "ms", 0, 2000, 0)};
        v.push_back(o);
    }
    {
        OpDef o; o.nome = "limiar.anel"; o.versao = 0; o.kind = OpKind::Anel; o.fundamento = "Limiar"; o.obsoleta = true;
        o.rotulo = "Anel (v0, por diâmetro)"; o.descricao = "Versão antiga: media diâmetro. Executa via adaptador; o editor oferece migrar para v1.";
        o.saidas = {out("ao_tocar", "se tocar um alvo", true), out("ao_terminar", "quando o anel terminar")};
        o.params = {pint("diametro_cm", "diâmetro", "cm", 20, 1600, 200),
                    pint("preparo_ms", "preparo", "ms", 0, 3000, 200),
                    pint("contato_ms", "contato", "ms", 16, 2000, 100),
                    pint("impacto", "impacto", "", 0, 3, 1)};
        v.push_back(o);
    }
    {
        OpDef o; o.nome = "limiar.sondar"; o.kind = OpKind::Sondar; o.fundamento = "Limiar";
        o.rotulo = "Sondar superfície"; o.descricao = "Consulta a superfície num ponto e ramifica.";
        o.saidas = {out("se_sim", "se tocar a superfície", true), out("se_nao", "se não tocar", true)};
        o.din = {{"ponto", "ponto", "ponto (âncora marca)"}};
        o.params = {penum("ancora", "onde sondar", kAncoras, "autor"), penum("superficie", "superfície", kSuperficies, "agua")};
        v.push_back(o);
    }
    {
        OpDef o; o.nome = "limiar.vigiar"; o.kind = OpKind::Vigiar; o.fundamento = "Limiar";
        o.rotulo = "Vigiar travessia"; o.descricao = "Por um tempo, detecta quando o autor entra numa superfície (varredura contínua, não perde travessias rápidas).";
        o.saidas = {out("ao_entrar", "ao cruzar para a superfície", true), out("ao_terminar", "quando a vigia terminar")};
        o.params = {penum("superficie", "superfície", kSuperficies, "agua"),
                    pint("duracao_ms", "duração", "ms", 16, 4000, 300),
                    pint("disparos", "disparos máximos", "", 1, 4, 1)};
        v.push_back(o);
    }
    {
        OpDef o; o.nome = "lastro.anteparo"; o.kind = OpKind::Anteparo; o.fundamento = "Lastro";
        o.rotulo = "Anteparo"; o.descricao = "Defesa direcional no autor: absorve impactos que chegam dentro da abertura e se desfaz ao gastar as cargas.";
        o.saidas = {out("ao_absorver", "ao absorver um impacto", true), out("ao_romper", "ao se desfazer", true),
                    out("ao_expirar", "se expirar intacto", true), out("ao_terminar", "quando o anteparo terminar")};
        o.params = {pint("abertura_graus", "abertura total", "graus", 10, 360, 90),
                    pint("orientacao_graus", "orientação relativa à frente", "graus", -180, 180, 0),
                    pbool("segue_autor", "acompanha o giro do autor", true),
                    penum("gesto", "gesto de preparação", {"palma_frente", "bracos_cruzados", "ombro"}, "palma_frente"),
                    pint("preparo_ms", "preparo (gesto)", "ms", 0, 2000, 150),
                    pint("duracao_ms", "persistência", "ms", 50, 6000, 1500),
                    pint("cargas", "cargas", "", 1, 3, 1),
                    pint("recuperacao_ms", "recuperação", "ms", 0, 2000, 200)};
        v.push_back(o);
    }
    {
        OpDef o; o.nome = "lastro.amparo"; o.kind = OpKind::Amparo; o.fundamento = "Lastro";
        o.rotulo = "Amparo"; o.descricao = "Suporte: concede um anteparo de 360° e fôlego aos aliados dentro do raio.";
        o.saidas = {out("ao_amparar", "ao amparar um aliado", true), out("ao_terminar", "ao terminar")};
        o.params = {pint("raio_cm", "raio", "cm", 50, 800, 300),
                    pint("preparo_ms", "preparo", "ms", 0, 2000, 250),
                    pint("duracao_ms", "duração da guarda concedida", "ms", 100, 6000, 1200),
                    pint("cargas", "cargas concedidas", "", 1, 2, 1),
                    pint("folego", "fôlego concedido", "", 0, 30, 0),
                    pbool("inclui_autor", "inclui o autor", false)};
        v.push_back(o);
    }
    {
        OpDef o; o.nome = "cadencia.janela"; o.kind = OpKind::Janela; o.fundamento = "Cadência";
        o.rotulo = "Janela de comando"; o.descricao = "Abre uma janela de tempo; um comando dentro dela segue por 'ao comando'.";
        o.saidas = {out("ao_comando", "se o comando vier na janela", true, true), out("ao_expirar", "se a janela fechar sem comando", true)};
        o.params = {pint("abre_ms", "abre após", "ms", 0, 3000, 0),
                    pint("dura_ms", "duração", "ms", 30, 3000, 300),
                    penum("gatilho", "gatilho", {"pressionar", "soltar"}, "pressionar"),
                    penum("comando", "comando", {"mesmo_botao", "secundario"}, "mesmo_botao"),
                    penum("cedo", "comando cedo", {"ignorar", "anular"}, "ignorar"),
                    pint("tolerancia_cedo_ms", "tolerância para 'cedo'", "ms", 0, 1000, 150)};
        v.push_back(o);
    }
    {
        OpDef o; o.nome = "cadencia.esperar"; o.kind = OpKind::Esperar; o.fundamento = "Cadência";
        o.rotulo = "Esperar"; o.descricao = "Atraso fixo.";
        o.saidas = {out("depois", "depois de esperar")};
        o.params = {pint("ms", "espera", "ms", 1, 5000, 200)};
        v.push_back(o);
    }
    {
        OpDef o; o.nome = "cadencia.repetir"; o.kind = OpKind::Repetir; o.fundamento = "Cadência";
        o.rotulo = "Repetir"; o.descricao = "Repetição controlada: N disparos com intervalo mínimo (nunca instantânea).";
        o.saidas = {out("a_cada", "a cada repetição"), out("ao_terminar", "ao terminar as repetições")};
        o.params = {pint("vezes", "vezes", "", 1, 8, 3), pint("intervalo_ms", "intervalo", "ms", 50, 3000, 200)};
        v.push_back(o);
    }
    {
        OpDef o; o.nome = "cadencia.reunir"; o.kind = OpKind::Reunir; o.fundamento = "Cadência";
        o.rotulo = "Reunir"; o.descricao = "Segue quando N ramos chegarem.";
        o.saidas = {out("depois", "quando todos chegarem", true)};
        o.params = {pint("n", "ramos", "", 2, 4, 2)};
        v.push_back(o);
    }
    {
        OpDef o; o.nome = "cadencia.encerrar"; o.kind = OpKind::Encerrar; o.fundamento = "Cadência";
        o.rotulo = "Interromper"; o.descricao = "Encerra a execução inteira, liberando efeitos ligados ao autor.";
        v.push_back(o);
    }
    {
        OpDef o; o.nome = "sub.incorporar"; o.kind = OpKind::Incorporar; o.fundamento = "Subcriação";
        o.rotulo = "Incorporar criação"; o.descricao = "Executa uma revisão imutável de outra criação; segue quando ela termina.";
        o.saidas = {out("ao_terminar", "quando a subcriação terminar")};
        o.params = {pref("ref", "revisão (digest)"), pobj("sobrepor", "parâmetros expostos sobrepostos")};
        v.push_back(o);
    }
    // Operações planejadas: nomeadas para que receitas que as usam sejam preservadas e a falta seja explícita.
    struct Pl { const char* n; const char* f; const char* r; const char* d; };
    const Pl planned[] = {
        {"vetor.velocidade", "Vetor", "Velocidade", "Impõe velocidade contínua ao autor ou a um projétil."},
        {"vetor.acelerar", "Vetor", "Acelerar", "Curva de aceleração sobre um movimento existente."},
        {"vetor.perseguir", "Vetor", "Perseguir", "Trajetória que persegue um alvo com taxa de giro limitada."},
        {"vetor.orbitar", "Vetor", "Orbitar", "Mantém um efeito em órbita ao redor de um ponto."},
        {"limiar.projetil", "Limiar", "Projétil", "Volume que viaja e detecta contato ao longo do caminho."},
        {"limiar.dividir", "Limiar", "Dividir", "Divide uma área em N partes com direções próprias."},
        {"limiar.reunir_formas", "Limiar", "Reunir formas", "Funde áreas próximas numa só."},
        {"lastro.zona", "Lastro", "Zona persistente", "Área persistente que sustenta um efeito por tempo."},
        {"lastro.transmitir", "Lastro", "Transmitir", "Passa um efeito absorvido para outro alvo."},
        {"forma.mudar", "Forma", "Mudar forma", "Troca a forma de uma área durante a vida dela."},
        {"entrada.carregar", "Entrada", "Carregar", "Escala parâmetros pelo tempo de carga do botão."},
        {"entrada.manter", "Entrada", "Manter", "Mantém um efeito enquanto o botão estiver pressionado."},
    };
    for (auto& p : planned) {
        OpDef o; o.nome = p.n; o.fundamento = p.f; o.rotulo = p.r; o.descricao = p.d; o.planejada = true;
        o.kind = OpKind::Planejada;
        o.saidas = {out("depois", "depois")};
        v.push_back(o);
    }
    return v;
}

}  // namespace

const std::vector<OpDef>& all_ops() {
    static const std::vector<OpDef> ops = build();
    return ops;
}

int OpDef::out_index(const std::string& port) const {
    for (size_t i = 0; i < saidas.size(); ++i) if (saidas[i].nome == port) return static_cast<int>(i);
    return -1;
}
int OpDef::din_index(const std::string& port) const {
    for (size_t i = 0; i < din.size(); ++i) if (din[i].nome == port) return static_cast<int>(i);
    return -1;
}
int OpDef::dout_index(const std::string& port) const {
    for (size_t i = 0; i < dout.size(); ++i) if (dout[i].nome == port) return static_cast<int>(i);
    return -1;
}
const ParamDef* OpDef::param(const std::string& n) const {
    for (auto& p : params) if (p.nome == n) return &p;
    return nullptr;
}

const OpDef* find_op(const std::string& nome, int versao) {
    for (auto& o : all_ops()) if (o.nome == nome && o.versao == versao) return &o;
    return nullptr;
}

const OpDef* latest_op(const std::string& nome) {
    const OpDef* best = nullptr;
    for (auto& o : all_ops())
        if (o.nome == nome && !o.obsoleta && (!best || o.versao > best->versao)) best = &o;
    return best;
}

std::vector<int> op_versions(const std::string& nome) {
    std::vector<int> v;
    for (auto& o : all_ops()) if (o.nome == nome) v.push_back(o.versao);
    return v;
}

bool normalize_params(const OpDef& op, Json& params, std::vector<std::string>& errs) {
    bool ok = true;
    if (!params.is_obj()) params = Json::obj();
    for (auto& kv : params.members()) {
        if (!op.param(kv.first)) { errs.push_back("parâmetro desconhecido '" + kv.first + "'"); ok = false; }
    }
    for (auto& pd : op.params) {
        Json* cur = params.find(pd.nome);
        switch (pd.tipo) {
            case PType::Int:
                if (!cur) { params.set(pd.nome, Json(pd.def)); break; }
                if (!cur->is_int()) { errs.push_back(pd.nome + ": esperava inteiro (" + pd.unidade + ")"); ok = false; break; }
                if (cur->as_int() < pd.min || cur->as_int() > pd.max) {
                    errs.push_back(pd.nome + " = " + std::to_string(cur->as_int()) + " fora da faixa [" + std::to_string(pd.min) + ", " + std::to_string(pd.max) + "] " + pd.unidade);
                    ok = false;
                }
                break;
            case PType::Bool:
                if (!cur) { params.set(pd.nome, Json(pd.def != 0)); break; }
                if (!cur->is_bool()) { errs.push_back(pd.nome + ": esperava verdadeiro/falso"); ok = false; }
                break;
            case PType::Enum: {
                if (!cur) { params.set(pd.nome, Json(pd.def_s)); break; }
                bool found = false;
                for (auto& o : pd.opcoes) if (cur->is_str() && cur->as_str() == o) found = true;
                if (!found) {
                    std::string opts;
                    for (auto& o : pd.opcoes) opts += (opts.empty() ? "" : ", ") + o;
                    errs.push_back(pd.nome + ": valor inválido (opções: " + opts + ")");
                    ok = false;
                }
                break;
            }
            case PType::Vec: {
                if (!cur) {
                    Json a = Json::arr();
                    for (int i = 0; i < 3; ++i) a.push(Json(pd.def_v[i]));
                    params.set(pd.nome, a);
                    break;
                }
                if (!cur->is_arr() || cur->size() != 3) { errs.push_back(pd.nome + ": esperava [frente, direita, cima] em cm"); ok = false; break; }
                for (size_t i = 0; i < 3; ++i) {
                    if (!cur->at(i).is_int() || cur->at(i).as_int() < pd.min || cur->at(i).as_int() > pd.max) {
                        errs.push_back(pd.nome + ": componente fora da faixa ±" + std::to_string(pd.max) + " cm");
                        ok = false;
                    }
                }
                break;
            }
            case PType::Ref:
                if (!cur || !cur->is_str() || cur->as_str().rfind("sha256:", 0) != 0) {
                    errs.push_back(pd.nome + ": referência ausente ou inválida (esperava sha256:...)");
                    ok = false;
                }
                break;
            case PType::Obj:
                if (!cur) { params.set(pd.nome, Json::obj()); break; }
                if (!cur->is_obj()) { errs.push_back(pd.nome + ": esperava objeto"); ok = false; }
                break;
        }
    }
    return ok;
}

bool adapt_to_latest(Json& node, std::string& err) {
    std::string op = node.get("op").as_str();
    int v = static_cast<int>(node.get("v").as_int(1));
    // Cadeia de migrações. Cada passo preserva a semântica observável.
    if (op == "limiar.anel" && v == 0) {
        Json p = node.get("p");
        Json np = Json::obj();
        np.set("raio_cm", Json(p.get("diametro_cm").as_int(200) / 2));
        if (p.has("preparo_ms")) np.set("preparo_ms", p.get("preparo_ms"));
        if (p.has("contato_ms")) np.set("contato_ms", p.get("contato_ms"));
        if (p.has("impacto")) np.set("impacto", p.get("impacto"));
        node.set("p", np);
        node.set("v", Json(1));
        // portas de v0 são subconjunto das de v1 (mesmos nomes)
        return true;
    }
    const OpDef* cur = find_op(op, v);
    if (cur && !cur->obsoleta) return true;
    std::string vs;
    for (int x : op_versions(op)) vs += (vs.empty() ? "" : ", ") + std::to_string(x);
    err = "versão incompatível: " + op + "@" + std::to_string(v) + (vs.empty() ? " (operação inexistente)" : " (suportadas: " + vs + ")");
    return false;
}

std::string fmt_m(int64_t cm) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.2f m", static_cast<double>(cm) / 100.0);
    std::string s = b;
    for (auto& c : s) if (c == '.') c = ',';
    return s;
}
std::string fmt_ms(int64_t ms) { return std::to_string(ms) + " ms"; }

static std::string vec_text(const Json& v, const std::string& espaco) {
    int64_t x = v.at(0).as_int(), y = v.at(1).as_int(), z = v.at(2).as_int();
    std::string s;
    auto add = [&](int64_t val, const char* pos, const char* neg) {
        if (val == 0) return;
        if (!s.empty()) s += " + ";
        s += fmt_m(val < 0 ? -val : val) + " " + (val > 0 ? pos : neg);
    };
    add(x, "à frente", "para trás");
    add(y, "à direita", "à esquerda");
    add(z, "para cima", "para baixo");
    if (s.empty()) s = "0 m";
    if (espaco == "ativacao") s += " (referencial da ativação)";
    if (espaco == "mira") s += " (na direção da mira)";
    return s;
}

static std::string anchor_text(const std::string& a) {
    if (a == "autor") return "na posição atual do autor";
    if (a == "ativacao") return "onde a criação foi ativada";
    if (a == "evento") return "no ponto do evento que disparou";
    if (a == "marca") return "na marca recebida";
    return a;
}

static std::string surf_text(const std::string& s) {
    if (s == "agua") return "água";
    return s;
}

std::string describe_node(const OpDef& op, const Json& p) {
    auto I = [&](const char* k) { return p.get(k).as_int(); };
    auto S = [&](const char* k) { return p.get(k).as_str(); };
    std::string r;
    switch (op.kind) {
        case OpKind::Ativar: return "ao pressionar o botão do espaço equipado";
        case OpKind::Deslocar:
            r = "desloca " + vec_text(p.get("vetor_cm"), S("espaco")) + " em " + fmt_ms(I("duracao_ms"));
            if (I("curva_cm")) r += ", com curva de " + fmt_m(I("curva_cm"));
            if (I("recuperacao_ms")) r += "; recuperação " + fmt_ms(I("recuperacao_ms"));
            return r;
        case OpKind::Retornar: return "volta ao destino em " + fmt_ms(I("duracao_ms")) + " (até " + fmt_m(I("alcance_max_cm")) + ")";
        case OpKind::Marca: return "guarda a posição " + std::string(S("ancora") == "autor" ? "atual do autor" : anchor_text(S("ancora")));
        case OpKind::Anel: {
            if (op.versao == 0) return "anel de diâmetro " + fmt_m(I("diametro_cm"));
            r = std::string(I("espessura_cm") ? "anel" : "disco") + " de raio " + fmt_m(I("raio_cm")) + " " + anchor_text(S("ancora"));
            r += "; preparo " + fmt_ms(I("preparo_ms")) + ", contato " + fmt_ms(I("contato_ms"));
            if (I("impacto")) r += ", impacto " + std::to_string(I("impacto"));
            else r += ", sem impacto (só detecta)";
            if (S("afeta") != "inimigos") r += ", afeta " + S("afeta");
            return r;
        }
        case OpKind::Sondar: return "verifica se " + anchor_text(S("ancora")) + " há " + surf_text(S("superficie"));
        case OpKind::Vigiar: return "por " + fmt_ms(I("duracao_ms")) + ", vigia quando o autor cruza para " + surf_text(S("superficie"));
        case OpKind::Anteparo:
            r = "anteparo de " + std::to_string(I("abertura_graus")) + "° ";
            r += I("orientacao_graus") == 0 ? "à frente" : ("girado " + std::to_string(I("orientacao_graus")) + "°");
            r += ", gesto " + S("gesto") + " (" + fmt_ms(I("preparo_ms")) + "), dura " + fmt_ms(I("duracao_ms"));
            r += ", " + std::to_string(I("cargas")) + (I("cargas") == 1 ? " carga" : " cargas");
            return r;
        case OpKind::Amparo:
            return "ampara aliados num raio de " + fmt_m(I("raio_cm")) + ": guarda 360° por " + fmt_ms(I("duracao_ms")) +
                   (I("folego") ? " e +" + std::to_string(I("folego")) + " de fôlego" : "");
        case OpKind::Janela:
            r = "abre janela de " + fmt_ms(I("dura_ms"));
            if (I("abre_ms")) r += " após " + fmt_ms(I("abre_ms"));
            r += std::string(S("gatilho") == "soltar" ? " (ao soltar" : " (ao pressionar") + (S("comando") == "mesmo_botao" ? " o mesmo botão)" : " o botão secundário)");
            if (S("cedo") == "anular") r += "; comando cedo anula";
            return r;
        case OpKind::Esperar: return "espera " + fmt_ms(I("ms"));
        case OpKind::Repetir: return "repete " + std::to_string(I("vezes")) + " vezes a cada " + fmt_ms(I("intervalo_ms"));
        case OpKind::Reunir: return "aguarda " + std::to_string(I("n")) + " ramos";
        case OpKind::Encerrar: return "interrompe a execução";
        case OpKind::Incorporar: return "executa a subcriação " + S("ref").substr(0, 15) + "…";
        case OpKind::Planejada: return "[operação planejada, ainda não disponível] " + op.descricao;
    }
    return op.rotulo;
}

Json ops_catalog() {
    Json arr = Json::arr();
    for (auto& o : all_ops()) {
        Json j = Json::obj();
        j.set("op", o.nome);
        j.set("v", o.versao);
        j.set("fundamento", o.fundamento);
        j.set("rotulo", o.rotulo);
        j.set("descricao", o.descricao);
        j.set("planejada", o.planejada);
        j.set("obsoleta", o.obsoleta);
        j.set("entrada", o.tem_entrada);
        Json outs = Json::arr();
        for (auto& s : o.saidas) {
            Json x = Json::obj();
            x.set("nome", s.nome); x.set("rotulo", s.rotulo); x.set("condicional", s.condicional); x.set("manual", s.manual);
            outs.push(x);
        }
        j.set("saidas", outs);
        Json din = Json::arr();
        for (auto& d : o.din) { Json x = Json::obj(); x.set("nome", d.nome); x.set("tipo", d.tipo); x.set("rotulo", d.rotulo); din.push(x); }
        j.set("dados_entrada", din);
        Json dout = Json::arr();
        for (auto& d : o.dout) { Json x = Json::obj(); x.set("nome", d.nome); x.set("tipo", d.tipo); x.set("rotulo", d.rotulo); dout.push(x); }
        j.set("dados_saida", dout);
        Json ps = Json::arr();
        for (auto& p : o.params) {
            Json x = Json::obj();
            x.set("nome", p.nome); x.set("rotulo", p.rotulo); x.set("unidade", p.unidade);
            const char* t = "int";
            switch (p.tipo) { case PType::Int: t = "int"; break; case PType::Enum: t = "enum"; break; case PType::Vec: t = "vec"; break; case PType::Bool: t = "bool"; break; case PType::Ref: t = "ref"; break; case PType::Obj: t = "obj"; break; }
            x.set("tipo", t);
            if (p.tipo == PType::Int) { x.set("min", p.min); x.set("max", p.max); x.set("padrao", p.def); }
            if (p.tipo == PType::Bool) x.set("padrao", p.def != 0);
            if (p.tipo == PType::Enum) { Json op = Json::arr(); for (auto& s : p.opcoes) op.push(s); x.set("opcoes", op); x.set("padrao", p.def_s); }
            if (p.tipo == PType::Vec) { x.set("min", p.min); x.set("max", p.max); Json d = Json::arr(); for (int i = 0; i < 3; ++i) d.push(p.def_v[i]); x.set("padrao", d); }
            ps.push(x);
        }
        j.set("params", ps);
        arr.push(j);
    }
    return arr;
}

}  // namespace forja
