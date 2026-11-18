// Cliente web jogável: entrada, saguão, treino/duelo, HUD e controles. A lógica de jogo é do servidor.
import { Cena } from './cena.js';
import { Mundo, deLocal } from './modelo.js';
import { Conexao } from './net.js';
import { Oficina } from './oficina.js';

const $ = (id) => document.getElementById(id);
const qs = new URLSearchParams(location.search);
const WS = qs.get('ws') || `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/`;
const COR_TIME = { 0: '#d9683b', 1: '#3b8fd9', 2: '#8f6bd9', 3: '#6bd98f' };

class App {
  constructor() {
    this.cena = new Cena($('vista'), { qualidade: qs.get('q') || 'alto' });
    this.con = null;
    this.mundo = null;
    this.sala = null;
    this.assento = -1;
    this.criacoes = [];
    this.equip = {};
    this.teclas = new Set();
    this.yaw = 0;
    this.ultMov = 0;
    this.ultDir = '0,0';
    this.ultYaw = 0;
    this.ativos = new Map();
    this.recarga = new Map();
    this.janela = null;
    this.modoAlvo = false;
    this.cameras = ['jogador', 'oponente', 'cima'];
    this.camIdx = 0;
    this.linhas = [];
    this.oficina = null;
    $('srvUrl').textContent = WS;
    $('nome').value = qs.get('nome') || '';
    this._ligarUI();
    this._redimensionar();
    window.addEventListener('resize', () => this._redimensionar());
    requestAnimationFrame((t) => this._quadro(t));
    if (qs.get('nome') && qs.get('auto') !== '0') this.entrar();
  }

  _redimensionar() { this.cena.resize(window.innerWidth, window.innerHeight); if (this.oficina) this.oficina.redimensionar(); }

  toast(msg, ms = 2600) {
    const d = document.createElement('div');
    d.className = 'toast';
    d.textContent = msg;
    $('toasts').appendChild(d);
    setTimeout(() => d.remove(), ms);
  }

  async entrar() {
    const nome = $('nome').value.trim() || 'Jogador';
    $('status').textContent = 'conectando…';
    this.con = new Conexao(WS);
    try {
      const tokenSalvo = (() => { try { return localStorage.getItem('forja.token.' + nome) || undefined; } catch { return undefined; } })();
      const b = await this.con.conectar(nome, tokenSalvo);
      try { localStorage.setItem('forja.token.' + nome, b.token); } catch {}
      this.catalogo = b.catalogo;
    } catch (e) { $('status').textContent = 'falha: ' + e.message; return; }
    this.mundo = new Mundo(this.con);
    this.mundo.aoEvento((e) => this._evento(e));
    this.con.on('sala', (m) => this._sala(m.estado, m.assento));
    this.con.on('fechou', () => { $('status').textContent = 'desconectado'; });
    $('status').textContent = `${nome} · conectado · RTT ${Math.round(this.con.rtt)} ms`;
    setInterval(() => { if (this.con) $('status').textContent = `${nome} · ${this.sala ? 'sala ' + this.sala.codigo + ' · ' : ''}RTT ${Math.round(this.con.rtt)} ms · ${this.cena.stats.fps.toFixed(0)} fps`; }, 1000);
    $('entrada').classList.add('oculto');
    $('saguao').classList.remove('oculto');
    $('btnOficina').classList.remove('oculto');
    this.oficina = new Oficina(this);
    await this.atualizarCriacoes();
    if (qs.get('sala')) { $('codigo').value = qs.get('sala'); this.entrarSala(); }
  }

  async atualizarCriacoes() {
    const r = await this.con.oficina({ cmd: 'listar' });
    this.criacoes = r.criacoes || [];
    this._renderSlotsEquip();
  }

  async criarSala() {
    const r = await this.con.pedir('sala.criar', { modo: $('modoNova').value });
    if (!r.ok) return this.toast(r.erro);
    this._naSala(r);
  }
  async entrarSala() {
    const r = await this.con.pedir('sala.entrar', { codigo: $('codigo').value.trim() });
    if (!r.ok) return this.toast(r.erro);
    this._naSala(r);
  }
  _naSala(r) {
    this.assento = r.assento;
    this.mundo.carregarSnapshot(r.snapshot);
    $('semSala').classList.add('oculto');
    $('comSala').classList.remove('oculto');
    $('codSala').textContent = r.codigo;
    $('hud').classList.remove('oculto');
    $('btnCamera').classList.remove('oculto');
    $('ajuda').classList.remove('oculto');
    history.replaceState(null, '', `?nome=${encodeURIComponent(this.con.nome)}&sala=${r.codigo}${qs.get('q') ? '&q=' + qs.get('q') : ''}`);
  }

  _sala(st, assento) {
    this.sala = st;
    this.assento = assento;
    const eu = st.assentos[assento];
    if (eu) this.mundo.meuEnt = eu.ent;
    $('faseSala').textContent = st.fase;
    $('assentos').innerHTML = st.assentos.map((s) => `<div class="assento"><span class="ponto" style="background:${COR_TIME[s.time] || '#888'}"></span><b>${esc(s.nome)}</b>${s.espectador ? ' <i>espectador</i>' : ''}
      <span style="flex:1"></span>${s.conectado ? '' : '<span class="prob">desconectado</span>'}${s.pronto ? '<span style="color:var(--ok)">pronto</span>' : ''}
      ${s.equipamento_ok ? '' : `<span class="comp" title="${esc((s.equipamento_problemas || []).join('\n'))}">equip.?</span>`}</div>`).join('');
    $('equipProb').textContent = eu && !eu.equipamento_ok ? (eu.equipamento_problemas || []).join(' · ') : '';
    $('modoSala').value = st.config.modo;
    $('modoSala').disabled = assento !== 0;
    $('btnPronto').textContent = eu && eu.pronto ? 'Não pronto' : 'Pronto';
    $('btnRevanche').classList.toggle('oculto', st.fase !== 'fim');
    $('treinoPainel').classList.toggle('oculto', st.fase !== 'treino');
    $('chkRecarga').checked = !!st.sem_recarga;
    const pl = st.placar.placar || {};
    $('placar').innerHTML = st.fase === 'treino' || st.fase === 'saguao' ? '' : Object.entries(pl).map(([t, p]) => `<span style="color:${COR_TIME[t]}">${p}</span>`).join('<span>×</span>') + `<span style="color:var(--fraco);font-size:12px">até ${st.placar.alvo}</span>`;
    this.equip = {};
    if (eu) for (const [slot, e] of Object.entries(eu.equipamento)) {
      const c = this.criacoes.find((x) => x.digest === e.digest) || {};
      this.equip[slot] = { ...e, nome: c.nome || e.digest.slice(7, 13) };
    }
    this._renderSlotsEquip();
    this._renderBarra();
  }

  _renderSlotsEquip() {
    const el = $('slotsEquip');
    if (!el) return;
    const ops = ['<option value="">—</option>'].concat(this.criacoes.filter((c) => c.revisoes > 0).map((c) => `<option value="${c.criacao}">${esc(c.nome)} r${c.ultima_revisao}</option>`)).join('');
    el.innerHTML = [0, 1, 2, 3].map((s) => `<b>${s + 1}</b><select data-slot="${s}" data-testid="equip-${s}">${ops}</select>`).join('');
    for (const sel of el.querySelectorAll('select')) {
      const s = sel.dataset.slot;
      const e = this.equip[s];
      if (e) { const c = this.criacoes.find((x) => x.digest === e.digest); if (c) sel.value = c.criacao; }
      sel.onchange = async () => {
        const r = await this.con.pedir('equipar', { slot: Number(s), criacao: sel.value || undefined, digest: sel.value ? undefined : '' });
        if (!r.ok) return this.toast(r.erro || 'não equipado');
        if (r.previsao) this.mundo.previsoes.set(Number(s), r.previsao); else this.mundo.previsoes.delete(Number(s));
        if (r.competitiva_ok === false) this.toast('equipado; não atende aos critérios competitivos (vale no treino e na arena livre)');
      };
    }
  }

  _renderBarra() {
    $('barraSlots').innerHTML = [0, 1, 2, 3].map((s) => {
      const e = this.equip[s];
      return `<div class="slot ${this.ativos.has(s) ? 'ativo' : ''}" id="slot${s}"><span class="tecla">${s + 1}</span><div class="nome">${e ? esc(e.nome) : '<span style="color:var(--fraco)">vazio</span>'}</div>${e ? `<div style="color:var(--fraco);font-size:11px">${e.custo} pts · ${e.recarga_ms} ms</div>` : ''}<div class="recarga"></div></div>`;
    }).join('');
  }

  _evento(e) {
    const eu = this.mundo.meuEnt;
    const meu = e.autor === eu || (e.exec && this.ativosExec && this.ativosExec.has(e.exec));
    this.ativosExec ||= new Map();
    switch (e.tipo) {
      case 'exec.inicio':
        if (e.autor === eu) { this.ativos.set(e.slot, e.exec); this.ativosExec.set(e.exec, e.slot); this.recarga.set(e.slot, { ate: e.recarga_ate, total: Math.max(1, e.recarga_ate - e.t) }); this._renderBarra(); }
        break;
      case 'exec.fim':
        if (this.ativosExec.has(e.exec)) { this.ativos.delete(this.ativosExec.get(e.exec)); this.ativosExec.delete(e.exec); this._renderBarra(); }
        break;
      case 'janela.abre': this.janela = e; break;
      case 'janela.resultado': if (this.janela && this.janela.no === e.no) { this.janela.resultado = e.resultado; setTimeout(() => { this.janela = null; }, 400); } break;
      case 'cmd.cedo': this.toast('comando cedo — ' + e.motivo, 1500); break;
      case 'cmd.tarde': this.toast('comando tarde — ' + e.motivo, 1500); break;
      case 'ponto': this._banner(`PONTO · ${e.autor_nome}`, 1400); break;
      case 'sala.fase':
        if (e.fase === 'fim') this._banner(e.motivo === 'abandono' ? 'FIM · abandono' : `FIM · vence ${this._nomeTime(e.vencedor)}`, 4000);
        if (e.fase === 'pausa') this._banner(`PAUSA · aguardando ${e.aguardando}`, 3000);
        if (e.fase === 'treino') this._banner('TREINO', 1200);
        break;
      default: break;
    }
    void meu;
    if ($('chkReg').checked) this._linha(e);
  }

  _nomeTime(t) { const s = this.sala && this.sala.assentos.find((x) => x.time === t); return s ? s.nome : 'time ' + t; }

  _linha(e) {
    const n = (id) => { const x = this.mundo.ent.get(id); return x ? x.nome || id : id; };
    let s = null;
    switch (e.tipo) {
      case 'exec.inicio': s = `${n(e.autor)} ativa espaço ${e.slot + 1} (${e.digest.slice(7, 15)})`; break;
      case 'area.anuncio': s = `${e.rotulo}: área r=${(e.raio / 100).toFixed(2)} m em (${e.centro[0]}, ${e.centro[1]}) contato em ${e.t_contato - e.t} ms [${e.superficie}]`; break;
      case 'area.contato': s = `${e.rotulo} contato: ` + e.resultados.map((r) => `${r.nome} ${r.resultado} (${r.motivo})`).join('; '); break;
      case 'guarda.inicio': s = `${n(e.ent)} ${e.rotulo || 'guarda'} ${e.abertura}° eixo ${e.eixo_yaw}° ativa em ${e.t_ativa - e.t} ms`; break;
      case 'guarda.absorve': s = `absorvido a ${e.angulo}° (cargas ${e.cargas})`; break;
      case 'guarda.fim': s = `guarda ${e.motivo}`; break;
      case 'mov.inicio': s = `${n(e.ent)} ${e.rotulo}: (${e.de[0]}, ${e.de[1]}) → (${e.para[0]}, ${e.para[1]}) em ${e.t1 - e.t0} ms`; break;
      case 'janela.abre': s = `janela ${e.t_abre - e.t}–${e.t_fecha - e.t} ms`; break;
      case 'janela.resultado': s = `janela: ${e.resultado}`; break;
      case 'ponto': s = `PONTO ${e.autor_nome} → ${e.alvo_nome}`; break;
      case 'exec.fim': s = `execução ${e.exec} ${e.motivo}`; break;
      default: return;
    }
    this.linhas.push(`<div>${e.t} ${esc(s)}</div>`);
    if (this.linhas.length > 40) this.linhas.shift();
    $('registro').innerHTML = this.linhas.slice(-24).join('');
  }

  _banner(txt, ms) { $('banner').textContent = txt; clearTimeout(this._bt); this._bt = setTimeout(() => { $('banner').textContent = ''; }, ms); }

  _ligarUI() {
    $('btnEntrar').onclick = () => this.entrar();
    $('nome').onkeydown = (e) => { if (e.key === 'Enter') this.entrar(); };
    $('btnCriarSala').onclick = () => this.criarSala();
    $('btnEntrarSala').onclick = () => this.entrarSala();
    $('btnPronto').onclick = () => { const eu = this.sala.assentos[this.assento]; this.con.pedir('sala.pronto', { valor: !(eu && eu.pronto) }); };
    $('btnTreino').onclick = async () => { const r = await this.con.pedir('sala.treino'); if (!r.ok) this.toast(r.erro); };
    $('btnDuelo').onclick = async () => { const r = await this.con.pedir('sala.duelo'); if (!r.ok) this.toast((r.problemas || [r.erro]).join(' · '), 5000); };
    $('btnRevanche').onclick = () => this.con.pedir('sala.revanche');
    $('modoSala').onchange = async () => { const r = await this.con.pedir('sala.modo', { modo: $('modoSala').value }); if (!r.ok) this.toast(r.erro); };
    $('btnOficina').onclick = () => this.oficina.abrir();
    $('btnCamera').onclick = () => this._trocarCamera();
    $('btnQualidade').onclick = () => this._trocarQualidade();
    $('btnAlvo').onclick = () => { this.modoAlvo = true; this.toast('clique no chão para pôr o alvo', 1500); };
    $('btnIr').onclick = () => { this.yaw = +$('irYaw').value; this.con.pedir('treino', { cmd: { cmd: 'treino.posicionar', pos: [+$('irX').value, +$('irY').value, 0], yaw: this.yaw } }); if (this.mundo.local) { this.mundo.local.x = +$('irX').value; this.mundo.local.y = +$('irY').value; } };
    $('btnLimpar').onclick = () => this.con.pedir('treino', { cmd: { cmd: 'treino.limpar' } });
    $('btnReiniciar').onclick = () => this.con.pedir('treino', { cmd: { cmd: 'treino.reiniciar' } });
    $('chkRecarga').onchange = () => this.con.pedir('treino', { cmd: { cmd: 'treino.sem_recarga', valor: $('chkRecarga').checked } });
    $('chkTraj').onchange = () => { this.cena.depuracao.trajetoria = $('chkTraj').checked; };
    $('chkVol').onchange = () => { this.cena.depuracao.volumes = $('chkVol').checked; };
    $('chkReg').onchange = () => $('registro').classList.toggle('oculto', !$('chkReg').checked);
    $('btnRede').onclick = async () => {
      const r = await this.con.pedir('rede', { lag_subida_ms: +$('lagUp').value, lag_descida_ms: +$('lagDown').value, perda_subida: +$('perdaUp').value, perda_descida: +$('perdaDown').value });
      $('redeInfo').textContent = r.ok ? JSON.stringify(r.rede) : r.erro;
    };
    const cv = $('vista');
    cv.addEventListener('click', (ev) => {
      if (this.modoAlvo && this.con) {
        this.modoAlvo = false;
        const p = this.cena.chao((ev.clientX / innerWidth) * 2 - 1, -(ev.clientY / innerHeight) * 2 + 1);
        if (p) this.con.pedir('treino', { cmd: { cmd: 'treino.alvo', nome: 'alvo', pos: [Math.round(p[0]), Math.round(p[1]), 0] } });
        return;
      }
      this.cena.audio.ligar();
      if (this.sala && cv.requestPointerLock) cv.requestPointerLock();
    });
    cv.addEventListener('contextmenu', (e) => e.preventDefault());
    cv.addEventListener('mousedown', (e) => { if (e.button === 2) this._secundario(); });
    document.addEventListener('mousemove', (e) => { if (document.pointerLockElement === cv) this.yaw = norm(this.yaw + e.movementX * 0.18); });
    window.addEventListener('keydown', (e) => {
      if (!this.oficina || this.oficina.aberta || ['INPUT', 'SELECT', 'TEXTAREA'].includes(document.activeElement.tagName)) return;
      const k = e.key.toLowerCase();
      if (k === 'tab') { e.preventDefault(); this._trocarCamera(); return; }
      if (k === 'f') { this._trocarQualidade(); return; }
      if (k === 'o') { this.oficina.abrir(); return; }
      if (k === 'c' || k === 'backspace') { this.con.cancelar(); return; }
      if (k === 'r') { this._secundario(); return; }
      if (['1', '2', '3', '4'].includes(k) && !e.repeat) { this.pressionar(Number(k) - 1); return; }
      this.teclas.add(k);
    });
    window.addEventListener('keyup', (e) => {
      const k = e.key.toLowerCase();
      if (['1', '2', '3', '4'].includes(k) && this.con) { const s = Number(k) - 1; if (this.ativos.has(s)) this.con.botao(s, 'soltar', this.yaw, this._mira()); return; }
      this.teclas.delete(k);
    });
  }

  _mira() { const p = this.cena.chao(0, 0); return p ? [Math.round(p[0]), Math.round(p[1]), 0] : [0, 0, 0]; }

  pressionar(slot) {
    if (!this.con || !this.sala) return;
    if (!this.equip[slot]) return this.toast(`nada equipado no espaço ${slot + 1}`, 1200);
    const vivo = this.ativos.has(slot);
    if (!vivo) this.mundo.preverAtivacao(slot, this.yaw);
    const b = this.con.botao(slot, 'pressionar', this.yaw, this._mira());
    b.resposta.then((r) => { if (!r.ok && !r.foi_comando) { this.mundo.rejeitarPrevisao(r.motivo); this.toast(r.motivo); } });
    return b;
  }
  _secundario() {
    if (!this.con) return;
    const s = [...this.ativos.keys()].pop();
    if (s !== undefined) this.con.botao(s, 'secundario', this.yaw, this._mira());
  }

  _trocarCamera() { this.camIdx = (this.camIdx + 1) % this.cameras.length; this.cena.modoCamera = this.cameras[this.camIdx] === 'cima' ? 'cima' : 'jogador'; $('btnCamera').textContent = 'Câmera: ' + this.cameras[this.camIdx]; }
  _trocarQualidade() { const q = this.cena.qualidade === 'alto' ? 'baixo' : 'alto'; this.cena.setQualidade(q); $('btnQualidade').textContent = 'Gráficos: ' + q; }

  _oponente() {
    if (!this.sala) return null;
    const eu = this.sala.assentos[this.assento];
    const o = this.sala.assentos.find((s, i) => i !== this.assento && !s.espectador && (!eu || s.time !== eu.time));
    return o ? o.ent : null;
  }

  _entrada(dt) {
    if (!this.con || !this.sala || (this.oficina && this.oficina.aberta)) return;
    const t = this.teclas;
    if (t.has('q')) this.yaw = norm(this.yaw - 120 * dt);
    if (t.has('e')) this.yaw = norm(this.yaw + 120 * dt);
    let f = 0, r = 0;
    if (t.has('w') || t.has('arrowup')) f += 1;
    if (t.has('s') || t.has('arrowdown')) f -= 1;
    if (t.has('d') || t.has('arrowright')) r += 1;
    if (t.has('a') || t.has('arrowleft')) r -= 1;
    const v = deLocal([f, r], this.yaw);
    const l = Math.hypot(v[0], v[1]);
    const dir = l > 0 ? [v[0] / l, v[1] / l] : [0, 0];
    this.mundo.entrada = { dir, yaw: this.yaw };
    const chave = dir.map((x) => x.toFixed(2)).join(',');
    const agora = performance.now();
    if (chave !== this.ultDir || Math.abs(this.yaw - this.ultYaw) > 1.5 || agora - this.ultMov > 100) {
      this.con.mover(dir, this.yaw);
      this.ultDir = chave; this.ultYaw = this.yaw; this.ultMov = agora;
    }
  }

  _hud() {
    if (!this.con || !this.sala) return;
    const ts = this.con.agoraServidor();
    for (const [s, r] of this.recarga) {
      const el = document.querySelector(`#slot${s} .recarga`);
      if (el) el.style.width = `${Math.max(0, Math.min(1, (r.ate - ts) / r.total)) * 100}%`;
    }
    const me = this.mundo.ent.get(this.mundo.meuEnt);
    if (me && me.folego !== undefined) $('folego').firstChild.style.width = me.folego + '%';
    const j = this.janela;
    const jh = $('janelaHud');
    if (j) {
      jh.classList.remove('oculto');
      const ini = j.t, fim = j.t_decide + 80, span = Math.max(1, fim - ini);
      jh.querySelector('.faixa').style.left = ((j.t_abre - ini) / span) * 100 + '%';
      jh.querySelector('.faixa').style.width = ((j.t_fecha - j.t_abre) / span) * 100 + '%';
      jh.querySelector('.cursor').style.left = Math.max(0, Math.min(1, (ts - ini) / span)) * 100 + '%';
      jh.querySelector('span').textContent = `${j.rotulo}: ${j.gatilho} ${j.comando === 'mesmo_botao' ? 'o mesmo botão' : 'o secundário'}${j.resultado ? ' → ' + j.resultado : ''}`;
      jh.style.borderColor = ts >= j.t_abre && ts < j.t_fecha ? '#fff' : '#c58bff';
    } else jh.classList.add('oculto');
    if (this.sala.fase === 'contagem') $('banner').textContent = String(Math.max(1, Math.ceil((this.sala.fase_ate - ts) / 1000)));
  }

  _quadro(t) {
    const dt = Math.min(0.05, (t - (this._tq || t)) / 1000);
    this._tq = t;
    if (this.mundo && this.sala) {
      this._entrada(dt);
      this.mundo.passo(dt * 1000);
      const ts = this.con.agoraServidor();
      this.cena.sincronizar(this.mundo, ts);
      const cam = this.cameras[this.camIdx];
      const alvo = cam === 'oponente' ? this._oponente() : this.mundo.meuEnt;
      this.cena.posicionarCamera(this.mundo, ts, alvo, cam === 'oponente' ? undefined : this.yaw);
      this._hud();
    } else {
      this.cena.orbita.yaw += dt * 0.05;
      this.cena.posicionarCamera(null, 0, null);
    }
    if (!(this.oficina && this.oficina.aberta)) this.cena.render();
    requestAnimationFrame((x) => this._quadro(x));
  }
}

const norm = (a) => { while (a > 180) a -= 360; while (a <= -180) a += 360; return a; };
export const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);

window.forja = new App();
