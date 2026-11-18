// Testes do núcleo. Toda criação nasce de comandos da oficina (Workshop::exec), os mesmos que a
// interface envia. Nenhum plano é montado à mão. Saída: relatório legível + tests/out/core_report.json.
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <string>

#include "forja/compile.h"
#include "forja/hash.h"
#include "forja/match.h"
#include "forja/runtime.h"
#include "forja/workshop.h"

using namespace forja;

static int g_pass = 0, g_fail = 0;
static Json g_report = Json::obj();
#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("  FALHOU (%s:%d): ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } \
    } while (0)

static std::string F(const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}

static Json W(Workshop& w, const std::string& text, bool expect_ok = true) {
    Json c;
    std::string err;
    if (!Json::parse(text, c, &err)) { std::printf("  JSON inválido no teste: %s\n  %s\n", err.c_str(), text.c_str()); ++g_fail; return Json(); }
    Json r = w.exec(c);
    if (expect_ok && !r.get("ok").as_bool()) { std::printf("  comando falhou: %s\n  -> %s\n", text.c_str(), r.get("erro").as_str().c_str()); ++g_fail; }
    return r;
}

static std::string tmpdir(const char* name) {
    std::string d = std::filesystem::temp_directory_path().string() + "/forja_test_" + name;
    std::error_code ec;
    std::filesystem::remove_all(d, ec);
    std::filesystem::create_directories(d, ec);
    return d;
}

static Json first_event(const Json& sim, const std::string& tipo, int nth = 0) {
    for (auto& e : sim.get("eventos").items())
        if (e.get("tipo").as_str() == tipo && nth-- == 0) return e;
    return Json();
}
static int count_events(const Json& sim, const std::string& tipo) {
    int n = 0;
    for (auto& e : sim.get("eventos").items()) if (e.get("tipo").as_str() == tipo) ++n;
    return n;
}
static Json last_frame_pos(const Json& sim, int ent) {
    const Json& qs = sim.get("quadros");
    const Json& f = qs.at(qs.size() - 1);
    for (auto& e : f.get("entidades").items()) if (e.get("id").as_int() == ent) return e.get("pos");
    return Json();
}
static double near(const Json& p, double x, double y) { return std::hypot(p.at(0).as_num() - x, p.at(1).as_num() - y); }

// Cria Passo, Pulso e Travessia exatamente como a primeira jogadora faria na oficina.
struct Autoria { std::string passo, pulso, travessia, invertida, contramare; };

static Autoria build_authoring(Workshop& ws) {
    Autoria a;
    Json r = W(ws, R"({"cmd":"criar","nome":"Passo"})");
    a.passo = r.get("criacao").as_str();
    W(ws, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"vetor.deslocar","p":{"vetor_cm":[300,0,0],"duracao_ms":150},"rotulo":"Passo","ligar_de":"n0","porta":"ao_ativar"})", a.passo.c_str()));
    W(ws, F(R"({"cmd":"salvar","criacao":"%s","nota":"deslocamento de 3 m"})", a.passo.c_str()));

    r = W(ws, R"({"cmd":"criar","nome":"Pulso"})");
    a.pulso = r.get("criacao").as_str();
    W(ws, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"limiar.anel","p":{"raio_cm":100,"ancora":"autor"},"rotulo":"Pulso","ligar_de":"n0","porta":"ao_ativar"})", a.pulso.c_str()));
    W(ws, F(R"({"cmd":"salvar","criacao":"%s","nota":"anel de 1 m na posição atual"})", a.pulso.c_str()));

    r = W(ws, R"({"cmd":"criar","nome":"Travessia"})");
    a.travessia = r.get("criacao").as_str();
    Json e1 = W(ws, F(R"({"cmd":"incorporar","criacao":"%s","alvo":"%s","ligar_de":"n0","porta":"ao_ativar"})", a.travessia.c_str(), a.passo.c_str()));
    std::string np = e1.get("no").as_str();
    W(ws, F(R"({"cmd":"incorporar","criacao":"%s","alvo":"%s","ligar_de":"%s","porta":"ao_terminar"})", a.travessia.c_str(), a.pulso.c_str(), np.c_str()));
    W(ws, F(R"({"cmd":"salvar","criacao":"%s","nota":"Passo, depois Pulso"})", a.travessia.c_str()));
    return a;
}

static void test_hash_json() {
    std::printf("[1] JSON canônico e SHA-256\n");
    CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "vetor de teste SHA-256");
    Json a, b;
    Json::parse(R"({"b":1,"a":[1,2,{"y":"é","x":true}]})", a);
    Json::parse(R"({ "a" : [1,2,{"x":true,"y":"é"}], "b" : 1 })", b);
    CHECK(a.dump(true) == b.dump(true), "forma canônica independe de ordem e espaços");
    CHECK(base64(sha1_raw("dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11")) == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "SHA-1/base64 do handshake WebSocket (RFC 6455)");
}

static void test_travessia(Workshop& ws, Autoria& a) {
    std::printf("[2] Primeiro teste de autoria: Travessia e Travessia invertida\n");
    // autor em S=(-600,-250) olhando +X; chegada A=(-300,-250); alvo 90 cm ao lado da chegada, perpendicular ao trajeto
    const char* cen = R"({"autor":{"pos":[-600,-250,0],"yaw":0},"alvos":[{"nome":"Alvo90","pos":[-300,-160,0]}]})";
    Json sim = W(ws, F(R"({"cmd":"simular","criacao":"%s","cenario":%s})", a.travessia.c_str(), cen));
    Json c = first_event(sim, "area.contato");
    Json res = c.get("resultados").at(0);
    std::printf("    Passo→Pulso: centro=%s raio=%lld t=%lld ms resultado=%s (%s)\n", c.get("centro").dump().c_str(), (long long)c.get("raio").as_int(),
                (long long)c.get("t_rel").as_int(), res.get("resultado").as_str().c_str(), res.get("motivo").as_str().c_str());
    CHECK(near(c.get("centro"), -300, -250) < 0.5, "anel nasce no ponto de chegada");
    CHECK(c.get("raio").as_int() == 100, "raio 1 m");
    CHECK(res.get("resultado").as_str() == "atingido", "alvo a 90 cm recebe o pulso");
    CHECK(c.get("t_rel").as_int() == 350, "contato em 150 (Passo) + 200 (preparo) = 350 ms, obtido %lld", (long long)c.get("t_rel").as_int());
    Json rep = Json::obj();
    Json run1 = Json::obj();
    run1.set("centro", c.get("centro")); run1.set("raio", c.get("raio")); run1.set("instante_ms", c.get("t_rel")); run1.set("resultado", res.get("resultado")); run1.set("motivo", res.get("motivo"));
    rep.set("passo_depois_pulso", run1);

    Json d = W(ws, F(R"({"cmd":"duplicar","criacao":"%s","nome":"Travessia invertida"})", a.travessia.c_str()));
    a.invertida = d.get("criacao").as_str();
    Json inv = W(ws, F(R"({"cmd":"sequencia.inverter","criacao":"%s"})", a.invertida.c_str()));
    CHECK(inv.get("nova_ordem").size() == 2, "inversão devolve a nova ordem");
    W(ws, F(R"({"cmd":"salvar","criacao":"%s","nota":"Pulso, depois Passo"})", a.invertida.c_str()));
    Json sim2 = W(ws, F(R"({"cmd":"simular","criacao":"%s","cenario":%s})", a.invertida.c_str(), cen));
    Json c2 = first_event(sim2, "area.contato");
    Json res2 = c2.get("resultados").at(0);
    std::printf("    Pulso→Passo: centro=%s raio=%lld t=%lld ms resultado=%s (%s)\n", c2.get("centro").dump().c_str(), (long long)c2.get("raio").as_int(),
                (long long)c2.get("t_rel").as_int(), res2.get("resultado").as_str().c_str(), res2.get("motivo").as_str().c_str());
    CHECK(near(c2.get("centro"), -600, -250) < 0.5, "anel nasce na saída");
    CHECK(res2.get("resultado").as_str() == "fora", "mesmo alvo fica fora de alcance");
    CHECK(c2.get("t_rel").as_int() == 200, "contato em 200 ms (preparo), antes do Passo");
    Json run2 = Json::obj();
    run2.set("centro", c2.get("centro")); run2.set("raio", c2.get("raio")); run2.set("instante_ms", c2.get("t_rel")); run2.set("resultado", res2.get("resultado")); run2.set("motivo", res2.get("motivo"));
    rep.set("pulso_depois_passo", run2);
    rep.set("digest_travessia", sim.get("analise").get("digest"));
    rep.set("digest_invertida", sim2.get("analise").get("digest"));
    CHECK(sim.get("analise").get("digest").as_str() != sim2.get("analise").get("digest").as_str(), "receitas diferentes têm digests diferentes");
    g_report.set("teste_autoria_travessia", rep);
}

static void test_contramare(Workshop& ws, Autoria& a) {
    std::printf("[3] Contramaré: janela de 300 ms após o fim de Travessia\n");
    Json r = W(ws, R"({"cmd":"criar","nome":"Contramaré"})");
    a.contramare = r.get("criacao").as_str();
    const char* id = a.contramare.c_str();
    Json m = W(ws, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"marca.guardar","p":{"ancora":"autor"},"rotulo":"Marca de saída","ligar_de":"n0","porta":"ao_ativar"})", id));
    std::string nm = m.get("no").as_str();
    Json t = W(ws, F(R"({"cmd":"incorporar","criacao":"%s","alvo":"%s","ligar_de":"%s","porta":"depois"})", id, a.travessia.c_str(), nm.c_str()));
    std::string nt = t.get("no").as_str();
    Json j = W(ws, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"cadencia.janela","p":{"dura_ms":300},"rotulo":"Janela de volta","ligar_de":"%s","porta":"ao_terminar"})", id, nt.c_str()));
    std::string nj = j.get("no").as_str();
    Json v = W(ws, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"vetor.retornar","p":{"duracao_ms":120},"rotulo":"Volta","ligar_de":"%s","porta":"ao_comando"})", id, nj.c_str()));
    std::string nv = v.get("no").as_str();
    Json dl = W(ws, F(R"({"cmd":"dados.ligar","criacao":"%s","de":"%s","porta":"ponto","para":"%s","entrada":"destino"})", id, nm.c_str(), nv.c_str()));
    Json an = dl.get("analise");
    CHECK(an.get("executavel").as_bool(), "Contramaré compila");
    // linha do tempo: automático x manual
    bool manual_ok = false, auto_ok = true;
    for (auto& s : an.get("linha_do_tempo").items()) {
        if (s.get("top").as_str() == nv && s.get("manual").as_bool()) manual_ok = true;
        if (s.get("top").as_str() == nt && s.get("manual").as_bool()) auto_ok = false;
        if (s.get("tipo").as_str() == "janela") {
            CHECK(s.get("ini").as_int() == 450 && s.get("fim").as_int() == 750, "janela de 450 a 750 ms (fim de Travessia + 300)");
        }
    }
    CHECK(manual_ok, "a volta aparece como manual na linha do tempo");
    CHECK(auto_ok, "Travessia aparece como automática");
    W(ws, F(R"({"cmd":"salvar","criacao":"%s","nota":"marca, Travessia, janela 300 ms, volta"})", id));
    struct Case { const char* nome; const char* cmds; bool volta; };
    Case cases[] = {
        {"cedo (t=300, durante Travessia)", R"([{"t":300}])", false},
        {"dentro (t=600)", R"([{"t":600}])", true},
        {"tarde (t=800, após fechar em 750)", R"([{"t":800}])", false},
        {"ausente", "[]", false},
    };
    Json rep = Json::arr();
    for (auto& c : cases) {
        Json sim = W(ws, F(R"({"cmd":"simular","criacao":"%s","cenario":{"autor":{"pos":[-600,-250,0],"yaw":0},"comandos":%s}})", id, c.cmds));
        Json jr = first_event(sim, "janela.resultado");
        Json ja = first_event(sim, "janela.abre");
        Json fim = last_frame_pos(sim, 1);
        bool voltou = near(fim, -600, -250) < 1.0;
        std::printf("    comando %-34s janela [%lld, %lld) -> %-8s posição final %s\n", c.nome, (long long)(ja.get("t_abre").as_int() - 1000),
                    (long long)(ja.get("t_fecha").as_int() - 1000), jr.get("resultado").as_str().c_str(), fim.dump().c_str());
        CHECK(voltou == c.volta, "%s: esperava %s", c.nome, c.volta ? "voltar à marca" : "ficar na chegada");
        CHECK(c.volta ? jr.get("resultado").as_str() == "comando" : jr.get("resultado").as_str() == "expirou", "%s: resultado da janela", c.nome);
        if (std::string(c.nome).rfind("cedo", 0) == 0) CHECK(count_events(sim, "cmd.recebido") == 1, "comando cedo fica registrado");
        if (std::string(c.nome).rfind("tarde", 0) == 0) CHECK(count_events(sim, "cmd.tarde") == 1, "comando tarde é identificado");
        Json x = Json::obj();
        x.set("caso", c.nome); x.set("janela_abre_ms", ja.get("t_abre").as_int() - 1000); x.set("janela_fecha_ms", ja.get("t_fecha").as_int() - 1000);
        x.set("resultado", jr.get("resultado")); x.set("posicao_final", fim); x.set("voltou", voltou);
        rep.push(x);
    }
    g_report.set("teste_contramare", rep);
}

static Json bundle_of(Workshop& ws, const std::string& cr) {
    Json r = W(ws, F(R"({"cmd":"exportar","criacao":"%s"})", cr.c_str()));
    return r.get("pacote");
}

static void test_anteparo(Workshop& wa, Workshop& wb, Autoria& a, std::string& anteparo_id) {
    std::printf("[4] Anteparo (abertura 90°): impacto de frente x a 90° do eixo\n");
    Json r = W(wb, R"({"cmd":"criar","nome":"Anteparo"})");
    anteparo_id = r.get("criacao").as_str();
    W(wb, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"lastro.anteparo","p":{"abertura_graus":90,"gesto":"bracos_cruzados","preparo_ms":150,"duracao_ms":1500,"cargas":1},"rotulo":"Anteparo","ligar_de":"n0","porta":"ao_ativar"})", anteparo_id.c_str()));
    W(wb, F(R"({"cmd":"salvar","criacao":"%s"})", anteparo_id.c_str()));
    Match m(MatchConfig::for_mode("duelo"));
    int t = 1000;
    int sa = m.join("Ana", "ta", t), sb = m.join("Bruno", "tb", t);
    m.start_training(t);
    Json pa = m.publish(bundle_of(wa, a.pulso));
    Json pb = m.publish(bundle_of(wb, anteparo_id));
    CHECK(pa.get("ok").as_bool() && pb.get("ok").as_bool(), "servidor aceita as receitas publicadas");
    m.equip(sa, 2, pa.get("digest").as_str(), t);
    m.equip(sb, 0, pb.get("digest").as_str(), t);
    Json tr = Json::obj(); tr.set("cmd", "treino.sem_recarga"); tr.set("valor", true);
    m.training(tr, sa, t);
    auto place = [&](int seat, double x, double y, double yaw) {
        Json c = Json::obj(); c.set("cmd", "treino.posicionar");
        Json p = Json::arr(); p.push(x); p.push(y); p.push(0.0); c.set("pos", p); c.set("yaw", yaw);
        m.training(c, seat, t);
    };
    Json rep = Json::arr();
    struct Case { const char* nome; double ax, ay; bool absorve; };
    Case cases[] = {{"de frente (0°)", -80, 250, true}, {"lateral (90° do eixo)", 0, 170, false}};
    for (auto& c : cases) {
        t += 3000;
        m.tick(t);
        m.drain();
        place(sb, 0, 250, 180);   // Bruno olha para -X: a frente protegida aponta para o oeste
        place(sa, c.ax, c.ay, 0);
        m.tick(t);
        Runtime::Act g = m.press(sb, 0, false, false, t, t, 180, Vec3());
        CHECK(g.ok, "Bruno ativa o Anteparo (%s)", g.motivo.c_str());
        t += 200;
        m.tick(t);
        Runtime::Act p = m.press(sa, 2, false, false, t, t, 0, Vec3());
        CHECK(p.ok, "Ana ativa o Pulso (%s)", p.motivo.c_str());
        t += 500;
        m.tick(t);
        Json absorve, contato;
        for (auto& e : m.drain()) {
            if (e.get("tipo").as_str() == "guarda.absorve") absorve = e;
            if (e.get("tipo").as_str() == "area.contato" && contato.is_null()) contato = e;
        }
        Json res = contato.get("resultados").at(0);
        std::printf("    %-24s resultado=%-9s %s\n", c.nome, res.get("resultado").as_str().c_str(), res.get("motivo").as_str().c_str());
        CHECK((res.get("resultado").as_str() == "absorvido") == c.absorve, "%s: absorção esperada=%d", c.nome, c.absorve);
        Json x = Json::obj(); x.set("caso", c.nome); x.set("resultado", res.get("resultado")); x.set("motivo", res.get("motivo")); x.set("angulo", res.get("angulo"));
        rep.push(x);
    }
    g_report.set("teste_anteparo", rep);
}

static void test_versions(Workshop& ws, Library& lib, Autoria& a, const std::string& root) {
    std::printf("[5] Revisões: Travessia r2, Contramaré antiga reproduz r1, comparar, decidir, desfazer, reiniciar\n");
    // Pulso r2 (raio 150) e Travessia r2 apontando para ele
    W(ws, F(R"({"cmd":"no.param","criacao":"%s","no":"n1","nome":"raio_cm","valor":150})", a.pulso.c_str()));
    W(ws, F(R"({"cmd":"salvar","criacao":"%s","nota":"raio maior"})", a.pulso.c_str()));
    Json deps = W(ws, F(R"({"cmd":"dependencias","criacao":"%s"})", a.travessia.c_str()));
    std::string pulso_node;
    for (auto& d : deps.get("dependencias").items()) if (d.get("desatualizada").as_bool()) pulso_node = d.get("no").as_str();
    CHECK(!pulso_node.empty(), "Travessia percebe Pulso r2 disponível");
    W(ws, F(R"({"cmd":"dependencias.atualizar","criacao":"%s","no":"%s"})", a.travessia.c_str(), pulso_node.c_str()));
    Json s2 = W(ws, F(R"({"cmd":"salvar","criacao":"%s","nota":"usa Pulso r2"})", a.travessia.c_str()));
    CHECK(s2.get("revisao").as_int() == 2, "Travessia r2 publicada");
    const char* cen = R"({"autor":{"pos":[-600,-250,0],"yaw":0}})";
    Json sim_old = W(ws, F(R"({"cmd":"simular","criacao":"%s","cenario":%s})", a.contramare.c_str(), cen));
    CHECK(first_event(sim_old, "area.anuncio").get("raio").as_int() == 100, "Contramaré antiga reproduz Travessia r1 (raio 1 m)");
    Json dd = W(ws, F(R"({"cmd":"dependencias","criacao":"%s"})", a.contramare.c_str()));
    std::string tnode;
    for (auto& d : dd.get("dependencias").items()) if (d.get("desatualizada").as_bool()) tnode = d.get("no").as_str();
    CHECK(!tnode.empty(), "editor oferece Travessia r2 à Contramaré");
    Json cmp = W(ws, F(R"({"cmd":"comparar","criacao":"%s","a":1,"b":2})", a.travessia.c_str()));
    CHECK(cmp.get("dependencias").size() == 1, "comparar mostra a dependência alterada (Pulso r1 → r2)");
    Json up = W(ws, F(R"({"cmd":"dependencias.atualizar","criacao":"%s","no":"%s"})", a.contramare.c_str(), tnode.c_str()));
    CHECK(up.has("diferenca"), "atualização mostra a diferença antes de salvar");
    Json sim_new = W(ws, F(R"({"cmd":"simular","criacao":"%s","cenario":%s})", a.contramare.c_str(), cen));
    CHECK(first_event(sim_new, "area.anuncio").get("raio").as_int() == 150, "após aceitar a atualização: raio 1,5 m");
    W(ws, F(R"({"cmd":"dependencias.reverter","criacao":"%s","no":"%s"})", a.contramare.c_str(), tnode.c_str()));
    Json sim_rev = W(ws, F(R"({"cmd":"simular","criacao":"%s","cenario":%s})", a.contramare.c_str(), cen));
    CHECK(first_event(sim_rev, "area.anuncio").get("raio").as_int() == 100, "desfazer a atualização volta a r1");
    std::string d1 = lib.get(a.contramare)->revs.back().digest;

    // Reinício: acervo recarregado do disco
    Library lib2;
    lib2.open(root);
    Workshop ws2(lib2, Rules::duelo(), "Ana");
    CHECK(lib2.creations.size() == lib.creations.size(), "acervo recarregado com as mesmas criações");
    CHECK(lib2.get(a.contramare)->revs.back().digest == d1, "Contramaré r1 mantém o digest após reiniciar");
    Json rev1 = W(ws2, F(R"({"cmd":"revisao.abrir","criacao":"%s","revisao":1})", a.contramare.c_str()));
    Json sim_r = W(ws2, F(R"({"cmd":"simular","criacao":"%s","cenario":%s})", a.contramare.c_str(), cen));
    CHECK(first_event(sim_r, "area.anuncio").get("raio").as_int() == 100, "após reiniciar, a Contramaré antiga ainda reproduz r1");
    W(ws2, F(R"({"cmd":"dependencias.atualizar","criacao":"%s","no":"%s"})", a.contramare.c_str(), tnode.c_str()));
    W(ws2, F(R"({"cmd":"dependencias.reverter","criacao":"%s","no":"%s"})", a.contramare.c_str(), tnode.c_str()));
    Json und = W(ws2, F(R"({"cmd":"desfazer","criacao":"%s"})", a.contramare.c_str()));
    CHECK(und.get("ok").as_bool(), "desfazer funciona depois de reiniciar (histórico persistido)");
    Json rep = Json::obj();
    rep.set("contramare_r1", d1);
    rep.set("raio_r1", 100); rep.set("raio_apos_atualizar", 150); rep.set("raio_apos_desfazer", 100); rep.set("raio_apos_reiniciar", 100);
    g_report.set("teste_revisoes", rep);
}

static void test_invalid(Workshop& ws, Autoria& a) {
    std::printf("[6] Receitas inválidas, operação ausente, versão incompatível e migração\n");
    Json r = W(ws, R"({"cmd":"criar","nome":"Laço"})");
    std::string id = r.get("criacao").as_str();
    Json x = W(ws, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"cadencia.esperar","ligar_de":"n0","porta":"ao_ativar"})", id.c_str()));
    Json y = W(ws, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"cadencia.esperar","ligar_de":"%s","porta":"depois"})", id.c_str(), x.get("no").as_str().c_str()));
    W(ws, F(R"({"cmd":"fluxo.ligar","criacao":"%s","de":"%s","porta":"depois","para":"%s"})", id.c_str(), y.get("no").as_str().c_str(), x.get("no").as_str().c_str()));
    Json s = W(ws, F(R"({"cmd":"salvar","criacao":"%s"})", id.c_str()), false);
    bool cyc = false;
    for (auto& p : s.get("analise").get("problemas").items()) if (p.get("codigo").as_str() == "ciclo_instantaneo") cyc = true;
    CHECK(!s.get("ok").as_bool() && cyc, "ciclo instantâneo é recusado");
    Json st = W(ws, F(R"({"cmd":"abrir","criacao":"%s"})", id.c_str()));
    CHECK(st.get("rascunho").get("comportamento").get("fluxo").size() == 3, "rascunho preservado apesar do erro");

    r = W(ws, R"({"cmd":"criar","nome":"Órbita"})");
    std::string ob = r.get("criacao").as_str();
    Json o = W(ws, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"vetor.orbitar","ligar_de":"n0","porta":"ao_ativar"})", ob.c_str()));
    CHECK(o.has("aviso"), "adicionar operação planejada avisa");
    Json an = o.get("analise");
    std::string miss;
    for (auto& p : an.get("problemas").items()) if (p.get("codigo").as_str() == "operacao_ausente") miss = p.get("msg").as_str();
    CHECK(miss.find("vetor.orbitar") != std::string::npos, "a operação que falta é nomeada: %s", miss.c_str());

    // versão incompatível: pacote com limiar.anel@9
    Json beh = new_behavior();
    Json n = Json::obj(); n.set("id", "n1"); n.set("op", "limiar.anel"); n.set("v", 9); n.set("p", Json::obj());
    beh["nos"].push(n);
    Json f = Json::obj(); f.set("de", "n0"); f.set("porta", "ao_ativar"); f.set("para", "n1"); beh["fluxo"].push(f);
    Json cb = canonical_behavior(beh);
    std::string dg = digest_of(cb);
    Json pk = Json::obj(); pk.set("formato", kFormatoPacote); pk.set("raiz", dg);
    Json objs = Json::obj(); objs.set(dg, cb); pk.set("objetos", objs);
    Json nm = Json::obj(); Json ni = Json::obj(); ni.set("nome", "Do futuro"); ni.set("revisao", 1); ni.set("autor", "Outra"); ni.set("criacao", "cr-x"); nm.set(dg, ni); pk.set("nomes", nm);
    Json cmd = Json::obj(); cmd.set("cmd", "importar"); cmd.set("pacote", pk);
    Json imp = ws.exec(cmd);
    CHECK(imp.get("ok").as_bool(), "importação preserva a receita mesmo com versão desconhecida");
    std::string fut = imp.get("criacoes").at(0).get("criacao").as_str();
    Json an2 = W(ws, F(R"({"cmd":"analisar","criacao":"%s"})", fut.c_str()));
    bool vi = false;
    for (auto& p : an2.get("analise").get("problemas").items()) if (p.get("codigo").as_str() == "versao_incompativel") vi = true;
    CHECK(vi, "versão incompatível é identificada");
    // adulteração
    Json pk2 = pk;
    pk2["objetos"][dg]["nos"].at_mut(1)["p"].set("raio_cm", 999);
    Json cmd2 = Json::obj(); cmd2.set("cmd", "importar"); cmd2.set("pacote", pk2);
    CHECK(!ws.exec(cmd2).get("ok").as_bool(), "pacote adulterado é recusado pelo digest");

    // versão obsoleta (anel@0, por diâmetro): executa por adaptador e migra para nova revisão preservando a original
    Json old = new_behavior();
    Json n0 = Json::obj(); n0.set("id", "n1"); n0.set("op", "limiar.anel"); n0.set("v", 0);
    Json p0 = Json::obj(); p0.set("diametro_cm", 240); n0.set("p", p0);
    old["nos"].push(n0); old["fluxo"].push(f);
    Json cold = canonical_behavior(old);
    std::string dold = digest_of(cold);
    Json pk3 = Json::obj(); pk3.set("formato", kFormatoPacote); pk3.set("raiz", dold);
    Json o3 = Json::obj(); o3.set(dold, cold); pk3.set("objetos", o3);
    Json nm3 = Json::obj(); Json ni3 = ni; ni3.set("nome", "Anel antigo"); nm3.set(dold, ni3); pk3.set("nomes", nm3);
    Json c3 = Json::obj(); c3.set("cmd", "importar"); c3.set("pacote", pk3);
    Json imp3 = ws.exec(c3);
    std::string antigo = imp3.get("criacoes").at(0).get("criacao").as_str();
    Json sim = W(ws, F(R"({"cmd":"simular","criacao":"%s","cenario":{}})", antigo.c_str()));
    CHECK(first_event(sim, "area.anuncio").get("raio").as_int() == 120, "anel@0 (diâmetro 240) executa como raio 120");
    Json mg = W(ws, F(R"({"cmd":"migrar","criacao":"%s"})", antigo.c_str()));
    CHECK(mg.get("migrados").size() == 1, "migração aponta o nó migrado");
    Json sv = W(ws, F(R"({"cmd":"salvar","criacao":"%s","nota":"migrado para anel@1"})", antigo.c_str()));
    CHECK(sv.get("digest").as_str() != dold, "migração gera digest novo");
    CHECK(W(ws, F(R"({"cmd":"historico","criacao":"%s"})", antigo.c_str())).get("revisoes").size() == 2, "revisão original preservada no histórico");
    Json sim2 = W(ws, F(R"({"cmd":"simular","criacao":"%s","cenario":{}})", antigo.c_str()));
    CHECK(first_event(sim2, "area.anuncio").get("raio").as_int() == 120, "comportamento idêntico após migrar");

    // parâmetro fora da faixa é recusado na edição
    Json bad = W(ws, F(R"({"cmd":"no.param","criacao":"%s","no":"n1","nome":"duracao_ms","valor":99999})", a.passo.c_str()), false);
    CHECK(!bad.get("ok").as_bool(), "duração fora da faixa é recusada: %s", bad.get("erro").as_str().c_str());
    // dado sem origem: Retornar sem marca
    r = W(ws, R"({"cmd":"criar","nome":"Volta solta"})");
    std::string vs = r.get("criacao").as_str();
    W(ws, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"vetor.retornar","ligar_de":"n0","porta":"ao_ativar"})", vs.c_str()));
    Json sv2 = W(ws, F(R"({"cmd":"salvar","criacao":"%s"})", vs.c_str()), false);
    CHECK(!sv2.get("ok").as_bool(), "Retornar sem marca não vira revisão");
}

static void test_peak(Workshop& ws) {
    std::printf("[7] Pico simultâneo: duas áreas cabem em sequência e excedem em paralelo\n");
    Json r = W(ws, R"({"cmd":"criar","nome":"Duas marés"})");
    std::string id = r.get("criacao").as_str();
    Json a = W(ws, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"limiar.anel","p":{"raio_cm":300},"rotulo":"Maré A","ligar_de":"n0","porta":"ao_ativar"})", id.c_str()));
    Json b = W(ws, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"limiar.anel","p":{"raio_cm":300},"rotulo":"Maré B","ligar_de":"n0","porta":"ao_ativar"})", id.c_str()));
    Json an = b.get("analise");
    Json cc = an.get("custo_computacional");
    std::printf("    paralelo: pico %lld unidades/tick (limite %lld); cada área de 3 m = %d unidades\n", (long long)cc.get("pico_trabalho").as_int(),
                (long long)cc.get("limite_trabalho").as_int(), area_work_units(300));
    CHECK(cc.get("pico_trabalho").as_int() == 66 && !an.get("executavel").as_bool(), "em paralelo: 33 + 33 = 66 > 64, recusado");
    std::string resp;
    for (auto& p : an.get("problemas").items()) if (p.get("codigo").as_str() == "pico_excedido") resp = p.get("msg").as_str();
    CHECK(resp.find("Maré A") != std::string::npos && resp.find("Maré B") != std::string::npos, "o editor aponta as etapas responsáveis: %s", resp.c_str());
    Json sugs = an.get("sugestoes");
    CHECK(sugs.size() >= 2, "há sugestões que preservam a ideia (%zu)", sugs.size());
    std::string seq_id, raio_id;
    for (auto& s : sugs.items()) {
        std::printf("    sugestão %s: %s\n", s.get("id").as_str().c_str(), s.get("texto").as_str().c_str());
        if (s.get("texto").as_str().find("Encadear") == 0) seq_id = s.get("id").as_str();
        if (s.get("texto").as_str().find("paralelo") != std::string::npos) raio_id = s.get("id").as_str();
    }
    Json st = W(ws, F(R"({"cmd":"abrir","criacao":"%s"})", id.c_str()));
    CHECK(st.get("rascunho").get("comportamento").get("fluxo").size() == 2, "sugestão não entra sem aprovação");
    Json ap = W(ws, F(R"({"cmd":"sugestao.aplicar","criacao":"%s","id":"%s"})", id.c_str(), seq_id.c_str()));
    Json c2 = ap.get("analise").get("custo_computacional");
    std::printf("    em sequência: pico %lld unidades/tick\n", (long long)c2.get("pico_trabalho").as_int());
    CHECK(ap.get("analise").get("executavel").as_bool() && c2.get("pico_trabalho").as_int() == 33, "em sequência cabem (pico 33)");
    W(ws, F(R"({"cmd":"desfazer","criacao":"%s"})", id.c_str()));
    // jogador edita a sugestão de raio antes de aprovar (290 em vez do sugerido)
    Json an3 = W(ws, F(R"({"cmd":"analisar","criacao":"%s"})", id.c_str())).get("analise");
    for (auto& s : an3.get("sugestoes").items()) if (s.get("texto").as_str().find("paralelo") != std::string::npos) raio_id = s.get("id").as_str();
    Json ap2 = W(ws, F(R"({"cmd":"sugestao.aplicar","criacao":"%s","id":"%s","alteracoes":[{"cmd":"no.param","no":"%s","nome":"raio_cm","valor":290},{"cmd":"no.param","no":"%s","nome":"raio_cm","valor":290}]})",
                       id.c_str(), raio_id.c_str(), a.get("no").as_str().c_str(), b.get("no").as_str().c_str()));
    CHECK(ap2.get("analise").get("executavel").as_bool(), "sugestão editada pelo jogador (290 cm) é aplicada e cabe em paralelo");
    Json rep = Json::obj();
    rep.set("unidades_area_3m", area_work_units(300)); rep.set("limite", 64); rep.set("pico_paralelo", 66); rep.set("pico_sequencia", 33);
    rep.set("raio_maximo_paralelo_sugerido", 298);
    g_report.set("teste_pico", rep);
}

static void test_multiplication(Workshop& ws) {
    std::printf("[8] Multiplicação de eventos e geração de recursos\n");
    Json r = W(ws, R"({"cmd":"criar","nome":"Cascata"})");
    std::string id = r.get("criacao").as_str();
    Json rep = W(ws, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"cadencia.repetir","p":{"vezes":8,"intervalo_ms":50},"ligar_de":"n0","porta":"ao_ativar"})", id.c_str()));
    Json an1 = W(ws, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"limiar.anel","p":{"raio_cm":50,"preparo_ms":150},"ligar_de":"%s","porta":"a_cada"})", id.c_str(), rep.get("no").as_str().c_str()));
    Json rep2 = W(ws, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"cadencia.repetir","p":{"vezes":8,"intervalo_ms":50},"ligar_de":"%s","porta":"ao_tocar"})", id.c_str(), an1.get("no").as_str().c_str()));
    Json fin = W(ws, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"limiar.anel","p":{"raio_cm":50,"preparo_ms":150},"ligar_de":"%s","porta":"a_cada"})", id.c_str(), rep2.get("no").as_str().c_str()));
    bool mult = false;
    for (auto& p : fin.get("analise").get("problemas").items()) if (p.get("codigo").as_str() == "multiplicacao_eventos") { mult = true; std::printf("    %s\n", p.get("msg").as_str().c_str()); }
    CHECK(mult, "repetição × contato × repetição é limitada estaticamente");

    r = W(ws, R"({"cmd":"criar","nome":"Fonte"})");
    std::string f = r.get("criacao").as_str();
    Json rp = W(ws, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"cadencia.repetir","p":{"vezes":8,"intervalo_ms":100},"ligar_de":"n0","porta":"ao_ativar"})", f.c_str()));
    Json am = W(ws, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"lastro.amparo","p":{"folego":30},"ligar_de":"%s","porta":"a_cada"})", f.c_str(), rp.get("no").as_str().c_str()));
    bool gen = false;
    for (auto& p : am.get("analise").get("problemas").items()) if (p.get("codigo").as_str() == "geracao_recursos") { gen = true; std::printf("    %s\n", p.get("msg").as_str().c_str()); }
    CHECK(gen, "geração líquida de fôlego é recusada");
}

static void test_room_limits_cancel(Workshop& ws, Autoria& a) {
    std::printf("[9] Limite da sala, cancelamento e saída durante efeito\n");
    Rules r = Rules::arena_livre();
    r.sala_trabalho = 35;  // cada Contramaré reserva 8 un. (área de 1 m): cabem 4 (32), a 5ª passaria de 35
    MatchConfig cfg = MatchConfig::for_mode("livre");
    cfg.regras = r;
    Match m(cfg);
    int64_t t = 1000;
    Json bt = bundle_of(ws, a.contramare);
    std::vector<int> seats;
    for (int i = 0; i < 5; ++i) seats.push_back(m.join("J" + std::to_string(i), "t", t));
    m.start_training(t);
    Json pub = m.publish(bt);
    for (int s : seats) m.equip(s, 0, pub.get("digest").as_str(), t);
    int ok = 0;
    std::string why;
    for (int s : seats) { auto act = m.press(s, 0, false, false, t, t, 0, Vec3()); if (act.ok) ++ok; else why = act.motivo; }
    std::printf("    %d de 5 execuções aceitas; recusa: %s\n", ok, why.c_str());
    CHECK(ok == 4 && why.find("sala no limite de trabalho") != std::string::npos, "orçamento total ativo da sala é aplicado");
    CHECK(m.rt().trabalho_reservado() == 4 * m.plan_of(pub.get("digest").as_str())->pico_trabalho, "trabalho reservado = soma dos picos");
    // cancelamento libera tudo
    t += 200;
    m.tick(t);
    m.cancel(seats[0], t);
    m.tick(t + 1);
    bool canceled_area = false, exec_end = false;
    for (auto& e : m.drain()) {
        if (e.get("tipo").as_str() == "area.cancelada") canceled_area = true;
        if (e.get("tipo").as_str() == "exec.fim" && e.get("cancelada").as_bool()) exec_end = true;
    }
    CHECK(canceled_area && exec_end, "cancelar no preparo remove a área e encerra a execução");
    CHECK(m.rt().execucoes_ativas() == 3, "recurso liberado após cancelar (%d ativas)", m.rt().execucoes_ativas());
    // saída durante efeito: área já em contato resolve; o resto é liberado
    t += 160;  // Pulso entra em contato em 150+200=350 ms
    m.tick(t);
    m.drain();
    m.disconnect(seats[1], t);
    t += 2000;
    m.tick(t);
    bool orphan_end = false;
    for (auto& e : m.drain()) if (e.get("tipo").as_str() == "area.fim") orphan_end = true;
    CHECK(orphan_end, "área comprometida termina normalmente após a saída do autor");
    CHECK(m.rt().execucoes_ativas() == 0, "nenhuma execução sobra após o fim");
}

static void test_duel(Workshop& wa, Workshop& wb, Autoria& a, const std::string& anteparo) {
    std::printf("[10] Duelo até o placar, ponto decisivo e revanche\n");
    Match m(MatchConfig::for_mode("duelo"));
    int64_t t = 1000;
    int sa = m.join("Ana", "ta", t), sb = m.join("Bruno", "tb", t);
    m.start_training(t);
    Json tv = m.publish(bundle_of(wa, a.travessia));
    Json pu = m.publish(bundle_of(wa, a.pulso));
    Json an = m.publish(bundle_of(wb, anteparo));
    m.equip(sa, 0, tv.get("digest").as_str(), t);
    m.equip(sa, 2, pu.get("digest").as_str(), t);
    m.equip(sb, 0, an.get("digest").as_str(), t);
    m.equip(sb, 2, pu.get("digest").as_str(), t);
    std::string train_tv = m.seats()[static_cast<size_t>(sa)].equip.at(0);
    m.set_ready(sa, true, t); m.set_ready(sb, true, t);
    Json sd = m.start_duel(t);
    CHECK(sd.get("ok").as_bool(), "duelo começa (%s)", sd.get("problemas").dump().c_str());
    CHECK(m.seats()[static_cast<size_t>(sa)].equip.at(0) == train_tv, "treino e duelo usam o mesmo digest");
    t += 3000;
    m.tick(t);
    CHECK(m.fase() == "duelo", "contagem termina no duelo");
    // Ana anda até ficar a 300 cm de Bruno, 90 cm de lado do ponto de chegada, e usa Travessia.
    auto walk_to = [&](int seat, Vec3 target) {
        for (int i = 0; i < 400; ++i) {
            const Entity* e = m.rt().ent(m.seats()[static_cast<size_t>(seat)].ent);
            Vec3 d = target - e->pos;
            if (d.len2d() < 4) { m.move(seat, Vec3(), e->yaw, t); return; }
            m.move(seat, d, e->yaw, t);
            t += 16;
            m.tick(t);
        }
    };
    int points = 0;
    for (int round = 0; round < 6 && m.fase() != "fim"; ++round) {
        const Entity* b = m.rt().ent(m.seats()[static_cast<size_t>(sb)].ent);
        Vec3 bp = b->pos;
        double dir = m.rt().ent(m.seats()[static_cast<size_t>(sa)].ent)->pos.x < bp.x ? 1 : -1;
        walk_to(sa, Vec3(bp.x - dir * 300, bp.y - 90, 0));
        const Entity* ae = m.rt().ent(m.seats()[static_cast<size_t>(sa)].ent);
        m.move(sa, Vec3(), dir > 0 ? 0 : 180, t);
        auto act = m.press(sa, 0, false, false, t, t, dir > 0 ? 0 : 180, ae->pos);
        CHECK(act.ok, "Ana ativa Travessia (%s)", act.motivo.c_str());
        for (int k = 0; k < 40; ++k) { t += 16; m.tick(t); }
        for (auto& e : m.drain()) if (e.get("tipo").as_str() == "ponto") ++points;
        t += 1600;
        m.tick(t);
        t += 1100;
        m.tick(t);
    }
    CHECK(m.fase() == "fim" && points == 3, "partida termina pelo placar (%d pontos, fase %s)", points, m.fase().c_str());
    const auto& dec = m.decisive_impacts();
    CHECK(!dec.empty(), "o servidor registra os impactos decisivos");
    if (!dec.empty()) {
        std::printf("    impacto do ponto final (servidor): %s\n", dec.back().get("impacto").dump().c_str());
        g_report.set("duelo_impacto_decisivo_servidor", dec.back());
    }
    m.request_rematch(sa, t);
    Json rr = m.request_rematch(sb, t);
    CHECK(rr.get("iniciada").as_bool() && m.fase() == "contagem", "revanche reinicia com os mesmos equipamentos e lados trocados");
}

static void test_water(Workshop& wa, Workshop& wb, Autoria& a) {
    std::printf("[11] Criação surpresa (Bruno): Esteira muda ao cruzar o canal; previsão x registro do servidor\n");
    // Bruno importa as peças de Ana e combina com operações existentes, sem tocar em código.
    Json cmd;
    Json::parse(F(R"({"cmd":"importar","pacote":%s})", bundle_of(wa, a.passo).dump().c_str()), cmd);
    Json imp = wb.exec(cmd);
    std::string passo_b = imp.get("criacoes").at(0).get("criacao").as_str();
    Json r = W(wb, R"({"cmd":"criar","nome":"Esteira"})");
    std::string id = r.get("criacao").as_str();
    W(wb, F(R"({"cmd":"incorporar","criacao":"%s","alvo":"%s","sobrepor":{},"ligar_de":"n0","porta":"ao_ativar"})", id.c_str(), passo_b.c_str()));
    Json v = W(wb, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"limiar.vigiar","p":{"superficie":"agua","duracao_ms":200},"rotulo":"Vigia do canal","ligar_de":"n0","porta":"ao_ativar"})", id.c_str()));
    Json an = W(wb, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"limiar.anel","p":{"ancora":"evento","raio_cm":120,"preparo_ms":200},"rotulo":"Respingo","ligar_de":"%s","porta":"ao_entrar"})", id.c_str(), v.get("no").as_str().c_str()));
    CHECK(an.get("analise").get("executavel").as_bool(), "Esteira compila");
    W(wb, F(R"({"cmd":"salvar","criacao":"%s"})", id.c_str()));
    // Previsão (antes de executar, por geometria): Bruno em (200,-250) olhando +Y, Passo de 3 m em 150 ms.
    // Cruza a borda do canal (y=-70) a 180/300 = 60%% do trajeto: ~90 ms. Anel em (200,-70), raio 120.
    // Alvo T1 em (300,-70): dist 100 <= 154 -> atingido. Alvo T2 em (200,-250): dist 180 > 154 -> fora.
    Json pred = Json::obj();
    Json pc = Json::arr(); pc.push(200.0); pc.push(-70.0); pc.push(0.0);
    pred.set("centro", pc); pred.set("raio", 120); pred.set("contato_ms_aprox", 90 + 200); pred.set("T1", "atingido"); pred.set("T2", "fora");
    Json sim = W(wb, F(R"({"cmd":"simular","criacao":"%s","cenario":{"autor":{"pos":[200,-250,0],"yaw":90},"alvos":[{"nome":"T1","pos":[300,-70,0]},{"nome":"T2","pos":[200,-250,0]}]}})", id.c_str()));
    Json c = first_event(sim, "area.contato");
    std::string r1, r2;
    for (auto& x : c.get("resultados").items()) { if (x.get("nome").as_str() == "T1") r1 = x.get("resultado").as_str(); if (x.get("nome").as_str() == "T2") r2 = x.get("resultado").as_str(); }
    double err = near(c.get("centro"), 200, -70);
    std::printf("    previsto: centro (200,-70) raio 120 contato ~290 ms T1=atingido T2=fora\n");
    std::printf("    servidor: centro %s raio %lld contato %lld ms T1=%s T2=%s (erro do centro %.1f cm)\n", c.get("centro").dump().c_str(), (long long)c.get("raio").as_int(),
                (long long)c.get("t_rel").as_int(), r1.c_str(), r2.c_str(), err);
    CHECK(err < 1.0 && r1 == "atingido" && r2 == "fora", "previsão confere com o registro do servidor");
    CHECK(std::llabs(c.get("t_rel").as_int() - 290) <= 16, "instante dentro de um tick da previsão");
    // controle: sem cruzar o canal, nenhum anel
    Json sim2 = W(wb, F(R"({"cmd":"simular","criacao":"%s","cenario":{"autor":{"pos":[200,150,0],"yaw":90}}})", id.c_str()));
    CHECK(count_events(sim2, "area.anuncio") == 0, "sem cruzar o canal o anel não aparece");
    Json rep = Json::obj();
    rep.set("previsao", pred);
    Json obs = Json::obj(); obs.set("centro", c.get("centro")); obs.set("raio", c.get("raio")); obs.set("contato_ms", c.get("t_rel")); obs.set("T1", r1); obs.set("T2", r2); obs.set("erro_centro_cm", err);
    rep.set("servidor", obs);
    g_report.set("teste_surpresa_esteira", rep);
}

static void test_determinism_and_ties(Workshop& wa, Autoria& a) {
    std::printf("[12] Determinismo, empate defesa x contato e comando duplicado\n");
    auto run = [&]() {
        Json sim = W(wa, F(R"({"cmd":"simular","criacao":"%s","cenario":{"autor":{"pos":[-600,-250,0],"yaw":0},"alvos":[{"pos":[-300,-160,0]}],"comandos":[{"t":600},{"t":600}]}})", a.contramare.c_str()));
        Json ev = sim.get("eventos");
        return std::make_pair(sha256_hex(ev.dump(true)), sim);
    };
    auto r1 = run();
    auto r2 = run();
    CHECK(r1.first == r2.first, "mesmas entradas produzem o mesmo registro (sha256 %s…)", r1.first.substr(0, 12).c_str());
    int results = count_events(r1.second, "janela.resultado");
    CHECK(results == 1, "comando duplicado no mesmo instante decide a janela uma única vez");
    // empate: anteparo fica ativo no mesmo ms do contato -> absorve (fase 1 antes da fase 2)
    Workshop& w = wa;
    Json g = W(w, R"({"cmd":"criar","nome":"Guarda instantânea"})");
    std::string gid = g.get("criacao").as_str();
    W(w, F(R"({"cmd":"no.adicionar","criacao":"%s","op":"lastro.anteparo","p":{"preparo_ms":200,"abertura_graus":90},"ligar_de":"n0","porta":"ao_ativar"})", gid.c_str()));
    W(w, F(R"({"cmd":"salvar","criacao":"%s"})", gid.c_str()));
    Match m(MatchConfig::for_mode("duelo"));
    int64_t t = 1000;
    int sa = m.join("A", "", t), sb = m.join("B", "", t);
    m.start_training(t);
    Json pg = m.publish(bundle_of(w, gid)), pp = m.publish(bundle_of(w, a.pulso));
    m.equip(sb, 0, pg.get("digest").as_str(), t);
    m.equip(sa, 0, pp.get("digest").as_str(), t);
    Json c = Json::obj(); c.set("cmd", "treino.posicionar"); Json p = Json::arr(); p.push(0.0); p.push(250.0); p.push(0.0); c.set("pos", p); c.set("yaw", 180.0);
    m.training(c, sb, t);
    Json c2 = c; Json p2 = Json::arr(); p2.push(-80.0); p2.push(250.0); p2.push(0.0); c2.set("pos", p2); c2.set("yaw", 0.0);
    m.training(c2, sa, t);
    m.press(sb, 0, false, false, t, t, 180, Vec3());  // ativa em t+200
    m.press(sa, 0, false, false, t, t, 0, Vec3());    // contato em t+200
    m.tick(t + 400);
    std::string res;
    for (auto& e : m.drain()) if (e.get("tipo").as_str() == "area.contato") res = e.get("resultados").at(0).get("resultado").as_str();
    CHECK(res == "absorvido", "no mesmo milissegundo, a defesa vence (%s)", res.c_str());
}

static void test_costs_report(Workshop& wa, Autoria& a) {
    std::printf("[13] Custos (computacional e competitivo) das criações de exemplo\n");
    Json rep = Json::obj();
    for (auto& id : {a.passo, a.pulso, a.travessia, a.contramare}) {
        Json an = W(wa, F(R"({"cmd":"analisar","criacao":"%s"})", id.c_str())).get("analise");
        Json cc = an.get("custo_computacional"), cp = an.get("custo_competitivo");
        std::printf("    %-12s competitivo %5.1f pts (recarga %lld ms, fôlego %lld, alcance %lld cm, preparo mín %lld ms) | pico %lld un/tick, %lld áreas, %lld eventos, %lld B mem, %lld B disco, %lld B rede\n",
                    id.c_str(),
                    an.get("custo_competitivo").get("pontos").as_num(), (long long)an.get("custo_competitivo").get("recarga_ms").as_int(),
                    (long long)an.get("custo_competitivo").get("folego").as_int(), (long long)an.get("custo_competitivo").get("alcance_cm").as_int(),
                    (long long)an.get("custo_competitivo").get("preparo_min_ms").as_int(), (long long)cc.get("pico_trabalho").as_int(),
                    (long long)cc.get("pico_areas").as_int(), (long long)cc.get("eventos").as_int(), (long long)cc.get("memoria_bytes").as_int(),
                    (long long)cc.get("armazenamento_bytes").as_int(), (long long)cc.get("trafego_bytes_binario").as_int());
        rep.set(id, an);
        CHECK(an.get("competitiva_ok").as_bool(), "%s atende aos critérios competitivos", id.c_str());
    }
    g_report.set("custos", rep);
}

int main() {
    std::printf("forja-core: testes\n");
    std::string rootA = tmpdir("ana"), rootB = tmpdir("bruno");
    Library libA, libB;
    libA.open(rootA);
    libB.open(rootB);
    Workshop wa(libA, Rules::duelo(), "Ana"), wb(libB, Rules::duelo(), "Bruno");
    test_hash_json();
    Autoria a = build_authoring(wa);
    test_travessia(wa, a);
    test_contramare(wa, a);
    std::string anteparo;
    test_anteparo(wa, wb, a, anteparo);
    test_costs_report(wa, a);
    test_duel(wa, wb, a, anteparo);
    test_water(wa, wb, a);
    test_determinism_and_ties(wa, a);
    test_peak(wa);
    test_multiplication(wa);
    test_room_limits_cancel(wa, a);
    test_invalid(wa, a);
    test_versions(wa, libA, a, rootA);
    std::printf("\nresultado: %d verificações aprovadas, %d falharam\n", g_pass, g_fail);
    g_report.set("aprovadas", g_pass);
    g_report.set("falhas", g_fail);
    std::error_code ec;
    std::filesystem::create_directories("tests/out", ec);
    write_file_atomic("tests/out/core_report.json", g_report.pretty());
    return g_fail == 0 ? 0 : 1;
}
