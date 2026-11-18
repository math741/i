// Percurso pela interface real (Chromium headless, duas páginas = dois clientes). Clica nos mesmos botões,
// listas e teclas que um jogador usa. Captura o quadro exato do anúncio e do contato nas câmeras de Ana e de
// Bruno, em dois níveis gráficos. Saída: evidencias/ui/*.png e evidencias/ui_relatorio.json.
import { spawn } from 'node:child_process';
import { mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import os from 'node:os';
import path from 'node:path';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const RAIZ = path.resolve(import.meta.dirname, '../..');
const EV = path.join(RAIZ, 'evidencias/ui');
const RUN = path.join(RAIZ, 'run/ui');
const PORTA = 8797;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const rel = { inicio: new Date().toISOString(), verificacoes: [], capturas: [], fps: {} };
let falhas = 0;
const ok = (c, d, x) => { rel.verificacoes.push({ ok: !!c, desc: d, ...(x !== undefined ? { dados: x } : {}) }); if (!c) falhas++; console.log(`${c ? '  ok   ' : '  FALHA'} ${d}${!c && x !== undefined ? ' :: ' + JSON.stringify(x).slice(0, 300) : ''}`); };

rmSync(RUN, { recursive: true, force: true });
mkdirSync(EV, { recursive: true });
const srv = spawn(path.join(RAIZ, 'build/forja_server'), ['--porta', String(PORTA), '--cliente', path.join(RAIZ, 'client'), '--dados', path.join(RUN, 'saves'), '--logs', path.join(RUN, 'logs')], { stdio: 'ignore' });
await sleep(400);
const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const ver = browser.version();
rel.ambiente = { navegador: 'Chromium ' + ver + ' headless', render: 'SwiftShader (CPU, sem GPU)', cpu: os.cpus()[0].model, nucleos: os.cpus().length, ram_gb: Math.round(os.totalmem() / 2 ** 30), resolucao: '1280x720', captura: 'canvas.toDataURL no instante do evento + Playwright page.screenshot' };

async function pagina(nome, extra = '') {
  const ctx = await browser.newContext({ viewport: { width: 1280, height: 720 } });
  const p = await ctx.newPage();
  p.erros = [];
  p.on('pageerror', (e) => p.erros.push(e.message));
  await p.goto(`http://127.0.0.1:${PORTA}/?nome=${nome}&q=baixo${extra}`);
  await p.waitForFunction(() => window.forja && window.forja.oficina, null, { timeout: 30000 });
  return p;
}
async function shot(p, nome, desc) { const f = path.join(EV, nome); await p.screenshot({ path: f }); rel.capturas.push({ arquivo: 'evidencias/ui/' + nome, desc }); }
async function selecionarOpcao(p, sel, contem) {
  const v = await p.$eval(sel, (el, c) => { const o = [...el.options].find((x) => c.every((k) => x.textContent.includes(k))); return o ? o.value : null; }, contem);
  if (v === null) throw new Error(`opção ${contem} não encontrada em ${sel}`);
  await p.selectOption(sel, v);
}
async function esperarOficina(p) { await p.waitForFunction(() => !document.querySelector('#ofTitulo').textContent.includes('crie'), null, { timeout: 20000 }); await sleep(300); }
async function aguardarSim(p) { await sleep(400); await p.waitForFunction(() => window.forja.oficina.sim !== undefined, null, { timeout: 20000 }); await sleep(600); }
async function novaCriacao(p, nome) { await p.fill('[data-testid=nome-criacao]', nome); await p.click('[data-testid=nova-criacao]'); await p.waitForFunction((n) => document.querySelector('#ofTitulo').textContent.includes(n), nome); await sleep(300); }
async function salvar(p) { await p.click('[data-testid=salvar-revisao]'); await sleep(900); }
const explica = (p) => p.$eval('#ofExplica', (e) => e.innerText);

// captura do quadro exato do evento (renderizado e lido no mesmo instante em que o evento chega)
async function armarCaptura(p) {
  await p.evaluate(() => {
    window.__cap = {};
    if (window.__capArmada) return;
    window.__capArmada = true;
    window.forja.mundo.aoEvento((e) => {
      if (e.tipo !== 'area.anuncio' && e.tipo !== 'area.contato') return;
      const f = window.forja;
      const ts = f.con.agoraServidor();
      f.cena.sincronizar(f.mundo, ts);
      f.cena.posicionarCamera(f.mundo, ts, f.cameras[f.camIdx] === 'oponente' ? f._oponente() : f.mundo.meuEnt, f.yaw);
      f.cena.render();
      window.__cap[e.tipo] = { png: f.cena.renderer.domElement.toDataURL('image/png'), t: e.t, recebido: Math.round(ts), resultados: e.resultados, centro: e.centro, raio: e.raio };
    });
  });
}
async function salvarCaptura(p, prefixo, desc) {
  await p.waitForFunction(() => window.__cap && window.__cap['area.contato'], null, { timeout: 15000 });
  const c = await p.evaluate(() => window.__cap);
  const out = {};
  for (const [k, v] of Object.entries(c)) {
    const nome = `${prefixo}_${k === 'area.anuncio' ? 'anuncio' : 'contato'}.png`;
    writeFileSync(path.join(EV, nome), Buffer.from(v.png.split(',')[1], 'base64'));
    rel.capturas.push({ arquivo: 'evidencias/ui/' + nome, desc: `${desc} — ${k} (servidor t=${v.t}, recebido ${v.recebido})` });
    out[k] = { t: v.t, recebido: v.recebido, centro: v.centro, raio: v.raio, resultados: v.resultados && v.resultados.map((r) => ({ nome: r.nome, resultado: r.resultado })) };
  }
  return out;
}
async function medirFps(p, ms = 6000) { const a = await p.evaluate(() => window.forja.cena.stats.quadros); await sleep(ms); return p.evaluate(() => window.forja.cena.stats.fps); }

try {
  console.log('[1] Sala privada pela interface');
  const ana = await pagina('Ana');
  await ana.click('[data-testid=criar-sala]');
  await ana.waitForFunction(() => document.querySelector('#codSala').textContent.length === 4);
  const codigo = await ana.$eval('#codSala', (e) => e.textContent);
  const bruno = await pagina('Bruno', `&sala=${codigo}`);
  await bruno.waitForFunction(() => document.querySelector('#codSala').textContent.length === 4);
  ok(true, `sala ${codigo}: Ana criou, Bruno entrou pelo código`);
  await ana.click('[data-testid=treino]');
  await bruno.waitForFunction(() => window.forja.sala && window.forja.sala.fase === 'treino');
  ok(true, 'treino compartilhado iniciado pelo botão Treino');

  console.log('[2] Oficina de Ana: Passo, Pulso, Travessia e inversão pelos botões');
  await ana.click('[data-testid=abrir-oficina]');
  await sleep(800);
  await novaCriacao(ana, 'Passo');
  await ana.click('[data-testid="peca-vetor.deslocar"]');
  await aguardarSim(ana);
  // ajusta o vetor pelo campo do nó (3 m à frente); o mesmo comando é enviado pelo gizmo da cena
  await ana.fill('#ofNos input[data-p=vetor_cm][data-k="0"]', '300');
  await ana.press('#ofNos input[data-p=vetor_cm][data-k="0"]', 'Tab');
  await aguardarSim(ana);
  await ana.fill('#ofNota', 'deslocamento de 3 m');
  await salvar(ana);
  await novaCriacao(ana, 'Pulso');
  await ana.click('[data-testid="peca-limiar.anel"]');
  await aguardarSim(ana);
  // gizmo de raio arrastado na cena: o servidor recebe no.param raio_cm
  const giz = await ana.evaluate(() => {
    const o = window.forja.oficina;
    const h = o.handles.find((x) => x.userData.tipo === 'anel.raio');
    if (!h) return null;
    const cv = document.getElementById('ofCanvas').getBoundingClientRect();
    const proj = (v) => { const q = v.clone().project(o.cena.camera); return [cv.left + (q.x + 1) / 2 * cv.width, cv.top + (1 - q.y) / 2 * cv.height]; };
    const a = proj(h.position);
    const b = proj(h.position.clone().set(h.position.x + 0.5, h.position.y, h.position.z));
    return { a, b };
  });
  if (giz) {
    await ana.mouse.move(giz.a[0], giz.a[1]); await ana.mouse.down(); await ana.mouse.move(giz.b[0], giz.b[1], { steps: 4 }); await ana.mouse.up();
    await aguardarSim(ana);
    const raio = await ana.evaluate(() => window.forja.oficina.atual.rascunho.comportamento.nos.find((n) => n.op === 'limiar.anel').p.raio_cm);
    ok(raio > 120 && raio < 180, `arrastar o gizmo de raio na cena muda o raio para ${raio} cm`);
    await shot(ana, 'oficina_gizmo_raio.png', 'Oficina: raio do Pulso arrastado na cena');
    await ana.click('#ofDesfazer'); await aguardarSim(ana);
    const r2 = await ana.evaluate(() => window.forja.oficina.atual.rascunho.comportamento.nos.find((n) => n.op === 'limiar.anel').p.raio_cm);
    ok(r2 === 100, `desfazer volta o raio para ${r2} cm`);
  } else ok(false, 'gizmo de raio presente');
  await salvar(ana);
  await novaCriacao(ana, 'Travessia');
  await selecionarOpcao(ana, '#ofSubSel', ['Passo']);
  await ana.click('[data-testid=incorporar]'); await aguardarSim(ana);
  await selecionarOpcao(ana, '#ofApos', ['Passo', 'subcriação terminar']);
  await selecionarOpcao(ana, '#ofSubSel', ['Pulso']);
  await ana.click('[data-testid=incorporar]'); await aguardarSim(ana);
  await ana.fill('#ofNota', 'Passo, depois Pulso');
  await salvar(ana);
  await aguardarSim(ana);
  let txt = await explica(ana);
  ok(/Alvo<?\/?b?>? atingido|Alvo atingido/.test(txt.replace(/\s+/g, ' ')), 'prévia da Travessia: o alvo a 90 cm é atingido (explicação passo a passo)', txt.split('\n').filter((l) => l.includes('CONTATO')));
  await shot(ana, 'oficina_travessia.png', 'Oficina: Travessia (Passo → Pulso) no instante do contato, com regras, custo e linha do tempo');
  await ana.click('#ofDuplicar'); await esperarOficina(ana); await sleep(600);
  await ana.click('[data-testid=inverter]'); await aguardarSim(ana);
  await salvar(ana); await aguardarSim(ana);
  txt = await explica(ana);
  ok(/Alvo fora/.test(txt.replace(/\s+/g, ' ')), 'prévia da inversão: o mesmo alvo fica fora de alcance', txt.split('\n').filter((l) => l.includes('CONTATO')));
  await shot(ana, 'oficina_invertida.png', 'Oficina: Travessia invertida (Pulso → Passo): anel na saída, alvo fora');

  console.log('[3] Contramaré pela interface (marca, Travessia, janela, volta) e ensaio de comandos');
  await novaCriacao(ana, 'Contramaré');
  await ana.click('[data-testid="peca-marca.guardar"]'); await aguardarSim(ana);
  await selecionarOpcao(ana, '#ofApos', ['depois de marcar']);
  await selecionarOpcao(ana, '#ofSubSel', ['Travessia r1']);
  await ana.click('[data-testid=incorporar]'); await aguardarSim(ana);
  await selecionarOpcao(ana, '#ofApos', ['Travessia', 'subcriação terminar']);
  await ana.click('[data-testid="peca-cadencia.janela"]'); await aguardarSim(ana);
  await selecionarOpcao(ana, '#ofApos', ['comando vier na janela']);
  await ana.click('[data-testid="peca-vetor.retornar"]'); await aguardarSim(ana);
  await selecionarOpcao(ana, '#ofNos select[data-dado]', ['Guardar marca']);
  await aguardarSim(ana);
  await ana.fill('#cenCmds', '600');
  await ana.click('[data-testid=ensaiar]'); await aguardarSim(ana);
  txt = await explica(ana);
  ok(/janela: comando/.test(txt), 'ensaio com comando em 600 ms: a janela aceita e a volta acontece');
  await ana.evaluate(() => window.forja.oficina.setT(640));
  await sleep(1200);
  await shot(ana, 'oficina_contramare.png', 'Oficina: Contramaré — linha do tempo com a janela (manual, hachurada) e o comando ensaiado');
  await salvar(ana);

  console.log('[4] Pico: duas áreas em paralelo excedem; sugestão editável só entra com aprovação');
  await novaCriacao(ana, 'Duas marés');
  await ana.click('[data-testid="peca-limiar.anel"]'); await aguardarSim(ana);
  await ana.fill('#ofNos input[data-p=raio_cm]', '300'); await ana.press('#ofNos input[data-p=raio_cm]', 'Tab'); await aguardarSim(ana);
  await selecionarOpcao(ana, '#ofApos', ['Ativar', 'ao ativar']);
  await ana.click('[data-testid="peca-limiar.anel"]'); await aguardarSim(ana);
  await ana.fill('#ofNos input[data-p=raio_cm]', '300'); await ana.press('#ofNos input[data-p=raio_cm]', 'Tab'); await aguardarSim(ana);
  const probs = await ana.$eval('#ofProblemas', (e) => e.innerText);
  ok(/pico de 66/.test(probs), 'o editor destaca o pico (66 > 64) e as etapas responsáveis', probs);
  await shot(ana, 'oficina_pico.png', 'Oficina: duas áreas de 3 m em paralelo excedem o pico; sugestões que preservam a ideia');
  await ana.click('[data-testid=aprovar-0]'); await aguardarSim(ana);
  const probs2 = await ana.$eval('#ofProblemas', (e) => e.innerText);
  ok(!/pico de/.test(probs2), 'após aprovar a sugestão (encadear), o pico cabe');
  await ana.click('[data-testid=fechar-oficina]');
  await sleep(800);

  console.log('[5] Bruno cria Anteparo na oficina; equipamentos pelas listas do saguão');
  await bruno.click('[data-testid=abrir-oficina]'); await sleep(800);
  await novaCriacao(bruno, 'Anteparo');
  await bruno.click('[data-testid="peca-lastro.anteparo"]'); await aguardarSim(bruno);
  await bruno.selectOption('#ofNos select[data-p=gesto]', 'bracos_cruzados'); await aguardarSim(bruno);
  await salvar(bruno);
  await bruno.click('[data-testid=fechar-oficina]'); await sleep(800);
  await selecionarOpcao(ana, '[data-testid=equip-0]', ['Travessia r1']); await sleep(500);
  await selecionarOpcao(ana, '[data-testid=equip-2]', ['Pulso']); await sleep(500);
  await selecionarOpcao(bruno, '[data-testid=equip-0]', ['Anteparo']); await sleep(600);
  ok(await ana.$eval('#barraSlots', (e) => e.innerText.includes('Travessia')), 'barra de criações mostra Travessia no espaço 1');

  console.log('[6] Travessia em jogo: quadro de anúncio e de contato pelas câmeras de Ana e de Bruno (gráficos baixo)');
  await ana.check('#chkRecarga'); await sleep(300);
  const irPara = async (p, x, y, g) => { await p.fill('#irX', String(x)); await p.fill('#irY', String(y)); await p.fill('#irYaw', String(g)); await p.click('[data-testid=ir]'); await sleep(500); };
  const porAlvo = async (p, x, y) => {
    await p.click('[data-testid=por-alvo]');
    const pt = await p.evaluate(([gx, gy]) => { const f = window.forja; const cam = f.cena.camera; const v = cam.position.clone().set(gx / 100, 0, gy / 100).project(cam); return [(v.x + 1) / 2 * innerWidth, (1 - v.y) / 2 * innerHeight]; }, [x, y]);
    await p.mouse.click(pt[0], pt[1]);
    await sleep(600);
  };
  await irPara(ana, -600, -250, 0);
  await irPara(bruno, -150, 120, -115);
  await sleep(1500);
  await porAlvo(ana, -300, -160);
  const alvos = await ana.evaluate(() => [...window.forja.mundo.ent.values()].filter((e) => e.tipo === 'alvo').length);
  ok(alvos >= 1, 'alvo de treino posto com um clique no chão');
  const executar = async (rotulo) => {
    await armarCaptura(ana); await armarCaptura(bruno);
    await ana.keyboard.press('1');
    const ca = await salvarCaptura(ana, `${rotulo}_ana`, `Câmera de Ana (${rotulo})`);
    const cb = await salvarCaptura(bruno, `${rotulo}_bruno`, `Câmera de Bruno (${rotulo})`);
    return { ana: ca, bruno: cb };
  };
  const baixo = await executar('travessia_baixo');
  ok(baixo.ana['area.contato'].resultados.some((r) => r.nome === 'alvo' && r.resultado === 'atingido'), 'em jogo: Travessia atinge o alvo (servidor), visto pelas duas câmeras', baixo);
  rel.fps.baixo = await medirFps(ana);
  await shot(ana, 'jogo_ana_baixo.png', 'Ana em jogo, gráficos baixo (HUD completo)');
  await shot(bruno, 'jogo_bruno_baixo.png', 'Bruno observando, gráficos baixo');

  console.log('[7] Mesma ação em gráficos alto (mesmos volumes, regras e sinais)');
  await ana.keyboard.press('f'); await bruno.keyboard.press('f');
  await sleep(1500);
  await irPara(ana, -600, -250, 0);
  await sleep(1200);
  const alto = await executar('travessia_alto');
  ok(alto.ana['area.contato'].raio === baixo.ana['area.contato'].raio && JSON.stringify(alto.ana['area.contato'].resultados) === JSON.stringify(baixo.ana['area.contato'].resultados), 'níveis alto e baixo: mesmo raio, mesmo resultado; só a apresentação muda');
  rel.fps.alto = await medirFps(ana);
  await shot(ana, 'jogo_ana_alto.png', 'Ana em jogo, gráficos alto');
  await shot(bruno, 'jogo_bruno_alto.png', 'Bruno observando, gráficos alto');
  rel.travessia_em_jogo = { baixo, alto };

  console.log('[8] Anteparo de Bruno em jogo: frente (absorve) e flanco (atinge)');
  await bruno.check('#chkRecarga').catch(() => {});
  for (const [caso, ax, ay, ag] of [['frente', -80, 250, 0], ['flanco', 0, 170, 90]]) {
    await irPara(bruno, 0, 250, 180);
    await irPara(ana, ax, ay, ag);
    await sleep(1500);
    await armarCaptura(ana); await armarCaptura(bruno);
    await bruno.keyboard.press('1');
    await sleep(250);
    await ana.keyboard.press('3');
    const ca = await salvarCaptura(ana, `anteparo_${caso}_ana`, `Câmera de Ana — Anteparo, impacto pela ${caso}`);
    const cb = await salvarCaptura(bruno, `anteparo_${caso}_bruno`, `Câmera de Bruno — Anteparo, impacto pela ${caso}`);
    const res = ca['area.contato'].resultados.find((r) => r.nome === 'Bruno');
    ok(res && res.resultado === (caso === 'frente' ? 'absorvido' : 'atingido'), `Anteparo, impacto pela ${caso}: ${res && res.resultado}`);
    rel['anteparo_' + caso] = { ana: ca, bruno: cb };
    await sleep(1800);
  }

  console.log('[9] Duelo pelos botões');
  await ana.click('[data-testid=pronto]'); await bruno.click('[data-testid=pronto]'); await sleep(600);
  await selecionarOpcao(bruno, '[data-testid=equip-2]', ['—']).catch(() => {});
  await ana.click('[data-testid=duelo]');
  const comecou = await ana.waitForFunction(() => window.forja.sala && ['contagem', 'duelo'].includes(window.forja.sala.fase), null, { timeout: 8000 }).then(() => true).catch(() => false);
  ok(comecou, 'duelo começa pelo botão (contagem)');
  await sleep(1200);
  await shot(ana, 'duelo_contagem.png', 'Duelo: contagem e placar');
  rel.erros_js = { ana: ana.erros, bruno: bruno.erros };
  ok(ana.erros.length === 0 && bruno.erros.length === 0, 'nenhum erro de JavaScript nas duas páginas', rel.erros_js);
} catch (e) {
  console.error(e);
  rel.erro = String(e.stack || e);
  falhas++;
} finally {
  await browser.close();
  srv.kill();
  rel.fim = new Date().toISOString();
  rel.falhas = falhas;
  rel.aprovadas = rel.verificacoes.filter((v) => v.ok).length;
  writeFileSync(path.join(RAIZ, 'evidencias/ui_relatorio.json'), JSON.stringify(rel, null, 2));
  console.log(`\nresultado: ${rel.aprovadas} verificações aprovadas, ${falhas} falharam; fps baixo=${(rel.fps.baixo || 0).toFixed(1)} alto=${(rel.fps.alto || 0).toFixed(1)} (SwiftShader)`);
  process.exit(falhas ? 1 : 0);
}
