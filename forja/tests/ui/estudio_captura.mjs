// Captura determinística do Estúdio de animação (clipe, instante) -> evidencias/estudio/*.png
import { spawn } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
const RAIZ = path.resolve(import.meta.dirname, '../..');
const EV = path.join(RAIZ, 'evidencias/estudio');
mkdirSync(EV, { recursive: true });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const srv = spawn(path.join(RAIZ, 'build/forja_server'), ['--porta', '8795', '--cliente', path.join(RAIZ, 'client'), '--dados', path.join(RAIZ, 'run/estudio/saves'), '--logs', path.join(RAIZ, 'run/estudio/logs')], { stdio: 'ignore' });
await sleep(300);
const b = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const p = await b.newPage({ viewport: { width: Number(process.env.W || 960), height: Number(process.env.H || 720) } });
const erros = [];
p.on('pageerror', (e) => erros.push(e.message));
p.on('console', (m) => { if (m.type() === 'error') erros.push(m.text()); });
await p.goto('http://127.0.0.1:8795/estudio.html?q=' + (process.env.Q || 'alto'));
await p.waitForFunction(() => window.estudio, null, { timeout: 30000 });
await p.evaluate(() => { document.getElementById('painel').style.display = 'none'; });
const lista = JSON.parse(process.argv[2] || '[[0,2000],[4,500],[4,820],[5,560],[6,1500],[8,1320],[9,1400],[10,1200]]');
for (const [i, t, cy] of lista) {
  const png = await p.evaluate(([i2, t2, c2]) => window.estudio.irPara(i2, t2, c2 === null ? undefined : c2), [i, t, cy ?? null]);
  const nome = await p.evaluate((i2) => window.estudio.clipes[i2], i);
  const arq = `${String(i).padStart(2, '0')}_${nome.normalize('NFD').replace(/[^\w]+/g, '_').replace(/_+$/, '').toLowerCase()}_${String(Math.round(t)).padStart(4, '0')}ms${cy !== undefined ? '_cam' + cy : ''}.png`;
  writeFileSync(path.join(EV, arq), Buffer.from(png.split(',')[1], 'base64'));
  console.log(arq);
}
console.log('erros:', erros.length ? erros.slice(0, 5) : 'nenhum');
await b.close(); srv.kill();
