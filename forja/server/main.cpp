// forja_server: servidor dedicado autoritativo do protótipo (Linux/macOS; POSIX).
// Serve o cliente web (HTTP) e o protocolo do jogo (WebSocket, JSON). Hospeda as salas (forja::Match),
// a oficina de cada jogador (forja::Workshop, acervo em disco) e um condicionador de rede por sentido.
//
//   ./forja_server --porta 8787 --cliente ../client --dados ./run/saves --logs ./run/logs [--dev]
//
// Topologia: servidor dedicado (processo próprio). Clientes: navegador ou bots Node em processos separados.
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "forja/hash.h"
#include "forja/match.h"
#include "forja/ops.h"
#include "forja/recipe.h"
#include "forja/workshop.h"

using namespace forja;

static const int kProtocolo = 1;

static int64_t mono_ms() {
    static const auto t0 = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}

struct Options {
    int porta = 8787;
    std::string host = "0.0.0.0";
    std::string cliente = "client";
    std::string dados = "run/saves";
    std::string logs = "run/logs";
    bool dev = false;
    int lag_up = 0, lag_down = 0, jitter = 0;
    double loss_up = 0, loss_down = 0, dup = 0;
    uint32_t seed = 12345;
};

// ---------------- condicionador de rede ----------------
struct Cond {
    int up_ms = 0, down_ms = 0, jitter_ms = 0;
    double loss_up = 0, loss_down = 0, dup = 0;
    int64_t last_rel_in = 0, last_rel_out = 0;
    long dropped_in = 0, dropped_out = 0, retrans_in = 0, retrans_out = 0, duplicated = 0;
    Json to_json() const {
        Json j = Json::obj();
        j.set("lag_subida_ms", up_ms); j.set("lag_descida_ms", down_ms); j.set("jitter_ms", jitter_ms);
        j.set("perda_subida", loss_up); j.set("perda_descida", loss_down); j.set("duplicar", dup);
        j.set("descartadas_subida", static_cast<int64_t>(dropped_in)); j.set("descartadas_descida", static_cast<int64_t>(dropped_out));
        j.set("retransmitidas_subida", static_cast<int64_t>(retrans_in)); j.set("retransmitidas_descida", static_cast<int64_t>(retrans_out));
        j.set("duplicadas", static_cast<int64_t>(duplicated));
        return j;
    }
};

struct Pending { int64_t at; std::string text; };

struct Conn {
    int id = 0;
    int fd = -1;
    bool ws = false;
    bool closing = false;
    std::string in, out;
    std::string frag;
    // jogo
    std::string nome, token, sala;
    int assento = -1;
    Cond cond;
    std::vector<Pending> inq, outq;
    double rtt = 0;
    int64_t last_btn_seq = -1, last_mov_seq = -1, last_cancel_seq = -1;
    std::mt19937 rng;
};

struct PlayerLib {
    std::unique_ptr<Library> lib;
    std::unique_ptr<Workshop> ws;
};

struct Room {
    std::string codigo;
    std::unique_ptr<Match> match;
    std::vector<int> conns;
    FILE* log = nullptr;
    int64_t last_snap = 0;
    std::map<uint32_t, int> exec_owner;  // exec -> ent
    std::string fase_anterior;
};

class Server {
public:
    explicit Server(Options o) : opt_(std::move(o)), rng_(opt_.seed) {}
    bool start();
    void run();
    volatile bool running = true;

private:
    void accept_conns();
    void read_conn(Conn& c);
    void handle_http(Conn& c);
    void handle_frames(Conn& c);
    void ws_send_raw(Conn& c, const std::string& text);
    void send(Conn& c, const Json& msg, bool reliable = true);
    void on_message(Conn& c, const std::string& text);
    void process(Conn& c, const Json& m);
    void close_conn(Conn& c);
    void tick();
    void broadcast_room(Room& r, const std::vector<Json>& evs);
    void send_room_state(Room& r);
    PlayerLib& lib_for(const std::string& nome);
    Room* room_of(Conn& c);
    void log_room(Room& r, const Json& line);

    Options opt_;
    int lfd_ = -1;
    std::map<int, Conn> conns_;
    std::map<std::string, PlayerLib> libs_;
    std::map<std::string, Room> rooms_;
    int next_id_ = 1;
    std::mt19937 rng_;
};

static std::string slug(const std::string& s) {
    std::string o;
    for (unsigned char ch : s) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) o.push_back(static_cast<char>(ch));
        else if (ch >= 'A' && ch <= 'Z') o.push_back(static_cast<char>(ch - 'A' + 'a'));
        else if (ch == '-' || ch == '_') o.push_back('-');
    }
    return o.empty() ? "jogador" : o;
}

bool Server::start() {
    signal(SIGPIPE, SIG_IGN);
    lfd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd_ < 0) { perror("socket"); return false; }
    int one = 1;
    setsockopt(lfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<uint16_t>(opt_.porta));
    inet_pton(AF_INET, opt_.host.c_str(), &a.sin_addr);
    if (bind(lfd_, reinterpret_cast<sockaddr*>(&a), sizeof(a)) < 0) { perror("bind"); return false; }
    if (listen(lfd_, 64) < 0) { perror("listen"); return false; }
    fcntl(lfd_, F_SETFL, O_NONBLOCK);
    make_dirs(opt_.dados);
    make_dirs(opt_.logs);
    std::printf("[forja] servidor dedicado em %s:%d  cliente=%s dados=%s logs=%s%s\n", opt_.host.c_str(), opt_.porta, opt_.cliente.c_str(),
                opt_.dados.c_str(), opt_.logs.c_str(), opt_.dev ? " (modo dev: condicionador de rede liberado)" : "");
    std::fflush(stdout);
    return true;
}

void Server::accept_conns() {
    while (true) {
        int fd = accept(lfd_, nullptr, nullptr);
        if (fd < 0) return;
        fcntl(fd, F_SETFL, O_NONBLOCK);
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        Conn c;
        c.id = next_id_++;
        c.fd = fd;
        c.rng.seed(opt_.seed + static_cast<uint32_t>(c.id) * 7919u);
        c.cond.up_ms = opt_.lag_up; c.cond.down_ms = opt_.lag_down; c.cond.jitter_ms = opt_.jitter;
        c.cond.loss_up = opt_.loss_up; c.cond.loss_down = opt_.loss_down; c.cond.dup = opt_.dup;
        conns_[c.id] = std::move(c);
    }
}

static std::string mime(const std::string& p) {
    auto ends = [&](const char* s) { size_t n = std::strlen(s); return p.size() >= n && p.compare(p.size() - n, n, s) == 0; };
    if (ends(".html")) return "text/html; charset=utf-8";
    if (ends(".js") || ends(".mjs")) return "text/javascript; charset=utf-8";
    if (ends(".css")) return "text/css; charset=utf-8";
    if (ends(".json")) return "application/json";
    if (ends(".png")) return "image/png";
    if (ends(".svg")) return "image/svg+xml";
    return "application/octet-stream";
}

void Server::handle_http(Conn& c) {
    size_t end = c.in.find("\r\n\r\n");
    if (end == std::string::npos) { if (c.in.size() > 65536) c.closing = true; return; }
    std::string head = c.in.substr(0, end);
    c.in.erase(0, end + 4);
    std::string line = head.substr(0, head.find("\r\n"));
    std::string method = line.substr(0, line.find(' '));
    std::string path = line.substr(line.find(' ') + 1);
    path = path.substr(0, path.find(' '));
    std::map<std::string, std::string> h;
    size_t pos = head.find("\r\n");
    while (pos != std::string::npos && pos + 2 < head.size()) {
        size_t nx = head.find("\r\n", pos + 2);
        std::string l = head.substr(pos + 2, nx == std::string::npos ? std::string::npos : nx - pos - 2);
        size_t col = l.find(':');
        if (col != std::string::npos) {
            std::string k = l.substr(0, col);
            for (auto& ch : k) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            std::string v = l.substr(col + 1);
            while (!v.empty() && v[0] == ' ') v.erase(0, 1);
            h[k] = v;
        }
        pos = nx;
    }
    if (h.count("upgrade") && (h["upgrade"] == "websocket" || h["upgrade"] == "WebSocket")) {
        std::string acc = base64(sha1_raw(h["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
        c.out += "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + acc + "\r\n\r\n";
        c.ws = true;
        return;
    }
    if (method != "GET") { c.out += "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"; c.closing = true; return; }
    std::string p = path.substr(0, path.find('?'));
    if (p == "/") p = "/index.html";
    std::string body;
    int code = 200;
    if (p.find("..") != std::string::npos || !read_file(opt_.cliente + p, body)) { code = 404; body = "não encontrado"; }
    char hdr[256];
    std::snprintf(hdr, sizeof(hdr), "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
                  code, code == 200 ? "OK" : "Not Found", code == 200 ? mime(p).c_str() : "text/plain; charset=utf-8", body.size());
    c.out += hdr;
    c.out += body;
    c.closing = true;
}

void Server::ws_send_raw(Conn& c, const std::string& text) {
    std::string f;
    f.push_back(static_cast<char>(0x81));
    size_t n = text.size();
    if (n < 126) f.push_back(static_cast<char>(n));
    else if (n < 65536) { f.push_back(126); f.push_back(static_cast<char>((n >> 8) & 0xFF)); f.push_back(static_cast<char>(n & 0xFF)); }
    else { f.push_back(127); for (int i = 7; i >= 0; --i) f.push_back(static_cast<char>((static_cast<uint64_t>(n) >> (i * 8)) & 0xFF)); }
    c.out += f;
    c.out += text;
}

void Server::send(Conn& c, const Json& msg, bool reliable) {
    if (!c.ws) return;
    std::uniform_real_distribution<double> U(0, 1);
    int64_t now = mono_ms();
    int64_t at = now + c.cond.down_ms + (c.cond.jitter_ms ? static_cast<int64_t>(U(c.rng) * c.cond.jitter_ms) : 0);
    if (c.cond.loss_down > 0) {
        if (!reliable) {
            if (U(c.rng) < c.cond.loss_down) { c.cond.dropped_out++; return; }
        } else {
            // canal confiável sobre perda: cada perda custa uma retransmissão (RTO)
            int64_t rto = std::max<int64_t>(200, 2 * (c.cond.up_ms + c.cond.down_ms));
            while (U(c.rng) < c.cond.loss_down) { at += rto; c.cond.retrans_out++; }
        }
    }
    if (reliable) { at = std::max(at, c.cond.last_rel_out); c.cond.last_rel_out = at; }
    std::string text = msg.dump();
    if (at <= now && c.outq.empty()) ws_send_raw(c, text);
    else c.outq.push_back({at, text});
}

void Server::handle_frames(Conn& c) {
    while (c.in.size() >= 2) {
        unsigned char b0 = static_cast<unsigned char>(c.in[0]), b1 = static_cast<unsigned char>(c.in[1]);
        bool fin = b0 & 0x80;
        int op = b0 & 0x0F;
        bool masked = b1 & 0x80;
        uint64_t len = b1 & 0x7F;
        size_t hp = 2;
        if (len == 126) { if (c.in.size() < 4) return; len = (uint64_t(uint8_t(c.in[2])) << 8) | uint8_t(c.in[3]); hp = 4; }
        else if (len == 127) { if (c.in.size() < 10) return; len = 0; for (int i = 0; i < 8; ++i) len = (len << 8) | uint8_t(c.in[2 + static_cast<size_t>(i)]); hp = 10; }
        if (len > (8u << 20)) { c.closing = true; return; }
        size_t need = hp + (masked ? 4 : 0) + static_cast<size_t>(len);
        if (c.in.size() < need) return;
        std::string payload = c.in.substr(hp + (masked ? 4 : 0), static_cast<size_t>(len));
        if (masked) {
            const char* mk = c.in.data() + hp;
            for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>(payload[i] ^ mk[i % 4]);
        }
        c.in.erase(0, need);
        if (op == 8) { c.closing = true; return; }
        if (op == 9) { std::string f; f.push_back(static_cast<char>(0x8A)); f.push_back(static_cast<char>(payload.size())); c.out += f + payload; continue; }
        if (op == 10) continue;
        if (op == 0 || op == 1 || op == 2) {
            c.frag += payload;
            if (!fin) continue;
            std::string msg;
            msg.swap(c.frag);
            // condicionador: atraso/perda no sentido cliente -> servidor
            Json peek;
            bool unreliable = false;
            if (Json::parse(msg, peek)) unreliable = peek.get("t").as_str() == "in.mover";
            std::uniform_real_distribution<double> U(0, 1);
            int64_t now = mono_ms();
            int64_t at = now + c.cond.up_ms + (c.cond.jitter_ms ? static_cast<int64_t>(U(c.rng) * c.cond.jitter_ms) : 0);
            if (c.cond.loss_up > 0) {
                if (unreliable) { if (U(c.rng) < c.cond.loss_up) { c.cond.dropped_in++; continue; } }
                else { int64_t rto = std::max<int64_t>(200, 2 * (c.cond.up_ms + c.cond.down_ms)); while (U(c.rng) < c.cond.loss_up) { at += rto; c.cond.retrans_in++; } }
            }
            if (!unreliable) { at = std::max(at, c.cond.last_rel_in); c.cond.last_rel_in = at; }
            c.inq.push_back({at, msg});
            if (!unreliable && c.cond.dup > 0 && U(c.rng) < c.cond.dup && peek.get("t").as_str().rfind("in.", 0) == 0) {
                c.cond.duplicated++;
                c.inq.push_back({at + 5, msg});
            }
        }
    }
}

void Server::read_conn(Conn& c) {
    char buf[65536];
    while (true) {
        ssize_t n = recv(c.fd, buf, sizeof(buf), 0);
        if (n > 0) { c.in.append(buf, static_cast<size_t>(n)); continue; }
        if (n == 0) { c.closing = true; }
        break;
    }
    if (!c.ws) handle_http(c);
    if (c.ws) handle_frames(c);
}

PlayerLib& Server::lib_for(const std::string& nome) {
    auto it = libs_.find(nome);
    if (it != libs_.end()) return it->second;
    PlayerLib pl;
    pl.lib = std::make_unique<Library>();
    pl.lib->open(opt_.dados + "/" + slug(nome));
    pl.ws = std::make_unique<Workshop>(*pl.lib, Rules::duelo(), nome);
    return libs_[nome] = std::move(pl);
}

Room* Server::room_of(Conn& c) {
    auto it = rooms_.find(c.sala);
    return it == rooms_.end() ? nullptr : &it->second;
}

void Server::log_room(Room& r, const Json& line) {
    if (!r.log) return;
    std::string s = line.dump();
    std::fwrite(s.data(), 1, s.size(), r.log);
    std::fputc('\n', r.log);
    std::fflush(r.log);
}

void Server::send_room_state(Room& r) {
    Json st = r.match->room_state();
    st.set("codigo", r.codigo);
    for (int id : r.conns) {
        auto it = conns_.find(id);
        if (it == conns_.end()) continue;
        Json m = Json::obj(); m.set("t", "sala"); m.set("estado", st); m.set("assento", it->second.assento);
        send(it->second, m);
    }
}

void Server::process(Conn& c, const Json& m) {
    std::string t = m.get("t").as_str();
    int64_t now = mono_ms();
    auto reply = [&](const char* type, Json body) { body.set("t", type); if (m.has("req")) body.set("req", m.get("req")); send(c, body); };
    if (t == "ping") { Json b = Json::obj(); b.set("c", m.get("c")); b.set("s", now); reply("pong", b); return; }
    if (t == "rtt") { c.rtt = c.rtt <= 0 ? m.get("ms").as_num() : 0.8 * c.rtt + 0.2 * m.get("ms").as_num(); return; }
    if (t == "ola") {
        if (m.get("versao").as_int(kProtocolo) != kProtocolo) { Json b = Json::obj(); b.set("erro", "versão de protocolo incompatível"); reply("erro", b); c.closing = true; return; }
        c.nome = m.get("nome").str_or("jogador");
        c.token = m.get("token").str_or(sha256_hex(c.nome + std::to_string(now) + std::to_string(c.id)).substr(0, 16));
        lib_for(c.nome);
        Json b = Json::obj();
        b.set("nome", c.nome); b.set("token", c.token); b.set("servidor_t", now); b.set("protocolo", kProtocolo);
        b.set("arena", Arena::galeria().to_json()); b.set("catalogo", ops_catalog()); b.set("dev", opt_.dev);
        b.set("topologia", "servidor dedicado (processo próprio)");
        reply("bemvindo", b);
        return;
    }
    if (c.nome.empty()) { Json b = Json::obj(); b.set("erro", "envie 'ola' primeiro"); reply("erro", b); return; }
    if (t == "oficina") {
        PlayerLib& pl = lib_for(c.nome);
        Json r = pl.ws->exec(m.get("cmd"));
        Json b = Json::obj(); b.set("r", r);
        reply("oficina.r", b);
        return;
    }
    if (t == "rede") {
        Room* r0 = room_of(c);
        bool allowed = opt_.dev || (r0 && r0->match->fase() == "treino");
        if (!allowed) { Json b = Json::obj(); b.set("ok", false); b.set("erro", "condicionador só no treino ou com --dev"); reply("rede.r", b); return; }
        if (m.has("lag_subida_ms")) c.cond.up_ms = static_cast<int>(m.get("lag_subida_ms").as_int());
        if (m.has("lag_descida_ms")) c.cond.down_ms = static_cast<int>(m.get("lag_descida_ms").as_int());
        if (m.has("jitter_ms")) c.cond.jitter_ms = static_cast<int>(m.get("jitter_ms").as_int());
        if (m.has("perda_subida")) c.cond.loss_up = m.get("perda_subida").as_num();
        if (m.has("perda_descida")) c.cond.loss_down = m.get("perda_descida").as_num();
        if (m.has("duplicar")) c.cond.dup = m.get("duplicar").as_num();
        Json b = Json::obj(); b.set("ok", true); b.set("rede", c.cond.to_json());
        reply("rede.r", b);
        if (r0) { Json l = Json::obj(); l.set("log", "rede"); l.set("t", now); l.set("jogador", c.nome); l.set("rede", c.cond.to_json()); log_room(*r0, l); }
        return;
    }
    if (t == "sala.criar" || t == "sala.entrar") {
        std::string code;
        if (t == "sala.criar") {
            static const char* L = "ABCDEFGHJKLMNPQRSTUVWXYZ";
            do { code.clear(); for (int i = 0; i < 4; ++i) code.push_back(L[rng_() % 24]); } while (rooms_.count(code));
            Room rm;
            rm.codigo = code;
            rm.match = std::make_unique<Match>(MatchConfig::for_mode(m.get("modo").str_or("duelo")));
            rm.match->rt().advance(now);
            std::string lp = opt_.logs + "/sala-" + code + ".jsonl";
            rm.log = std::fopen(lp.c_str(), "w");
            rooms_[code] = std::move(rm);
            std::printf("[forja] sala privada %s criada por %s (log %s)\n", code.c_str(), c.nome.c_str(), lp.c_str());
            std::fflush(stdout);
        } else {
            code = m.get("codigo").as_str();
            for (auto& ch : code) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if (!rooms_.count(code)) { Json b = Json::obj(); b.set("ok", false); b.set("erro", "sala inexistente"); reply("sala.r", b); return; }
        }
        Room& r = rooms_[code];
        int seat = r.match->join(c.nome, c.token, now);
        if (seat < 0) { Json b = Json::obj(); b.set("ok", false); b.set("erro", "nome em uso nesta sala por outra pessoa"); reply("sala.r", b); return; }
        c.sala = code;
        c.assento = seat;
        bool already = false;
        for (int id : r.conns) if (id == c.id) already = true;
        if (!already) r.conns.push_back(c.id);
        Json b = Json::obj();
        b.set("ok", true); b.set("codigo", code); b.set("assento", seat);
        b.set("snapshot", r.match->late_join_snapshot());
        reply("sala.r", b);
        Json l = Json::obj(); l.set("log", "entrada"); l.set("t", now); l.set("jogador", c.nome); l.set("assento", seat); log_room(r, l);
        send_room_state(r);
        return;
    }
    Room* r = room_of(c);
    if (!r) { Json b = Json::obj(); b.set("ok", false); b.set("erro", "entre numa sala primeiro"); reply("erro", b); return; }
    Match& mt = *r->match;
    if (t == "in.mover") {
        int64_t seq = m.get("seq").as_int();
        if (seq <= c.last_mov_seq) return;  // pacote antigo/reordenado
        c.last_mov_seq = seq;
        const Json& d = m.get("dir");
        mt.move(c.assento, Vec3(d.at(0).as_num(), d.at(1).as_num(), 0), m.get("yaw").as_num(), now);
        return;
    }
    if (t == "in.botao") {
        int64_t seq = m.get("seq").as_int();
        if (seq <= c.last_btn_seq) {
            Json b = Json::obj(); b.set("seq", seq); b.set("duplicado", true); b.set("ok", false); b.set("motivo", "comando duplicado ignorado (seq já processado)");
            reply("in.r", b);
            Json l = Json::obj(); l.set("log", "entrada.duplicada"); l.set("t", now); l.set("jogador", c.nome); l.set("seq", seq); log_room(*r, l);
            return;
        }
        c.last_btn_seq = seq;
        int64_t tcli = m.has("t_est") ? m.get("t_est").as_int() : Runtime::kSemRelogio;
        std::string tipo = m.get("tipo").str_or("pressionar");
        const Json& aim = m.get("mira");
        Runtime::Act a = mt.press(c.assento, static_cast<int>(m.get("slot").as_int()), tipo == "soltar", tipo == "secundario", tcli, now,
                                  m.get("mira_yaw").as_num(), Vec3(aim.at(0).as_num(), aim.at(1).as_num(), aim.at(2).as_num()));
        Json b = Json::obj();
        b.set("seq", seq); b.set("ok", a.ok); b.set("exec", a.exec); b.set("motivo", a.motivo); b.set("foi_comando", a.foi_comando);
        b.set("t_servidor", now); b.set("t_julgado", a.t_julgado); b.set("t_estimado", m.get("t_est"));
        reply("in.r", b);
        Json l = Json::obj(); l.set("log", "entrada"); l.set("t", now); l.set("jogador", c.nome); l.set("seq", seq); l.set("slot", m.get("slot"));
        l.set("tipo", tipo); l.set("t_est", m.get("t_est")); l.set("t_julgado", a.t_julgado); l.set("rtt", c.rtt); l.set("aceita", a.ok); l.set("motivo", a.motivo); l.set("exec", a.exec);
        log_room(*r, l);
        return;
    }
    if (t == "in.cancelar") {
        int64_t seq = m.get("seq").as_int();
        if (seq <= c.last_cancel_seq) return;
        c.last_cancel_seq = seq;
        bool any = mt.cancel(c.assento, now);
        Json b = Json::obj(); b.set("seq", seq); b.set("ok", any); reply("in.r", b);
        return;
    }
    if (t == "sala.pronto") { mt.set_ready(c.assento, m.get("valor").as_bool(true), now); Json b = Json::obj(); b.set("ok", true); reply("sala.r", b); send_room_state(*r); return; }
    if (t == "sala.modo") {
        Json b = Json::obj();
        if (c.assento != 0) { b.set("ok", false); b.set("erro", "só quem criou a sala muda o modo"); }
        else if (mt.fase() != "saguao" && mt.fase() != "treino" && mt.fase() != "fim") { b.set("ok", false); b.set("erro", "partida em andamento"); }
        else { mt.set_mode(m.get("modo").str_or("duelo")); b.set("ok", true); }
        reply("sala.r", b);
        send_room_state(*r);
        return;
    }
    if (t == "sala.treino") { Json b = mt.start_training(now); reply("sala.r", b); send_room_state(*r); return; }
    if (t == "sala.duelo") { Json b = mt.start_duel(now); reply("sala.r", b); send_room_state(*r); return; }
    if (t == "sala.revanche") { Json b = mt.request_rematch(c.assento, now); reply("sala.r", b); send_room_state(*r); return; }
    if (t == "sala.sair") { close_conn(c); return; }
    if (t == "publicar") {
        Json b = mt.publish(m.get("pacote"));
        reply("publicar.r", b);
        return;
    }
    if (t == "equipar") {
        PlayerLib& pl = lib_for(c.nome);
        Json b;
        std::string digest = m.get("digest").as_str();
        if (digest.empty() && m.has("criacao")) {
            Creation* cr = pl.lib->get(m.get("criacao").as_str());
            if (!cr || cr->revs.empty()) { b = Json::obj(); b.set("ok", false); b.set("erro", "criação sem revisão salva"); reply("equipar.r", b); return; }
            const RevRecord* rr = &cr->revs.back();
            if (m.has("revisao")) for (auto& x : cr->revs) if (x.rev == m.get("revisao").as_int()) rr = &x;
            digest = rr->digest;
        }
        if (!digest.empty()) {
            // o cliente web guarda o acervo no servidor: publica a partir dele pelo mesmo caminho de validação
            Json pub = mt.publish(pl.lib->bundle(digest));
            if (!pub.get("ok").as_bool()) { pub.set("t", "equipar.r"); reply("equipar.r", pub); return; }
        }
        b = mt.equip(c.assento, static_cast<int>(m.get("slot").as_int()), digest, now);
        if (auto p = mt.plan_of(digest)) {
            // resumo para previsão no cliente: primeiro deslocamento da cadeia inicial
            for (auto& n : p->nodes) {
                if (!n.alcancavel || n.kind != OpKind::Deslocar) continue;
                if (n.smin != 0 || n.smax != 0) break;
                Json pv = Json::obj();
                Json v = Json::arr(); v.push(n.vec.x); v.push(n.vec.y); v.push(n.vec.z);
                pv.set("vetor", v); pv.set("dur", n.dur); pv.set("curva", n.curva);
                pv.set("espaco", n.espaco == Espaco::Mira ? "mira" : (n.espaco == Espaco::Ativacao ? "ativacao" : "autor"));
                b.set("previsao", pv);
                break;
            }
            b.set("nome", m.get("nome"));
        }
        reply("equipar.r", b);
        send_room_state(*r);
        return;
    }
    if (t == "treino") {
        Json b = mt.training(m.get("cmd"), c.assento, now);
        reply("treino.r", b);
        return;
    }
    if (t == "receita") {
        Json b = Json::obj();
        PlayerLib& pl = lib_for(c.nome);
        Json bundle = pl.lib->bundle(m.get("digest").as_str());
        b.set("pacote", bundle);
        reply("receita.r", b);
        return;
    }
    Json b = Json::obj(); b.set("erro", "mensagem desconhecida '" + t + "'"); reply("erro", b);
}

void Server::on_message(Conn& c, const std::string& text) {
    Json m;
    std::string err;
    if (!Json::parse(text, m, &err)) { Json b = Json::obj(); b.set("t", "erro"); b.set("erro", "JSON inválido: " + err); send(c, b); return; }
    process(c, m);
}

void Server::close_conn(Conn& c) {
    if (Room* r = room_of(c)) {
        int64_t now = mono_ms();
        r->match->disconnect(c.assento, now);
        r->conns.erase(std::remove(r->conns.begin(), r->conns.end(), c.id), r->conns.end());
        Json l = Json::obj(); l.set("log", "desconexao"); l.set("t", now); l.set("jogador", c.nome); l.set("rede", c.cond.to_json()); log_room(*r, l);
        send_room_state(*r);
    }
    c.sala.clear();
    c.closing = true;
}

void Server::broadcast_room(Room& r, const std::vector<Json>& evs) {
    if (evs.empty()) return;
    for (auto& e : evs) {
        if (e.get("tipo").as_str() == "exec.inicio") r.exec_owner[static_cast<uint32_t>(e.get("exec").as_int())] = static_cast<int>(e.get("autor").as_int());
        log_room(r, e);
    }
    for (int id : r.conns) {
        auto it = conns_.find(id);
        if (it == conns_.end()) continue;
        Conn& c = it->second;
        const Seat* s = r.match->seat(c.assento);
        int my_ent = s ? s->ent : -1;
        Json list = Json::arr();
        for (auto& e0 : evs) {
            std::string tp = e0.get("tipo").as_str();
            Json e = e0;
            if (tp == "exec.inicio") e.set("aparencia", r.match->aparencia_of(e.get("digest").as_str()));
            // janelas e comandos são privados do autor: o oponente lê o corpo e as áreas, não o plano de comandos
            if (tp.rfind("cmd.", 0) == 0 || tp.rfind("janela.", 0) == 0) {
                int owner = tp == "cmd.ignorado" ? static_cast<int>(e.get("ent").as_int()) : r.exec_owner[static_cast<uint32_t>(e.get("exec").as_int())];
                if (owner != my_ent) continue;
            }
            list.push(e);
        }
        if (list.size() == 0) continue;
        Json m = Json::obj(); m.set("t", "ev"); m.set("lista", list);
        send(c, m);
    }
}

void Server::tick() {
    int64_t now = mono_ms();
    // entrega mensagens atrasadas pelo condicionador
    for (auto& kv : conns_) {
        Conn& c = kv.second;
        std::vector<Pending> due;
        for (auto it = c.inq.begin(); it != c.inq.end();) {
            if (it->at <= now) { due.push_back(*it); it = c.inq.erase(it); } else ++it;
        }
        std::stable_sort(due.begin(), due.end(), [](const Pending& a, const Pending& b) { return a.at < b.at; });
        for (auto& p : due) on_message(c, p.text);
    }
    for (auto& kv : rooms_) {
        Room& r = kv.second;
        r.match->tick(now);
        std::vector<Json> evs = r.match->drain();
        bool phase_change = false;
        for (auto& e : evs) {
            std::string tp = e.get("tipo").as_str();
            if (tp == "sala.fase" || tp == "ponto" || tp == "equipado" || tp == "sala.entrada" || tp == "revanche.pedido") phase_change = true;
        }
        broadcast_room(r, evs);
        if (phase_change) send_room_state(r);
        if (now - r.last_snap >= 50) {
            r.last_snap = now;
            Json ents = Json::arr();
            for (auto& e : r.match->rt().entities()) {
                if (!e.ativo) continue;
                Json x = Json::arr();
                x.push(e.id); x.push(std::round(e.pos.x * 10) / 10); x.push(std::round(e.pos.y * 10) / 10); x.push(std::round(e.yaw * 10) / 10);
                x.push(e.em_mov ? 1 : 0); x.push(static_cast<int64_t>(e.folego)); x.push(e.recup_ate);
                ents.push(x);
            }
            for (int id : r.conns) {
                auto it = conns_.find(id);
                if (it == conns_.end()) continue;
                Json m = Json::obj();
                m.set("t", "estado"); m.set("s", now); m.set("ents", ents); m.set("ack", it->second.last_mov_seq); m.set("fase", r.match->fase());
                send(it->second, m, false);
            }
        }
    }
    for (auto& kv : conns_) {
        Conn& c = kv.second;
        if (c.outq.empty()) continue;
        size_t k = 0;
        // mantém ordem de envio; mensagens não confiáveis podem sair antes se o atraso for menor
        std::stable_sort(c.outq.begin(), c.outq.end(), [](const Pending& a, const Pending& b) { return a.at < b.at; });
        while (k < c.outq.size() && c.outq[k].at <= now) { ws_send_raw(c, c.outq[k].text); ++k; }
        c.outq.erase(c.outq.begin(), c.outq.begin() + static_cast<long>(k));
    }
}

void Server::run() {
    int64_t next_tick = mono_ms();
    while (running) {
        std::vector<pollfd> fds;
        fds.push_back({lfd_, POLLIN, 0});
        std::vector<int> ids;
        for (auto& kv : conns_) {
            short ev = POLLIN;
            if (!kv.second.out.empty()) ev |= POLLOUT;
            fds.push_back({kv.second.fd, ev, 0});
            ids.push_back(kv.first);
        }
        int64_t wait = std::max<int64_t>(0, next_tick - mono_ms());
        poll(fds.data(), fds.size(), static_cast<int>(wait));
        if (fds[0].revents & POLLIN) accept_conns();
        for (size_t i = 1; i < fds.size(); ++i) {
            auto it = conns_.find(ids[i - 1]);
            if (it == conns_.end()) continue;
            Conn& c = it->second;
            if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) read_conn(c);
        }
        if (mono_ms() >= next_tick) {
            tick();
            next_tick += 1000 / 60;
            if (mono_ms() > next_tick + 100) next_tick = mono_ms();
        }
        for (auto it = conns_.begin(); it != conns_.end();) {
            Conn& c = it->second;
            while (!c.out.empty()) {
                ssize_t n = ::send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
                if (n <= 0) break;
                c.out.erase(0, static_cast<size_t>(n));
            }
            if (c.closing && (c.out.empty() || !c.ws)) {
                if (!c.sala.empty()) close_conn(c);
                ::close(c.fd);
                it = conns_.erase(it);
                continue;
            }
            ++it;
        }
    }
    for (auto& kv : rooms_) if (kv.second.log) std::fclose(kv.second.log);
}

static Server* g_server = nullptr;
static void on_sig(int) { if (g_server) g_server->running = false; }

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--porta") o.porta = std::atoi(next().c_str());
        else if (a == "--host") o.host = next();
        else if (a == "--cliente") o.cliente = next();
        else if (a == "--dados") o.dados = next();
        else if (a == "--logs") o.logs = next();
        else if (a == "--dev") o.dev = true;
        else if (a == "--lag-subida") o.lag_up = std::atoi(next().c_str());
        else if (a == "--lag-descida") o.lag_down = std::atoi(next().c_str());
        else if (a == "--jitter") o.jitter = std::atoi(next().c_str());
        else if (a == "--perda-subida") o.loss_up = std::atof(next().c_str());
        else if (a == "--perda-descida") o.loss_down = std::atof(next().c_str());
        else if (a == "--duplicar") o.dup = std::atof(next().c_str());
        else if (a == "--semente") o.seed = static_cast<uint32_t>(std::strtoul(next().c_str(), nullptr, 10));
        else { std::fprintf(stderr, "opção desconhecida: %s\n", a.c_str()); return 2; }
    }
    Server s(o);
    g_server = &s;
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    if (!s.start()) return 1;
    s.run();
    std::printf("[forja] servidor encerrado\n");
    return 0;
}
