// Conexão com o servidor dedicado. Mesmo código no navegador e nos bots de teste (Node 22+).
// - pedir(): mensagem com resposta (req/resposta)
// - relógio: estimativa de offset e RTT por ping; agoraServidor() dá o instante estimado do servidor
// - botao(): entrada confiável com seq (o servidor descarta duplicadas) e t_est (compensação limitada)
// - mover(): entrada não confiável (pode ser perdida; só a mais nova vale)

const now = () => (globalThis.performance ? performance.now() : Date.now());

export class Conexao {
  constructor(url) {
    this.url = url;
    this.ws = null;
    this.req = 1;
    this.pend = new Map();
    this.seqBotao = 0;
    this.seqMover = 0;
    this.seqCancel = 0;
    this.offset = 0;
    this.rtt = 0;
    this.amostras = [];
    this.ouvintes = { ev: [], estado: [], sala: [], fechou: [], qualquer: [] };
    this.nome = '';
    this.token = '';
    this.bemvindo = null;
    this.botoesPendentes = new Map();
    this._pinger = null;
  }

  on(tipo, fn) { (this.ouvintes[tipo] ||= []).push(fn); return () => { this.ouvintes[tipo] = this.ouvintes[tipo].filter((f) => f !== fn); }; }
  _emit(tipo, x) { for (const f of this.ouvintes[tipo] || []) f(x); }

  conectar(nome, token) {
    this.nome = nome;
    return new Promise((ok, falha) => {
      const ws = new WebSocket(this.url);
      this.ws = ws;
      ws.onopen = async () => {
        try {
          const b = await this.pedir('ola', { nome, token, versao: 1 });
          this.bemvindo = b;
          this.token = b.token;
          this._sincronizar(b);
          ok(b);
        } catch (e) { falha(e); }
      };
      ws.onerror = (e) => falha(new Error('falha na conexão WebSocket: ' + (e.message || '')));
      ws.onclose = () => {
        clearInterval(this._pinger);
        for (const [, p] of this.pend) p.falha(new Error('conexão fechada'));
        this.pend.clear();
        this._emit('fechou', null);
      };
      ws.onmessage = (m) => this._receber(m.data);
    });
  }

  fechar() { clearInterval(this._pinger); if (this.ws) this.ws.close(); }

  _sincronizar(b) {
    const ping = () => this.enviar('ping', { c: now() });
    for (let i = 0; i < 5; i++) setTimeout(ping, i * 60);
    this._pinger = setInterval(ping, 500);
  }

  _receber(texto) {
    const m = JSON.parse(texto);
    const t = now();
    if (m.t === 'pong') {
      const rtt = t - m.c;
      this.amostras.push({ rtt, off: m.s + rtt / 2 - t });
      if (this.amostras.length > 12) this.amostras.shift();
      const melhor = this.amostras.reduce((a, b) => (b.rtt < a.rtt ? b : a));
      this.offset = melhor.off;
      this.rtt = this.rtt ? 0.8 * this.rtt + 0.2 * rtt : rtt;
      this.enviar('rtt', { ms: Math.round(this.rtt) });
      return;
    }
    m._recebido = t;
    m._recebido_servidor = this.agoraServidor();
    if (m.req && this.pend.has(m.req)) {
      const p = this.pend.get(m.req);
      this.pend.delete(m.req);
      p.ok(m);
    }
    if (m.t === 'ev') this._emit('ev', m);
    else if (m.t === 'estado') this._emit('estado', m);
    else if (m.t === 'sala') this._emit('sala', m);
    else if (m.t === 'in.r' && this.botoesPendentes.has(m.seq)) {
      const p = this.botoesPendentes.get(m.seq);
      this.botoesPendentes.delete(m.seq);
      p(m);
    }
    this._emit('qualquer', m);
  }

  agoraServidor() { return now() + this.offset; }

  enviar(t, corpo = {}) {
    if (!this.ws || this.ws.readyState !== 1) return false;
    this.ws.send(JSON.stringify({ t, ...corpo }));
    return true;
  }

  pedir(t, corpo = {}, timeoutMs = 15000) {
    const req = this.req++;
    return new Promise((ok, falha) => {
      const to = setTimeout(() => { this.pend.delete(req); falha(new Error(`sem resposta para ${t}`)); }, timeoutMs);
      this.pend.set(req, { ok: (m) => { clearTimeout(to); ok(m); }, falha: (e) => { clearTimeout(to); falha(e); } });
      if (!this.enviar(t, { ...corpo, req })) { clearTimeout(to); this.pend.delete(req); falha(new Error('desconectado')); }
    });
  }

  async oficina(cmd) {
    const r = await this.pedir('oficina', { cmd });
    return r.r;
  }

  botao(slot, tipo = 'pressionar', miraYaw = 0, mira = [0, 0, 0], seqForcado) {
    const seq = seqForcado ?? ++this.seqBotao;
    const t_est = Math.round(this.agoraServidor());
    const p = new Promise((ok) => this.botoesPendentes.set(seq, ok));
    this.enviar('in.botao', { seq, slot, tipo, t_est, mira_yaw: miraYaw, mira });
    return { seq, t_est, resposta: p };
  }

  mover(dir, yaw) {
    const seq = ++this.seqMover;
    this.enviar('in.mover', { seq, dir, yaw });
    return seq;
  }

  cancelar() { this.enviar('in.cancelar', { seq: ++this.seqCancel }); }
}
