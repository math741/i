// Estado observado pelo cliente, reconstruído só a partir de eventos e quadros do servidor.
// Usado pelo renderizador (navegador) e pelos bots (registro do "estado observado nos clientes").
// Regras: deslocamentos são exatos (o evento mov.inicio traz a curva e os instantes); outras posições
// são interpoladas com 100 ms de atraso; a própria posição é prevista localmente e conciliada.

export const ATRASO_INTERP = 100;
export const VEL = 450;

export function bezier(p0, p1, p2, u) {
  const v = 1 - u;
  return [p0[0] * v * v + p1[0] * 2 * v * u + p2[0] * u * u, p0[1] * v * v + p1[1] * 2 * v * u + p2[1] * u * u];
}
export function deLocal(vec, yawGraus) {
  const r = (yawGraus * Math.PI) / 180, c = Math.cos(r), s = Math.sin(r);
  return [vec[0] * c - vec[1] * s, vec[0] * s + vec[1] * c];
}

export class Mundo {
  constructor(con) {
    this.con = con;
    this.ent = new Map();
    this.areas = new Map();
    this.guardas = new Map();
    this.movs = new Map();
    this.janelas = new Map();
    this.marcas = [];
    this.impactos = [];
    this.registro = [];
    this.meuEnt = -1;
    this.fase = 'saguao';
    this.sala = null;
    this.previsoes = new Map(); // slot -> resumo do primeiro deslocamento
    this.previsto = null;        // deslocamento previsto em curso
    this.conciliacoes = [];
    this.entrada = { dir: [0, 0], yaw: 0 };
    this.local = null;           // posição prevista do próprio jogador
    this.ouvintes = [];
    if (con) {
      con.on('ev', (m) => this.aplicarEventos(m));
      con.on('estado', (m) => this.aplicarEstado(m));
      con.on('sala', (m) => { this.sala = m.estado; this.fase = m.estado.fase; const s = m.estado.assentos[m.assento]; if (s) this.meuEnt = s.ent; });
    }
  }

  aoEvento(fn) { this.ouvintes.push(fn); }

  carregarSnapshot(snap) {
    if (!snap) return;
    const mundo = snap.mundo;
    for (const e of mundo.entidades) {
      this.ent.set(e.id, { id: e.id, nome: e.nome, tipo: e.tipo, time: e.time, amostras: [{ s: mundo.t, x: e.pos[0], y: e.pos[1], yaw: e.yaw }], folego: e.folego, recup: e.recup_ate, ativo: e.ativo });
      for (const g of e.guardas) this.guardas.set(g.id, { ...g, ent: e.id });
      if (e.mov) this.movs.set(e.id, { ...e.mov });
    }
    for (const a of mundo.areas) this.areas.set(a.id, { ...a, resultados: [] });
    this.sala = snap.sala;
    this.fase = snap.sala.fase;
    this.registro.push({ local: Date.now(), tipo: 'snapshot.entrada_tardia', areas: mundo.areas.length, guardas: [...this.guardas.keys()].length });
  }

  aplicarEventos(m) {
    for (const e of m.lista) {
      this.registro.push({ recebido_local_ms: Math.round(m._recebido), recebido_servidor_est: Math.round(m._recebido_servidor), atraso_ms: Math.round(m._recebido_servidor - (e.t ?? m._recebido_servidor)), ev: e, observado: this.instantaneo(m._recebido_servidor) });
      this.aplicar(e, m._recebido_servidor);
      for (const f of this.ouvintes) f(e);
    }
  }

  aplicar(e, tServ) {
    switch (e.tipo) {
      case 'entidade.nova':
        if (!this.ent.has(e.ent)) this.ent.set(e.ent, { id: e.ent, amostras: [{ s: e.t, x: e.pos[0], y: e.pos[1], yaw: e.yaw }], ativo: true });
        Object.assign(this.ent.get(e.ent), { nome: e.nome, tipo: e.classe, time: e.time });
        break;
      case 'mov.inicio':
        this.movs.set(e.ent, { de: e.de, ctrl: e.ctrl, para: e.para, t0: e.t0, t1: e.t1, exec: e.exec, rotulo: e.rotulo });
        if (e.ent === this.meuEnt && this.previsto) {
          const p = this.previsto;
          const err = Math.hypot(p.para[0] - e.para[0], p.para[1] - e.para[1]);
          this.conciliacoes.push({ tipo: 'aceita', erro_destino_cm: Math.round(err * 10) / 10, antecipacao_ms: Math.round(e.t0 - p.t0) });
          this.previsto = null;
        }
        break;
      case 'mov.fim':
        this.movs.delete(e.ent);
        if (e.ent === this.meuEnt && this.local) { this.local.x = e.pos[0]; this.local.y = e.pos[1]; }
        break;
      case 'area.anuncio': this.areas.set(e.id, { ...e, resultados: [], fase: 'preparo' }); break;
      case 'area.contato': {
        const a = this.areas.get(e.id);
        if (a) { a.fase = 'contato'; a.centro = e.centro; a.resultados.push(...e.resultados); a.t_contato_real = e.t; }
        break;
      }
      case 'area.fim': { const a = this.areas.get(e.id); if (a) { a.fase = 'fim'; a.t_fim_real = e.t; setTimeout(() => this.areas.delete(e.id), 900); } break; }
      case 'area.cancelada': { const a = this.areas.get(e.id); if (a) { a.fase = 'cancelada'; a.t_fim_real = e.t; setTimeout(() => this.areas.delete(e.id), 700); } break; }
      case 'guarda.inicio': this.guardas.set(e.id, { ...e }); break;
      case 'guarda.absorve': { const g = this.guardas.get(e.id); if (g) { g.ultima_absorcao = e; g.cargas = e.cargas; } this.impactos.push({ ...e, absorvido: true, visto: tServ }); break; }
      case 'guarda.fim': { const g = this.guardas.get(e.id); if (g) { g.fim = e; setTimeout(() => this.guardas.delete(e.id), e.motivo === 'rompida' ? 600 : 250); } break; }
      case 'janela.abre': this.janelas.set(e.id, { ...e }); break;
      case 'janela.resultado': for (const [k, j] of this.janelas) if (j.exec === e.exec && j.no === e.no) { j.resultado = e.resultado; setTimeout(() => this.janelas.delete(k), 500); } break;
      case 'marca': this.marcas.push({ ...e }); if (this.marcas.length > 8) this.marcas.shift(); break;
      case 'impacto': this.impactos.push({ ...e, visto: tServ }); if (this.impactos.length > 16) this.impactos.shift(); break;
      case 'sala.fase': this.fase = e.fase; if (e.fase !== 'duelo' && e.fase !== 'treino') { this.movs.clear(); } break;
      default: break;
    }
  }

  aplicarEstado(m) {
    for (const [id, x, y, yaw, mov, folego, recup] of m.ents) {
      let e = this.ent.get(id);
      if (!e) { e = { id, amostras: [], tipo: 'jogador' }; this.ent.set(id, e); }
      e.amostras.push({ s: m.s, x, y, yaw });
      if (e.amostras.length > 40) e.amostras.shift();
      e.folego = folego; e.recup = recup; e.emMov = !!mov; e.visto = m.s;
      if (id === this.meuEnt) {
        if (!this.local) this.local = { x, y, yaw };
        // conciliação suave: puxa a previsão local para a posição do servidor projetada pela latência
        const lat = (this.con ? this.con.rtt : 0) / 2000;
        const tx = x + this.entrada.dir[0] * VEL * lat, ty = y + this.entrada.dir[1] * VEL * lat;
        const d = Math.hypot(this.local.x - tx, this.local.y - ty);
        if (mov || d > 150) { this.local.x = tx; this.local.y = ty; }
        else { this.local.x += (tx - this.local.x) * 0.25; this.local.y += (ty - this.local.y) * 0.25; }
      }
    }
    this.fase = m.fase || this.fase;
  }

  // posição de uma entidade no instante de servidor tServ, como o cliente a mostra
  posicao(id, tServ) {
    const mv = this.movs.get(id);
    if (mv && tServ >= mv.t0 - 50) {
      const u = Math.max(0, Math.min(1, (tServ - mv.t0) / Math.max(1, mv.t1 - mv.t0)));
      return bezier(mv.de, mv.ctrl, mv.para, u);
    }
    if (id === this.meuEnt && this.previsto) {
      const p = this.previsto;
      const u = Math.max(0, Math.min(1, (tServ - p.t0) / Math.max(1, p.t1 - p.t0)));
      return bezier(p.de, p.ctrl, p.para, u);
    }
    if (id === this.meuEnt && this.local) return [this.local.x, this.local.y];
    const e = this.ent.get(id);
    if (!e || !e.amostras.length) return null;
    const alvo = tServ - ATRASO_INTERP;
    const a = e.amostras;
    if (alvo <= a[0].s) return [a[0].x, a[0].y];
    for (let i = a.length - 1; i > 0; i--) {
      if (a[i - 1].s <= alvo) {
        const u = Math.min(1, (alvo - a[i - 1].s) / Math.max(1, a[i].s - a[i - 1].s));
        return [a[i - 1].x + (a[i].x - a[i - 1].x) * u, a[i - 1].y + (a[i].y - a[i - 1].y) * u];
      }
    }
    const u = a[a.length - 1];
    return [u.x, u.y];
  }
  yaw(id) {
    if (id === this.meuEnt && this.local) return this.entrada.yaw;
    const e = this.ent.get(id);
    return e && e.amostras.length ? e.amostras[e.amostras.length - 1].yaw : 0;
  }

  // previsão local: anda imediatamente com a entrada e antecipa o primeiro deslocamento da criação
  passo(dtMs) {
    if (!this.local || this.previsto || this.movs.has(this.meuEnt)) return;
    const d = this.entrada.dir;
    const l = Math.hypot(d[0], d[1]);
    if (l < 1e-6) return;
    this.local.x = Math.max(-1366, Math.min(1366, this.local.x + (d[0] / l) * VEL * dtMs / 1000));
    this.local.y = Math.max(-416, Math.min(416, this.local.y + (d[1] / l) * VEL * dtMs / 1000));
  }

  preverAtivacao(slot, miraYaw) {
    const pv = this.previsoes.get(slot);
    if (!pv || !this.local || this.movs.has(this.meuEnt)) return null;
    const yaw = pv.espaco === 'mira' ? miraYaw : this.entrada.yaw;
    const d = deLocal(pv.vetor, yaw);
    const de = [this.local.x, this.local.y];
    const para = [Math.max(-1366, Math.min(1366, de[0] + d[0])), Math.max(-416, Math.min(416, de[1] + d[1]))];
    const right = deLocal([0, 1], yaw);
    const mid = [(de[0] + para[0]) / 2 + right[0] * pv.curva * 2, (de[1] + para[1]) / 2 + right[1] * pv.curva * 2];
    const t0 = this.con ? this.con.agoraServidor() : 0;
    this.previsto = { de, ctrl: mid, para, t0, t1: t0 + pv.dur, slot };
    return this.previsto;
  }
  rejeitarPrevisao(motivo) {
    if (!this.previsto) return;
    const p = this.previsto;
    this.conciliacoes.push({ tipo: 'rejeitada', motivo, recuo_cm: Math.round(Math.hypot(p.para[0] - p.de[0], p.para[1] - p.de[1])) });
    if (this.local) { this.local.x = p.de[0]; this.local.y = p.de[1]; }
    this.previsto = null;
  }

  instantaneo(tServ) {
    const o = {};
    for (const [id] of this.ent) {
      const p = this.posicao(id, tServ);
      if (p) o[id] = [Math.round(p[0] * 10) / 10, Math.round(p[1] * 10) / 10];
    }
    return o;
  }
}
