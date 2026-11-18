// Estúdio de animação: o operador e os efeitos em clipes roteirizados, sem servidor.
// Os clipes produzem os mesmos dados que os eventos do servidor (deslocamento, área, defesa, impacto),
// então a cena e a animação são exatamente as do jogo. irPara(clipe, t) é determinístico (simula a 60 Hz).
import * as THREE from 'three';
import { Cena } from './cena.js';
import { PRESETS, aleatoria, normalizar } from './materia.js';
import { genomaDe } from './humano.js';

const R = (t, a, b) => Math.max(0, Math.min(1, (t - a) / (b - a)));
const ease = (x) => x * x * (3 - 2 * x);

// ---------- clipes ----------
function clipe(nome, dur, ents, camera, estado) { return { nome, dur, ents, camera, estado }; }
const A = { id: 1, tipo: 'jogador', time: 0, nome: 'Ana' }, B = { id: 2, tipo: 'jogador', time: 1, nome: 'Bruno' };
const GALERIA = [101, 202, 303, 404, 505, 606].map((sem, i) => ({ id: 30 + i, tipo: 'jogador', time: i % 2, nome: 'v' + sem, forma: genomaDe(sem) }));
const CL = [
  clipe('Variações (genomas)', 6000, GALERIA, { yaw: 0.0, pitch: 0.12, dist: 5.2, alvo: [-6, 1.0, 0.0] }, () => ({
    pos: Object.fromEntries(GALERIA.map((e, i) => [e.id, [-560, -260 + i * 104]])), yaw: Object.fromEntries(GALERIA.map((e) => [e.id, 0])),
  })),
  clipe('Parado', 6000, [A], { yaw: 0.55, pitch: 0.1, dist: 2.5, alvo: [-6, 1.05, -2.5] }, () => ({ pos: { 1: [-600, -250] }, yaw: { 1: 0 } })),
  clipe('Andar', 8000, [A], { yaw: 0.9, pitch: 0.15, dist: 4.4, alvo: [-6, 0.9, -2.2] }, (t) => { const a = (t / 8000) * Math.PI * 2; return { pos: { 1: [-600 + 200 * Math.cos(a), -220 + 160 * Math.sin(a)] }, yaw: { 1: (a * 180) / Math.PI + 90 } }; }),
  clipe('Correr', 4000, [A], { yaw: 1.35, pitch: 0.12, dist: 5.2, alvo: [-6, 0.9, -2.6] }, (t) => { const k = (t % 4000) / 2000, ida = k < 1; const x = ida ? -900 + 600 * ease(k) : -300 - 600 * ease(k - 1); return { pos: { 1: [x, -260] }, yaw: { 1: ida ? 0 : 180 } }; }),
  clipe('Girar parado', 4000, [A], { yaw: 0.7, pitch: 0.25, dist: 3.0, alvo: [-6, 0.7, -2.5] }, (t) => ({ pos: { 1: [-600, -250] }, yaw: { 1: (ease(R(t, 500, 1300)) - ease(R(t, 2300, 3100))) * 160 } })),
  clipe('Pulso: preparo, contato, recuperação', 2600, [A], { yaw: 0.75, pitch: 0.16, dist: 3.2, alvo: [-6, 0.8, -2.5] }, (t) => ({
    pos: { 1: [-600, -250] }, yaw: { 1: 0 }, recup: { 1: t >= 900 ? 1700 : 0 },
    areas: [{ id: 11, autor: 1, centro: [-600, -250, 0], raio: 100, espessura: 0, altura: 200, t: 300, t_contato: 800, t_fim: 900, resultados: [], superficie: 'concreto' }],
  })),
  clipe('Passo (deslocamento de 3 m)', 2400, [A], { yaw: 1.2, pitch: 0.16, dist: 4.4, alvo: [-4.5, 0.8, -2.6] }, (t) => {
    const t0 = 500, t1 = 650, u = R(t, t0, t1);
    return { pos: { 1: [-600 + 300 * u, -250] }, yaw: { 1: 0 }, movs: t >= t0 && t <= t1 + 40 ? [{ ent: 1, de: [-600, -250], ctrl: [-450, -250], para: [-300, -250], t0, t1 }] : [] };
  }),
  ...['bracos_cruzados', 'palma_frente', 'ombro'].map((g) => clipe(`Anteparo — ${g.replace('_', ' ')}`, 3200, [B], { yaw: 0.25, pitch: 0.14, dist: 3.3, alvo: [-5.9, 1.0, 1.5] }, () => ({
    pos: { 2: [-600, 150] }, yaw: { 2: 0 },
    guardas: [{ id: 21, ent: 2, gesto: g, t: 400, t_ativa: 750, t_fim: 2600, abertura: 90, eixo_yaw: 0, orientacao: 0, segue: true, cargas: 1, absorcoes: [], fim: { t: 2600, motivo: 'expirou' } }],
  }))),
  clipe('Absorção de frente (Anteparo rompe)', 3000, [A, B], { yaw: -1.25, pitch: 0.16, dist: 3.4, alvo: [-6.4, 0.95, 1.5] }, () => ({
    pos: { 1: [-680, 150], 2: [-600, 150] }, yaw: { 1: 0, 2: 180 },
    guardas: [{ id: 22, ent: 2, gesto: 'bracos_cruzados', t: 300, t_ativa: 600, t_fim: 2400, abertura: 90, eixo_yaw: 180, orientacao: 0, segue: true, cargas: 1, absorcoes: [{ t: 1300, angulo: 0, fonte: [-680, 150, 0] }], fim: { t: 1300, motivo: 'rompida' } }],
    areas: [{ id: 12, autor: 1, centro: [-680, 150, 0], raio: 100, espessura: 0, altura: 200, t: 800, t_contato: 1300, t_fim: 1400, resultados: [{ nome: 'Bruno', resultado: 'absorvido' }], superficie: 'concreto' }],
  })),
  clipe('Golpe pelo flanco', 3000, [A, B], { yaw: 2.3, pitch: 0.16, dist: 3.6, alvo: [-6.0, 0.9, 1.1] }, () => ({
    pos: { 1: [-600, 70], 2: [-600, 150] }, yaw: { 1: 90, 2: 180 },
    areas: [{ id: 13, autor: 1, centro: [-600, 70, 0], raio: 100, espessura: 0, altura: 200, t: 800, t_contato: 1300, t_fim: 1400, resultados: [{ nome: 'Bruno', resultado: 'atingido' }], superficie: 'agua' }],
    impactos: [{ alvo: 2, t: 1300, centro: [-600, 70, 0] }],
  })),
  clipe('Pulso à distância (arremesso)', 2600, [A, { id: 3, tipo: 'alvo', time: -1, nome: 'alvo' }], { yaw: 1.05, pitch: 0.2, dist: 4.6, alvo: [-5.6, 0.8, -2.4] }, (t) => ({
    pos: { 1: [-720, -250], 3: [-420, -215] }, yaw: { 1: 0, 3: 180 }, recup: { 1: t >= 900 ? 1500 : 0 },
    areas: [{ id: 14, autor: 1, centro: [-420, -250, 0], raio: 110, espessura: 0, altura: 200, t: 300, t_contato: 800, t_fim: 900, resultados: [{ nome: 'alvo', resultado: 'atingido' }], superficie: 'concreto' }],
    impactos: [{ alvo: 3, t: 800, centro: [-420, -250, 0] }],
  })),
  clipe('Contramaré (marca, Travessia, volta)', 2800, [A], { yaw: 1.3, pitch: 0.22, dist: 5.2, alvo: [-4.6, 0.8, -2.5] }, (t) => {
    const ida = R(t, 400, 550), volta = R(t, 1000, 1120);
    const x = -600 + 300 * ida - 300 * volta;
    const movs = [];
    if (t >= 400 && t <= 590) movs.push({ ent: 1, de: [-600, -250], ctrl: [-450, -250], para: [-300, -250], t0: 400, t1: 550 });
    if (t >= 1000 && t <= 1160) movs.push({ ent: 1, de: [-300, -250], ctrl: [-450, -250], para: [-600, -250], t0: 1000, t1: 1120 });
    return {
      pos: { 1: [x, -250] }, yaw: { 1: 0 }, movs,
      marcas: t >= 400 ? [{ exec: 1, no: 'n1', t: 400, ponto: [-600, -250, 0] }] : [],
      areas: [{ id: 15, autor: 1, centro: [-300, -250, 0], raio: 100, espessura: 0, altura: 200, t: 550, t_contato: 750, t_fim: 850, resultados: [], superficie: 'concreto' }],
    };
  }),
  clipe('Recuperação (aberto)', 3000, [A], { yaw: 0.6, pitch: 0.12, dist: 2.6, alvo: [-6, 0.75, -2.5] }, () => ({ pos: { 1: [-600, -250] }, yaw: { 1: 0 }, recup: { 1: 2600 } })),
];

class MundoRoteiro {
  constructor() { this.ent = new Map(); this.areas = new Map(); this.guardas = new Map(); this.movs = new Map(); this.impactos = []; this.marcas = []; }
  carregar(c) { this.c = c; this.ent.clear(); for (const e of c.ents) this.ent.set(e.id, { ...e, ativo: true }); }
  em(t) {
    this.t = t;
    const s = this.c.estado(t);
    this.s = s;
    this.areas.clear(); this.guardas.clear(); this.movs.clear();
    for (const a of s.areas || []) {
      if (t < a.t) continue;
      const fase = t < a.t_contato ? 'preparo' : t < a.t_fim ? 'contato' : 'fim';
      if (t > a.t_fim + 4500) continue;
      this.areas.set(a.id, { ...a, fase, resultados: fase === 'preparo' ? [] : a.resultados, t_contato_real: a.t_contato, t_fim_real: a.t_fim, materia: a.materia || this.materia });
    }
    for (const g of s.guardas || []) {
      if (t < g.t || (g.fim && t > g.fim.t + 1000)) continue;
      const abs = g.absorcoes.filter((x) => x.t <= t);
      this.guardas.set(g.id, { ...g, absorcoes: abs, ultima_absorcao: abs[abs.length - 1], fim: g.fim && t >= g.fim.t ? g.fim : null, materia: g.materia || this.materia });
    }
    for (const m of s.movs || []) this.movs.set(m.ent, { ...m, materia: m.materia || this.materia });
    this.impactos = (s.impactos || []).filter((x) => x.t <= t);
    this.marcas = (s.marcas || []).filter((x) => x.t <= t);
  }
  posicao(id) { const p = this.s.pos[id]; return p ? [p[0], p[1]] : null; }
  yaw(id) { return (this.s.yaw && this.s.yaw[id]) || 0; }
  info(id) { return { recup: this.s.recup ? this.s.recup[id] : 0, folego: 100 - 40 * Math.max(0, Math.sin(this.t / 900)) }; }
}

class Estudio {
  constructor() {
    const q = new URLSearchParams(location.search).get('q') || 'alto';
    this.cena = new Cena(document.getElementById('vista'), { qualidade: q });
    this.cena.modoCamera = 'editor';
    this.cena.depuracao.volumes = false;
    this.mundo = new MundoRoteiro();
    this.idx = 0; this.t = 0; this.vel = 1; this.pausa = false;
    this.cena.resize(innerWidth, innerHeight);
    addEventListener('resize', () => this.cena.resize(innerWidth, innerHeight));
    this.ui();
    this.selecionar(0);
    this._ult = performance.now();
    requestAnimationFrame(() => this.quadro());
  }
  ui() {
    const lista = document.getElementById('clipes');
    lista.innerHTML = CL.map((c, i) => `<button data-i="${i}" data-testid="clipe-${i}">${c.nome}</button>`).join('');
    for (const b of lista.querySelectorAll('button')) b.onclick = () => this.selecionar(+b.dataset.i);
    const mats = document.getElementById('materias');
    mats.innerHTML = Object.keys(PRESETS).map((k) => `<button data-m="${k}">${k}</button>`).join('') + '<button data-m="*">Aleatória</button>';
    for (const b of mats.querySelectorAll('button')) b.onclick = () => this.definirMateria(b.dataset.m === '*' ? aleatoria(Math.floor(Math.random() * 1e6)) : PRESETS[b.dataset.m], b.dataset.m);
    document.getElementById('pers').onclick = () => { A.forma = genomaDe(Math.floor(Math.random() * 1e6)); B.forma = genomaDe(Math.floor(Math.random() * 1e6)); this.reiniciarEnt(); this.mundo.carregar(CL[this.idx]); };
    document.getElementById('lento').onclick = (e) => { this.vel = this.vel === 1 ? 0.25 : 1; e.target.textContent = this.vel === 1 ? 'Câmera lenta' : 'Velocidade normal'; };
    document.getElementById('pausar').onclick = (e) => { this.pausa = !this.pausa; e.target.textContent = this.pausa ? 'Continuar' : 'Pausar'; };
    document.getElementById('qual').onclick = (e) => { const q = this.cena.qualidade === 'alto' ? 'baixo' : 'alto'; this.cena.setQualidade(q); e.target.textContent = 'Gráficos: ' + q; this.reiniciarEnt(); };
    const cv = document.getElementById('vista');
    let arr = null;
    cv.onpointerdown = (e) => { arr = { x: e.clientX, y: e.clientY }; cv.setPointerCapture(e.pointerId); };
    cv.onpointermove = (e) => { if (!arr) return; const o = this.cena.orbita; o.yaw += (e.clientX - arr.x) * 0.006; o.pitch = Math.max(-0.1, Math.min(1.4, o.pitch + (e.clientY - arr.y) * 0.005)); arr = { x: e.clientX, y: e.clientY }; };
    cv.onpointerup = () => { arr = null; };
    cv.onwheel = (e) => { this.cena.orbita.dist = Math.max(1.2, Math.min(15, this.cena.orbita.dist * (1 + Math.sign(e.deltaY) * 0.08))); e.preventDefault(); };
  }
  definirMateria(m, nome) {
    this.mundo.materia = normalizar(m);
    this.materiaNome = nome || 'personalizada';
    this.cena.limparDinamicos();
    for (const b of document.querySelectorAll('#materias button')) b.classList.toggle('sel', b.dataset.m === nome);
  }
  reiniciarEnt() { for (const [, o] of this.cena.objetos.ent) { this.cena.scene.remove(o); for (const x of o.userData.extras || []) this.cena.scene.remove(x); } this.cena.objetos.ent.clear(); this.cena.limparDinamicos(); }
  selecionar(i) {
    this.idx = i; this.t = 0;
    const c = CL[i];
    this.mundo.carregar(c);
    this.reiniciarEnt();
    Object.assign(this.cena.orbita, { yaw: c.camera.yaw, pitch: c.camera.pitch, dist: c.camera.dist });
    this.cena.orbita.alvo.set(...c.camera.alvo);
    document.getElementById('titulo').textContent = c.nome + (this.materiaNome ? ' · ' + this.materiaNome : '');
    for (const b of document.querySelectorAll('#clipes button')) b.classList.toggle('sel', +b.dataset.i === i);
  }
  passo(dtMs) {
    const c = CL[this.idx];
    this.t += dtMs;
    if (this.t > c.dur) { this.t = 0; this.reiniciarEnt(); }
    this.mundo.em(this.t);
    this.cena._dt = dtMs / 1000;
    this.cena.sincronizar(this.mundo, this.t);
  }
  // determinístico: reinicia o clipe e simula até t a 60 Hz, depois renderiza um quadro
  irPara(i, t, camYaw, materia, semente) {
    if (semente !== undefined && semente !== null) { A.forma = genomaDe(semente); B.forma = genomaDe(semente + 1); }
    if (materia) this.definirMateria(materia === '*' ? aleatoria(t + i * 7) : PRESETS[materia] || materia, typeof materia === 'string' ? materia : 'personalizada');
    this.selecionar(i);
    if (camYaw !== undefined) this.cena.orbita.yaw = camYaw;
    this.pausa = true;
    const h = 1000 / 60;
    for (let k = 0; k < t; k += h) this.passo(Math.min(h, t - k));
    this.cena.posicionarCamera(null, 0, null);
    this.cena.render();
    return this.cena.renderer.domElement.toDataURL('image/png');
  }
  quadro() {
    const agora = performance.now(), dt = Math.min(50, agora - this._ult);
    this._ult = agora;
    if (!this.pausa) this.passo(dt * this.vel);
    document.getElementById('tempo').textContent = `${Math.round(this.t)} ms`;
    this.cena.posicionarCamera(null, 0, null);
    this.cena.render();
    requestAnimationFrame(() => this.quadro());
  }
}
window.estudio = new Estudio();
window.estudio.clipes = CL.map((c) => c.nome);
void THREE;
