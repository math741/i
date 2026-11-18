// Oficina: mexer no poder enquanto vê o que ele faz.
// Toda ação vira um comando da oficina no servidor (o mesmo usado pelos testes). A prévia é a simulação
// autoritativa da receita; a linha do tempo mostra o plano estático e os eventos reais do ensaio.
import * as THREE from 'three';
import { Cena, g2t } from './cena.js';
import { bezier } from './modelo.js';

const $ = (id) => document.getElementById(id);
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
const COR = { preparo: '#ffb347', contato: '#ff4b3a', deslocamento: '#5aa9ff', persistencia: '#4fe0d0', janela: '#c58bff', confirmacao: '#5d5d6a', recuperacao: '#3a3a44', vigia: '#9fe8ff', espera: '#777', repeticao: '#c58bff', instante: '#ddd' };
const TICK = 16;
const wrap = (a) => { while (a > 180) a -= 360; while (a <= -180) a += 360; return a; };

// Adaptador: apresenta um ensaio (eventos + quadros) com a mesma interface do Mundo ao vivo.
class MundoSim {
  constructor() { this.ent = new Map(); this.areas = new Map(); this.guardas = new Map(); this.movs = new Map(); this.impactos = []; this.meuEnt = -1; }
  definir(sim) {
    this.sim = sim;
    this.t0 = sim.t0;
    this.ent.clear();
    const q0 = sim.quadros[0];
    if (q0) for (const e of q0.entidades) this.ent.set(e.id, { id: e.id, nome: e.nome, tipo: e.tipo, time: e.time, ativo: true });
  }
  _quadro(tRel) {
    const q = this.sim.quadros;
    let lo = 0, hi = q.length - 1;
    while (lo < hi) { const m = (lo + hi + 1) >> 1; if (q[m].t <= tRel) lo = m; else hi = m - 1; }
    return [q[lo], q[Math.min(q.length - 1, lo + 1)]];
  }
  em(tRel) {
    const T = this.t0 + tRel;
    this.tRel = tRel;
    this.areas.clear(); this.guardas.clear(); this.movs.clear();
    this.impactos = [];
    this.marcas = [];
    for (const e of this.sim.eventos) {
      if (e.t > T) break;
      switch (e.tipo) {
        case 'area.anuncio': this.areas.set(e.id, { ...e, resultados: [], fase: 'preparo' }); break;
        case 'area.contato': { const a = this.areas.get(e.id); if (a) { a.fase = 'contato'; a.centro = e.centro; a.resultados.push(...e.resultados); } break; }
        case 'area.fim': { const a = this.areas.get(e.id); if (a) { if (T - e.t < 250) a.fase = 'fim'; else this.areas.delete(e.id); } break; }
        case 'area.cancelada': this.areas.delete(e.id); break;
        case 'guarda.inicio': this.guardas.set(e.id, { ...e, absorcoes: [] }); break;
        case 'guarda.absorve': { const g = this.guardas.get(e.id); if (g) { g.ultima_absorcao = e; g.absorcoes.push(e); } break; }
        case 'guarda.fim': { const g = this.guardas.get(e.id); if (g) { if (T - e.t < 1000) g.fim = e; else this.guardas.delete(e.id); } break; }
        case 'mov.inicio': this.movs.set(e.ent, { ...e }); break;
        case 'mov.fim': this.movs.delete(e.ent); break;
        case 'impacto': if (T - e.t < 900) this.impactos.push({ ...e }); break;
        case 'marca': (this.marcas ||= []).push(e); break;
        default: break;
      }
    }
    for (const [, a] of this.areas) if (a.fase === 'contato' && T >= a.t_fim) a.fase = 'fim';
  }
  posicao(id, tServ) {
    const tRel = tServ - this.t0;
    const [a, b] = this._quadro(tRel);
    const pa = a.entidades.find((x) => x.id === id), pb = b.entidades.find((x) => x.id === id);
    if (!pa) return null;
    if (!pb || b.t === a.t) return [pa.pos[0], pa.pos[1]];
    const u = Math.max(0, Math.min(1, (tRel - a.t) / (b.t - a.t)));
    return [pa.pos[0] + (pb.pos[0] - pa.pos[0]) * u, pa.pos[1] + (pb.pos[1] - pa.pos[1]) * u];
  }
  yaw(id) { const [a] = this._quadro(this.tRel || 0); const p = a.entidades.find((x) => x.id === id); return p ? p.yaw : 0; }
  info(id) { const [a] = this._quadro(this.tRel || 0); const p = a.entidades.find((x) => x.id === id); return p ? { recup: p.recup_ate, folego: p.folego } : {}; }
}

export class Oficina {
  constructor(app) {
    this.app = app;
    this.aberta = false;
    this.atual = null;
    this.sim = null;
    this.t = 0;
    this.cenario = { autor: { pos: [-600, -250, 0], yaw: 0 }, alvos: [{ nome: 'Alvo', pos: [-300, -160, 0] }], comandos: [] };
    this.selNo = null;
    this.mundoSim = new MundoSim();
    this.cena = null;
    this.tocando = false;
    this.handles = [];
    this.arrasto = null;
    this.ligar();
  }

  get con() { return this.app.con; }
  toast(m) { this.app.toast(m); }

  async abrir(criacao) {
    $('oficina').classList.remove('oculto');
    this.aberta = true;
    if (document.pointerLockElement) document.exitPointerLock();
    if (!this.cena) {
      this.cena = new Cena($('ofCanvas'), { qualidade: this.app.cena.qualidade, editor: true });
      this.cena.orbita = { yaw: -2.2, pitch: 0.95, dist: 7.5, alvo: new THREE.Vector3(-4.4, 0, -2.0) };
      this.redimensionar();
      requestAnimationFrame((t) => this.quadro(t));
    }
    await this.listar();
    const alvo = criacao || (this.atual && this.atual.criacao) || (this.lista[0] && this.lista[0].criacao);
    if (alvo) await this.selecionar(alvo);
    else this.render();
  }
  fechar() { $('oficina').classList.add('oculto'); this.aberta = false; this.app.atualizarCriacoes(); }
  redimensionar() {
    if (!this.cena) return;
    const c = $('ofCentro');
    this.cena.resize(c.clientWidth, c.clientHeight);
    const tc = $('ofTempoCanvas');
    tc.width = tc.clientWidth * devicePixelRatio; tc.height = tc.clientHeight * devicePixelRatio;
    this.desenharTempo();
  }

  async listar() {
    const r = await this.con.oficina({ cmd: 'listar' });
    this.lista = r.criacoes || [];
    $('ofLista').innerHTML = this.lista.map((c) => `<div class="cri ${this.atual && this.atual.criacao === c.criacao ? 'sel' : ''}" data-id="${c.criacao}" data-testid="criacao-${esc(c.nome)}">${esc(c.nome)}<small>${c.revisoes ? 'r' + c.ultima_revisao + ' · ' + c.digest.slice(7, 15) : 'rascunho'}${c.origem && c.origem !== 'nova' ? ' · ' + esc(c.origem) : ''}</small></div>`).join('');
    for (const el of $('ofLista').querySelectorAll('.cri')) el.onclick = () => this.selecionar(el.dataset.id);
    $('ofSubSel').innerHTML = this.lista.filter((c) => c.revisoes > 0 && (!this.atual || c.criacao !== this.atual.criacao)).map((c) => `<option value="${c.criacao}">${esc(c.nome)} r${c.ultima_revisao}</option>`).join('');
  }

  async selecionar(id) {
    const r = await this.con.oficina({ cmd: 'abrir', criacao: id });
    if (!r.ok) return this.toast(r.erro);
    this.atual = r;
    this.selNo = null;
    await this.listar();
    this.render();
    await this.simular();
  }

  // Envia um comando de edição; o servidor devolve o estado completo.
  async cmd(c, { simular = true } = {}) {
    if (!this.atual) return null;
    const r = await this.con.oficina({ ...c, criacao: this.atual.criacao });
    if (!r.ok) { this.toast(r.erro || 'falhou'); if (r.analise) { this.atual.analise = r.analise; this.render(); } return r; }
    if (r.criacao === this.atual.criacao && r.rascunho) this.atual = r;
    this.render();
    if (simular) await this.simular();
    return r;
  }

  async simular() {
    if (!this.atual) return;
    const cen = { ...this.cenario, comandos: this.cenario.comandos.map((t) => ({ t })) };
    const r = await this.con.oficina({ cmd: 'simular', criacao: this.atual.criacao, cenario: cen });
    if (!r.ok) { this.sim = null; $('cenInfo').textContent = r.erro || ''; this.desenharTempo(); this.explicar(); this.cena && this.cena.limparDinamicos(); return; }
    this.sim = r;
    this.mundoSim.definir(r);
    const contato = r.eventos.find((e) => e.tipo === 'area.contato');
    this.t = contato ? contato.t_rel : Math.min(400, r.quadros[r.quadros.length - 1].t);
    $('cenInfo').textContent = `${r.impactos.length} impacto(s) · ${r.eventos.length} eventos`;
    this.cena.limparDinamicos();
    this.desenharTempo();
    this.explicar();
  }

  setT(t) {
    const max = this.sim ? this.sim.quadros[this.sim.quadros.length - 1].t : 0;
    const novo = Math.max(0, Math.min(max, Math.round(t)));
    if (novo < this.t) this.cena.limparDinamicos();
    this.t = novo;
    this.desenharTempo();
    this.explicar();
  }

  // ---------------- render dos painéis ----------------
  render() {
    const a = this.atual;
    if (!a) { $('ofTitulo').textContent = 'Oficina — crie uma criação'; return; }
    const an = a.analise || {};
    $('ofTitulo').textContent = `Oficina — ${a.nome}`;
    $('ofDigest').textContent = `${a.revisoes.length ? 'r' + a.revisoes[a.revisoes.length - 1].rev : 'sem revisão'} · rascunho ${an.digest ? an.digest.slice(0, 19) + '…' : ''}`;
    $('ofDesfazer').disabled = !a.pode_desfazer;
    $('ofRefazer').disabled = !a.pode_refazer;
    const resp = new Set((an.problemas || []).filter((p) => p.nivel !== 'aviso').map((p) => p.no));
    for (const id of (an.custo_computacional && an.custo_computacional.responsaveis_pico) || []) resp.add(id.split('/')[0]);
    const beh = a.rascunho.comportamento;
    const rot = a.rascunho.rotulos || {};
    const cat = this.app.catalogo || [];
    const opDe = (n) => cat.find((o) => o.op === n.op && o.v === n.v) || cat.find((o) => o.op === n.op);
    // regras em linguagem de jogo, com a condição editável
    $('ofRegras').innerHTML = (an.regras || []).map((r, i) => {
      const alt = (r.alternativas || []).map((x) => `<option value="${x.nome}" ${x.nome === r.porta ? 'selected' : ''}>${esc(x.rotulo)}</option>`).join('');
      return `<div class="regra ${resp.has(r.para) ? 'resp' : ''}" data-i="${i}">${esc(r.texto)}${r.dado ? '' : `<div style="margin-top:3px;color:var(--fraco);font-size:11px">condição: <select data-i="${i}" data-testid="condicao-${i}">${alt}</select></div>`}</div>`;
    }).join('') || '<div style="color:var(--fraco)">Só o nó Ativar. Adicione uma peça abaixo.</div>';
    for (const sel of $('ofRegras').querySelectorAll('select')) sel.onchange = () => { const r = an.regras[sel.dataset.i]; this.cmd({ cmd: 'fluxo.trocar_porta', de: r.de, porta: r.porta, para: r.para, nova_porta: sel.value }); };
    for (const el of $('ofRegras').querySelectorAll('.regra')) el.onclick = (ev) => { if (ev.target.tagName !== 'SELECT') { this.selNo = an.regras[el.dataset.i].para; this.render(); } };
    // peças
    $('ofNos').innerHTML = beh.nos.filter((n) => n.op !== 'entrada.ativar').map((n) => {
      const op = opDe(n) || { params: [], fundamento: '?' };
      const nome = rot[n.id] || op.rotulo || n.op;
      let corpo = '';
      if (this.selNo === n.id) {
        if (n.op === 'sub.incorporar') {
          const d = (a.dependencias || []).find((x) => x.no === n.id) || {};
          corpo = `<div class="param"><span>revisão</span><span>${esc(d.nome || '?')} r${d.revisao || '?'} · ${esc(n.p.ref.slice(0, 15))}…</span></div>
            <div class="param"><span>sobrepor (expostos)</span><input data-sob="${n.id}" value='${esc(JSON.stringify(n.p.sobrepor || {}))}' size="16"></div>`;
        } else {
          corpo = op.params.map((p) => {
            const v = n.p[p.nome];
            let inp;
            if (p.tipo === 'int') inp = `<input type="number" data-no="${n.id}" data-p="${p.nome}" value="${v}" min="${p.min}" max="${p.max}">`;
            else if (p.tipo === 'bool') inp = `<input type="checkbox" data-no="${n.id}" data-p="${p.nome}" ${v ? 'checked' : ''}>`;
            else if (p.tipo === 'enum') inp = `<select data-no="${n.id}" data-p="${p.nome}">${p.opcoes.map((o) => `<option ${o === v ? 'selected' : ''}>${o}</option>`).join('')}</select>`;
            else if (p.tipo === 'vec') inp = `<span>${[0, 1, 2].map((k) => `<input type="number" style="width:58px" data-no="${n.id}" data-p="${p.nome}" data-k="${k}" value="${v ? v[k] : 0}">`).join('')}</span>`;
            else inp = `<span>${esc(JSON.stringify(v))}</span>`;
            return `<div class="param"><span title="${esc(p.nome)}">${esc(p.rotulo)}${p.unidade ? ' (' + p.unidade + ')' : ''}</span>${inp}</div>`;
          }).join('');
          for (const d of op.dados_entrada || []) {
            const fontes = beh.nos.filter((m) => (opDe(m) || {}).dados_saida && opDe(m).dados_saida.some((x) => x.tipo === d.tipo));
            const atual = beh.dados.find((x) => x.para === n.id && x.entrada === d.nome);
            corpo += `<div class="param"><span>${esc(d.rotulo)} ← dado</span><select data-dado="${n.id}" data-entrada="${d.nome}"><option value="">—</option>${fontes.map((f) => `<option value="${f.id}" ${atual && atual.de === f.id ? 'selected' : ''}>${esc(rot[f.id] || (opDe(f) || {}).rotulo || f.id)}</option>`).join('')}</select></div>`;
          }
        }
        corpo += `<div class="linha"><input data-rot="${n.id}" value="${esc(nome)}" size="14"><button data-rem="${n.id}" class="perigo">remover</button></div>`;
      }
      return `<div class="no ${this.selNo === n.id ? 'sel' : ''} ${resp.has(n.id) ? 'resp' : ''}" data-id="${n.id}" data-testid="no-${esc(nome)}"><div class="cab"><b>${esc(nome)}</b><span><span class="etq f-${op.fundamento}">${op.fundamento}</span> <small style="color:var(--fraco)">${n.op}@${n.v}</small></span></div>${corpo}</div>`;
    }).join('');
    for (const el of $('ofNos').querySelectorAll('.no')) el.onclick = (ev) => { if (['INPUT', 'SELECT', 'BUTTON'].includes(ev.target.tagName)) return; this.selNo = el.dataset.id === this.selNo ? null : el.dataset.id; this.render(); this.desenharGizmos(); };
    for (const inp of $('ofNos').querySelectorAll('[data-p]')) inp.onchange = () => {
      const n = beh.nos.find((x) => x.id === inp.dataset.no);
      let valor;
      if (inp.dataset.k !== undefined) { valor = [...n.p[inp.dataset.p]]; valor[+inp.dataset.k] = Math.round(+inp.value); }
      else if (inp.type === 'checkbox') valor = inp.checked;
      else if (inp.type === 'number') valor = Math.round(+inp.value);
      else valor = inp.value;
      this.cmd({ cmd: 'no.param', no: inp.dataset.no, nome: inp.dataset.p, valor });
    };
    for (const inp of $('ofNos').querySelectorAll('[data-dado]')) inp.onchange = () => inp.value ? this.cmd({ cmd: 'dados.ligar', de: inp.value, porta: 'ponto', para: inp.dataset.dado, entrada: inp.dataset.entrada }) : this.cmd({ cmd: 'dados.desligar', para: inp.dataset.dado, entrada: inp.dataset.entrada });
    for (const inp of $('ofNos').querySelectorAll('[data-rot]')) inp.onchange = () => this.cmd({ cmd: 'no.rotulo', no: inp.dataset.rot, rotulo: inp.value }, { simular: false });
    for (const b of $('ofNos').querySelectorAll('[data-rem]')) b.onclick = () => this.cmd({ cmd: 'no.remover', no: b.dataset.rem });
    for (const inp of $('ofNos').querySelectorAll('[data-sob]')) inp.onchange = async () => { let o; try { o = JSON.parse(inp.value); } catch { return this.toast('JSON inválido'); } const n = beh.nos.find((x) => x.id === inp.dataset.sob); for (const k of Object.keys(n.p.sobrepor || {})) if (!(k in o)) await this.cmd({ cmd: 'no.sobrepor', no: n.id, nome: k, valor: null }, { simular: false }); for (const [k, v] of Object.entries(o)) await this.cmd({ cmd: 'no.sobrepor', no: n.id, nome: k, valor: v }, { simular: false }); this.simular(); };
    // "depois de": pares (nó, saída)
    const pares = [];
    for (const n of beh.nos) { const op = opDe(n); if (!op) continue; for (const s of op.saidas) pares.push({ v: `${n.id}|${s.nome}`, t: `${rot[n.id] || op.rotulo} · ${s.rotulo}` }); }
    const prevApos = $('ofApos').value;
    $('ofApos').innerHTML = pares.map((p) => `<option value="${p.v}">${esc(p.t)}</option>`).join('') + '<option value="">(solto)</option>';
    if (pares.some((p) => p.v === prevApos)) $('ofApos').value = prevApos;
    else if (this.selNo) { const op = opDe(beh.nos.find((x) => x.id === this.selNo) || {}); if (op && op.saidas.length) $('ofApos').value = `${this.selNo}|${(op.saidas.find((s) => !s.condicional) || op.saidas[0]).nome}`; }
    // paleta por fundamento
    const grupos = {};
    for (const o of cat) { if (o.obsoleta || o.op === 'entrada.ativar' || o.op === 'sub.incorporar') continue; (grupos[o.fundamento] ||= []).push(o); }
    $('ofPaleta').innerHTML = Object.entries(grupos).map(([f, ops]) => `<div><span class="etq f-${f}">${f}</span> ${ops.map((o) => `<button class="${o.planejada ? 'plan' : ''}" data-op="${o.op}" title="${esc(o.descricao)}" data-testid="peca-${o.op}">${esc(o.rotulo)}</button>`).join('')}</div>`).join('');
    for (const b of $('ofPaleta').querySelectorAll('button')) b.onclick = async () => {
      const [de, porta] = ($('ofApos').value || '|').split('|');
      const op = cat.find((o) => o.op === b.dataset.op && !o.obsoleta);
      const r = await this.cmd({ cmd: 'no.adicionar', op: b.dataset.op, rotulo: op ? op.rotulo : undefined, ...(de ? { ligar_de: de, porta } : {}) });
      if (r && r.no) { this.selNo = r.no; this.render(); this.desenharGizmos(); }
      if (r && r.aviso) this.toast(r.aviso);
    };
    // custos
    const cc = an.custo_computacional || {}, cp = an.custo_competitivo || {};
    const barra = (v, lim, nome) => `<div style="display:flex;justify-content:space-between"><span>${nome}</span><span>${v} / ${lim}</span></div><div class="medidor"><div style="width:${Math.min(100, (v / lim) * 100)}%;background:${v > lim ? 'var(--erro)' : 'var(--ok)'}"></div></div>`;
    $('ofCusto').innerHTML = an.custo_computacional ? `<b>Computacional</b> (execução)${barra(cc.pico_trabalho, cc.limite_trabalho, 'pico de trabalho/tick')}${barra(cc.pico_areas, cc.limite_areas, 'áreas simultâneas')}${barra(cc.ativacoes, cc.limite_ativacoes, 'ativações de nós')}${barra(cc.eventos, cc.limite_eventos, 'eventos')}${barra(cc.duracao_ms, cc.limite_duracao_ms, 'duração (ms)')}
      <div style="color:var(--fraco);font-size:11px">memória ${cc.memoria_bytes} B · disco ${cc.armazenamento_bytes} B · rede ${cc.trafego_bytes_binario} B/ativação</div>
      <b>Competitivo</b>${barra(cp.pontos, cp.limite, 'pontos')}<div style="color:var(--fraco);font-size:11px">recarga ${cp.recarga_ms} ms · fôlego ${cp.folego} · alcance ${(cp.alcance_cm / 100).toFixed(2)} m · anúncio mínimo ${cp.preparo_min_ms < 0 ? '—' : cp.preparo_min_ms + ' ms'}</div>
      <div style="font-size:11px">${Object.entries(cp.por_no || {}).map(([k, v]) => `${esc(rot[k] || k)} ${v}`).join(' · ')}</div>` : '';
    $('ofProblemas').innerHTML = (an.problemas || []).map((p) => `<div class="${p.nivel === 'erro' ? 'prob' : p.nivel === 'competitivo' ? 'comp' : 'aviso'}">${p.nivel === 'erro' ? '✖' : p.nivel === 'competitivo' ? '◆' : '•'} ${esc(p.msg)}</div>`).join('');
    // sugestões editáveis, só entram com aprovação
    $('ofSugestoes').innerHTML = (an.sugestoes || []).map((s, i) => `<div class="sug" data-i="${i}"><b>Ideia:</b> ${esc(s.texto)} <small style="color:var(--fraco)">preserva: ${esc(s.preserva)}</small>
      ${(s.alteracoes || []).map((e, k) => e.cmd === 'no.param' ? `<div class="param"><span>${esc(rot[e.no] || e.no)}.${e.nome}</span><input type="number" data-k="${k}" value="${e.valor}"></div>` : `<div style="font-size:11px;color:var(--fraco)">${e.cmd} ${e.de || ''}.${e.porta || ''} → ${e.para || ''}</div>`).join('')}
      <div class="linha"><button class="prim" data-aprovar="${i}" data-testid="aprovar-${i}">Aprovar</button></div></div>`).join('');
    for (const b of $('ofSugestoes').querySelectorAll('[data-aprovar]')) b.onclick = () => {
      const s = an.sugestoes[b.dataset.aprovar];
      const box = b.closest('.sug');
      const alts = s.alteracoes.map((e, k) => { const inp = box.querySelector(`input[data-k="${k}"]`); return inp ? { ...e, valor: Math.round(+inp.value) } : e; });
      this.cmd({ cmd: 'sugestao.aplicar', id: s.id, alteracoes: alts });
    };
    // revisões, comparação e dependências
    $('ofRevisoes').innerHTML = a.revisoes.map((r) => `<div class="linha"><input type="checkbox" data-cmp="${r.rev}"><b>r${r.rev}</b> <small style="color:var(--fraco)">${r.digest.slice(7, 15)} ${esc(r.nota)}</small><button data-abrir="${r.rev}">abrir</button></div>`).join('') + (a.revisoes.length > 1 ? '<button id="ofComparar">Comparar marcadas</button>' : '');
    for (const b of $('ofRevisoes').querySelectorAll('[data-abrir]')) b.onclick = () => this.cmd({ cmd: 'revisao.abrir', revisao: +b.dataset.abrir });
    if ($('ofComparar')) $('ofComparar').onclick = async () => {
      const m = [...$('ofRevisoes').querySelectorAll('[data-cmp]:checked')].map((x) => +x.dataset.cmp);
      const [x, y] = m.length >= 2 ? m : [a.revisoes[a.revisoes.length - 2].rev, 'rascunho'];
      const r = await this.con.oficina({ cmd: 'comparar', criacao: a.criacao, a: x, b: y });
      this.mostrarDiff(`r${x} → ${y === 'rascunho' ? 'rascunho' : 'r' + y}`, r);
    };
    $('ofDeps').innerHTML = (a.dependencias || []).map((d) => `<div class="linha">${esc(rot[d.no] || d.no)}: <b>${esc(d.nome || '?')} r${d.revisao || '?'}</b>${d.desatualizada ? ` <span class="aviso">r${d.ultima_revisao} disponível</span> <button data-up="${d.no}" data-testid="atualizar-${d.no}">ver diferença e atualizar</button>` : ''}${this.ultimaAtualizacao === d.no ? ` <button data-rev="${d.no}" class="perigo" data-testid="desfazer-atualizacao">desfazer atualização</button>` : ''}</div>`).join('');
    for (const b of $('ofDeps').querySelectorAll('[data-up]')) b.onclick = async () => { const r = await this.cmd({ cmd: 'dependencias.atualizar', no: b.dataset.up }); if (r && r.ok) { this.ultimaAtualizacao = b.dataset.up; this.render(); this.mostrarDiff('atualização aplicada ao rascunho — salve para manter ou desfaça', r.diferenca); } };
    for (const b of $('ofDeps').querySelectorAll('[data-rev]')) b.onclick = async () => { await this.cmd({ cmd: 'dependencias.reverter', no: b.dataset.rev }); this.ultimaAtualizacao = null; this.render(); $('ofDiff').classList.add('oculto'); };
    const ap = a.rascunho.aparencia || {};
    if (ap.cor) $('ofCor').value = ap.cor;
    if (ap.material) $('ofMaterial').value = ap.material;
  }

  mostrarDiff(titulo, d) {
    if (!d) return;
    const L = [titulo, `${d.digest_a ? d.digest_a.slice(0, 19) : ''} → ${d.digest_b ? d.digest_b.slice(0, 19) : ''}${d.iguais ? ' (iguais)' : ''}`];
    for (const x of d.nos_adicionados || []) L.push(`+ nó ${x.no} (${x.op})`);
    for (const x of d.nos_removidos || []) L.push(`− nó ${x.no} (${x.op})`);
    for (const x of d.parametros || []) L.push(`~ ${x.no}.${x.nome}: ${JSON.stringify(x.de)} → ${JSON.stringify(x.para)}`);
    for (const x of d.fluxo_adicionado || []) L.push(`+ fluxo ${x.de}.${x.porta} → ${x.para}`);
    for (const x of d.fluxo_removido || []) L.push(`− fluxo ${x.de}.${x.porta} → ${x.para}`);
    for (const x of d.dependencias || []) L.push(`↺ ${x.no}: ${x.de.nome || ''} r${x.de.revisao || '?'} → ${x.para.nome || ''} r${x.para.revisao || '?'}`);
    $('ofDiff').textContent = L.join('\n');
    $('ofDiff').classList.remove('oculto');
  }

  // ---------------- linha do tempo ----------------
  desenharTempo() {
    const cv = $('ofTempoCanvas');
    const ctx = cv.getContext('2d');
    const W = cv.width, H = cv.height, k = devicePixelRatio;
    ctx.clearRect(0, 0, W, H);
    ctx.fillStyle = '#0d1014'; ctx.fillRect(0, 0, W, H);
    const an = this.atual && this.atual.analise;
    if (!an || !an.linha_do_tempo) return;
    const segs = an.linha_do_tempo;
    const fimSim = this.sim ? this.sim.quadros[this.sim.quadros.length - 1].t : 0;
    const T = Math.max(300, fimSim, ...segs.map((s) => s.fim_max)) + 50;
    const L = 170 * k, R = W - 10 * k, top = 22 * k;
    const x = (t) => L + (t / T) * (R - L);
    this._escala = { L, R, T, k };
    const linhas = [];
    for (const s of segs) if (!linhas.includes(s.no)) linhas.push(s.no);
    const rh = Math.min(24 * k, (H - top - 34 * k) / Math.max(1, linhas.length));
    ctx.font = `${11 * k}px system-ui`;
    ctx.fillStyle = '#556';
    for (let t = 0; t <= T; t += 100) { ctx.fillRect(x(t), top - 6 * k, 1, H - top - 30 * k); if (t % 500 === 0) { ctx.fillStyle = '#889'; ctx.fillText(t + ' ms', x(t) + 2, top - 8 * k); ctx.fillStyle = '#556'; } }
    linhas.forEach((no, i) => {
      const y = top + i * rh;
      const s0 = segs.find((s) => s.no === no);
      ctx.fillStyle = s0.manual ? '#c58bff' : '#cfd3d8';
      ctx.fillText(`${s0.escopo !== 'raiz' ? s0.escopo.split(' ')[0] + ' › ' : ''}${s0.rotulo}`.slice(0, 26), 8 * k, y + rh * 0.68);
      for (const s of segs.filter((q) => q.no === no)) {
        const c = COR[s.tipo] || '#999';
        const x0 = x(s.ini), x1 = x(Math.max(s.fim, s.ini + 2)), x2 = x(s.fim_max);
        ctx.globalAlpha = 0.28; ctx.fillStyle = c; ctx.fillRect(x(s.ini), y + 3 * k, Math.max(2, x2 - x0), rh - 6 * k);
        ctx.globalAlpha = 1;
        if (s.tipo === 'janela') { ctx.strokeStyle = c; ctx.lineWidth = 2 * k; ctx.strokeRect(x0, y + 3 * k, x1 - x0, rh - 6 * k); }
        else if (s.tipo === 'instante') { ctx.fillStyle = c; ctx.fillRect(x0 - k, y + 2 * k, 3 * k, rh - 4 * k); }
        else { ctx.fillStyle = c; ctx.fillRect(x0, y + 3 * k, Math.max(2, x1 - x0), rh - 6 * k); }
        if (s.manual) { ctx.strokeStyle = '#0d1014'; ctx.lineWidth = 2 * k; for (let h = x0; h < x1; h += 7 * k) { ctx.beginPath(); ctx.moveTo(h, y + rh - 3 * k); ctx.lineTo(h + 6 * k, y + 3 * k); ctx.stroke(); } }
        if (s.condicional) { ctx.setLineDash([4 * k, 3 * k]); ctx.strokeStyle = '#fff8'; ctx.lineWidth = k; ctx.strokeRect(x0, y + 3 * k, Math.max(2, x2 - x0), rh - 6 * k); ctx.setLineDash([]); }
        if (x1 - x0 > 55 * k) { ctx.fillStyle = '#111'; ctx.fillText(s.tipo + (s.manual ? ' · manual' : ''), x0 + 3 * k, y + rh * 0.68); }
      }
    });
    // eventos reais do ensaio
    if (this.sim) {
      for (const e of this.sim.eventos) {
        if (!['area.contato', 'janela.resultado', 'guarda.absorve', 'impacto', 'mov.fim', 'vigia.cruzou', 'cmd.cedo', 'cmd.tarde', 'escopo.fim'].includes(e.tipo)) continue;
        const i = linhas.findIndex((n) => n === e.no) >= 0 ? linhas.findIndex((n) => n === e.no) : linhas.length - 1;
        const y = top + i * rh + rh / 2;
        let c = '#fff';
        if (e.tipo === 'area.contato') c = e.resultados.some((r) => r.resultado === 'atingido') ? '#ff3b2f' : e.resultados.some((r) => r.resultado === 'absorvido') ? '#5ff2ff' : '#999';
        if (e.tipo === 'janela.resultado') c = e.resultado === 'comando' ? '#6fdc8c' : '#ff9a3c';
        ctx.fillStyle = c; ctx.beginPath(); ctx.arc(x(e.t_rel), y, 4 * k, 0, 7); ctx.fill();
      }
    }
    for (const t of this.cenario.comandos) { ctx.fillStyle = '#c58bff'; ctx.fillRect(x(t) - k, top - 4 * k, 2 * k, H - top - 26 * k); ctx.fillText('comando', x(t) + 3 * k, H - 30 * k); }
    ctx.fillStyle = '#fff'; ctx.fillRect(x(this.t) - k, 0, 2 * k, H - 28 * k);
    $('tLabel').textContent = `t = ${this.t} ms`;
  }

  explicar() {
    if (!this.sim) { $('ofExplica').innerHTML = '<span style="color:var(--fraco)">sem ensaio</span>'; return; }
    const txt = (e) => {
      switch (e.tipo) {
        case 'exec.inicio': return `ativação (custo, recarga ${e.recarga_ate - e.t} ms)`;
        case 'mov.inicio': return `${e.rotulo}: deslocamento (${e.de[0]}, ${e.de[1]}) → (${e.para[0]}, ${e.para[1]}) em ${e.t1 - e.t0} ms${e.parede ? ' [parede]' : ''}`;
        case 'mov.fim': return `${e.rotulo || 'deslocamento'} termina em (${e.pos[0]}, ${e.pos[1]})${e.interrompido ? ' — ' + e.interrompido : ''}`;
        case 'marca': return `${e.rotulo}: guarda (${e.ponto[0]}, ${e.ponto[1]})`;
        case 'area.anuncio': return `${e.rotulo}: anuncia ${e.espessura ? 'anel' : 'disco'} r=${e.raio} cm em (${e.centro[0]}, ${e.centro[1]}) [${e.superficie}], contato em ${e.t_contato - e.t} ms`;
        case 'area.contato': return `${e.rotulo}: CONTATO em (${e.centro[0]}, ${e.centro[1]}) — ` + e.resultados.map((r) => `<b>${esc(r.nome)}</b> ${r.resultado}: ${esc(r.motivo)}`).join('; ');
        case 'area.fim': return `área termina (${e.atingidos} atingido(s))`;
        case 'guarda.inicio': return `${e.rotulo}: gesto ${e.gesto}, ${e.abertura}° no eixo ${e.eixo_yaw}°, ativa em ${e.t_ativa - e.t} ms`;
        case 'guarda.absorve': return `absorve a ${e.angulo}° do eixo (restam ${e.cargas})`;
        case 'guarda.fim': return `guarda ${e.motivo}`;
        case 'janela.abre': return `${e.rotulo}: janela de ${e.t_abre - this.sim.t0} a ${e.t_fecha - this.sim.t0} ms (decide em ${e.t_decide - this.sim.t0}) — depende de comando`;
        case 'janela.resultado': return `janela: <b>${e.resultado}</b>${e.t_comando ? ` (comando em ${e.t_comando - this.sim.t0} ms)` : ''}`;
        case 'cmd.recebido': return `comando recebido em ${e.t_julgado - this.sim.t0} ms`;
        case 'cmd.cedo': case 'cmd.tarde': return esc(e.motivo);
        case 'escopo.fim': return `fim da subcriação ${e.rotulo}`;
        case 'escopo.inicio': return `início da subcriação ${e.rotulo}`;
        case 'vigia.cruzou': return `${e.rotulo}: cruzou para ${e.superficie} em (${e.ponto[0]}, ${e.ponto[1]}) aos ${e.t_cruzamento - this.sim.t0} ms`;
        case 'sondar': return `${e.rotulo}: superfície ${e.superficie} → ${e.resultado ? 'sim' : 'não'}`;
        case 'impacto': return `IMPACTO em ${esc(e.alvo_nome)} (valor ${e.valor})`;
        case 'exec.fim': return `execução ${e.motivo} (${e.duracao_ms ?? '?'} ms)`;
        default: return null;
      }
    };
    const linhas = this.sim.eventos.filter((e) => e.t_rel >= 0).map((e) => ({ e, s: txt(e) })).filter((x) => x.s);
    let atual = -1;
    linhas.forEach((l, i) => { if (l.e.t_rel <= this.t) atual = i; });
    $('ofExplica').innerHTML = linhas.map((l, i) => `<div data-t="${l.e.t_rel}" class="${i === atual ? 'atual' : ''}" style="${l.e.t_rel > this.t ? 'opacity:.45' : ''}">${String(l.e.t_rel).padStart(5)} ms · ${l.s}</div>`).join('');
    for (const el of $('ofExplica').querySelectorAll('[data-t]')) el.onclick = () => this.setT(+el.dataset.t);
    const cur = $('ofExplica').querySelector('.atual');
    if (cur) cur.scrollIntoView({ block: 'nearest' });
  }

  // ---------------- gizmos na cena ----------------
  desenharGizmos() {
    const G = this.cena.objetos.gizmos;
    while (G.children.length) G.remove(G.children[0]);
    this.handles = [];
    const handle = (pos, cor, tipo, dados) => {
      const m = new THREE.Mesh(new THREE.SphereGeometry(0.13, 16, 12), new THREE.MeshBasicMaterial({ color: cor, depthTest: false }));
      m.position.copy(pos); m.renderOrder = 10; m.userData = { tipo, ...dados };
      G.add(m); this.handles.push(m);
      return m;
    };
    // autor e alvos do ensaio (arrastáveis)
    const a = this.cenario.autor;
    handle(g2t(a.pos[0], a.pos[1], 15), 0xd9683b, 'autor', {});
    const dirA = new THREE.ArrowHelper(new THREE.Vector3(Math.cos(a.yaw * Math.PI / 180), 0, Math.sin(a.yaw * Math.PI / 180)), g2t(a.pos[0], a.pos[1], 15), 0.8, 0xd9683b);
    G.add(dirA);
    this.cenario.alvos.forEach((t, i) => handle(g2t(t.pos[0], t.pos[1], 15), 0xb8b8a0, 'alvo', { i }));
    if (!this.sim || !this.selNo || !this.atual) return;
    const n = this.atual.rascunho.comportamento.nos.find((x) => x.id === this.selNo);
    if (!n) return;
    const ev = (tipo) => this.sim.eventos.find((e) => e.tipo === tipo && e.no === n.id);
    if (n.op === 'vetor.deslocar') {
      const m = ev('mov.inicio');
      if (!m) return;
      const yaw = this._yawEm(m.t - this.sim.t0, m.ent);
      handle(g2t(m.para[0], m.para[1], 15), 0xffe14d, 'desloc.fim', { de: m.de, yaw, no: n.id });
      const meio = bezier(m.de, m.ctrl, m.para, 0.5);
      handle(g2t(meio[0], meio[1], 15), 0x9fe8ff, 'desloc.curva', { de: m.de, para: m.para, yaw, no: n.id });
    } else if (n.op === 'limiar.anel') {
      const ar = ev('area.anuncio');
      if (!ar) return;
      handle(g2t(ar.centro[0] + ar.raio, ar.centro[1], 15), 0xff9a3c, 'anel.raio', { centro: ar.centro, no: n.id });
      const yaw = this._yawEm(ar.t - this.sim.t0, ar.autor);
      handle(g2t(ar.centro[0], ar.centro[1], 15), 0xffffff, 'anel.centro', { centro: ar.centro, desloc: n.p.deslocamento_cm, yaw, no: n.id });
    } else if (n.op === 'lastro.anteparo') {
      const g = ev('guarda.inicio');
      if (!g) return;
      const p = this.mundoSim.posicao(g.ent, g.t);
      const ab = g.abertura;
      for (const s of [-1, 1]) { const ang = (g.eixo_yaw + (s * ab) / 2) * Math.PI / 180; handle(g2t(p[0] + Math.cos(ang) * 150, p[1] + Math.sin(ang) * 150, 15), 0x5ff2ff, 'guarda.abertura', { centro: p, eixo: g.eixo_yaw, no: n.id }); }
      const ang = g.eixo_yaw * Math.PI / 180;
      handle(g2t(p[0] + Math.cos(ang) * 190, p[1] + Math.sin(ang) * 190, 15), 0xbffaff, 'guarda.eixo', { centro: p, yawEnt: g.eixo_yaw - g.orientacao, no: n.id });
    }
  }
  _yawEm(tRel, ent) { const [q] = this.mundoSim._quadro(tRel); const e = q.entidades.find((x) => x.id === ent); return e ? e.yaw : 0; }

  async soltarHandle(h, p) {
    const d = h.userData;
    const rot = (v, yaw) => { const r = -yaw * Math.PI / 180, c = Math.cos(r), s = Math.sin(r); return [v[0] * c - v[1] * s, v[0] * s + v[1] * c]; };
    switch (d.tipo) {
      case 'autor': this.cenario.autor.pos = [Math.round(p[0]), Math.round(p[1]), 0]; $('cenX').value = this.cenario.autor.pos[0]; $('cenY').value = this.cenario.autor.pos[1]; return this.simular();
      case 'alvo': this.cenario.alvos[d.i].pos = [Math.round(p[0]), Math.round(p[1]), 0]; return this.simular();
      case 'desloc.fim': { const v = rot([p[0] - d.de[0], p[1] - d.de[1]], d.yaw); const n = this.atual.rascunho.comportamento.nos.find((x) => x.id === d.no); return this.cmd({ cmd: 'no.param', no: d.no, nome: 'vetor_cm', valor: [Math.round(v[0]), Math.round(v[1]), n.p.vetor_cm[2]] }); }
      case 'desloc.curva': { const meio = [(d.de[0] + d.para[0]) / 2, (d.de[1] + d.para[1]) / 2]; const dx = d.para[0] - d.de[0], dy = d.para[1] - d.de[1], L = Math.hypot(dx, dy) || 1; const dir = [-dy / L, dx / L]; const c = (p[0] - meio[0]) * dir[0] + (p[1] - meio[1]) * dir[1]; return this.cmd({ cmd: 'no.param', no: d.no, nome: 'curva_cm', valor: Math.max(-600, Math.min(600, Math.round(c))) }); }
      case 'anel.raio': return this.cmd({ cmd: 'no.param', no: d.no, nome: 'raio_cm', valor: Math.max(10, Math.min(800, Math.round(Math.hypot(p[0] - d.centro[0], p[1] - d.centro[1])))) });
      case 'anel.centro': { const base = [d.centro[0], d.centro[1]]; const dw = rot(d.desloc, -d.yaw); const anc = [base[0] - dw[0], base[1] - dw[1]]; const v = rot([p[0] - anc[0], p[1] - anc[1]], d.yaw); return this.cmd({ cmd: 'no.param', no: d.no, nome: 'deslocamento_cm', valor: [Math.round(v[0]), Math.round(v[1]), 0] }); }
      case 'guarda.abertura': { const ang = Math.atan2(p[1] - d.centro[1], p[0] - d.centro[0]) * 180 / Math.PI; const ab = Math.round(Math.min(360, Math.max(10, 2 * Math.abs(wrap(ang - d.eixo))))); return this.cmd({ cmd: 'no.param', no: d.no, nome: 'abertura_graus', valor: ab }); }
      case 'guarda.eixo': { const ang = Math.atan2(p[1] - d.centro[1], p[0] - d.centro[0]) * 180 / Math.PI; return this.cmd({ cmd: 'no.param', no: d.no, nome: 'orientacao_graus', valor: Math.round(wrap(ang - d.yawEnt)) }); }
      default: return null;
    }
  }

  quadro() {
    if (this.aberta && this.cena) {
      if (this.tocando && this.sim) { this.t += 16; if (this.t >= this.sim.quadros[this.sim.quadros.length - 1].t) { this.tocando = false; $('tPlay').textContent = '▶ tocar'; } this.desenharTempo(); if (this.t % 64 < 16) this.explicar(); }
      if (this.sim) {
        this.mundoSim.em(this.t);
        const ap = (this.atual && this.atual.rascunho.aparencia) || {};
        if (ap.cor) this.cena.aparencias.set(undefined, { cor: new THREE.Color(ap.cor).getHex() });
        this.cena.sincronizar(this.mundoSim, this.sim.t0 + this.t);
      }
      if (!this.arrasto) this.desenharGizmosSeMudou();
      this.cena.posicionarCamera(null, 0, null);
      this.cena.render();
    }
    requestAnimationFrame((t) => this.quadro(t));
  }
  desenharGizmosSeMudou() {
    const chave = `${this.selNo}|${this.sim ? this.sim.eventos.length + ':' + (this.atual.analise.digest || '') : ''}|${JSON.stringify(this.cenario)}`;
    if (chave !== this._chaveGiz) { this._chaveGiz = chave; this.desenharGizmos(); }
  }

  ligar() {
    $('ofFechar').onclick = () => this.fechar();
    $('ofNova').onclick = async () => {
      const r = await this.con.oficina({ cmd: 'criar', nome: $('ofNomeNova').value.trim() || 'Sem nome' });
      if (!r.ok) return this.toast(r.erro);
      $('ofNomeNova').value = '';
      this.atual = r; await this.listar(); this.render(); this.simular();
    };
    $('ofDesfazer').onclick = () => this.cmd({ cmd: 'desfazer' });
    $('ofRefazer').onclick = () => this.cmd({ cmd: 'refazer' });
    $('ofSalvar').onclick = async () => {
      const r = await this.con.oficina({ cmd: 'salvar', criacao: this.atual.criacao, nota: $('ofNota').value });
      if (!r.ok) { this.toast(r.erro); if (r.analise) { this.atual.analise = r.analise; this.render(); } return; }
      this.toast(r.inalterada ? `sem mudanças: continua r${r.revisao}` : `revisão r${r.revisao} salva · ${r.digest.slice(0, 19)}…`);
      $('ofNota').value = '';
      this.ultimaAtualizacao = null;
      await this.selecionar(this.atual.criacao);
    };
    $('ofDuplicar').onclick = async () => { const r = await this.cmd({ cmd: 'duplicar', nome: this.atual.nome + ' (cópia)' }); if (r && r.ok) { this.atual = r; await this.listar(); this.render(); this.simular(); } };
    $('ofInverter').onclick = () => this.cmd({ cmd: 'sequencia.inverter' });
    $('ofIncorporar').onclick = async () => {
      const [de, porta] = ($('ofApos').value || '|').split('|');
      const r = await this.cmd({ cmd: 'incorporar', alvo: $('ofSubSel').value, ...(de ? { ligar_de: de, porta } : {}) });
      if (r && r.no) { this.selNo = r.no; this.render(); }
    };
    $('ofExportar').onclick = async () => {
      const r = await this.con.oficina({ cmd: 'exportar', criacao: this.atual.criacao });
      if (!r.ok) return this.toast(r.erro);
      const t = $('ofPacote'); t.classList.remove('oculto'); t.value = JSON.stringify(r.pacote); t.select();
      const blob = new Blob([JSON.stringify(r.pacote, null, 2)], { type: 'application/json' });
      const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = `${this.atual.nome}.forja.json`; a.click();
    };
    $('ofImportar').onclick = async () => {
      const t = $('ofPacote');
      if (t.classList.contains('oculto') || !t.value.trim()) { t.classList.remove('oculto'); t.value = ''; t.focus(); return this.toast('cole o pacote e clique em Importar de novo'); }
      let p; try { p = JSON.parse(t.value); } catch { return this.toast('JSON inválido'); }
      const r = await this.con.oficina({ cmd: 'importar', pacote: p });
      if (!r.ok) return this.toast(r.erro);
      t.classList.add('oculto'); t.value = '';
      await this.listar();
      const raiz = r.criacoes.find((c) => c.raiz);
      if (raiz) this.selecionar(raiz.criacao);
    };
    $('ofCor').onchange = () => this.cmd({ cmd: 'aparencia', cor: $('ofCor').value }, { simular: false });
    $('ofMaterial').onchange = () => this.cmd({ cmd: 'aparencia', material: $('ofMaterial').value }, { simular: false });
    for (const id of ['cenX', 'cenY', 'cenYaw']) $(id).onchange = () => { this.cenario.autor = { pos: [+$('cenX').value, +$('cenY').value, 0], yaw: +$('cenYaw').value }; this.simular(); };
    $('cenAlvo').onclick = () => { this.modoAlvo = true; this.toast('clique no chão da prévia'); };
    $('cenLimpar').onclick = () => { this.cenario.alvos = []; this.simular(); };
    $('cenEnsaiar').onclick = () => { this.cenario.comandos = $('cenCmds').value.split(/[,; ]+/).filter(Boolean).map(Number).filter((x) => x >= 0); this.simular(); };
    $('tPrev').onclick = () => this.setT(this.t - TICK);
    $('tNext').onclick = () => this.setT(this.t + TICK);
    $('tPlay').onclick = () => { this.tocando = !this.tocando; if (this.tocando && this.sim && this.t >= this.sim.quadros[this.sim.quadros.length - 1].t) { this.t = 0; this.cena.limparDinamicos(); } $('tPlay').textContent = this.tocando ? '❚❚ pausar' : '▶ tocar'; };
    const evTimes = () => (this.sim ? [...new Set(this.sim.eventos.map((e) => e.t_rel).filter((t) => t >= 0))].sort((a, b) => a - b) : []);
    $('tNextEv').onclick = () => { const t = evTimes().find((x) => x > this.t); if (t !== undefined) this.setT(t); };
    $('tPrevEv').onclick = () => { const t = evTimes().reverse().find((x) => x < this.t); if (t !== undefined) this.setT(t); };
    const tc = $('ofTempoCanvas');
    const tempoDe = (ev) => { const r = tc.getBoundingClientRect(), s = this._escala; if (!s) return 0; const xx = (ev.clientX - r.left) * s.k; return ((xx - s.L) / (s.R - s.L)) * s.T; };
    tc.onpointerdown = (ev) => { this._scrub = true; this.setT(tempoDe(ev)); };
    tc.onpointermove = (ev) => { if (this._scrub) this.setT(tempoDe(ev)); };
    window.addEventListener('pointerup', () => { this._scrub = false; });
    tc.ondblclick = (ev) => { const t = Math.round(tempoDe(ev)); this.cenario.comandos.push(t); $('cenCmds').value = this.cenario.comandos.join(', '); this.simular(); };
    const cv = $('ofCanvas');
    const ndc = (ev) => { const r = cv.getBoundingClientRect(); return [((ev.clientX - r.left) / r.width) * 2 - 1, -((ev.clientY - r.top) / r.height) * 2 + 1]; };
    cv.onpointerdown = (ev) => {
      const [nx, ny] = ndc(ev);
      if (this.modoAlvo) { this.modoAlvo = false; const p = this.cena.chao(nx, ny); if (p) { this.cenario.alvos.push({ nome: 'Alvo ' + (this.cenario.alvos.length + 1), pos: [Math.round(p[0]), Math.round(p[1]), 0] }); this.simular(); } return; }
      const ray = new THREE.Raycaster(); ray.setFromCamera(new THREE.Vector2(nx, ny), this.cena.camera);
      const hit = ray.intersectObjects(this.handles)[0];
      if (hit) { this.arrasto = { h: hit.object }; cv.setPointerCapture(ev.pointerId); return; }
      this.arrasto = null;
      this._orb = { x: ev.clientX, y: ev.clientY, pan: ev.button === 2 || ev.shiftKey };
      cv.setPointerCapture(ev.pointerId);
    };
    cv.onpointermove = (ev) => {
      if (this.arrasto) { const [nx, ny] = ndc(ev); const p = this.cena.chao(nx, ny); if (p) this.arrasto.h.position.copy(g2t(p[0], p[1], 15)); return; }
      if (this._orb) {
        const dx = ev.clientX - this._orb.x, dy = ev.clientY - this._orb.y;
        this._orb.x = ev.clientX; this._orb.y = ev.clientY;
        const o = this.cena.orbita;
        if (this._orb.pan) { o.alvo.x -= dx * 0.01 * Math.cos(o.yaw + Math.PI / 2) + dy * 0.01 * Math.cos(o.yaw); o.alvo.z -= dx * 0.01 * Math.sin(o.yaw + Math.PI / 2) + dy * 0.01 * Math.sin(o.yaw); }
        else { o.yaw += dx * 0.006; o.pitch = Math.max(0.15, Math.min(1.5, o.pitch + dy * 0.005)); }
      }
    };
    cv.onpointerup = async (ev) => {
      if (this.arrasto) { const [nx, ny] = ndc(ev); const p = this.cena.chao(nx, ny); const h = this.arrasto.h; this.arrasto = null; if (p) await this.soltarHandle(h, p); this._chaveGiz = null; }
      this._orb = null;
    };
    cv.oncontextmenu = (e) => e.preventDefault();
    cv.onwheel = (e) => { this.cena.orbita.dist = Math.max(3, Math.min(25, this.cena.orbita.dist * (1 + Math.sign(e.deltaY) * 0.1))); e.preventDefault(); };
    window.addEventListener('keydown', (e) => {
      if (!this.aberta || ['INPUT', 'TEXTAREA'].includes(document.activeElement.tagName)) return;
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'z') { e.preventDefault(); this.cmd({ cmd: e.shiftKey ? 'refazer' : 'desfazer' }); }
      else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'y') { e.preventDefault(); this.cmd({ cmd: 'refazer' }); }
      else if (e.key === 'ArrowRight') this.setT(this.t + TICK);
      else if (e.key === 'ArrowLeft') this.setT(this.t - TICK);
      else if (e.key === 'Escape') this.fechar();
    });
  }
}
