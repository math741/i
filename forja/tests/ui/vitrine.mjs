// Vitrine visual: usa a prévia determinística da oficina (efeitos = função do tempo do servidor) para
// capturar quadros exatos de uma criação: deslocamento, preparo, contato, onda de choque e rescaldo.
// As criações são feitas pelos comandos da oficina (os mesmos dos botões).
import { spawn } from 'node:child_process';
import { mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const RAIZ = path.resolve(import.meta.dirname, '../..');
const EV = path.join(RAIZ, 'evidencias/vitrine');
const RUN = path.join(RAIZ, 'run/vitrine');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
rmSync(RUN, { recursive: true, force: true }); mkdirSync(EV, { recursive: true });
const srv = spawn(path.join(RAIZ, 'build/forja_server'), ['--porta', '8796', '--cliente', path.join(RAIZ, 'client'), '--dados', path.join(RUN, 'saves'), '--logs', path.join(RUN, 'logs')], { stdio: 'ignore' });
await sleep(400);
const b = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const p = await b.newPage({ viewport: { width: 1280, height: 720 } });
const erros = [];
p.on('pageerror', (e) => erros.push(e.message));
await p.goto('http://127.0.0.1:8796/?nome=Vitrine&q=' + (process.argv[2] || 'alto'));
await p.waitForFunction(() => window.forja && window.forja.oficina, null, { timeout: 30000 });
await p.evaluate(async () => {
  const o = (c) => window.forja.con.oficina(c);
  const passo = (await o({ cmd: 'criar', nome: 'Passo' })).criacao;
  await o({ cmd: 'no.adicionar', criacao: passo, op: 'vetor.deslocar', p: { vetor_cm: [300, 0, 0], duracao_ms: 150 }, rotulo: 'Passo', ligar_de: 'n0', porta: 'ao_ativar' });
  await o({ cmd: 'salvar', criacao: passo });
  const pulso = (await o({ cmd: 'criar', nome: 'Pulso' })).criacao;
  await o({ cmd: 'no.adicionar', criacao: pulso, op: 'limiar.anel', p: { raio_cm: 100, preparo_ms: 300 }, rotulo: 'Pulso', ligar_de: 'n0', porta: 'ao_ativar' });
  await o({ cmd: 'salvar', criacao: pulso });
  const tr = (await o({ cmd: 'criar', nome: 'Travessia' })).criacao;
  const a = await o({ cmd: 'incorporar', criacao: tr, alvo: passo, ligar_de: 'n0', porta: 'ao_ativar' });
  await o({ cmd: 'incorporar', criacao: tr, alvo: pulso, ligar_de: a.no, porta: 'ao_terminar' });
  await o({ cmd: 'salvar', criacao: tr });
  const es = (await o({ cmd: 'criar', nome: 'Esteira' })).criacao;
  await o({ cmd: 'incorporar', criacao: es, alvo: passo, ligar_de: 'n0', porta: 'ao_ativar' });
  const v = await o({ cmd: 'no.adicionar', criacao: es, op: 'limiar.vigiar', p: { superficie: 'agua', duracao_ms: 200 }, rotulo: 'Vigia', ligar_de: 'n0', porta: 'ao_ativar' });
  await o({ cmd: 'no.adicionar', criacao: es, op: 'limiar.anel', p: { ancora: 'evento', raio_cm: 140, preparo_ms: 200 }, rotulo: 'Respingo', ligar_de: v.no, porta: 'ao_entrar' });
  await o({ cmd: 'salvar', criacao: es });
  const an = (await o({ cmd: 'criar', nome: 'Anteparo' })).criacao;
  await o({ cmd: 'no.adicionar', criacao: an, op: 'lastro.guarda', p: { abertura_graus: 120, gesto: 'bracos_cruzados', preparo_ms: 300 }, rotulo: 'Anteparo', ligar_de: 'n0', porta: 'ao_ativar' });
  await o({ cmd: 'salvar', criacao: an });
  window.__ids = { tr, es, an };
});
const ids = await p.evaluate(() => window.__ids);
await p.evaluate(() => window.forja.oficina.abrir());
await sleep(1500);
async function quadros(id, cenario, camera, tempos, prefixo) {
  await p.evaluate(async ([id2, cen, cam]) => {
    const of = window.forja.oficina;
    of.cenario = cen;
    await of.selecionar(id2);
    Object.assign(of.cena.orbita, { yaw: cam.yaw, pitch: cam.pitch, dist: cam.dist });
    of.cena.orbita.alvo.set(cam.alvo[0], cam.alvo[1], cam.alvo[2]);
  }, [id, cenario, camera]);
  await sleep(1500);
  for (const t of tempos) {
    await p.evaluate((tt) => { const of = window.forja.oficina; of.cena.limparDinamicos(); for (let k = Math.max(0, tt - 400); k <= tt; k += 40) { of.t = k; of.mundoSim.em(k); of.cena._dt = 0.04; of.cena.sincronizar(of.mundoSim, of.sim.t0 + k); } of.setT(tt); }, t);
    await sleep(1400);
    const png = await p.evaluate(() => document.getElementById('ofCanvas').toDataURL('image/png'));
    writeFileSync(path.join(EV, `${prefixo}_${String(t).padStart(4, '0')}ms.png`), Buffer.from(png.split(',')[1], 'base64'));
  }
}
const alvoT = { autor: { pos: [-600, -250, 0], yaw: 0 }, alvos: [{ nome: 'Alvo', pos: [-300, -160, 0] }], comandos: [] };
await quadros(ids.tr, alvoT, { yaw: 2.35, pitch: 0.32, dist: 4.2, alvo: [-3.6, 0.8, -2.3] }, [70, 260, 451, 470, 560, 1200], 'travessia');
await quadros(ids.es, { autor: { pos: [200, -250, 0], yaw: 90 }, alvos: [{ nome: 'T1', pos: [300, -70, 0] }], comandos: [] }, { yaw: -0.6, pitch: 0.35, dist: 4.6, alvo: [2.2, 0.6, -0.9] }, [90, 300, 330, 420], 'esteira');
await quadros(ids.an, { autor: { pos: [-500, 150, 0], yaw: 180 }, alvos: [], comandos: [] }, { yaw: 3.6, pitch: 0.28, dist: 3.4, alvo: [-5.0, 1.0, 1.5] }, [150, 320, 700], 'anteparo');
console.log('erros:', erros.length ? erros : 'nenhum');
await b.close();
srv.kill();
