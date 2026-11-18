// Processo de um jogador (bot). Usa exatamente os módulos do cliente web (net.js e modelo.js).
// Recebe ações do orquestrador por IPC e responde com o resultado. Cada jogador é um processo do SO.
import { writeFileSync } from 'node:fs';
import { Conexao } from '../../client/js/net.js';
import { Mundo } from '../../client/js/modelo.js';

let con = null;
let mundo = null;
let nome = '';
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function novoMundo() {
  mundo = new Mundo(con);
  con.on('qualquer', (m) => {
    if (m.t === 'equipar.r' && m.ok && m.previsao) mundo.previsoes.set(m._slot ?? -1, m.previsao);
  });
}

const acoes = {
  async conectar({ url, nome: n, token }) {
    nome = n;
    con = new Conexao(url);
    const b = await con.conectar(n, token);
    novoMundo();
    await sleep(350); // amostras de relógio
    return { token: b.token, rtt: con.rtt, offset: con.offset, topologia: b.topologia };
  },
  async pedir({ t, corpo }) {
    const r = await con.pedir(t, corpo || {});
    if (t === 'sala.criar' || t === 'sala.entrar') { if (r.ok) mundo.carregarSnapshot(r.snapshot); }
    delete r._recebido; delete r._recebido_servidor;
    if (r.t === 'sala.r' && r.snapshot) { r.snapshot = { resumo: true, areas: r.snapshot.mundo.areas.length, guardas: r.snapshot.mundo.entidades.reduce((a, e) => a + e.guardas.length, 0), fase: r.snapshot.sala.fase } }
    return r;
  },
  async oficina({ cmd }) { return await con.oficina(cmd); },
  async equipar({ slot, criacao, revisao }) {
    const r = await con.pedir('equipar', { slot, criacao, revisao });
    if (r.ok && r.previsao) mundo.previsoes.set(slot, r.previsao);
    delete r._recebido; delete r._recebido_servidor;
    return r;
  },
  // pressiona botões em instantes relativos (relógio local do processo do jogador)
  async sequencia({ passos }) {
    const t0 = performance.now();
    const res = [];
    for (const p of passos) {
      const espera = t0 + p.em - performance.now();
      if (espera > 0) await sleep(espera);
      if (p.rede) { con.enviar('rede', p.rede); res.push({ em: p.em, rede: p.rede, real_ms: Math.round(performance.now() - t0) }); continue; }
      if (p.tipo !== 'soltar' && p.prever) mundo.preverAtivacao(p.slot, p.mira_yaw || 0);
      const b = con.botao(p.slot, p.tipo || 'pressionar', p.mira_yaw || 0, p.mira || [0, 0, 0]);
      res.push({ em: p.em, real_ms: Math.round(performance.now() - t0), seq: b.seq, t_est: b.t_est, r: b.resposta });
    }
    for (const x of res) {
      if (!x.r) continue;
      const m = await Promise.race([x.r, sleep(4000).then(() => ({ timeout: true }))]);
      if (m && !m.ok && !m.foi_comando) mundo.rejeitarPrevisao(m.motivo);
      x.r = m ? { ok: m.ok, exec: m.exec, motivo: m.motivo, foi_comando: m.foi_comando, t_servidor: m.t_servidor, t_julgado: m.t_julgado, duplicado: m.duplicado } : null;
    }
    return res;
  },
  async botaoRepetido({ slot, seq }) {
    // envia o mesmo seq duas vezes (retransmissão do cliente) para testar a deduplicação no servidor
    const vistas = [];
    const off = con.on('qualquer', (m) => { if (m.t === 'in.r' && m.seq === seq) vistas.push({ ok: m.ok, duplicado: !!m.duplicado, motivo: m.motivo }); });
    con.botao(slot, 'pressionar', 0, [0, 0, 0], seq);
    con.botao(slot, 'pressionar', 0, [0, 0, 0], seq);
    for (let i = 0; i < 300 && vistas.length < 2; i++) await sleep(10);
    off();
    return { respostas: vistas, segundo: vistas[1] || null };
  },
  async mover({ dir, yaw }) { mundo.entrada = { dir, yaw }; con.mover(dir, yaw); return true; },
  async andarAte({ x, y, yaw, tol = 6, maxMs = 9000 }) {
    const t0 = performance.now();
    while (performance.now() - t0 < maxMs) {
      const p = mundo.ent.get(mundo.meuEnt);
      const s = p && p.amostras.length ? p.amostras[p.amostras.length - 1] : null;
      if (s) {
        const dx = x - s.x, dy = y - s.y, d = Math.hypot(dx, dy);
        if (d < tol) { mundo.entrada = { dir: [0, 0], yaw }; con.mover([0, 0], yaw); await sleep(120); return { ok: true, pos: [s.x, s.y], ms: Math.round(performance.now() - t0) }; }
        // desacelera no fim para não passar do ponto (o servidor integra a 450 cm/s)
        const k = d < 25 ? 0.3 : 1;
        mundo.entrada = { dir: [(dx / d) * k, (dy / d) * k], yaw };
        con.mover(d < 25 ? [dx / d, dy / d] : [dx / d, dy / d], yaw);
        if (d < 25) { await sleep(Math.max(8, (d / 450) * 1000 - 10)); con.mover([0, 0], yaw); await sleep(110); continue; }
      }
      await sleep(33);
    }
    con.mover([0, 0], yaw);
    const p = mundo.ent.get(mundo.meuEnt);
    const s = p && p.amostras.length ? p.amostras[p.amostras.length - 1] : null;
    return { ok: false, pos: s ? [s.x, s.y] : null };
  },
  async posicao({ ent }) {
    const id = ent ?? mundo.meuEnt;
    const e = mundo.ent.get(id);
    const s = e && e.amostras.length ? e.amostras[e.amostras.length - 1] : null;
    return { ent: id, servidor: s ? [s.x, s.y, s.yaw] : null, meuEnt: mundo.meuEnt, fase: mundo.fase };
  },
  async esperar({ tipo, filtro = {}, ms = 8000, desde = 0 }) {
    const t0 = performance.now();
    while (performance.now() - t0 < ms) {
      for (let i = desde; i < mundo.registro.length; i++) {
        const r = mundo.registro[i];
        if (!r.ev || r.ev.tipo !== tipo) continue;
        if (Object.entries(filtro).every(([k, v]) => r.ev[k] === v)) return { ...r, indice: i };
      }
      await sleep(10);
    }
    return null;
  },
  async esperarFase({ fase, ms = 10000 }) {
    const t0 = performance.now();
    while (performance.now() - t0 < ms) { if (mundo.fase === fase) return true; await sleep(15); }
    return false;
  },
  async marcador() { return mundo.registro.length; },
  async sala() { return { sala: mundo.sala, meuEnt: mundo.meuEnt, fase: mundo.fase }; },
  async observados({ tipo, desde = 0 }) { return mundo.registro.slice(desde).filter((r) => r.ev && (!tipo || r.ev.tipo === tipo)); },
  async conciliacoes() { return mundo.conciliacoes; },
  async rtt() { return { rtt: con.rtt, offset: con.offset }; },
  async fechar() { con.fechar(); await sleep(150); return true; },
  async salvarRegistro({ caminho }) {
    writeFileSync(caminho, mundo.registro.map((r) => JSON.stringify(r)).join('\n') + '\n');
    return mundo.registro.length;
  },
  async sair() { setTimeout(() => process.exit(0), 50); return true; },
  async morrer() { process.exit(3); },
};

process.on('message', async (m) => {
  try {
    const r = await acoes[m.acao](m.args || {});
    process.send({ id: m.id, ok: true, r });
  } catch (e) {
    process.send({ id: m.id, ok: false, erro: String(e && e.stack ? e.stack : e) });
  }
});
process.send({ pronto: true });
