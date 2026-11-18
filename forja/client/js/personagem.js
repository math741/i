// Operador da galeria — modelo procedural de alto detalhe e animação física/procedural.
//
// Modelo: silhueta em V com músculos por perfis de revolução, armadura em camadas (peitoral, placas
// abdominais, ombreiras duplas, braçadeiras emissoras, coxais, joelheiras, caneleiras, botas com sola),
// capacete facetado com visor emissivo, respirador e lanternas, mãos com 5 dedos articulados, mochila com
// reservatório de fôlego e dutos, emblema do time e duas faixas de tecido simuladas (verlet).
//
// Animação (camadas, todas com molas estáveis por subpassos):
//  • pernas: IK de dois ossos + controlador de passos com pés plantados no mundo (sem deslizar), passos
//    de correção ao girar parado, descida ao canal e balanço do quadril sobre o pé de apoio;
//  • braços: IK de dois ossos guiado por alvos de mão (balanço da marcha, carga, golpe, gestos de defesa,
//    mãos nos joelhos na recuperação), punho e dedos posados;
//  • tronco e cabeça: inclinação pela aceleração, torção da marcha, olhar para a ação com limites;
//  • ações derivadas dos eventos: antecipação → golpe → acompanhamento → recuperação; arrancada,
//    voo e aterrissagem do deslocamento; reação direcional ao golpe; empurrão ao absorver.
// Frente do personagem = +X local, cima = +Y, direita = +Z.
import * as THREE from 'three';

const COR_TIME = { 0: 0xa8522c, 1: 0x2f64a6, 2: 0x62489a, 3: 0x3b8058, '-1': 0xb39b5a };
const COR_LUZ = { 0: 0xff7a3a, 1: 0x4fb4ff, 2: 0xb48bff, 3: 0x6fe0a0, '-1': 0xffe0a0 };
const V = (x, y, z) => new THREE.Vector3(x, y, z);
const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
const clamp01 = (x) => clamp(x, 0, 1);
const lerp = (a, b, k) => a + (b - a) * k;
const suave = (x) => { x = clamp01(x); return x * x * (3 - 2 * x); };

// ======================= materiais =======================
function canvasTex(n, desenhar, srgb = true) {
  const c = document.createElement('canvas'); c.width = c.height = n;
  desenhar(c.getContext('2d'), n);
  const t = new THREE.CanvasTexture(c); t.wrapS = t.wrapT = THREE.RepeatWrapping; t.anisotropy = 4;
  if (srgb) t.colorSpace = THREE.SRGBColorSpace;
  return t;
}
let _m = null;
function mats() {
  if (_m) return _m;
  const trama = canvasTex(128, (g, n) => {
    const img = g.createImageData(n, n);
    for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
      const a = Math.sin(x * 0.9) * 0.5 + 0.5, b = Math.sin(y * 0.9) * 0.5 + 0.5;
      const ch = ((Math.floor(x / 3.5) + Math.floor(y / 3.5)) % 2) ? a : b;
      const i = (y * n + x) * 4; img.data[i] = 128 + (ch - 0.5) * 70; img.data[i + 1] = 128 + (b - a) * 50; img.data[i + 2] = 255; img.data[i + 3] = 255;
    }
    g.putImageData(img, 0, 0);
  }, false);
  trama.repeat.set(5, 5);
  const desgaste = canvasTex(256, (g, n) => {
    g.fillStyle = '#6c6c6c'; g.fillRect(0, 0, n, n);
    for (let i = 0; i < 380; i++) { const v = 40 + Math.random() * 150 | 0; g.fillStyle = `rgb(${v},${v},${v})`; g.globalAlpha = 0.22; g.beginPath(); g.ellipse(Math.random() * n, Math.random() * n, 1 + Math.random() * 9, 1 + Math.random() * 3, Math.random() * 3, 0, 7); g.fill(); }
    g.globalAlpha = 0.7; g.strokeStyle = '#d0d0d0';
    for (let i = 0; i < 70; i++) { g.lineWidth = 0.4 + Math.random() * 0.8; g.beginPath(); const x = Math.random() * n, y = Math.random() * n; g.moveTo(x, y); g.lineTo(x + (Math.random() - 0.5) * 40, y + (Math.random() - 0.5) * 8); g.stroke(); }
  }, false);
  const sola = canvasTex(64, (g, n) => { g.fillStyle = '#808080'; g.fillRect(0, 0, n, n); g.fillStyle = '#2a2a2a'; for (let y = 0; y < n; y += 8) for (let x = (y / 8) % 2 * 4; x < n; x += 8) g.fillRect(x, y, 5, 4); }, false);
  _m = {
    tecido: new THREE.MeshStandardMaterial({ color: 0x30353d, roughness: 0.86, metalness: 0, normalMap: trama, normalScale: new THREE.Vector2(0.55, 0.55) }),
    tecidoClaro: new THREE.MeshStandardMaterial({ color: 0x353b44, roughness: 0.85, normalMap: trama, normalScale: new THREE.Vector2(0.4, 0.4) }),
    borracha: new THREE.MeshStandardMaterial({ color: 0x131518, roughness: 0.7, metalness: 0.04, bumpMap: sola, bumpScale: 0.6 }),
    couro: new THREE.MeshStandardMaterial({ color: 0x2c241d, roughness: 0.62, metalness: 0.05 }),
    metal: new THREE.MeshStandardMaterial({ color: 0x4a5059, roughness: 0.32, metalness: 0.9, roughnessMap: desgaste }),
    metalEscuro: new THREE.MeshStandardMaterial({ color: 0x23272d, roughness: 0.4, metalness: 0.85, roughnessMap: desgaste }),
    vidro: new THREE.MeshPhysicalMaterial({ color: 0xa8dcff, roughness: 0.04, metalness: 0, transparent: true, opacity: 0.3, clearcoat: 1 }),
    desgaste,
    madeira: new THREE.MeshStandardMaterial({ color: 0x6b4a2e, roughness: 0.75 }),
  };
  return _m;
}
function emblema(time) {
  return canvasTex(128, (g, n) => {
    g.fillStyle = '#' + (COR_TIME[time] ?? 0x777777).toString(16).padStart(6, '0'); g.fillRect(0, 0, n, n);
    g.strokeStyle = 'rgba(255,240,220,0.85)'; g.lineWidth = 6; g.lineCap = 'round';
    g.beginPath();
    if (time === 1) { for (let k = 0; k < 3; k++) { g.moveTo(24, 40 + k * 22); g.bezierCurveTo(44, 26 + k * 22, 84, 54 + k * 22, 104, 40 + k * 22); } }   // maré
    else { g.moveTo(64, 22); g.lineTo(98, 96); g.lineTo(64, 78); g.lineTo(30, 96); g.closePath(); }                                                     // brasa
    g.stroke();
    g.globalAlpha = 0.35; g.fillStyle = '#000'; for (let i = 0; i < 60; i++) g.fillRect(Math.random() * n, Math.random() * n, 2, 1);
  });
}
function placa(time) {
  const m = mats();
  return new THREE.MeshPhysicalMaterial({ color: COR_TIME[time] ?? 0x888888, roughness: 0.48, metalness: 0.4, roughnessMap: m.desgaste, clearcoat: 0.45, clearcoatRoughness: 0.35 });
}
function emissivo(time, k = 2.2) { return new THREE.MeshStandardMaterial({ color: 0x0d0d0d, emissive: COR_LUZ[time] ?? 0xaaaaaa, emissiveIntensity: k, roughness: 0.3 }); }

// ======================= geometria =======================
// perfil de revolução descendo do topo (y=0) até -L; pts: [raio, fração do comprimento]
function perfil(pts, L, seg = 18) {
  const g = new THREE.LatheGeometry(pts.map(([r, f]) => new THREE.Vector2(r, -f * L)).reverse(), seg);
  return g;
}
function chapa(w, h, d, bev, mat, curva = 0) {
  const s = new THREE.Shape(), r = Math.min(w, h) * 0.24;
  s.moveTo(-w / 2 + r, -h / 2); s.lineTo(w / 2 - r, -h / 2); s.quadraticCurveTo(w / 2, -h / 2, w / 2, -h / 2 + r);
  s.lineTo(w / 2, h / 2 - r); s.quadraticCurveTo(w / 2, h / 2, w / 2 - r, h / 2); s.lineTo(-w / 2 + r, h / 2);
  s.quadraticCurveTo(-w / 2, h / 2, -w / 2, h / 2 - r); s.lineTo(-w / 2, -h / 2 + r); s.quadraticCurveTo(-w / 2, -h / 2, -w / 2 + r, -h / 2);
  const g = new THREE.ExtrudeGeometry(s, { depth: d, bevelEnabled: true, bevelThickness: bev, bevelSize: bev, bevelSegments: 3, curveSegments: 8 });
  g.translate(0, 0, -d / 2);
  if (curva) { const p = g.attributes.position; for (let i = 0; i < p.count; i++) { const x = p.getX(i); p.setZ(i, p.getZ(i) - curva * x * x); } g.computeVertexNormals(); }
  return new THREE.Mesh(g, mat);
}
// casca: fatia de esfera (ombreiras, joelheiras)
function casca(r, mat, phi = Math.PI * 2, theta = Math.PI * 0.5) { return new THREE.Mesh(new THREE.SphereGeometry(r, 22, 12, 0, phi, 0, theta), mat); }
function add(pai, mesh, x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0) { mesh.position.set(x, y, z); mesh.rotation.set(rx, ry, rz); pai.add(mesh); return mesh; }

// ======================= operador =======================
// casca de armadura que acompanha o perfil do corpo (fatia de revolução deslocada para fora)
function concha(pts, L, phiC, phiLen, off, mat, seg = 18) {
  const g = new THREE.LatheGeometry(pts.map(([r, f]) => new THREE.Vector2(r + off, -f * L)).reverse(), seg, phiC - phiLen / 2, phiLen);
  return new THREE.Mesh(g, mat);
}
const FRENTE = Math.PI / 2, TRAS = -Math.PI / 2, ESQ = Math.PI, DIR = 0;
const P_PELVE = [[0.12, 0], [0.165, 0.25], [0.172, 0.55], [0.13, 0.9], [0.05, 1]];
const P_ABD = [[0.152, 0], [0.142, 0.5], [0.158, 1]];
const P_PEITO = [[0.07, 0], [0.175, 0.12], [0.222, 0.36], [0.21, 0.7], [0.165, 1]];
const P_BRACO = [[0.066, 0], [0.066, 0.22], [0.071, 0.45], [0.056, 0.8], [0.048, 1]];
const P_ANTE = [[0.054, 0], [0.061, 0.22], [0.05, 0.65], [0.039, 1]];
const P_COXA = [[0.092, 0], [0.096, 0.18], [0.086, 0.5], [0.066, 0.9], [0.058, 1]];
const P_CANELA = [[0.057, 0], [0.066, 0.24], [0.055, 0.52], [0.041, 0.88], [0.039, 1]];
const faixa = (pts, f0, f1) => pts.filter(([, f]) => f >= f0 && f <= f1).concat([]).length >= 2 ? pts.filter(([, f]) => f >= f0 && f <= f1) : [[pts[0][0], f0], [pts[pts.length - 1][0], f1]];
function trecho(pts, f0, f1) {
  // reamostra o perfil entre f0 e f1 (6 pontos) para cascas que cobrem só um trecho
  const r = (f) => { for (let i = 1; i < pts.length; i++) if (f <= pts[i][1]) { const a = pts[i - 1], b = pts[i]; const k = (f - a[1]) / Math.max(1e-6, b[1] - a[1]); return a[0] + (b[0] - a[0]) * k; } return pts[pts.length - 1][0]; };
  const o = []; for (let i = 0; i <= 5; i++) { const f = f0 + (f1 - f0) * i / 5; o.push([r(f), f]); } return o;
}
void faixa;

export function criarOperador(time, { simples = false } = {}) {
  const m = mats();
  const pl = placa(time); pl.side = THREE.DoubleSide;
  const pl2 = pl.clone(); pl2.color.multiplyScalar(0.62);
  const em = emissivo(time), emFio = emissivo(time, 1.2);
  const raiz = new THREE.Group();
  const quadril = new THREE.Group(); quadril.position.y = 0.97; raiz.add(quadril);
  // ---- pelve, cinto, tassets ----
  const pg = new THREE.Group(); pg.position.y = 0.08; pg.scale.z = 0.86; quadril.add(pg);
  pg.add(new THREE.Mesh(perfil(P_PELVE, 0.16), m.tecido));
  for (const ph of [ESQ, DIR]) { add(pg, concha(trecho(P_PELVE, 0.2, 0.85), 0.16, ph, 1.15, 0.016, pl2)); }
  add(pg, concha(trecho(P_PELVE, 0.05, 0.4), 0.16, FRENTE, 1.6, 0.012, m.metalEscuro));
  const cinto = add(quadril, new THREE.Mesh(new THREE.TorusGeometry(0.17, 0.026, 10, 40), m.couro), 0, 0.05, 0, Math.PI / 2); cinto.scale.y = 0.86;
  add(quadril, chapa(0.07, 0.048, 0.012, 0.005, m.metal), 0.172, 0.05, 0, 0, Math.PI / 2);
  for (const [a, w, h] of [[0.8, 0.065, 0.085], [-0.8, 0.065, 0.085], [2.35, 0.085, 0.095], [-2.35, 0.085, 0.095]]) {
    const b = add(quadril, chapa(w, h, 0.05, 0.012, m.couro), Math.cos(a) * 0.178, 0.0, Math.sin(a) * 0.152, 0, -a + Math.PI / 2);
    add(b, chapa(w * 0.85, 0.018, 0.055, 0.004, m.metalEscuro), 0, h / 2 - 0.01, 0);
  }
  const ferramenta = add(quadril, new THREE.Mesh(new THREE.CylinderGeometry(0.017, 0.021, 0.22, 10), m.metalEscuro), 0.03, -0.08, 0.195, 0, 0, 0.15);
  add(ferramenta, new THREE.Mesh(new THREE.CylinderGeometry(0.025, 0.025, 0.03, 12), em), 0, 0.11, 0);
  // ---- abdômen segmentado ----
  const coluna = new THREE.Group(); coluna.position.y = 0.1; quadril.add(coluna);
  const ag = new THREE.Group(); ag.position.y = 0.22; ag.scale.z = 0.8; coluna.add(ag);
  ag.add(new THREE.Mesh(perfil(P_ABD, 0.22, 22), m.tecido));
  for (let i = 0; i < 3; i++) add(ag, concha(trecho(P_ABD, 0.06 + i * 0.31, 0.3 + i * 0.31), 0.22, FRENTE, 2.0 - i * 0.12, 0.013, i % 2 ? pl2 : pl));
  add(ag, concha(trecho(P_ABD, 0.1, 0.9), 0.22, TRAS, 1.6, 0.01, pl2));
  // ---- peito em V com armadura moldada ----
  const peito = new THREE.Group(); peito.position.y = 0.22; coluna.add(peito);
  const tg = new THREE.Group(); tg.position.y = 0.36; tg.scale.z = 0.74; peito.add(tg);
  tg.add(new THREE.Mesh(perfil(P_PEITO, 0.36, 24), m.tecido));
  add(tg, concha(trecho(P_PEITO, 0.16, 0.66), 0.36, FRENTE, 2.35, 0.02, pl));
  add(tg, concha(trecho(P_PEITO, 0.66, 0.84), 0.36, FRENTE, 2.1, 0.022, pl2));
  add(tg, concha(trecho(P_PEITO, 0.12, 0.8), 0.36, TRAS, 2.3, 0.018, pl));
  for (const d of [-0.42, 0.42]) add(tg, concha(trecho(P_PEITO, 0.2, 0.62), 0.36, FRENTE + d, 0.045, 0.026, emFio));
  for (const ph of [ESQ, DIR]) add(tg, concha(trecho(P_PEITO, 0.3, 0.7), 0.36, ph, 0.5, 0.012, m.metalEscuro));
  const trap = add(peito, new THREE.Mesh(new THREE.SphereGeometry(0.15, 18, 12), m.tecido), -0.02, 0.32, 0); trap.scale.set(0.7, 0.33, 1.25);
  const emb = new THREE.Mesh(new THREE.PlaneGeometry(0.09, 0.09), new THREE.MeshStandardMaterial({ map: emblema(time), roughness: 0.6, metalness: 0.2, transparent: true, polygonOffset: true, polygonOffsetFactor: -4 }));
  add(peito, emb, 0.197, 0.2, -0.075, 0, Math.PI / 2 + 0.25, 0.06);
  add(peito, new THREE.Mesh(new THREE.TorusGeometry(0.1, 0.018, 8, 26, Math.PI * 1.2), m.metalEscuro), 0.03, 0.345, 0, Math.PI / 2, 0, Math.PI * 0.4).scale.set(1, 1.35, 1);
  // ---- mochila: reservatório, aletas, luzes e dutos ----
  const mochila = new THREE.Group(); mochila.position.set(-0.2, 0.19, 0); peito.add(mochila);
  add(mochila, chapa(0.24, 0.3, 0.06, 0.018, m.metalEscuro, -1.2), 0, 0, 0, 0, Math.PI / 2);
  add(mochila, new THREE.Mesh(new THREE.CylinderGeometry(0.056, 0.056, 0.26, 20, 1, true), m.vidro), -0.062, 0, 0);
  const liquido = add(mochila, new THREE.Mesh(new THREE.CylinderGeometry(0.046, 0.046, 0.24, 18), emissivo(time, 1.8)), -0.062, 0, 0);
  for (const y of [-0.14, 0.14]) add(mochila, new THREE.Mesh(new THREE.CylinderGeometry(0.064, 0.064, 0.026, 20), m.metal), -0.062, y, 0);
  for (let i = 0; i < 5; i++) for (const z of [-1, 1]) add(mochila, new THREE.Mesh(new THREE.BoxGeometry(0.05, 0.006, 0.03), m.metal), -0.03, -0.1 + i * 0.05, 0.095 * z);
  for (const z of [-0.07, 0.07]) add(mochila, new THREE.Mesh(new THREE.SphereGeometry(0.009, 8, 6), em), -0.035, 0.155, z);
  const dutos = [];
  for (const s of [-1, 1]) {
    const c = new THREE.CatmullRomCurve3([V(-0.21, 0.31, 0.045 * s), V(-0.17, 0.43, 0.1 * s), V(-0.05, 0.41, 0.18 * s), V(-0.005, 0.33, 0.205 * s)]);
    dutos.push(add(peito, new THREE.Mesh(new THREE.TubeGeometry(c, 18, 0.015, 8), emissivo(time, 0.6))));
  }
  // ---- cabeça: capacete com visor curvo, máscara moldada, lanternas ----
  const pescoco = new THREE.Group(); pescoco.position.y = 0.36; peito.add(pescoco);
  add(pescoco, new THREE.Mesh(new THREE.CylinderGeometry(0.056, 0.072, 0.1, 18), m.tecido), 0, 0.03);
  for (let i = 0; i < 3; i++) add(pescoco, new THREE.Mesh(new THREE.TorusGeometry(0.06 + i * 0.004, 0.006, 6, 20), m.metalEscuro), 0, 0.0 + i * 0.025, 0, Math.PI / 2);
  const cabeca = new THREE.Group(); cabeca.position.y = 0.1; pescoco.add(cabeca);
  const hg = new THREE.Group(); hg.position.set(-0.005, 0.085, 0); hg.scale.set(1.1, 1.02, 0.95); cabeca.add(hg);
  hg.add(new THREE.Mesh(new THREE.SphereGeometry(0.128, 32, 24), m.metalEscuro));
  hg.add(new THREE.Mesh(new THREE.SphereGeometry(0.135, 32, 12, Math.PI - 1.0, 2.0, Math.PI / 2 - 0.02, 0.62), pl));                 // máscara
  const visor = hg.add(new THREE.Mesh(new THREE.SphereGeometry(0.137, 32, 4, Math.PI - 0.95, 1.9, Math.PI / 2 - 0.2, 0.15), em)) && hg.children[hg.children.length - 1];
  hg.add(new THREE.Mesh(new THREE.SphereGeometry(0.133, 32, 10, -0.5, 1.0, 0.25, 1.1), pl));                                       // placa da nuca
  hg.add(new THREE.Mesh(new THREE.SphereGeometry(0.131, 4, 16, Math.PI - 0.12, 0.24, 0.05, 1.4), pl2));                           // crista
  for (const s of [-1, 1]) {
    const lan = add(cabeca, new THREE.Mesh(new THREE.CylinderGeometry(0.03, 0.033, 0.03, 16), pl2), 0.0, 0.075, 0.128 * s, Math.PI / 2);
    add(lan, new THREE.Mesh(new THREE.CircleGeometry(0.019, 16), em), 0, 0.016 * s, 0, -Math.PI / 2 * s);
    add(cabeca, new THREE.Mesh(new THREE.CylinderGeometry(0.022, 0.018, 0.045, 12), m.metal), 0.1, 0.005, 0.042 * s, 0, 0.25 * s, Math.PI / 2 - 0.35);
  }
  add(cabeca, new THREE.Mesh(new THREE.CylinderGeometry(0.0035, 0.0035, 0.14, 6), m.metal), -0.07, 0.24, -0.08, 0.2, 0, -0.35);
  // ---- braços ----
  const braco = (s) => {
    const fora = s < 0 ? ESQ : DIR;
    const ombro = new THREE.Group(); ombro.position.set(0, 0.3, 0.218 * s); peito.add(ombro);
    const delt = add(ombro, new THREE.Mesh(new THREE.SphereGeometry(0.074, 16, 12), m.tecido), 0, -0.02, 0); delt.scale.set(1.05, 1.1, 1);
    ombro.add(new THREE.Mesh(perfil(P_BRACO, 0.29), m.tecido));
    add(ombro, concha(trecho(P_BRACO, 0.12, 0.72), 0.29, fora, 2.2, 0.012, pl2));
    const o1 = add(ombro, casca(0.104, pl, Math.PI * 2, Math.PI * 0.42), 0, 0.03, 0.012 * s); o1.scale.set(1.15, 0.72, 1.02);
    const o2 = add(ombro, casca(0.098, pl2, Math.PI * 2, Math.PI * 0.3), 0, -0.035, 0.03 * s, 0, 0, 0.28 * s); o2.scale.set(1.1, 0.6, 0.95);
    add(ombro, casca(0.092, emFio, Math.PI * 2, 0.06), 0, -0.012, 0.03 * s, 0, 0, 0.28 * s).scale.set(1.12, 0.6, 0.97);
    const cot = new THREE.Group(); cot.position.y = -0.29; ombro.add(cot);
    add(cot, casca(0.058, pl, Math.PI * 2, Math.PI * 0.5), -0.012, 0, 0, 0, 0, Math.PI / 2 + 0.2).scale.set(1, 0.9, 1.1);
    cot.add(new THREE.Mesh(perfil(P_ANTE, 0.27), m.tecido));
    add(cot, concha(trecho(P_ANTE, 0.08, 0.82), 0.27, FRENTE, 4.8, 0.012, pl));
    const emissor = add(cot, new THREE.Mesh(new THREE.TorusGeometry(0.052, 0.007, 8, 28), emissivo(time, 0.5)), 0, -0.235, 0, Math.PI / 2);
    add(cot, concha(trecho(P_ANTE, 0.25, 0.7), 0.27, FRENTE, 0.07, 0.02, emFio));
    const pulso = new THREE.Group(); pulso.position.y = -0.27; cot.add(pulso);
    add(pulso, chapa(0.072, 0.088, 0.034, 0.013, m.borracha), 0, -0.05, 0, 0, Math.PI / 2);
    add(pulso, chapa(0.066, 0.05, 0.012, 0.005, m.metalEscuro), 0.0, -0.045, 0.025 * s);
    const dedos = [];
    for (let i = 0; i < 4; i++) {
      const base = new THREE.Group(); base.position.set(0.027 - i * 0.018, -0.094, 0); pulso.add(base);
      add(base, new THREE.Mesh(new THREE.CapsuleGeometry(0.0083, 0.028, 3, 8), m.borracha), 0, -0.02);
      add(base, new THREE.Mesh(new THREE.BoxGeometry(0.014, 0.012, 0.018), m.metalEscuro), 0, -0.006, 0.006 * s);
      const meio = new THREE.Group(); meio.position.y = -0.042; base.add(meio);
      add(meio, new THREE.Mesh(new THREE.CapsuleGeometry(0.0078, 0.022, 3, 8), m.borracha), 0, -0.017);
      dedos.push({ base, meio });
    }
    const pol = new THREE.Group(); pol.position.set(0.03, -0.05, -0.02 * s); pulso.add(pol);
    add(pol, new THREE.Mesh(new THREE.CapsuleGeometry(0.0098, 0.03, 3, 8), m.borracha), 0, -0.022, 0, 0, 0, 0.6);
    const orbe = new THREE.Group(); orbe.position.set(0.0, -0.13, 0); pulso.add(orbe);
    add(orbe, new THREE.Mesh(new THREE.SphereGeometry(0.038, 16, 12), new THREE.MeshBasicMaterial({ color: 0xffffff })));
    const halo = add(orbe, new THREE.Mesh(new THREE.SphereGeometry(0.085, 16, 12), new THREE.MeshBasicMaterial({ color: COR_LUZ[time] ?? 0xffa040, transparent: true, opacity: 0.35, blending: THREE.AdditiveBlending, depthWrite: false })));
    const faisc = [];
    for (let i = 0; i < 12; i++) faisc.push(add(orbe, new THREE.Mesh(new THREE.SphereGeometry(0.007, 6, 4), new THREE.MeshBasicMaterial({ color: 0xffe0b0, transparent: true, blending: THREE.AdditiveBlending, depthWrite: false }))));
    orbe.visible = false;
    return { ombro, cot, pulso, dedos, pol, emissor, orbe, halo, faisc, s };
  };
  // ---- pernas ----
  const perna = (s) => {
    const fora = s < 0 ? ESQ : DIR;
    const coxa = new THREE.Group(); coxa.position.set(0, -0.05, 0.1 * s); quadril.add(coxa);
    coxa.add(new THREE.Mesh(perfil(P_COXA, 0.44), m.tecido));
    add(coxa, concha(trecho(P_COXA, 0.1, 0.78), 0.44, FRENTE, 2.2, 0.014, pl));
    add(coxa, concha(trecho(P_COXA, 0.14, 0.62), 0.44, fora, 1.3, 0.016, pl2));
    add(coxa, concha(trecho(P_COXA, 0.2, 0.6), 0.44, FRENTE + 0.45 * s, 0.05, 0.02, emFio));
    add(coxa, chapa(0.05, 0.08, 0.04, 0.01, m.couro), -0.03, -0.13, 0.09 * s, 0, s > 0 ? 0 : Math.PI);
    const joelho = new THREE.Group(); joelho.position.y = -0.44; coxa.add(joelho);
    add(joelho, casca(0.064, pl, Math.PI * 2, Math.PI * 0.5), 0.042, 0.01, 0, 0, 0, -Math.PI / 2).scale.set(1.15, 0.9, 1.05);
    add(joelho, casca(0.05, pl2, Math.PI * 2, Math.PI * 0.4), 0.05, -0.04, 0, 0, 0, -Math.PI / 2 - 0.3);
    joelho.add(new THREE.Mesh(perfil(P_CANELA, 0.42), m.tecido));
    add(joelho, concha(trecho(P_CANELA, 0.08, 0.86), 0.42, FRENTE, 2.0, 0.014, m.metal));
    add(joelho, concha(trecho(P_CANELA, 0.15, 0.55), 0.42, TRAS, 1.6, 0.01, pl2));
    const tornozelo = new THREE.Group(); tornozelo.position.y = -0.42; joelho.add(tornozelo);
    add(tornozelo, new THREE.Mesh(perfil([[0.058, 0], [0.066, 0.6], [0.069, 1]], 0.1), m.borracha), 0, 0.045);
    add(tornozelo, chapa(0.25, 0.1, 0.052, 0.022, m.borracha), 0.045, -0.044, 0, Math.PI / 2);
    add(tornozelo, chapa(0.265, 0.108, 0.018, 0.006, m.metalEscuro), 0.045, -0.071, 0, Math.PI / 2);
    add(tornozelo, casca(0.062, pl, Math.PI * 2, Math.PI * 0.45), 0.14, -0.035, 0, 0, 0, -Math.PI / 2).scale.set(1.4, 0.75, 0.9);
    add(tornozelo, casca(0.05, m.metal, Math.PI * 2, Math.PI * 0.5), 0.0, -0.005, 0.044 * s, s * Math.PI / 2, 0, 0).scale.set(1, 0.8, 1);
    return { coxa, joelho, tornozelo, s };
  };
  const bL = braco(-1), bR = braco(1), pL = perna(-1), pR = perna(1);
  const faixas = [];
  if (!simples) for (const s of [-1, 1]) {
    const N = 8, larg = 0.11;
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(new Float32Array(N * 2 * 3), 3));
    const uv = new Float32Array(N * 2 * 2);
    for (let i = 0; i < N; i++) uv.set([0, i / (N - 1), 1, i / (N - 1)], i * 4);
    g.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
    const idx = []; for (let i = 0; i < N - 1; i++) idx.push(i * 2, i * 2 + 1, i * 2 + 2, i * 2 + 1, i * 2 + 3, i * 2 + 2);
    g.setIndex(idx);
    const tex = canvasTex(32, (c, n) => { c.fillStyle = '#1b1e23'; c.fillRect(0, 0, n, n); c.fillStyle = '#' + (COR_TIME[time] ?? 0x777777).toString(16).padStart(6, '0'); c.fillRect(n * 0.12, 0, n * 0.1, n); c.fillRect(n * 0.78, 0, n * 0.1, n); c.fillStyle = '#000'; c.globalAlpha = 0.4; for (let y = 0; y < n; y += 3) c.fillRect(0, y, n, 1); });
    const mesh = new THREE.Mesh(g, new THREE.MeshStandardMaterial({ map: tex, roughness: 0.92, side: THREE.DoubleSide }));
    mesh.frustumCulled = false; mesh.castShadow = true;
    const ancora = new THREE.Object3D(); ancora.position.set(-0.17, 0.0, 0.07 * s); quadril.add(ancora);
    faixas.push({ mesh, ancora, N, larg, seg: 0.075, p: [], q: [], s });
  }
  raiz.traverse((o) => { if (o.isMesh) { o.castShadow = true; o.receiveShadow = true; } });
  raiz.userData = {
    tipo: 'operador', time, quadril, coluna, peito, pescoco, cabeca, bL, bR, pL, pR, liquido, visor, dutos, pl, pl2, emFio,
    emissores: [bL.emissor, bR.emissor], faixas, extras: faixas.map((f) => f.mesh), anim: novoEstado(),
  };
  return raiz;
}

export function criarAlvo() {
  const m = mats();
  const raiz = new THREE.Group();
  add(raiz, new THREE.Mesh(new THREE.CylinderGeometry(0.32, 0.37, 0.08, 30), m.metalEscuro), 0, 0.04);
  for (let i = 0; i < 4; i++) add(raiz, new THREE.Mesh(new THREE.BoxGeometry(0.08, 0.02, 0.5), m.metal), 0, 0.085, 0, 0, i * Math.PI / 4);
  const mola = new THREE.Group(); mola.position.y = 0.09; raiz.add(mola);
  for (let i = 0; i < 7; i++) add(mola, new THREE.Mesh(new THREE.TorusGeometry(0.055, 0.011, 8, 18), m.metal), 0, 0.02 + i * 0.03, 0, Math.PI / 2);
  const poste = new THREE.Group(); poste.position.y = 0.25; mola.add(poste);
  add(poste, new THREE.Mesh(new THREE.CylinderGeometry(0.03, 0.035, 0.9, 12), m.madeira), 0, 0.45);
  const corpo = add(poste, new THREE.Mesh(new THREE.LatheGeometry([[0.08, 0.56], [0.18, 0.5], [0.22, 0.3], [0.2, 0.1], [0.12, 0]].map(([r, y]) => new THREE.Vector2(r, y)), 22), m.madeira), 0, 0.72);
  corpo.scale.z = 0.6;
  const face = add(poste, new THREE.Mesh(new THREE.CircleGeometry(0.17, 36), new THREE.MeshStandardMaterial({ map: canvasTex(256, (g, n) => { g.fillStyle = '#d8cfb8'; g.fillRect(0, 0, n, n); for (let r = 5; r >= 1; r--) { g.fillStyle = r % 2 ? '#b8442e' : '#e8e0cc'; g.beginPath(); g.arc(n / 2, n / 2, r * 22, 0, 7); g.fill(); } g.globalAlpha = 0.3; for (let i = 0; i < 120; i++) { g.fillStyle = '#3a2a1a'; g.beginPath(); g.arc(Math.random() * n, Math.random() * n, Math.random() * 3, 0, 7); g.fill(); } }), roughness: 0.8 })), 0.135, 0.98, 0, 0, Math.PI / 2);
  void face;
  add(poste, new THREE.Mesh(new THREE.SphereGeometry(0.1, 18, 12), m.madeira), 0, 1.4);
  for (const s of [-1, 1]) add(poste, new THREE.Mesh(new THREE.CylinderGeometry(0.025, 0.025, 0.36, 8), m.madeira), 0, 1.12, 0.22 * s, Math.PI / 2 - s * 0.4);
  raiz.traverse((o) => { if (o.isMesh) { o.castShadow = true; o.receiveShadow = true; } });
  raiz.userData = { tipo: 'alvo', poste, anim: novoEstado() };
  return raiz;
}

// ======================= molas estáveis =======================
function mola(v = 0) { return { x: v, v: 0 }; }
function passoMola(s, alvo, dt, freq = 6, amort = 0.8) {
  const w = 2 * Math.PI * freq, n = Math.max(1, Math.ceil(dt / 0.004)), h = dt / n;
  for (let i = 0; i < n; i++) { s.v += (w * w * (alvo - s.x) - 2 * amort * w * s.v) * h; s.x += s.v * h; }
  if (!Number.isFinite(s.x)) { s.x = alvo; s.v = 0; }
  return s.x;
}
function molaV(v) { return { x: v.clone(), v: new THREE.Vector3() }; }
function passoMolaV(s, alvo, dt, freq, amort) {
  const w = 2 * Math.PI * freq, n = Math.max(1, Math.ceil(dt / 0.004)), h = dt / n;
  for (let i = 0; i < n; i++) {
    s.v.x += (w * w * (alvo.x - s.x.x) - 2 * amort * w * s.v.x) * h; s.v.y += (w * w * (alvo.y - s.x.y) - 2 * amort * w * s.v.y) * h; s.v.z += (w * w * (alvo.z - s.x.z) - 2 * amort * w * s.v.z) * h;
    s.x.addScaledVector(s.v, h);
  }
  if (!Number.isFinite(s.x.x + s.x.y + s.x.z)) { s.x.copy(alvo); s.v.set(0, 0, 0); }
  return s.x;
}
function novoEstado() {
  return {
    t: 0, lean: mola(), leanL: mola(), agacha: mola(), torcao: mola(), olharY: mola(), olharP: mola(), wobble: mola(), wobble2: mola(),
    recuo: molaV(V(0, 0, 0)), maos: null, pes: null, faixasIni: false, ultYaw: null, giro: mola(),
    curl: [mola(0.3), mola(0.3)], punho: [mola(0), mola(0)], aterrissa: mola(), ultMov: null,
  };
}

// ======================= IK de dois ossos =======================
const _a = new THREE.Vector3(), _b = new THREE.Vector3(), _c = new THREE.Vector3(), _d = new THREE.Vector3(), _e = new THREE.Vector3();
const _mat = new THREE.Matrix4(), _q = new THREE.Quaternion(), _qi = new THREE.Quaternion();
// sup: osso superior (filho de pai); inf: osso inferior (filho de sup, gira em Z local)
// alvoMundo: posição do fim da cadeia; poloPai: direção da dobra no espaço do pai; frente: +1 (X do osso para o polo) / -1
function ik2(pai, sup, inf, a, b, alvoMundo, poloPai, frente) {
  pai.updateWorldMatrix(true, false);
  const T = pai.worldToLocal(_a.copy(alvoMundo));
  const P = sup.position;
  const D = _b.copy(T).sub(P);
  let d = D.length();
  if (d < 1e-5) return;
  const dir = D.divideScalar(d);
  d = clamp(d, Math.abs(a - b) + 1e-3, a + b - 1e-3);
  const x = (a * a - b * b + d * d) / (2 * d), h = Math.sqrt(Math.max(0, a * a - x * x));
  const perp = _c.copy(poloPai).addScaledVector(dir, -poloPai.dot(dir));
  if (perp.lengthSq() < 1e-8) perp.set(1, 0, 0).addScaledVector(dir, -dir.x);
  perp.normalize();
  const E = _d.copy(P).addScaledVector(dir, x).addScaledVector(perp, h);
  const U = _e.copy(E).sub(P).normalize();
  const fim = _a.copy(P).addScaledVector(dir, d);
  const L = fim.sub(E).normalize();
  const Y = U.clone().negate();
  let Z = new THREE.Vector3().crossVectors(U, L);
  if (Z.lengthSq() < 1e-8) Z.crossVectors(U, perp);
  Z.normalize();
  let X = new THREE.Vector3().crossVectors(Y, Z).normalize();
  if (X.dot(perp) * frente < 0) { Z.negate(); X.negate(); }
  Z.crossVectors(X, Y).normalize();
  _mat.makeBasis(X, Y, Z);
  sup.quaternion.setFromRotationMatrix(_mat);
  _qi.copy(sup.quaternion).invert();
  const Ll = L.applyQuaternion(_qi);
  inf.rotation.set(0, 0, Math.atan2(Ll.x, -Ll.y));
}
// orienta um osso para uma rotação no mundo (pés planos, mãos)
function orientarMundo(osso, qMundo) {
  osso.parent.updateWorldMatrix(true, false);
  osso.parent.getWorldQuaternion(_q);
  osso.quaternion.copy(_q.invert().multiply(qMundo));
}

// ======================= animação =======================
// est: { tServ, velW (Vector3 m/s, mundo), mov{u, fim}, prep{k, centro}, contato{dt, centro}, recup, guarda{gesto,k,absorveDt,fonte},
//        golpe{dt, fonte}, folego, olhar (Vector3 mundo), solo(x,z)->altura }
export function animar(obj, est, dt) {
  const u = obj.userData;
  if (!u.anim) u.anim = novoEstado();
  const A = u.anim;
  const D = u.dims || { coxa: 0.44, canela: 0.42, braco: 0.29, ante: 0.29, quadril: 0.97, larg: 0.115, tornozelo: 0.065, ombroY: 0.3, escala: 1 };
  const E = D.escala;
  A.t += dt;
  if (u.tipo === 'alvo') {
    const imp = est.golpe && est.golpe.dt < 2500 ? Math.exp(-est.golpe.dt / 500) * Math.sin(est.golpe.dt / 55) : 0;
    u.poste.rotation.z = passoMola(A.wobble, -imp * 0.55, dt, 2.6, 0.18);
    u.poste.rotation.x = passoMola(A.wobble2, imp * 0.18, dt, 2.2, 0.2);
    return;
  }
  obj.updateMatrixWorld(true);
  const yaw = -obj.rotation.y;
  const fwd = V(Math.cos(yaw), 0, Math.sin(yaw)), dir = V(-fwd.z, 0, fwd.x);
  const velW = est.velW || V(0, 0, 0);
  const spd = Math.hypot(velW.x, velW.z);
  const vF = velW.x * fwd.x + velW.z * fwd.z, vS = velW.x * dir.x + velW.z * dir.z;
  const solo = est.solo || (() => 0);
  const base = obj.position;
  // ---------- pesos das camadas ----------
  const kPrep = est.prep ? suave(est.prep.k) : 0;
  const cdt = est.contato ? est.contato.dt : 9999;
  const kGolpeArea = cdt < 520 ? (cdt < 85 ? suave(cdt / 85) : 1 - suave((cdt - 85) / 435)) : 0;
  const kMov = est.mov ? 1 : 0;
  const movU = est.mov ? clamp01(est.mov.u) : 0;
  const kGuarda = est.guarda ? suave(est.guarda.k) : 0;
  const kRecup = est.recup ? 1 : 0;
  const gdt = est.golpe ? est.golpe.dt : 9999;
  // estilo do ataque: golpe no chão (área nos pés) ou arremesso (área à distância), com direção local
  const focoA = (est.prep && est.prep.centro) || (est.contato && est.contato.centro);
  let distA = 0, dirA = V(1, 0, 0);
  if (focoA) { const d = focoA.clone().sub(obj.position); d.y = 0; distA = d.length(); const yy = -obj.rotation.y; if (distA > 1e-3) { d.normalize(); dirA = V(d.x * Math.cos(yy) + d.z * Math.sin(yy), 0, -d.x * Math.sin(yy) + d.z * Math.cos(yy)); } }
  const arremesso = distA > 0.9;
  const recuoMov = est.mov && est.mov.recuo;
  // aterrissagem do deslocamento: compressão ao terminar
  if (A.ultMov && !est.mov) A.aterrissa.v -= 9;
  A.ultMov = est.mov ? { ...est.mov } : null;
  const aterr = passoMola(A.aterrissa, 0, dt, 3.2, 0.42);
  // ---------- tronco ----------
  let lean = vF * 0.045 + Math.max(0, spd - 3) * 0.04, leanL = -vS * 0.03, agacha = 0, torcao = 0;
  if (arremesso) { lean += kPrep * 0.05 - kPrep * 0.1; agacha += kPrep * 0.06; torcao += kPrep * 0.75; lean += kGolpeArea * 0.32; torcao -= kGolpeArea * 0.55; agacha += kGolpeArea * 0.08; }
  else { lean += kPrep * 0.18; agacha += kPrep * 0.12; torcao += kPrep * 0.45; lean += kGolpeArea * 0.55; agacha += kGolpeArea * 0.26; torcao -= kGolpeArea * 0.35; }
  if (kMov) { const ant = movU < 0.18 ? suave(movU / 0.18) : 1; lean = lerp(lean, recuoMov ? -0.4 : 0.75, ant); agacha = lerp(agacha, 0.2 - 0.12 * Math.sin(movU * Math.PI), ant); }
  if (est.guarda) {
    const g = est.guarda.gesto;
    if (g === 'ombro') { torcao += 0.85 * kGuarda; lean += 0.05 * kGuarda; }
    else if (g === 'bracos_cruzados') { agacha += 0.08 * kGuarda; lean += 0.12 * kGuarda; }
    else if (g === 'palma_frente') { lean += 0.06 * kGuarda; torcao -= 0.25 * kGuarda; }
    else agacha += 0.03 * kGuarda;
  }
  if (kRecup) { lean = lerp(lean, 0.72, 0.9); agacha = lerp(agacha, 0.17, 0.9); }
  agacha += Math.max(0, -aterr) * 0.18;
  // recuo: golpe recebido (direcional) e absorção (empurrão) — mola em 3D no espaço local
  let alvoR = V(0, 0, 0);
  const fonteLocal = (f) => { if (!f) return V(-1, 0, 0); const d = f.clone().sub(base); d.y = 0; if (d.lengthSq() < 1e-6) return V(-1, 0, 0); d.normalize(); return V(d.x * fwd.x + d.z * fwd.z, 0, d.x * dir.x + d.z * dir.z); };
  if (gdt < 700) { const fl = fonteLocal(est.golpe.fonte); alvoR = fl.multiplyScalar(-Math.exp(-gdt / 150) * 0.32); }
  if (est.guarda && est.guarda.absorveDt !== undefined && est.guarda.absorveDt < 500) { const fl = fonteLocal(est.guarda.fonte); alvoR.add(fl.multiplyScalar(-Math.exp(-est.guarda.absorveDt / 110) * 0.16)); }
  const rec = passoMolaV(A.recuo, alvoR, dt, 3.6, 0.38);
  // respiração e peso
  const ritmo = kRecup ? 3.3 : 1.35 + spd * 0.25;
  const resp = Math.sin(A.t * ritmo * Math.PI);
  const lS = passoMola(A.lean, lean + rec.x * 1.6, dt, 4.2, 0.72);
  const lL = passoMola(A.leanL, leanL - rec.z * 1.4, dt, 4.2, 0.72);
  const ag = passoMola(A.agacha, agacha + rec.length() * 0.25, dt, 6, 0.75);
  const tor = passoMola(A.torcao, torcao, dt, 4.5, 0.7);
  // ---------- pés: controlador de passos com plantio no mundo ----------
  if (!A.pes) {
    A.pes = [u.pL, u.pR].map((p) => { const w = base.clone().addScaledVector(dir, D.larg * p.s); w.y = solo(w.x, w.z); return { plant: w, swing: null, ultimo: -1 }; });
  }
  const desejo = (i) => { const p = i === 0 ? u.pL : u.pR; const w = base.clone().addScaledVector(dir, D.larg * p.s).addScaledVector(velW, 0.2).addScaledVector(fwd, i === 0 ? 0.02 : -0.02); w.y = solo(w.x, w.z); return w; };
  if (kMov) {
    // em deslocamento os pés não acompanham: recolhidos; replanta ao aterrissar
    for (let i = 0; i < 2; i++) { A.pes[i].swing = null; A.pes[i].plant = desejo(i); A.pes[i].voo = true; }
  } else {
    let melhor = -1, dmax = 0;
    for (let i = 0; i < 2; i++) {
      const p = A.pes[i];
      if (p.voo) { p.plant = desejo(i).addScaledVector(fwd, i === 0 ? 0.12 : -0.1); p.voo = false; }
      if (p.plant.distanceTo(desejo(i)) > 1.4) { p.plant = desejo(i); p.swing = null; }
      if (p.swing) continue;
      const d = p.plant.distanceTo(desejo(i));
      const lim = 0.07 + Math.min(spd, 5) * 0.075;
      if (d > lim && d > dmax) { dmax = d; melhor = i; }
    }
    const outroVoa = A.pes.some((p) => p.swing && p.swing.t < (spd > 3.2 ? 0.55 : 0.92));
    // giro parado: passos de correção
    if (A.ultYaw !== null) { const dy = Math.abs(((yaw - A.ultYaw + Math.PI * 3) % (Math.PI * 2)) - Math.PI); A.giro.x += dy; }
    A.ultYaw = yaw;
    if (melhor >= 0 && !outroVoa) {
      const p = A.pes[melhor];
      const dur = clamp(0.36 - 0.045 * spd, 0.17, 0.36);
      const alvo = desejo(melhor).addScaledVector(velW, dur * 0.55);
      alvo.y = solo(alvo.x, alvo.z);
      p.swing = { de: p.plant.clone(), para: alvo, t: 0, dur, alt: 0.07 + 0.035 * Math.min(spd, 5) };
    }
    for (const p of A.pes) {
      if (!p.swing) continue;
      p.swing.t += dt / p.swing.dur;
      if (p.swing.t >= 1) { p.plant.copy(p.swing.para); p.swing = null; }
    }
  }
  const pePos = (i) => {
    const p = A.pes[i];
    if (!p.swing) return p.plant.clone();
    const k = suave(p.swing.t);
    const w = p.swing.de.clone().lerp(p.swing.para, k);
    w.y = lerp(p.swing.de.y, p.swing.para.y, k) + p.swing.alt * Math.sin(Math.PI * clamp01(p.swing.t));
    return w;
  };
  // quadril sobre os pés: altura pela passada, balanço lateral para o pé de apoio
  const pW = [pePos(0), pePos(1)];
  const abertura = pW[0].distanceTo(pW[1]);
  const apoio = A.pes[0].swing ? 1 : A.pes[1].swing ? -1 : 0;
  const soloMedio = (pW[0].y + pW[1].y) / 2;
  const bob = -Math.max(0, abertura - 0.42 * E) * 0.22 + (A.pes.some((p) => p.swing) ? 0.012 : 0) * Math.min(1, spd);
  const yQuadril = D.quadril - ag * E + bob + soloMedio - base.y + 0.008 * resp;
  u.quadril.position.set(rec.x * 0.6 - lS * 0.08, yQuadril, rec.z * 0.6 + apoio * 0.035 * (spd < 3 ? 1 : 0.4));
  u.quadril.rotation.set(-lL * 0.5 + apoio * 0.05, tor * 0.25 - (pW[0].clone().sub(pW[1]).dot(fwd)) * 0.35, -lS * 0.38);
  u.coluna.rotation.set(-lL * 0.4, tor * 0.45 + (pW[0].clone().sub(pW[1]).dot(fwd)) * 0.55, -lS * 0.32);
  u.peito.rotation.set(-lL * 0.3, tor * 0.3, -lS * 0.22 + resp * (kRecup ? 0.05 : 0.012));
  obj.updateMatrixWorld(true);
  // pernas por IK
  const pernas = [u.pL, u.pR];
  for (let i = 0; i < 2; i++) {
    const p = pernas[i];
    let alvo = pW[i].clone(); alvo.y += D.tornozelo;
    if (kMov) {
      // pose de voo: perna da frente recolhida, de trás estendida (no espaço do quadril)
      const loc = (i === 0 ? V(0.28, -0.52, 0.12 * p.s) : V(-0.32, -0.72, 0.1 * p.s)).multiplyScalar(E);
      const ant = movU < 0.15 ? 0 : suave((movU - 0.15) / 0.2) * (1 - suave((movU - 0.85) / 0.15));
      const wv = u.quadril.localToWorld(loc.clone());
      alvo = alvo.lerp(wv, ant);
    }
    ik2(u.quadril, p.coxa, p.joelho, D.coxa, D.canela, alvo, V(1, 0, 0.12 * p.s), +1);
    // pé plano no chão; rola o calcanhar na saída do passo
    const sw = A.pes[i].swing;
    const rola = sw ? -0.55 * Math.sin(Math.PI * clamp01(sw.t * 1.4)) * (1 - clamp01(sw.t)) + 0.25 * Math.sin(Math.PI * clamp01((sw.t - 0.6) / 0.4)) : 0;
    const qPe = new THREE.Quaternion().setFromEuler(new THREE.Euler(0, -yaw, kMov ? -0.4 : rola, 'YXZ'));
    orientarMundo(p.tornozelo, qPe);
  }
  // ---------- braços por IK: alvos de mão no espaço do peito ----------
  const L = [], ladoF = [pW[0].clone().sub(base).dot(fwd), pW[1].clone().sub(base).dot(fwd)];
  for (const b of [u.bL, u.bR]) {
    const s = b.s, S = b.ombro.position;
    const balanco = (s < 0 ? ladoF[1] - ladoF[0] : ladoF[0] - ladoF[1]) * (0.5 + 0.25 * Math.min(1, spd / 3));
    const corre = suave((spd - 2.8) / 2);
    const Rel = (x, y, z) => V(S.x + x * E, S.y + y * E, S.z + z * E);
    let m = Rel(0.03 + balanco * 0.9, -lerp(0.55, 0.36, corre), s * (0.05 + 0.04 * corre));
    m.y += Math.max(0, balanco) * 0.25;
    let curl = lerp(0.35, 0.8, corre);
    let punho = 0;
    let estica = false;
    const blend = (alvo, k) => { m.lerp(alvo, k); };
    if (kMov) { const ant = movU < 0.18 ? suave(movU / 0.18) : 1; blend(recuoMov ? Rel(0.34, -0.16, s * 0.12) : Rel(-0.32, -0.32, s * 0.28), ant); curl = lerp(curl, recuoMov ? 0.0 : 0.2, ant); }
    if (est.prep && kPrep > 0) {
      if (arremesso) {
        const elasticoP = ((est.prep.materia && est.prep.materia.elast) || 0) > 0.4;
        if (s > 0 && elasticoP && est.prep.k > 0.76 && focoA) {
          // braço elástico é o próprio projétil: estica nos últimos 24% do preparo e chega no contato
          const pv = suave((est.prep.k - 0.76) / 0.24);
          blend(Rel(-0.26, 0.2, 0.08), 1 - pv);
          blend(u.peito.worldToLocal(focoA.clone().add(V(0, 0.55, 0))), pv); curl = 1; estica = true;
        } else if (s > 0) { blend(Rel(-0.26, 0.2, 0.08), kPrep); curl = lerp(curl, 0.5, kPrep); punho = -0.6 * kPrep; }
        else { blend(Rel(dirA.x * 0.52, -0.02, dirA.z * 0.52), kPrep); curl = lerp(curl, 0.05, kPrep); punho = lerp(punho, -0.9, kPrep); }
      } else {
        if (s > 0) { blend(Rel(-0.2, 0.16, 0.12), kPrep); curl = lerp(curl, 0.55, kPrep); punho = -0.5 * kPrep; }
        else { blend(Rel(0.34, -0.12, 0.12), kPrep); curl = lerp(curl, 0.1, kPrep); }
      }
    }
    if (kGolpeArea > 0) {
      if (arremesso) {
        const elastico = ((est.contato && est.contato.materia && est.contato.materia.elast) || 0) > 0.4;
        if (s > 0 && elastico && focoA) { const fica = cdt < 110 ? 1 : kGolpeArea; blend(u.peito.worldToLocal(focoA.clone().add(V(0, 0.55, 0))), fica); curl = 1; estica = fica > 0.5; }
        else if (s > 0) { blend(Rel(dirA.x * 0.64, 0.02, dirA.z * 0.64), kGolpeArea); curl = lerp(curl, 0.0, kGolpeArea); punho = lerp(punho, -1.2, kGolpeArea); }
        else blend(Rel(-0.2, -0.3, -0.25), kGolpeArea);
      } else {
        if (s > 0) { const chao = u.peito.worldToLocal(base.clone().addScaledVector(fwd, 0.52).addScaledVector(dir, 0.12).setY(solo(base.x, base.z) + 0.05)); blend(chao, kGolpeArea); curl = lerp(curl, 0.95, kGolpeArea); punho = lerp(punho, 0.3, kGolpeArea); }
        else blend(Rel(-0.05, -0.18, -0.32), kGolpeArea);
      }
    }
    if (est.guarda && kGuarda > 0) {
      const g = est.guarda.gesto;
      if (g === 'bracos_cruzados') { blend(s < 0 ? Rel(0.36, 0.12, 0.315) : Rel(0.33, 0.17, -0.315), kGuarda); curl = lerp(curl, 0.9, kGuarda); punho = lerp(punho, 0.25, kGuarda); }
      else if (g === 'palma_frente') { if (s > 0) { blend(Rel(0.62, 0.03, -0.115), kGuarda); curl = lerp(curl, 0.0, kGuarda); punho = lerp(punho, -1.35, kGuarda); } else { blend(Rel(0.22, -0.1, 0.135), kGuarda); curl = lerp(curl, 0.85, kGuarda); } }
      else if (g === 'ombro') { if (s < 0) { blend(Rel(0.24, 0.2, 0.155), kGuarda); curl = lerp(curl, 0.9, kGuarda); } else blend(Rel(-0.12, -0.5, 0.085), kGuarda); }
      else { blend(Rel(0.05, 0.15, s * 0.45), kGuarda); curl = lerp(curl, 0.0, kGuarda); }
    }
    if (kRecup) {
      const p = s < 0 ? u.pL : u.pR;
      p.joelho.updateWorldMatrix(true, false);
      const kj = p.joelho.getWorldPosition(new THREE.Vector3()).addScaledVector(fwd, 0.09).add(V(0, 0.04, 0));
      blend(u.peito.worldToLocal(kj), 0.92); curl = lerp(curl, 0.6, 0.9);
    }
    if (gdt < 700) { const k = Math.exp(-gdt / 220); blend(Rel(-0.15, 0.1 * (s > 0 ? 1 : 0.4), s * 0.4), k * 0.8); curl = lerp(curl, 0.1, k); }
    L.push({ b, m, curl, punho, estica });
  }
  // suaviza as mãos (inércia) e resolve o IK
  if (!A.maos) A.maos = L.map((x) => molaV(x.m));
  if (!A.estica) A.estica = [mola(1), mola(1)];
  L.forEach((x, i) => {
    const mm = passoMolaV(A.maos[i], x.m, dt, x.estica ? 40 : kGolpeArea > 0.2 || gdt < 300 ? 9 : 5.5, x.estica ? 0.85 : 0.8);
    const w = u.peito.localToWorld(mm.clone());
    // braço elástico: os ossos esticam para alcançar o alvo e voltam com mola (oscila na volta)
    let alvoSc = 1;
    if (x.estica) { x.b.ombro.updateWorldMatrix(true, false); const sh = x.b.ombro.getWorldPosition(new THREE.Vector3()); alvoSc = Math.max(1, sh.distanceTo(w) / ((D.braco + D.ante) * 0.99)); }
    const sc = Math.max(0.6, passoMola(A.estica[i], alvoSc, dt, x.estica ? 30 : 3.2, x.estica ? 0.9 : 0.22));
    x.b.ombro.scale.set(1, sc, 1); x.b.cot.scale.set(1, sc > 1 ? 1 : 1, 1);
    x.b.pulso.scale.set(1, 1 / sc, 1);
    ik2(u.peito, x.b.ombro, x.b.cot, D.braco * sc, D.ante * sc, w, V(-0.7, -0.35, 0.65 * x.b.s), -1);
    const cu = passoMola(A.curl[i], x.curl, dt, 7, 0.8);
    const pu = passoMola(A.punho[i], x.punho, dt, 6, 0.8);
    x.b.pulso.rotation.set(0, 0, pu);
    for (let k = 0; k < x.b.dedos.length; k++) { const c = cu * (1 + k * 0.08); x.b.dedos[k].base.rotation.set(0, 0, -c * 1.25); x.b.dedos[k].meio.rotation.set(0, 0, -c * 1.4); }
    x.b.pol.rotation.set(0, 0, -cu * 0.6);
  });
  // orbe de carga na mão direita
  const orbe = u.bR.orbe;
  orbe.visible = kPrep > 0.02 || (cdt < 90);
  if (orbe.visible) {
    const k = est.prep ? kPrep : 1 - cdt / 90;
    orbe.scale.setScalar(0.4 + 1.1 * k);
    u.bR.halo.material.opacity = 0.25 + 0.3 * Math.sin(A.t * 30) * 0.5 + 0.2 * k;
    u.bR.faisc.forEach((f, i) => { const a = A.t * (6 + i) + i * 1.7, r = 0.08 + 0.05 * Math.sin(A.t * 9 + i); f.position.set(Math.cos(a) * r, Math.sin(a * 1.3) * r * 0.7, Math.sin(a) * r); f.material.opacity = 0.9 * k; });
  }
  // ---------- cabeça: olhar para a ação, com limites ----------
  let yAlvo = 0, pAlvo = -0.05 + resp * 0.02;
  const foco = (est.prep && est.prep.centro) || (est.contato && cdt < 600 && est.contato.centro) || est.olhar;
  if (foco) {
    u.pescoco.updateWorldMatrix(true, false);
    const lp = u.pescoco.worldToLocal(foco.clone().add(V(0, 1.0, 0)));
    yAlvo = clamp(Math.atan2(-lp.z, lp.x), -1.1, 1.1);
    pAlvo = clamp(Math.atan2(lp.y - 0.1, Math.hypot(lp.x, lp.z)), -0.7, 0.45);
  }
  if (kRecup) pAlvo = -0.55;
  if (kMov) pAlvo = 0.25;
  const oy = passoMola(A.olharY, yAlvo, dt, 3.5, 0.75), op = passoMola(A.olharP, pAlvo, dt, 3.5, 0.75);
  u.pescoco.rotation.set(0, oy * 0.4, op * 0.4 + lS * 0.3);
  u.cabeca.rotation.set(rec.z * 1.2, oy * 0.6, op * 0.6 + lS * 0.25 - rec.x * 1.6);
  // ---------- emissivos e reservatório ----------
  const brilho = 0.45 + 4.5 * kPrep + 5 * kGolpeArea + 2.5 * kMov + 1.5 * kGuarda;
  for (const e of u.emissores) e.material.emissiveIntensity = lerp(e.material.emissiveIntensity, brilho, 0.35);
  for (const d of u.dutos || []) d.material.emissiveIntensity = 0.5 + 0.6 * Math.max(0, Math.sin(A.t * 6 - 1)) * (kPrep + kMov + 0.2) + 2 * kGolpeArea;
  if (u.pl) {
    const hit = gdt < 160 ? 1 - gdt / 160 : 0;
    u.pl.emissive.setRGB(1, 0.15, 0.08); u.pl.emissiveIntensity = hit * hit * 0.9;
    if (u.pl2) { u.pl2.emissive.setRGB(1, 0.15, 0.08); u.pl2.emissiveIntensity = hit * hit * 0.7; }
    if (u.emFio) u.emFio.emissiveIntensity = 0.9 + 2.5 * kPrep + 3 * kGolpeArea + 1.5 * kMov + Math.sin(A.t * 3) * 0.25;
  }
  const f = clamp01((est.folego ?? 100) / 100);
  if (u.liquido) { u.liquido.scale.y = Math.max(0.03, f); u.liquido.position.y = -0.125 + 0.125 * Math.max(0.03, f); }
  if (u.olhos) for (const o of u.olhos) { o.rotation.set(0, -Math.PI / 2 + oy * 0.25, op * 0.2); }
  if (u.visor) u.visor.material.emissiveIntensity = gdt < 300 ? (Math.sin(gdt / 18) > 0 ? 0.3 : 4) : kRecup ? 0.8 + 0.5 * Math.sin(A.t * 9) : 2.4 + kPrep * 2;
  // ---------- faixas de tecido (verlet) ----------
  for (const fx of u.faixas) {
    fx.ancora.updateWorldMatrix(true, false);
    const ponta = fx.ancora.getWorldPosition(new THREE.Vector3());
    if (!A.faixasIni || fx.p.length === 0) { fx.p = []; fx.q = []; for (let i = 0; i < fx.N; i++) { const w = ponta.clone().add(V(0, -i * fx.seg, 0)).addScaledVector(fwd, -0.02 * i); fx.p.push(w); fx.q.push(w.clone()); } }
    const nsub = Math.max(1, Math.ceil(dt / 0.008)), h = dt / nsub;
    for (let s = 0; s < nsub; s++) {
      fx.p[0].copy(ponta);
      for (let i = 1; i < fx.N; i++) {
        const p = fx.p[i], q = fx.q[i];
        const vx = (p.x - q.x) * 0.985, vy = (p.y - q.y) * 0.985, vz = (p.z - q.z) * 0.985;
        q.copy(p);
        p.x += vx; p.y += vy - 9.8 * h * h; p.z += vz;
      }
      for (let it = 0; it < 3; it++) {
        for (let i = 1; i < fx.N; i++) {
          const a = fx.p[i - 1], b = fx.p[i];
          const d = b.clone().sub(a), l = d.length() || 1e-6;
          const corr = d.multiplyScalar((l - fx.seg) / l);
          if (i === 1) b.sub(corr); else { a.addScaledVector(corr, 0.5); b.addScaledVector(corr, -0.5); }
        }
        // colisão: atrás do corpo e acima do chão
        for (let i = 1; i < fx.N; i++) {
          const p = fx.p[i], rel = p.clone().sub(base);
          const xf = rel.dot(fwd), yy = p.y - base.y;
          const lim = yy > 0.35 ? -0.16 - (yy < 0.75 ? 0.03 : 0) : -0.06;
          if (xf > lim) p.addScaledVector(fwd, lim - xf);
          const sy = solo(p.x, p.z) + 0.015;
          if (p.y < sy) p.y = sy;
        }
        fx.p[0].copy(ponta);
      }
    }
    const pos = fx.mesh.geometry.attributes.position;
    for (let i = 0; i < fx.N; i++) {
      const a = fx.p[i], w = fx.larg * (1 - i / (fx.N * 1.6));
      pos.setXYZ(i * 2, a.x + dir.x * w * 0.5, a.y, a.z + dir.z * w * 0.5);
      pos.setXYZ(i * 2 + 1, a.x - dir.x * w * 0.5, a.y, a.z - dir.z * w * 0.5);
    }
    pos.needsUpdate = true;
    fx.mesh.geometry.computeVertexNormals();
    fx.mesh.geometry.computeBoundingSphere();
  }
  A.faixasIni = true;
}

// Posa um operador estático (fantasma do deslocamento) sem simulação de passos.
export function posarDeslocamento(obj, u) {
  for (let i = 0; i < 30; i++) animar(obj, { tServ: 0, velW: V(0, 0, 0), mov: { u }, folego: 100 }, 0.02);
}
