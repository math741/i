// Percurso completo da primeira entrega, em processos separados:
//   1 processo servidor dedicado (build/forja_server) + 1 processo por jogador (jogador.mjs).
// Os jogadores usam os mesmos módulos e mensagens do cliente web. Toda criação nasce na oficina.
// Saída: evidencias/e2e_relatorio.json, evidencias/e2e_relatorio.md, logs do servidor e dos clientes.
//
//   node tests/e2e/percurso.mjs
import { fork, spawn } from 'node:child_process';
import { copyFileSync, existsSync, mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';

const RAIZ = path.resolve(import.meta.dirname, '../..');
const RUN = path.join(RAIZ, 'run/e2e');
const EVID = path.join(RAIZ, 'evidencias');
const PORTA = 8799;
const URL = `ws://127.0.0.1:${PORTA}/`;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const relatorio = { inicio: new Date().toISOString(), topologia: 'servidor dedicado local + 2 clientes em processos separados (mesma máquina, loopback)', verificacoes: [], secoes: {} };
let falhas = 0;
function verificar(cond, desc, dados) {
  relatorio.verificacoes.push({ ok: !!cond, desc, ...(dados !== undefined ? { dados } : {}) });
  if (!cond) falhas++;
  console.log(`${cond ? '  ok   ' : '  FALHA'} ${desc}${!cond && dados !== undefined ? ' :: ' + JSON.stringify(dados).slice(0, 400) : ''}`);
}

let servidor = null;
let saidaServidor = '';
async function iniciarServidor() {
  saidaServidor = '';
  servidor = spawn(path.join(RAIZ, 'build/forja_server'), ['--porta', String(PORTA), '--cliente', path.join(RAIZ, 'client'), '--dados', path.join(RUN, 'saves'), '--logs', path.join(RUN, 'logs'), '--dev'], { stdio: ['ignore', 'pipe', 'pipe'] });
  servidor.stdout.on('data', (d) => { saidaServidor += d; });
  servidor.stderr.on('data', (d) => { saidaServidor += d; });
  for (let i = 0; i < 100 && !saidaServidor.includes('servidor dedicado'); i++) await sleep(30);
  return servidor.pid;
}
async function pararServidor() {
  const p = servidor;
  const fim = new Promise((r) => p.on('exit', r));
  p.kill('SIGTERM');
  await fim;
}

class Bot {
  constructor(nome) {
    this.nome = nome;
    this.p = fork(path.join(import.meta.dirname, 'jogador.mjs'), [], { stdio: ['ignore', 'inherit', 'inherit', 'ipc'] });
    this.n = 1;
    this.pend = new Map();
    this.pronto = new Promise((r) => this.p.on('message', (m) => { if (m.pronto) r(); else if (this.pend.has(m.id)) { const f = this.pend.get(m.id); this.pend.delete(m.id); f(m); } }));
  }
  async call(acao, args = {}) {
    await this.pronto;
    const id = this.n++;
    const m = await new Promise((r) => { this.pend.set(id, r); this.p.send({ id, acao, args }); });
    if (!m.ok) throw new Error(`[${this.nome}] ${acao}: ${m.erro}`);
    return m.r;
  }
  of(cmd) { return this.call('oficina', { cmd }); }
  pedir(t, corpo) { return this.call('pedir', { t, corpo }); }
}

function logServidor() {
  const dir = path.join(RUN, 'logs');
  const linhas = [];
  for (const f of readdirSync(dir).filter((x) => x.endsWith('.jsonl')).sort()) {
    for (const l of readFileSync(path.join(dir, f), 'utf8').split('\n')) if (l.trim()) { const j = JSON.parse(l); j._arquivo = f; linhas.push(j); }
  }
  return linhas;
}
const ponto = (p) => `(${p[0]}, ${p[1]})`;

async function construirAutoria(ana) {
  const ids = {};
  let r = await ana.of({ cmd: 'criar', nome: 'Passo' });
  ids.passo = r.criacao;
  // "desenha um deslocamento de três metros": o gizmo da cena envia este mesmo comando com o vetor arrastado
  await ana.of({ cmd: 'no.adicionar', criacao: ids.passo, op: 'vetor.deslocar', p: { vetor_cm: [300, 0, 0], duracao_ms: 150 }, rotulo: 'Passo', ligar_de: 'n0', porta: 'ao_ativar' });
  await ana.of({ cmd: 'salvar', criacao: ids.passo, nota: 'deslocamento de 3 m' });
  r = await ana.of({ cmd: 'criar', nome: 'Pulso' });
  ids.pulso = r.criacao;
  await ana.of({ cmd: 'no.adicionar', criacao: ids.pulso, op: 'limiar.anel', p: { raio_cm: 100, ancora: 'autor' }, rotulo: 'Pulso', ligar_de: 'n0', porta: 'ao_ativar' });
  await ana.of({ cmd: 'salvar', criacao: ids.pulso });
  r = await ana.of({ cmd: 'criar', nome: 'Travessia' });
  ids.travessia = r.criacao;
  const a = await ana.of({ cmd: 'incorporar', criacao: ids.travessia, alvo: ids.passo, ligar_de: 'n0', porta: 'ao_ativar' });
  await ana.of({ cmd: 'incorporar', criacao: ids.travessia, alvo: ids.pulso, ligar_de: a.no, porta: 'ao_terminar' });
  const s = await ana.of({ cmd: 'salvar', criacao: ids.travessia, nota: 'Passo, depois Pulso' });
  ids.travessia_digest = s.digest;
  return ids;
}

async function contatoTriplo(ana, bruno, ma, mb, filtro = {}) {
  const ea = await ana.call('esperar', { tipo: 'area.contato', desde: ma, filtro });
  const eb = await bruno.call('esperar', { tipo: 'area.contato', desde: mb, filtro });
  await sleep(50);
  const srv = logServidor().filter((l) => l.tipo === 'area.contato' && ea && l.id === ea.ev.id && l.exec === ea.ev.exec).pop();
  return { servidor: srv, ana: ea, bruno: eb };
}

function resumoContato(c, nome) {
  if (!c) return null;
  const e = c.ev || c;
  const r = (nome ? e.resultados.find((x) => x.nome === nome) : e.resultados.find((x) => x.resultado !== 'fora') || e.resultados[0]) || {};
  return { centro: e.centro, raio: e.raio, t: e.t, resultado: r.resultado, motivo: r.motivo, ...(c.recebido_servidor_est ? { recebido_em: c.recebido_servidor_est, atraso_ms: c.atraso_ms } : {}) };
}

async function main() {
  rmSync(RUN, { recursive: true, force: true });
  mkdirSync(path.join(RUN, 'logs'), { recursive: true });
  mkdirSync(path.join(RUN, 'clientes'), { recursive: true });
  mkdirSync(EVID, { recursive: true });
  const pid1 = await iniciarServidor();
  const ana = new Bot('Ana'), bruno = new Bot('Bruno');
  const ca = await ana.call('conectar', { url: URL, nome: 'Ana' });
  const cb = await bruno.call('conectar', { url: URL, nome: 'Bruno' });
  relatorio.processos = { servidor_pid: pid1, ana_pid: ana.p.pid, bruno_pid: bruno.p.pid, orquestrador_pid: process.pid };
  verificar(ana.p.pid !== bruno.p.pid && bruno.p.pid !== pid1, 'servidor e jogadores em processos distintos', relatorio.processos);
  verificar(ca.topologia.startsWith('servidor dedicado'), 'topologia anunciada pelo servidor: ' + ca.topologia);

  console.log('\n[1] Sala privada e treino compartilhado');
  const sala = await ana.pedir('sala.criar', { modo: 'duelo' });
  const sb = await bruno.pedir('sala.entrar', { codigo: sala.codigo });
  verificar(sala.ok && sb.ok && sb.assento === 1, `sala privada ${sala.codigo}: Ana no assento 0, Bruno no 1`);
  const tr = await ana.pedir('sala.treino');
  verificar(tr.ok && (await bruno.call('esperarFase', { fase: 'treino' })), 'os dois estão no treino');
  await ana.pedir('treino', { cmd: { cmd: 'treino.sem_recarga', valor: true } });

  console.log('\n[2] Primeiro teste de autoria: Passo, Pulso, Travessia e a inversão');
  const ids = await construirAutoria(ana);
  const alvo = await ana.pedir('treino', { cmd: { cmd: 'treino.alvo', nome: 'Alvo 90 cm', pos: [-300, -160, 0] } });
  await ana.call('equipar', { slot: 0, criacao: ids.travessia });
  await ana.pedir('treino', { cmd: { cmd: 'treino.posicionar', pos: [-600, -250, 0], yaw: 0 } });
  await sleep(150);
  let ma = await ana.call('marcador'), mb = await bruno.call('marcador');
  const p1 = await ana.call('sequencia', { passos: [{ em: 0, slot: 0, prever: true }] });
  const c1 = await contatoTriplo(ana, bruno, ma, mb);
  const conc = await ana.call('conciliacoes');
  const r1 = { servidor: resumoContato(c1.servidor, 'Alvo 90 cm'), cliente_ana: resumoContato(c1.ana, 'Alvo 90 cm'), cliente_bruno: resumoContato(c1.bruno, 'Alvo 90 cm'), previsao_local: conc[conc.length - 1] };
  verificar(p1[0].r.ok, 'servidor aceita a ativação de Travessia');
  verificar(r1.servidor.resultado === 'atingido' && Math.hypot(r1.servidor.centro[0] + 300, r1.servidor.centro[1] + 250) < 1, `Passo→Pulso: centro ${ponto(r1.servidor.centro)} raio ${r1.servidor.raio} → ${r1.servidor.resultado}`, r1);
  verificar(r1.cliente_ana.resultado === r1.servidor.resultado && r1.cliente_bruno.t === r1.servidor.t, 'os dois clientes recebem o mesmo contato que o servidor decidiu');
  verificar(conc.length && conc[conc.length - 1].tipo === 'aceita', 'previsão local do deslocamento foi aceita pelo servidor', conc[conc.length - 1]);
  const dup = await ana.of({ cmd: 'duplicar', criacao: ids.travessia, nome: 'Travessia invertida' });
  ids.invertida = dup.criacao;
  await ana.of({ cmd: 'sequencia.inverter', criacao: ids.invertida });
  await ana.of({ cmd: 'salvar', criacao: ids.invertida, nota: 'Pulso, depois Passo' });
  await ana.call('equipar', { slot: 1, criacao: ids.invertida });
  await sleep(500);
  await ana.pedir('treino', { cmd: { cmd: 'treino.posicionar', pos: [-600, -250, 0], yaw: 0 } });
  await sleep(150);
  ma = await ana.call('marcador'); mb = await bruno.call('marcador');
  await ana.call('sequencia', { passos: [{ em: 0, slot: 1 }] });
  const c2 = await contatoTriplo(ana, bruno, ma, mb);
  const r2 = { servidor: resumoContato(c2.servidor, 'Alvo 90 cm'), cliente_ana: resumoContato(c2.ana, 'Alvo 90 cm'), cliente_bruno: resumoContato(c2.bruno, 'Alvo 90 cm') };
  verificar(r2.servidor.resultado === 'fora' && Math.hypot(r2.servidor.centro[0] + 600, r2.servidor.centro[1] + 250) < 1, `Pulso→Passo: centro ${ponto(r2.servidor.centro)} raio ${r2.servidor.raio} → ${r2.servidor.resultado}`, r2);
  relatorio.secoes.autoria_travessia = { passo_depois_pulso: r1, pulso_depois_passo: r2, alvo: [-300, -160], autor_saida: [-600, -250] };

  console.log('\n[3] Contramaré: marca de saída, Travessia incorporada, janela de 300 ms');
  let r = await ana.of({ cmd: 'criar', nome: 'Contramaré' });
  ids.contramare = r.criacao;
  const nm = await ana.of({ cmd: 'no.adicionar', criacao: ids.contramare, op: 'marca.guardar', p: { ancora: 'autor' }, rotulo: 'Marca de saída', ligar_de: 'n0', porta: 'ao_ativar' });
  const nt = await ana.of({ cmd: 'incorporar', criacao: ids.contramare, alvo: ids.travessia, ligar_de: nm.no, porta: 'depois' });
  const nj = await ana.of({ cmd: 'no.adicionar', criacao: ids.contramare, op: 'cadencia.janela', p: { dura_ms: 300 }, rotulo: 'Janela de volta', ligar_de: nt.no, porta: 'ao_terminar' });
  const nv = await ana.of({ cmd: 'no.adicionar', criacao: ids.contramare, op: 'vetor.retornar', p: { duracao_ms: 120 }, rotulo: 'Volta à marca', ligar_de: nj.no, porta: 'ao_comando' });
  const st = await ana.of({ cmd: 'dados.ligar', criacao: ids.contramare, de: nm.no, porta: 'ponto', para: nv.no, entrada: 'destino' });
  await ana.of({ cmd: 'salvar', criacao: ids.contramare });
  const linha = st.analise.linha_do_tempo.map((s) => ({ etapa: s.rotulo, tipo: s.tipo, de: s.ini, ate: s.fim_max, manual: s.manual, escopo: s.escopo }));
  relatorio.secoes.contramare_linha_do_tempo = linha;
  relatorio.secoes.contramare_regras = st.analise.regras.map((x) => x.texto);
  verificar(linha.some((s) => s.tipo === 'janela' && s.de === 450 && s.manual === false) && linha.some((s) => s.etapa === 'Volta à marca' && s.manual), 'linha do tempo: janela 450–750 ms automática, volta marcada como manual');
  await ana.call('equipar', { slot: 2, criacao: ids.contramare });
  const casos = [{ nome: 'cedo', em: 300, volta: false }, { nome: 'dentro', em: 600, volta: true }, { nome: 'tarde', em: 800, volta: false }, { nome: 'ausente', em: null, volta: false }];
  const resCasos = [];
  for (const c of casos) {
    await ana.pedir('treino', { cmd: { cmd: 'treino.posicionar', pos: [-600, -250, 0], yaw: 0 } });
    await sleep(300);
    ma = await ana.call('marcador'); mb = await bruno.call('marcador');
    const passos = [{ em: 0, slot: 2 }];
    if (c.em !== null) passos.push({ em: c.em, slot: 2 });
    const seq = await ana.call('sequencia', { passos });
    const jr = await ana.call('esperar', { tipo: 'janela.resultado', desde: ma, ms: 3000 });
    const ja = await ana.call('esperar', { tipo: 'janela.abre', desde: ma, ms: 100 });
    const ini = await ana.call('esperar', { tipo: 'exec.inicio', desde: ma, ms: 100 });
    await sleep(500);
    const pos = await ana.call('posicao', {});
    const voltou = Math.hypot(pos.servidor[0] + 600, pos.servidor[1] + 250) < 2;
    const brunoVeJanela = (await bruno.call('observados', { tipo: 'janela.abre', desde: mb })).length;
    const x = { caso: c.nome, comando_ms: c.em, enviado_ms_real: seq[1] ? seq[1].real_ms : null, julgado_rel_ms: seq[1] ? seq[1].r.t_julgado - ini.ev.t : null, janela: [ja.ev.t_abre - ini.ev.t, ja.ev.t_fecha - ini.ev.t], resultado: jr.ev.resultado, pos_final: pos.servidor.slice(0, 2), voltou };
    resCasos.push(x);
    verificar(voltou === c.volta && (c.volta ? jr.ev.resultado === 'comando' : jr.ev.resultado === 'expirou'), `comando ${c.nome}: janela [${x.janela}] → ${jr.ev.resultado}, posição ${ponto(x.pos_final)}`, x);
    verificar(brunoVeJanela === 0, `comando ${c.nome}: a janela é privada (o oponente não a recebe)`);
  }
  relatorio.secoes.contramare_comandos = resCasos;

  console.log('\n[4] Anteparo de Bruno (abertura 90°): frente x flanco');
  r = await bruno.of({ cmd: 'criar', nome: 'Anteparo' });
  ids.anteparo = r.criacao;
  await bruno.of({ cmd: 'no.adicionar', criacao: ids.anteparo, op: 'lastro.anteparo', p: { abertura_graus: 90, gesto: 'bracos_cruzados', preparo_ms: 150, duracao_ms: 1500, cargas: 1 }, rotulo: 'Anteparo', ligar_de: 'n0', porta: 'ao_ativar' });
  await bruno.of({ cmd: 'salvar', criacao: ids.anteparo });
  await bruno.call('equipar', { slot: 0, criacao: ids.anteparo });
  await ana.call('equipar', { slot: 3, criacao: ids.pulso });
  await ana.pedir('treino', { cmd: { cmd: 'treino.limpar' } });
  const casosA = [{ nome: 'frente (0°)', ana: [-80, 250], absorve: true }, { nome: 'flanco (90° do eixo)', ana: [0, 170], absorve: false }];
  const resA = [];
  for (const c of casosA) {
    await bruno.pedir('treino', { cmd: { cmd: 'treino.posicionar', pos: [0, 250, 0], yaw: 180 } });
    await ana.pedir('treino', { cmd: { cmd: 'treino.posicionar', pos: [...c.ana, 0], yaw: 0 } });
    await sleep(250);
    ma = await ana.call('marcador'); mb = await bruno.call('marcador');
    await bruno.call('sequencia', { passos: [{ em: 0, slot: 0 }] });
    await sleep(200);
    await ana.call('sequencia', { passos: [{ em: 0, slot: 3 }] });
    const cc = await contatoTriplo(ana, bruno, ma, mb);
    const gi = await ana.call('esperar', { tipo: 'guarda.inicio', desde: ma, ms: 100 });
    const res = { caso: c.nome, servidor: resumoContato(cc.servidor, 'Bruno'), ana_ve_frente: gi ? { eixo_yaw: gi.ev.eixo_yaw, abertura: gi.ev.abertura, gesto: gi.ev.gesto, ativa_em_ms: gi.ev.t_ativa - gi.ev.t } : null };
    resA.push(res);
    verificar((res.servidor.resultado === 'absorvido') === c.absorve, `${c.nome}: ${res.servidor.resultado} — ${res.servidor.motivo}`, res);
    verificar(res.ana_ve_frente && res.ana_ve_frente.abertura === 90 && Math.abs(res.ana_ve_frente.eixo_yaw) === 180, `${c.nome}: Ana recebe a frente protegida (eixo ${res.ana_ve_frente && res.ana_ve_frente.eixo_yaw}°, abertura 90°, gesto ${res.ana_ve_frente && res.ana_ve_frente.gesto})`);
    await sleep(1700);
  }
  relatorio.secoes.anteparo = resA;

  console.log('\n[5] Salvar, fechar, recarregar e equipar com os mesmos identificadores');
  const antes = (await ana.of({ cmd: 'listar' })).criacoes.concat((await bruno.of({ cmd: 'listar' })).criacoes);
  await ana.call('fechar'); await bruno.call('fechar');
  await ana.call('conectar', { url: URL, nome: 'Ana', token: ca.token });
  await bruno.call('conectar', { url: URL, nome: 'Bruno', token: cb.token });
  const ra = await ana.pedir('sala.entrar', { codigo: sala.codigo });
  const rb = await bruno.pedir('sala.entrar', { codigo: sala.codigo });
  verificar(ra.ok && rb.ok && ra.assento === 0 && rb.assento === 1, 'reconexão retoma os mesmos assentos (token)');
  const depois = (await ana.of({ cmd: 'listar' })).criacoes.concat((await bruno.of({ cmd: 'listar' })).criacoes);
  const iguais = antes.every((a) => depois.some((d) => d.criacao === a.criacao && d.digest === a.digest && d.ultima_revisao === a.ultima_revisao));
  verificar(iguais, `receitas recarregadas com o mesmo id, revisão e digest (${antes.length} criações)`);
  const pacotePulso = (await ana.of({ cmd: 'exportar', criacao: ids.pulso })).pacote;
  const imp = await bruno.of({ cmd: 'importar', pacote: pacotePulso });
  ids.pulso_bruno = imp.criacoes.find((c) => c.raiz).criacao;
  verificar(imp.ok && imp.criacoes.find((c) => c.raiz).digest === pacotePulso.raiz, 'Bruno importa o Pulso de Ana com o mesmo digest');
  const pacoteRuim = JSON.parse(JSON.stringify(pacotePulso));
  pacoteRuim.objetos[pacoteRuim.raiz].nos[1].p.raio_cm = 700;
  const pub = await bruno.pedir('publicar', { pacote: pacoteRuim });
  verificar(!pub.ok, 'servidor recusa pacote adulterado: ' + pub.erro);
  const equipTreino = {};
  for (const [slot, id] of [[0, ids.travessia], [1, ids.contramare], [2, ids.pulso]]) equipTreino['ana' + slot] = (await ana.call('equipar', { slot, criacao: id })).digest;
  for (const [slot, id] of [[0, ids.anteparo], [2, ids.pulso_bruno]]) equipTreino['bruno' + slot] = (await bruno.call('equipar', { slot, criacao: id })).digest;
  relatorio.secoes.equipamento = equipTreino;

  console.log('\n[6] Duelo até o placar, entrada tardia, revanche e saída durante efeito');
  await ana.pedir('sala.pronto', { valor: true });
  await bruno.pedir('sala.pronto', { valor: true });
  const duelo = await ana.pedir('sala.duelo');
  verificar(duelo.ok, 'duelo inicia: ' + JSON.stringify(duelo.problemas || 'ok'));
  const dIni = logServidor().filter((l) => l.tipo === 'duelo.inicio').pop();
  const eqDuelo = Object.fromEntries(dIni.equipamentos.flatMap((e) => Object.entries(e.equipamento).map(([s, d]) => [(e.nome === 'Ana' ? 'ana' : 'bruno') + s, d])));
  verificar(Object.entries(equipTreino).every(([k, v]) => eqDuelo[k] === v), 'treino e duelo usam os mesmos identificadores e versões (digests)', eqDuelo);
  const salaAna = await ana.call('sala');
  const entBruno = salaAna.sala.assentos[1].ent;
  let pontos = 0;
  let caio = null;
  const pontosObs = [];
  for (let rodada = 0; rodada < 6; rodada++) {
    if (!(await ana.call('esperarFase', { fase: 'duelo', ms: 8000 }))) break;
    const pb = await ana.call('posicao', { ent: entBruno });
    const pa = await ana.call('posicao', {});
    const oeste = pa.servidor[0] < pb.servidor[0];
    const alvoX = pb.servidor[0] + (oeste ? -300 : 300), alvoY = pb.servidor[1] + (oeste ? -90 : 90);
    const yaw = oeste ? 0 : 180;
    const anda = await ana.call('andarAte', { x: alvoX, y: alvoY, yaw });
    await ana.call('mover', { dir: [0, 0], yaw });
    // Bruno ergue o Anteparo virado para Ana (frente protegida); Ana usa a abertura lateral.
    await bruno.call('mover', { dir: [0, 0], yaw: oeste ? 180 : 0 });
    await sleep(80);
    ma = await ana.call('marcador'); mb = await bruno.call('marcador');
    await bruno.call('sequencia', { passos: [{ em: 0, slot: 0 }] });
    if (rodada === 1 && !caio) {
      caio = new Bot('Caio');
      await caio.call('conectar', { url: URL, nome: 'Caio' });
      await sleep(30);
      const entrada = await caio.pedir('sala.entrar', { codigo: sala.codigo });
      relatorio.secoes.entrada_tardia = entrada.snapshot;
      verificar(entrada.ok && entrada.snapshot.fase === 'duelo' && entrada.snapshot.guardas >= 1, `entrada tardia como espectador recebe o efeito ativo (guardas ativas: ${entrada.snapshot.guardas})`, entrada.snapshot);
    }
    await sleep(160);
    await ana.call('sequencia', { passos: [{ em: 0, slot: 0, prever: true, mira_yaw: yaw }] });
    const pa2 = await ana.call('esperar', { tipo: 'ponto', desde: ma, ms: 3000 });
    const pb2 = await bruno.call('esperar', { tipo: 'ponto', desde: mb, ms: 1500 });
    if (pa2) { pontos++; pontosObs.push({ rodada, chegada: anda, ana: pa2, bruno: pb2 }); }
    if (!(await ana.call('esperarFase', { fase: 'contagem', ms: 2500 }))) if ((await ana.call('sala')).fase === 'fim') break;
  }
  await sleep(200);
  const fim = logServidor().filter((l) => l.tipo === 'sala.fase' && l.fase === 'fim').pop();
  verificar(fim && pontos === 3 && fim.motivo === 'placar', `partida encerrada pelo placar ${JSON.stringify(fim && fim.placar)} (${pontos} pontos)`);
  const srvPontos = logServidor().filter((l) => l.tipo === 'ponto');
  const decisivo = srvPontos[srvPontos.length - 1];
  const obsA = pontosObs[pontosObs.length - 1];
  const imp_s = decisivo.impacto;
  const tripla = {
    servidor: { t: imp_s.t, centro: imp_s.centro, raio: imp_s.raio, alvo: imp_s.alvo_nome, pos_alvo: imp_s.pos_alvo, dist_cm: imp_s.dist, digest: imp_s.digest, placar: decisivo.placar },
    cliente_ana: { recebido_em_servidor_est: obsA.ana.recebido_servidor_est, atraso_ms: obsA.ana.atraso_ms, centro: obsA.ana.ev.impacto.centro, pos_alvo_visto: obsA.ana.observado[entBruno], placar: obsA.ana.ev.placar },
    cliente_bruno: { recebido_em_servidor_est: obsA.bruno.recebido_servidor_est, atraso_ms: obsA.bruno.atraso_ms, centro: obsA.bruno.ev.impacto.centro, pos_alvo_visto: obsA.bruno.observado[entBruno], placar: obsA.bruno.ev.placar },
  };
  relatorio.secoes.impacto_decisivo = tripla;
  const motivoDecisivo = logServidor().filter((l) => l.tipo === 'area.contato' && l.id === imp_s.area).pop();
  relatorio.secoes.impacto_decisivo.motivo_servidor = motivoDecisivo && motivoDecisivo.resultados.find((x) => x.nome === 'Bruno');
  verificar(JSON.stringify(tripla.cliente_ana.centro) === JSON.stringify(tripla.servidor.centro) && JSON.stringify(tripla.cliente_bruno.centro) === JSON.stringify(tripla.servidor.centro), 'impacto decisivo idêntico no servidor e nos dois clientes', tripla);
  verificar(relatorio.secoes.impacto_decisivo.motivo_servidor && /anteparo não cobre/.test(relatorio.secoes.impacto_decisivo.motivo_servidor.motivo), 'o ponto veio pela abertura lateral do Anteparo: ' + (relatorio.secoes.impacto_decisivo.motivo_servidor || {}).motivo);
  await ana.pedir('sala.revanche');
  const rev = await bruno.pedir('sala.revanche');
  verificar(rev.ok && rev.iniciada, 'revanche aceita pelos dois e iniciada');
  const salaRev = await ana.call('sala');
  verificar(salaRev.sala.partida === 2 && salaRev.sala.lados_trocados === true, 'revanche: partida 2, lados trocados, mesmos equipamentos');
  // saída durante efeito
  await ana.call('esperarFase', { fase: 'duelo', ms: 6000 });
  mb = await bruno.call('marcador');
  await ana.call('sequencia', { passos: [{ em: 0, slot: 0 }] });
  await sleep(60);
  await ana.call('fechar');
  const pausa = await bruno.call('esperar', { tipo: 'sala.fase', filtro: { fase: 'pausa' }, desde: mb, ms: 3000 });
  await sleep(100);
  const fimExec = logServidor().filter((l) => l.tipo === 'exec.fim' && l.motivo === 'desconexão do autor').pop();
  verificar(pausa && fimExec, `saída durante efeito: execução ${fimExec && fimExec.exec} cancelada e recursos liberados; partida em pausa aguardando ${pausa && pausa.ev.aguardando}`);
  await ana.call('conectar', { url: URL, nome: 'Ana', token: ca.token });
  const volta = await ana.pedir('sala.entrar', { codigo: sala.codigo });
  const retomada = await bruno.call('esperar', { tipo: 'sala.fase', filtro: { fase: 'contagem' }, desde: mb, ms: 3000 });
  verificar(volta.ok && retomada, 'reconexão retoma o duelo (contagem)');

  console.log('\n[7] Oficina: nova revisão de Travessia; Contramaré antiga reproduz a versão do autor');
  const raioAnuncio = (sim) => sim.eventos.find((e) => e.tipo === 'area.anuncio').raio;
  const cen = { autor: { pos: [-600, -250, 0], yaw: 0 } };
  await ana.of({ cmd: 'no.param', criacao: ids.pulso, no: 'n1', nome: 'raio_cm', valor: 150 });
  await ana.of({ cmd: 'salvar', criacao: ids.pulso, nota: 'raio 1,5 m' });
  let dps = await ana.of({ cmd: 'dependencias', criacao: ids.travessia });
  const noPulso = dps.dependencias.find((d) => d.desatualizada).no;
  await ana.of({ cmd: 'dependencias.atualizar', criacao: ids.travessia, no: noPulso });
  const tr2 = await ana.of({ cmd: 'salvar', criacao: ids.travessia, nota: 'usa Pulso r2' });
  verificar(tr2.revisao === 2, `Travessia r2 publicada (${tr2.digest.slice(0, 19)}…)`);
  const sOld = await ana.of({ cmd: 'simular', criacao: ids.contramare, cenario: cen });
  dps = await ana.of({ cmd: 'dependencias', criacao: ids.contramare });
  const noTrav = dps.dependencias.find((d) => d.desatualizada);
  const cmp = await ana.of({ cmd: 'comparar', criacao: ids.travessia, a: 1, b: 2 });
  const up = await ana.of({ cmd: 'dependencias.atualizar', criacao: ids.contramare, no: noTrav.no });
  const sNew = await ana.of({ cmd: 'simular', criacao: ids.contramare, cenario: cen });
  const rv = await ana.of({ cmd: 'dependencias.reverter', criacao: ids.contramare, no: noTrav.no });
  const sRev = await ana.of({ cmd: 'simular', criacao: ids.contramare, cenario: cen });
  relatorio.secoes.revisoes = { travessia_r1: ids.travessia_digest, travessia_r2: tr2.digest, contramare_usa: noTrav, comparar: { dependencias: cmp.dependencias }, diferenca_oferecida: up.diferenca && up.diferenca.dependencias, raio_antiga: raioAnuncio(sOld), raio_apos_aceitar: raioAnuncio(sNew), raio_apos_desfazer: raioAnuncio(sRev) };
  verificar(raioAnuncio(sOld) === 100 && noTrav.revisao === 1 && noTrav.ultima_revisao === 2, 'Contramaré antiga reproduz Travessia r1 (anel de 1 m) e o editor oferece r2');
  verificar(cmp.dependencias.length === 1 && up.diferenca, 'editor compara as revisões antes da decisão');
  verificar(raioAnuncio(sNew) === 150 && raioAnuncio(sRev) === 100 && rv.ok, 'aceitar mostra 1,5 m; desfazer a atualização volta a 1 m');

  console.log('\n[8] Reiniciar o jogo (servidor e clientes) e repetir o teste de versão');
  await ana.call('fechar'); await bruno.call('fechar');
  if (caio) await caio.call('sair');
  await pararServidor();
  const pid2 = await iniciarServidor();
  verificar(pid2 !== pid1, `servidor reiniciado (pid ${pid1} → ${pid2}); acervos recarregados do disco`);
  await ana.call('conectar', { url: URL, nome: 'Ana', token: ca.token });
  await bruno.call('conectar', { url: URL, nome: 'Bruno', token: cb.token });
  const sala2 = await ana.pedir('sala.criar', { modo: 'duelo' });
  await bruno.pedir('sala.entrar', { codigo: sala2.codigo });
  await ana.pedir('sala.treino');
  await ana.pedir('treino', { cmd: { cmd: 'treino.sem_recarga', valor: true } });
  const ab = await ana.of({ cmd: 'abrir', criacao: ids.contramare });
  const dp2 = ab.dependencias.find((d) => d.no === noTrav.no);
  const sR = await ana.of({ cmd: 'simular', criacao: ids.contramare, cenario: cen });
  await ana.of({ cmd: 'dependencias.atualizar', criacao: ids.contramare, no: noTrav.no });
  const sR2 = await ana.of({ cmd: 'simular', criacao: ids.contramare, cenario: cen });
  const desf = await ana.of({ cmd: 'desfazer', criacao: ids.contramare });
  const sR3 = await ana.of({ cmd: 'simular', criacao: ids.contramare, cenario: cen });
  verificar(dp2.revisao === 1 && dp2.desatualizada && raioAnuncio(sR) === 100, 'após reiniciar: Contramaré ainda aponta Travessia r1 e reproduz 1 m');
  verificar(raioAnuncio(sR2) === 150 && desf.ok && raioAnuncio(sR3) === 100, 'após reiniciar: aceitar → 1,5 m; desfazer → 1 m');
  await ana.call('equipar', { slot: 2, criacao: ids.contramare, revisao: 1 });
  await bruno.call('equipar', { slot: 0, criacao: ids.anteparo });
  await ana.pedir('treino', { cmd: { cmd: 'treino.posicionar', pos: [-600, -250, 0], yaw: 0 } });
  await sleep(200);
  ma = await ana.call('marcador');
  await ana.call('sequencia', { passos: [{ em: 0, slot: 2 }] });
  const an = await ana.call('esperar', { tipo: 'area.anuncio', desde: ma });
  verificar(an && an.ev.raio === 100, 'após reiniciar, a Contramaré r1 em jogo (servidor) anuncia o anel de 1 m');
  relatorio.secoes.reinicio = { pid_antes: pid1, pid_depois: pid2, contramare_dep: dp2, raio_r1_simulado: raioAnuncio(sR), raio_em_jogo: an && an.ev.raio };

  console.log('\n[9] Criação surpresa de Bruno, com previsão escrita antes da execução');
  const pacPasso = (await ana.of({ cmd: 'exportar', criacao: ids.passo })).pacote;
  const ip = await bruno.of({ cmd: 'importar', pacote: pacPasso });
  const passoB = (ip.criacoes.find((c) => c.raiz) || {}).criacao || (await bruno.of({ cmd: 'listar' })).criacoes.find((c) => c.digest === pacPasso.raiz).criacao;
  r = await bruno.of({ cmd: 'criar', nome: 'Esteira' });
  ids.esteira = r.criacao;
  await bruno.of({ cmd: 'incorporar', criacao: ids.esteira, alvo: passoB, ligar_de: 'n0', porta: 'ao_ativar' });
  const vg = await bruno.of({ cmd: 'no.adicionar', criacao: ids.esteira, op: 'limiar.vigiar', p: { superficie: 'agua', duracao_ms: 200 }, rotulo: 'Vigia do canal', ligar_de: 'n0', porta: 'ao_ativar' });
  await bruno.of({ cmd: 'no.adicionar', criacao: ids.esteira, op: 'limiar.anel', p: { ancora: 'evento', raio_cm: 120, preparo_ms: 200 }, rotulo: 'Respingo', ligar_de: vg.no, porta: 'ao_entrar' });
  const se = await bruno.of({ cmd: 'salvar', criacao: ids.esteira, nota: 'surpresa' });
  // previsão a partir das regras escritas, sem executar
  const S = [200, -250], borda = -70, passo = 300, dur = 150;
  const frac = (borda - S[1]) / passo;
  const previsao = { escrita_em: new Date().toISOString(), regras: se.analise.regras.map((x) => x.texto), centro: [S[0], borda], raio: 120, cruzamento_ms: frac * dur, contato_ms_aprox: Math.round(frac * dur + 200), alvos: { T1: { pos: [300, -70], previsto: Math.hypot(100, 0) <= 154 ? 'atingido' : 'fora' }, T2: { pos: [200, -250], previsto: Math.hypot(0, 180) <= 154 ? 'atingido' : 'fora' } }, controle_sem_agua: 'nenhum anel' };
  writeFileSync(path.join(RUN, 'previsao_esteira.json'), JSON.stringify(previsao, null, 2));
  await bruno.call('equipar', { slot: 1, criacao: ids.esteira });
  await bruno.pedir('treino', { cmd: { cmd: 'treino.alvo', nome: 'T1', pos: [300, -70, 0] } });
  await bruno.pedir('treino', { cmd: { cmd: 'treino.alvo', nome: 'T2', pos: [200, -250, 0] } });
  await bruno.pedir('treino', { cmd: { cmd: 'treino.posicionar', pos: [200, -250, 0], yaw: 90 } });
  await ana.pedir('treino', { cmd: { cmd: 'treino.posicionar', pos: [-900, 300, 0], yaw: 0 } });
  await sleep(200);
  mb = await bruno.call('marcador');
  await bruno.call('sequencia', { passos: [{ em: 0, slot: 1 }] });
  const ce = await bruno.call('esperar', { tipo: 'area.contato', desde: mb });
  const ini2 = await bruno.call('esperar', { tipo: 'exec.inicio', desde: mb, ms: 100 });
  await sleep(60);
  const srvE = logServidor().filter((l) => l.tipo === 'area.contato' && ce && l.id === ce.ev.id && l.exec === ce.ev.exec).pop();
  const res = Object.fromEntries(srvE.resultados.map((x) => [x.nome, x.resultado]));
  const obsE = { centro: srvE.centro, raio: srvE.raio, contato_ms: srvE.t - ini2.ev.t, T1: res.T1, T2: res.T2 };
  const erro = Math.hypot(obsE.centro[0] - previsao.centro[0], obsE.centro[1] - previsao.centro[1]);
  await bruno.pedir('treino', { cmd: { cmd: 'treino.posicionar', pos: [200, 150, 0], yaw: 90 } });
  await sleep(200);
  mb = await bruno.call('marcador');
  await bruno.call('sequencia', { passos: [{ em: 0, slot: 1 }] });
  await sleep(700);
  const semAgua = (await bruno.call('observados', { tipo: 'area.anuncio', desde: mb })).length;
  relatorio.secoes.surpresa = { previsao, servidor: obsE, erro_centro_cm: erro, controle_sem_agua_areas: semAgua };
  verificar(erro < 1 && obsE.T1 === previsao.alvos.T1.previsto && obsE.T2 === previsao.alvos.T2.previsto && Math.abs(obsE.contato_ms - previsao.contato_ms_aprox) <= 17, `previsão x servidor: centro ${ponto(obsE.centro)} (erro ${erro.toFixed(1)} cm), contato ${obsE.contato_ms} ms (previsto ~${previsao.contato_ms_aprox}), T1 ${obsE.T1}, T2 ${obsE.T2}`, relatorio.secoes.surpresa);
  verificar(semAgua === 0, 'controle: sem cruzar o canal, a Esteira não cria anel');

  console.log('\n[10] Latência e perda por sentido');
  const confA = { lag_subida_ms: 100, lag_descida_ms: 100, nota: 'a subida passa de 0 para 100 ms entre a ativação e o 2º comando' };
  const confB = { lag_subida_ms: 80, lag_descida_ms: 40, perda_subida: 0.05, perda_descida: 0.05, duplicar: 0.25 };
  await bruno.pedir('rede', confB);
  await ana.pedir('rede', { lag_subida_ms: 0, lag_descida_ms: 100 });
  await sleep(1500);
  const rttB = await bruno.call('rtt');
  await ana.pedir('treino', { cmd: { cmd: 'treino.limpar' } });
  await ana.pedir('treino', { cmd: { cmd: 'treino.posicionar', pos: [-600, -250, 0], yaw: 0 } });
  await sleep(500);
  ma = await ana.call('marcador');
  // ativação chega sem atraso; o 2º comando (700 ms depois, dentro da janela) chega 100 ms atrasado, depois de ela fechar
  const sq = await ana.call('sequencia', { passos: [{ em: 0, slot: 2 }, { em: 300, rede: { lag_subida_ms: 100 } }, { em: 700, slot: 2 }] });
  const jr = await ana.call('esperar', { tipo: 'janela.resultado', desde: ma, ms: 4000 });
  const ie = await ana.call('esperar', { tipo: 'exec.inicio', desde: ma, ms: 100 });
  const rttA = await ana.call('rtt');
  const cmd2 = sq[2].r;
  const compA = { comando_local_ms: 700, chegada_rel_ms: cmd2.t_servidor - ie.ev.t, julgado_rel_ms: cmd2.t_julgado - ie.ev.t, janela: [450, 750], decisao_em_ms: jr.ev.t_decisao - ie.ev.t, decisao: jr.ev.resultado, sem_compensacao_seria: cmd2.t_servidor - ie.ev.t < 750 ? 'comando' : 'tarde' };
  verificar(jr.ev.resultado === 'comando' && compA.chegada_rel_ms > 750 && compA.julgado_rel_ms < 750, `latência variável: o 2º comando chegou em ${compA.chegada_rel_ms} ms (janela fechou em 750), julgado em ${compA.julgado_rel_ms} ms pela linha do tempo do jogador → ${jr.ev.resultado} (sem compensação: ${compA.sem_compensacao_seria})`, compA);
  mb = await bruno.call('marcador');
  for (let i = 0; i < 10; i++) { await bruno.call('sequencia', { passos: [{ em: 0, slot: 0 }] }); await bruno.call('mover', { dir: [i % 2 ? 1 : -1, 0], yaw: 180 }); await sleep(250); }
  await bruno.call('mover', { dir: [0, 0], yaw: 180 });
  const dd = await bruno.call('botaoRepetido', { slot: 0, seq: 90000 });
  await sleep(1200);
  const obsB = await bruno.call('observados', { desde: mb });
  const atrasos = obsB.map((x) => x.atraso_ms).filter((x) => x >= 0).sort((a, b) => a - b);
  const linhasB = logServidor().filter((l) => l.log === 'entrada' && l.jogador === 'Bruno').slice(-11);
  const dupl = logServidor().filter((l) => l.log === 'entrada.duplicada' && l.jogador === 'Bruno');
  const redeB = (await bruno.pedir('rede', {})).rede;
  const redeA = (await ana.pedir('rede', {})).rede;
  relatorio.secoes.rede = {
    configuracao: { Ana: confA, Bruno: confB },
    medido: { Ana: { rtt_estimado_ms: Math.round(rttA.rtt), condicionador: redeA }, Bruno: { rtt_estimado_ms: Math.round(rttB.rtt), condicionador: redeB } },
    autoritativo: { compensacao_ana: compA, entradas_bruno: linhasB.map((l) => ({ seq: l.seq, t_est: l.t_est, t_julgado: l.t_julgado, chegada: l.t, aceita: l.aceita, motivo: l.motivo })), duplicadas_descartadas: dupl.length },
    observado_cliente_bruno: { eventos: atrasos.length, atraso_ms_mediana: atrasos[Math.floor(atrasos.length / 2)], atraso_ms_p95: atrasos[Math.floor(atrasos.length * 0.95)], atraso_ms_max: atrasos[atrasos.length - 1] },
    deduplicacao_manual: dd,
  };
  verificar(linhasB.filter((l) => l.aceita).length >= 5, `entradas de Bruno com perda/duplicação: ${linhasB.filter((l) => l.aceita).length} de ${linhasB.length} aceitas no servidor`);
  verificar(dd.segundo && dd.segundo.duplicado === true && dupl.length >= 1, `comando duplicado (mesmo seq) descartado pelo servidor; ${dupl.length} duplicata(s) no registro (inclui as injetadas pelo condicionador)`);
  verificar(relatorio.secoes.rede.observado_cliente_bruno.atraso_ms_mediana >= 35, `cliente de Bruno observa os eventos com atraso mediano de ${relatorio.secoes.rede.observado_cliente_bruno.atraso_ms_mediana} ms (descida configurada: 40 ms)`);
  await ana.pedir('rede', { lag_subida_ms: 0, lag_descida_ms: 0 });
  await bruno.pedir('rede', { lag_subida_ms: 0, lag_descida_ms: 0, perda_subida: 0, perda_descida: 0, duplicar: 0 });

  // registros
  await ana.call('salvarRegistro', { caminho: path.join(RUN, 'clientes/ana.jsonl') });
  await bruno.call('salvarRegistro', { caminho: path.join(RUN, 'clientes/bruno.jsonl') });
  await ana.call('sair'); await bruno.call('sair');
  await pararServidor();
  relatorio.fim = new Date().toISOString();
  relatorio.aprovadas = relatorio.verificacoes.filter((v) => v.ok).length;
  relatorio.falhas = falhas;
  relatorio.ids = ids;
  writeFileSync(path.join(EVID, 'e2e_relatorio.json'), JSON.stringify(relatorio, null, 2));
  copyFileSync(path.join(RUN, 'previsao_esteira.json'), path.join(EVID, 'previsao_esteira.json'));
  console.log(`\nresultado: ${relatorio.aprovadas} verificações aprovadas, ${falhas} falharam`);
  process.exit(falhas ? 1 : 0);
}

main().catch(async (e) => {
  console.error(e);
  try { if (servidor) servidor.kill('SIGKILL'); } catch {}
  writeFileSync(path.join(EVID, 'e2e_relatorio.json'), JSON.stringify({ ...relatorio, erro: String(e.stack || e) }, null, 2));
  process.exit(2);
});
