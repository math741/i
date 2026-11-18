// Humano procedural: esqueleto (THREE.Bone) + pele contínua por anéis anatômicos (SkinnedMesh),
// cabeça esculpida, olhos, cabelo, barba e roupas que deformam com o corpo. Tudo vem de um genoma
// paramétrico com semente — variações infinitas, determinísticas (o mesmo genoma gera o mesmo corpo
// em todos os clientes). A interface com a animação é a mesma do operador (personagem.js).
import * as THREE from 'three';

const V = (x, y, z) => new THREE.Vector3(x, y, z);
const lerp = (a, b, k) => a + (b - a) * k;
const c01 = (x) => Math.max(0, Math.min(1, x));
const PELES = ['#f7dcc8', '#ecc2a2', '#d4a07a', '#b07850', '#8a5636', '#5e3722', '#3e2418'];
const CABELOS = ['#121010', '#2a1a12', '#4a2e1c', '#7a4a26', '#a5662f', '#c9a063', '#e2cf9a', '#8e8e8e', '#c8c8c8', '#6a1d10'];
const OLHOS = ['#3b2a1a', '#5a3b20', '#6b7a3a', '#3a6a8a', '#7a9ab0', '#2a3a2a'];
const ROUPAS = ['tatico', 'casual', 'esportivo'];

function rndS(s) { return (i) => { const x = Math.sin(s * 127.1 + i * 311.7) * 43758.5453; return x - Math.floor(x); }; }
export function genomaDe(semente) {
  const s = typeof semente === 'number' ? semente : [...String(semente)].reduce((a, c) => (a * 31 + c.charCodeAt(0)) % 1e6, 7);
  const r = rndS(s);
  const pal = (lista, x) => lista[Math.floor(x * lista.length) % lista.length];
  const hsl = (h, sa, l) => '#' + new THREE.Color().setHSL(h, sa, l).getHexString();
  return normalizarGenoma({
    semente: s, altura: 1.62 + r(1) * 0.34, massa: r(2) ** 1.4, musculo: r(3), ombros: r(4), quadril: r(5),
    pele: r(6), subtom: r(7), idade: r(8) ** 2,
    rosto: { largura: r(9), mandibula: r(10), queixo: r(11), nariz: r(12), labios: r(13), olhos: r(14), macas: r(15), testa: r(16) },
    cabelo: { estilo: Math.floor(r(17) * 5), cor: pal(CABELOS, r(18)), volume: r(19) }, barba: r(20) > 0.55 ? r(21) : 0,
    corOlhos: pal(OLHOS, r(22)), roupa: { estilo: pal(ROUPAS, r(23)), cor1: hsl(r(24), 0.25 + r(25) * 0.4, 0.18 + r(26) * 0.3), cor2: hsl(r(27), 0.3 + 0.4 * r(28), 0.12 + 0.25 * r(29)), cor3: hsl(r(30), 0.2, 0.08 + 0.2 * r(31)) },
  });
}
export function normalizarGenoma(g) {
  const d = { altura: 1.78, massa: 0.4, musculo: 0.5, ombros: 0.5, quadril: 0.5, pele: 0.4, subtom: 0.5, idade: 0.2, barba: 0, corOlhos: '#4a3020',
    rosto: { largura: 0.5, mandibula: 0.5, queixo: 0.5, nariz: 0.5, labios: 0.5, olhos: 0.5, macas: 0.5, testa: 0.5 },
    cabelo: { estilo: 1, cor: '#2a1a12', volume: 0.5 }, roupa: { estilo: 'tatico', cor1: '#2c3440', cor2: '#3a3a2a', cor3: '#1c1e22' } };
  const o = { ...d, ...(g || {}) };
  o.rosto = { ...d.rosto, ...(g && g.rosto) }; o.cabelo = { ...d.cabelo, ...(g && g.cabelo) }; o.roupa = { ...d.roupa, ...(g && g.roupa) };
  o.altura = Math.max(1.5, Math.min(2.1, +o.altura));
  return o;
}
function corPele(g) {
  const x = c01(g.pele) * (PELES.length - 1), i = Math.floor(x), k = x - i;
  const c = new THREE.Color(PELES[i]).lerp(new THREE.Color(PELES[Math.min(PELES.length - 1, i + 1)]), k);
  c.offsetHSL((g.subtom - 0.5) * 0.03, (g.subtom - 0.5) * 0.08, 0);
  return c;
}

// ---------- texturas ----------
let _tex = null;
function texturas() {
  if (_tex) return _tex;
  const mk = (n, f, srgb = false) => { const c = document.createElement('canvas'); c.width = c.height = n; const g = c.getContext('2d'); f(g, n); const t = new THREE.CanvasTexture(c); t.wrapS = t.wrapT = THREE.RepeatWrapping; if (srgb) t.colorSpace = THREE.SRGBColorSpace; return t; };
  const ruidoNormal = (n, freq, amp) => mk(n, (g) => { const img = g.createImageData(n, n); const h = new Float32Array(n * n); for (let i = 0; i < n * n; i++) h[i] = Math.random(); for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) { const i = y * n + x; const dx = h[y * n + (x + 1) % n] - h[y * n + (x - 1 + n) % n], dy = h[((y + 1) % n) * n + x] - h[((y - 1 + n) % n) * n + x]; img.data[i * 4] = 128 + dx * amp; img.data[i * 4 + 1] = 128 + dy * amp; img.data[i * 4 + 2] = 255; img.data[i * 4 + 3] = 255; } g.putImageData(img, 0, 0); void freq; });
  const poros = ruidoNormal(256, 1, 22); poros.repeat.set(10, 10);
  const trama = mk(128, (g, n) => { const img = g.createImageData(n, n); for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) { const a = Math.sin(x * 1.1) * 0.5, b = Math.sin(y * 1.1) * 0.5; const ch = ((x >> 1) + (y >> 1)) % 2 ? a : b; const i = (y * n + x) * 4; img.data[i] = 128 + ch * 60; img.data[i + 1] = 128 + (b - a) * 40; img.data[i + 2] = 255; img.data[i + 3] = 255; } g.putImageData(img, 0, 0); }); trama.repeat.set(12, 12);
  const fios = mk(64, (g, n) => { g.fillStyle = '#8080ff'; g.fillRect(0, 0, n, n); for (let x = 0; x < n; x++) { const v = 100 + Math.random() * 60; g.fillStyle = `rgb(${v},128,255)`; g.fillRect(x, 0, 1, n); } }); fios.repeat.set(8, 1);
  const iris = (cor) => mk(128, (g, n) => { g.fillStyle = '#f4f1ec'; g.fillRect(0, 0, n, n); const cx = n / 2; const gr = g.createRadialGradient(cx, cx, 4, cx, cx, 30); gr.addColorStop(0, '#050505'); gr.addColorStop(0.32, '#050505'); gr.addColorStop(0.36, cor); gr.addColorStop(0.9, new THREE.Color(cor).multiplyScalar(0.5).getStyle()); gr.addColorStop(1, '#1a1a1a'); g.fillStyle = gr; g.beginPath(); g.arc(cx, cx, 30, 0, 7); g.fill(); g.strokeStyle = 'rgba(255,255,255,0.15)'; for (let i = 0; i < 40; i++) { const a = Math.random() * 6.28; g.beginPath(); g.moveTo(cx + Math.cos(a) * 11, cx + Math.sin(a) * 11); g.lineTo(cx + Math.cos(a) * 28, cx + Math.sin(a) * 28); g.stroke(); } g.fillStyle = 'rgba(200,60,60,0.25)'; for (let i = 0; i < 30; i++) g.fillRect(Math.random() * n, Math.random() * n, 4, 1); }, true);
  _tex = { poros, trama, fios, iris };
  return _tex;
}

// ---------- geometria por anéis com pesos de pele ----------
// anel: { c: Vector3 centro (pose de ligação), dx: meia-profundidade (frente/trás), dz: meia-largura,
//         fr: protuberância frontal, tr: traseira, w: [[osso, peso], ...] }
function tuboAneis(aneis, { seg = 28, exp = 2.2, off = 0, tampaFim = false } = {}) {
  const nA = aneis.length;
  const pos = [], uv = [], si = [], sw = [], idx = [];
  for (let a = 0; a < nA; a++) {
    const A = aneis[a];
    for (let j = 0; j <= seg; j++) {
      const th = (j / seg) * Math.PI * 2;
      const cx = Math.cos(th), sz = Math.sin(th);
      const px = Math.sign(cx) * Math.abs(cx) ** (2 / exp), pz = Math.sign(sz) * Math.abs(sz) ** (2 / exp);
      let dx = A.dx + off, dz = A.dz + off;
      if (cx > 0) dx += (A.fr || 0) * cx ** 4; else dx += (A.tr || 0) * cx ** 4;
      if (A.mod) { const m = A.mod(th); dx *= m; dz *= m; }
      pos.push(A.c.x + px * dx, A.c.y, A.c.z + pz * dz);
      uv.push(j / seg, a / (nA - 1));
      const w = A.w.slice(0, 4);
      while (w.length < 4) w.push([0, 0]);
      const tot = w.reduce((t, x) => t + x[1], 0) || 1;
      si.push(...w.map((x) => x[0])); sw.push(...w.map((x) => x[1] / tot));
    }
  }
  // anéis que descem (membros) invertem o sentido dos triângulos para as normais apontarem para fora
  const desce = nA > 1 && aneis[1].c.y < aneis[0].c.y;
  for (let a = 0; a < nA - 1; a++) for (let j = 0; j < seg; j++) {
    const i0 = a * (seg + 1) + j, i1 = i0 + seg + 1;
    if (desce) idx.push(i0, i0 + 1, i1, i1, i0 + 1, i1 + 1); else idx.push(i0, i1, i0 + 1, i1, i1 + 1, i0 + 1);
  }
  if (tampaFim) {
    const A = aneis[nA - 1], ci = pos.length / 3;
    pos.push(A.c.x, A.c.y + (aneis[nA - 1].c.y < aneis[0].c.y ? -0.01 : 0.01), A.c.z); uv.push(0.5, 1);
    const w = A.w.slice(0, 4); while (w.length < 4) w.push([0, 0]); const tot = w.reduce((t, x) => t + x[1], 0) || 1;
    si.push(...w.map((x) => x[0])); sw.push(...w.map((x) => x[1] / tot));
    const b = (nA - 1) * (seg + 1);
    for (let j = 0; j < seg; j++) { if (desce) idx.push(b + j, ci, b + j + 1); else idx.push(b + j, b + j + 1, ci); }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setAttribute('skinIndex', new THREE.Uint16BufferAttribute(si, 4));
  g.setAttribute('skinWeight', new THREE.Float32BufferAttribute(sw, 4));
  g.setIndex(idx);
  g.computeVertexNormals();
  return g;
}

// ---------- cabeça esculpida ----------
function gauss(x, y, z, cx, cy, cz, sx, sy, sz) { return Math.exp(-(((x - cx) / sx) ** 2 + ((y - cy) / sy) ** 2 + ((z - cz) / sz) ** 2)); }
function cabecaGeo(g, s, pele, cabeloCor) {
  const R = g.rosto;
  const geo = new THREE.SphereGeometry(1, 72, 56);
  const p = geo.attributes.position;
  const cores = [];
  const cPele = pele.clone(), cLabio = pele.clone().lerp(new THREE.Color('#a5524a'), 0.38), cSob = new THREE.Color(cabeloCor).lerp(pele, 0.25), cBarba = new THREE.Color(cabeloCor).lerp(pele, 0.45), cRubor = pele.clone().lerp(new THREE.Color('#d06a5a'), 0.12);
  const v = new THREE.Vector3();
  for (let i = 0; i < p.count; i++) {
    v.fromBufferAttribute(p, i);
    // forma base: profundidade (X) 0.098, altura (Y) 0.118, largura (Z) 0.077·largura
    let x = v.x * 0.098, y = v.y * 0.118, z = v.z * (0.074 + 0.008 * R.largura);
    // crânio maior atrás/acima, mandíbula mais estreita embaixo
    if (y < 0) { const k = c01(-y / 0.118); z *= 1 - (0.32 - 0.12 * R.mandibula) * k * k; x *= 1 - 0.12 * k * (x < 0 ? 1.6 : 0.4); }
    if (x < 0 && y > -0.04) { x *= 1.08; }
    const frente = x > 0 ? 1 : 0;
    // queixo e mandíbula
    x += frente * 0.012 * (0.6 + 0.8 * R.queixo) * gauss(0, y, z, 0, -0.1, 0, 1, 0.022, 0.03);
    z += Math.sign(z) * 0.008 * R.mandibula * gauss(x, y, 0, 0.02, -0.075, 0, 0.05, 0.03, 1);
    // testa e arco das sobrancelhas
    x += frente * 0.009 * gauss(0, y, z, 0, 0.035, 0.032, 1, 0.012, 0.03) + frente * 0.004 * gauss(0, y, z, 0, 0.035, -0.032, 1, 0.012, 0.03);
    x += frente * 0.006 * gauss(0, y, z, 0, 0.034, -0.032, 1, 0.012, 0.03);
    x += frente * 0.004 * R.testa * gauss(0, y, z, 0, 0.07, 0, 1, 0.03, 0.05);
    // órbitas
    const orb = gauss(0, y, Math.abs(z), 0, 0.016, 0.033, 1, 0.014, 0.018);
    x -= frente * 0.016 * orb;
    // maçãs do rosto
    x += frente * 0.006 * (0.5 + R.macas) * gauss(0, y, Math.abs(z), 0, -0.006, 0.048, 1, 0.016, 0.02);
    z += Math.sign(z) * 0.004 * R.macas * gauss(x, y, 0, 0.05, -0.006, 0, 0.04, 0.02, 1);
    // nariz: ponte fina, ponta mais larga, asas
    const nz = Math.abs(z);
    const ponte = gauss(0, y, nz, 0, 0.008, 0, 1, 0.022, 0.009 + 0.004 * R.nariz);
    const ponta = gauss(0, y, nz, 0, -0.022, 0, 1, 0.011, 0.012 + 0.004 * R.nariz);
    const asa = gauss(0, y, nz, 0, -0.026, 0.014, 1, 0.008, 0.008);
    x += frente * (0.016 * ponte + (0.026 + 0.006 * R.nariz) * ponta + 0.008 * asa);
    // lábios e sulco da boca
    const lab = gauss(0, y, nz, 0, -0.046, 0, 1, 0.009 + 0.002 * R.labios, 0.022);
    const sulco = gauss(0, y, nz, 0, -0.049, 0, 1, 0.0018, 0.02);
    x += frente * (0.009 * (0.6 + 0.6 * R.labios) * lab - 0.004 * sulco);
    x += frente * 0.004 * gauss(0, y, nz, 0, -0.037, 0, 1, 0.006, 0.006);   // filtro
    p.setXYZ(i, x * s, y * s, z * s);
    // cor por vértice: sobrancelhas, lábios, barba, rubor, couro cabeludo (cabelo raspado)
    let c = cPele.clone();
    const sob = frente * gauss(0, y, nz, 0, 0.037 + 0.002 * (nz - 0.03) * 10, 0.035, 1, 0.0055, 0.02);
    c.lerp(cRubor, frente * gauss(0, y, nz, 0, -0.012, 0.045, 1, 0.02, 0.02));
    c.lerp(cLabio, frente * c01(lab * 1.6) * 0.9);
    if (g.barba > 0) { const regB = c01((-y - 0.02) / 0.02) * (x > -0.03 ? 1 : 0) * (1 - c01(lab * 3)); c.lerp(cBarba, regB * g.barba * 0.85); }
    c.lerp(cSob, c01(sob * 1.4));
    if (g.cabelo.estilo === 0) { const couro = c01((y - 0.035 - (x > 0 ? 0.03 : -0.02)) / 0.02); c.lerp(cBarba, couro * 0.7); }
    cores.push(c.r, c.g, c.b);
  }
  geo.setAttribute('color', new THREE.Float32BufferAttribute(cores, 3));
  geo.computeVertexNormals();
  return geo;
}

// ---------- personagem humano ----------
export function criarHumano(genoma) {
  const g = normalizarGenoma(genoma);
  const s = g.altura / 1.8, T = texturas();
  const pele = corPele(g);
  const mPele = new THREE.MeshPhysicalMaterial({ color: pele, roughness: 0.52, metalness: 0, sheen: 0.5, sheenRoughness: 0.55, sheenColor: new THREE.Color('#ff7a66'), clearcoat: 0.06, clearcoatRoughness: 0.5, normalMap: T.poros, normalScale: new THREE.Vector2(0.25, 0.25) });
  const mRosto = mPele.clone(); mRosto.color = new THREE.Color('#ffffff'); mRosto.vertexColors = true;
  const pano = (cor, rug = 0.88) => new THREE.MeshStandardMaterial({ color: cor, roughness: rug, metalness: 0, normalMap: T.trama, normalScale: new THREE.Vector2(0.5, 0.5) });
  const R = g.roupa;
  const mRoupa1 = pano(R.cor1), mRoupa2 = pano(R.cor2, 0.8), mCalca = pano(R.cor3);
  const mCouro = new THREE.MeshStandardMaterial({ color: '#2a221c', roughness: 0.6 });
  const mBota = new THREE.MeshStandardMaterial({ color: R.estilo === 'esportivo' ? '#e8e6e0' : '#1c1a18', roughness: 0.7 });
  const mSola = new THREE.MeshStandardMaterial({ color: R.estilo === 'esportivo' ? '#f4f4f4' : '#0c0c0c', roughness: 0.85 });
  const mDisp = new THREE.MeshStandardMaterial({ color: '#111', emissive: '#ffb347', emissiveIntensity: 0.5, roughness: 0.3 });
  const W = 0.19 * s * (0.92 + 0.16 * g.ombros), Q = 0.092 * s * (0.94 + 0.12 * g.quadril);
  const m = g.massa, mu = g.musculo;
  // ---- esqueleto (mesmos nomes do operador) ----
  const raiz = new THREE.Group();
  const B = (nome, pai, x, y, z) => { const b = new THREE.Bone(); b.name = nome; b.position.set(x, y, z); (pai || raiz).add(b); return b; };
  const quadril = B('quadril', null, 0, 0.97 * s, 0);
  const coluna = B('coluna', quadril, 0, 0.1 * s, 0);
  const peito = B('peito', coluna, 0, 0.2 * s, 0);
  const pescoco = B('pescoco', peito, 0, 0.3 * s, 0);
  const cabeca = B('cabeca', pescoco, 0, 0.09 * s, 0);
  const braco = (lado) => { const ombro = B('ombro' + lado, peito, 0, 0.2 * s, W * lado); const cot = B('cot' + lado, ombro, 0, -0.29 * s, 0); const pulso = B('pulso' + lado, cot, 0, -0.27 * s, 0); return { ombro, cot, pulso, s: lado }; };
  const perna = (lado) => { const coxa = B('coxa' + lado, quadril, 0, -0.05 * s, Q * lado); const joelho = B('joelho' + lado, coxa, 0, -0.44 * s, 0); const tornozelo = B('tornozelo' + lado, joelho, 0, -0.42 * s, 0); return { coxa, joelho, tornozelo, s: lado }; };
  const bL = braco(-1), bR = braco(1), pL = perna(-1), pR = perna(1);
  const ossos = [quadril, coluna, peito, pescoco, cabeca, bL.ombro, bL.cot, bL.pulso, bR.ombro, bR.cot, bR.pulso, pL.coxa, pL.joelho, pL.tornozelo, pR.coxa, pR.joelho, pR.tornozelo];
  const I = (b) => ossos.indexOf(b);
  raiz.updateMatrixWorld(true);
  const esq = new THREE.Skeleton(ossos);
  const wp = (b) => b.getWorldPosition(new THREE.Vector3());
  // ---- anéis do tronco (pose de ligação) ----
  const Y = (y) => y * s;
  const tronco = (off = 0, y0 = 0.84, y1 = 1.66) => [
    { y: 0.84, dx: 0.072, dz: 0.105, w: [[I(quadril), 1]] },
    { y: 0.9, dx: 0.098 + 0.02 * m, dz: 0.148 + 0.02 * g.quadril + 0.015 * m, tr: 0.03 + 0.01 * m, w: [[I(quadril), 1]] },
    { y: 0.98, dx: 0.1 + 0.02 * m, dz: 0.152 + 0.015 * g.quadril + 0.02 * m, tr: 0.02, fr: 0.008 + 0.025 * m, w: [[I(quadril), 0.8], [I(coluna), 0.2]] },
    { y: 1.06, dx: 0.088 + 0.035 * m, dz: 0.132 + 0.035 * m, fr: 0.01 + 0.03 * m, w: [[I(quadril), 0.3], [I(coluna), 0.7]] },
    { y: 1.15, dx: 0.092 + 0.025 * m, dz: 0.138 + 0.025 * m, fr: 0.012 * m, w: [[I(coluna), 0.9], [I(peito), 0.1]] },
    { y: 1.25, dx: 0.1 + 0.012 * mu, dz: 0.152 + 0.016 * mu + 0.01 * m, w: [[I(coluna), 0.4], [I(peito), 0.6]] },
    { y: 1.34, dx: 0.104 + 0.018 * mu, dz: 0.165 + 0.02 * mu, fr: 0.012 + 0.018 * mu, w: [[I(peito), 1]] },
    { y: 1.42, dx: 0.096 + 0.01 * mu, dz: (0.172 + 0.025 * g.ombros) * (W / (0.19 * s)), fr: 0.006, w: [[I(peito), 1]] },
    { y: 1.48, dx: 0.078, dz: (0.155 + 0.025 * g.ombros) * (W / (0.19 * s)), w: [[I(peito), 1]] },
    { y: 1.535, dx: 0.06, dz: 0.085 + 0.01 * mu, w: [[I(peito), 0.6], [I(pescoco), 0.4]] },
    { y: 1.58, dx: 0.05, dz: 0.054 + 0.004 * mu, w: [[I(pescoco), 1]] },
    { y: 1.64, dx: 0.047, dz: 0.05, w: [[I(pescoco), 0.4], [I(cabeca), 0.6]] },
  ].filter((a) => a.y >= y0 - 1e-6 && a.y <= y1 + 1e-6).map((a) => ({ ...a, c: V(0.008 * s, Y(a.y), 0), dx: a.dx * s + off, dz: a.dz * s + off, fr: (a.fr || 0) * s, tr: (a.tr || 0) * s }));
  const brAneis = (b, off = 0, y1 = 0.6) => {
    const J = wp(b.ombro), L = b.s;
    return [
      { y: 0.035, r: 0.04, w: [[I(peito), 0.6], [I(b.ombro), 0.4]] },
      { y: -0.015, r: 0.05 + 0.008 * mu, w: [[I(peito), 0.2], [I(b.ombro), 0.8]] },
      { y: -0.08, r: 0.046 + 0.01 * mu + 0.006 * m, fr: 0.006 * mu, w: [[I(b.ombro), 1]] },
      { y: -0.17, r: 0.041 + 0.006 * m, fr: 0.01 * mu, w: [[I(b.ombro), 1]] },
      { y: -0.255, r: 0.035, w: [[I(b.ombro), 0.75], [I(b.cot), 0.25]] },
      { y: -0.29, r: 0.033, w: [[I(b.ombro), 0.35], [I(b.cot), 0.65]] },
      { y: -0.35, r: 0.039 + 0.007 * mu, w: [[I(b.cot), 1]] },
      { y: -0.45, r: 0.031 + 0.003 * mu, w: [[I(b.cot), 1]] },
      { y: -0.545, r: 0.025, dz: 0.021, w: [[I(b.cot), 0.6], [I(b.pulso), 0.4]] },
      { y: -0.565, r: 0.024, dz: 0.02, w: [[I(b.pulso), 1]] },
    ].filter((a) => -a.y <= y1).map((a) => ({ c: V(J.x, J.y + a.y * s, J.z + 0.004 * L), dx: a.r * s + off, dz: (a.dz || a.r) * s + off, fr: (a.fr || 0) * s, w: a.w }));
  };
  const pnAneis = (p, off = 0, y1 = 0.87) => {
    const J = wp(p.coxa);
    return [
      { y: 0.07, dx: 0.088, dz: 0.082, w: [[I(quadril), 0.7], [I(p.coxa), 0.3]] },
      { y: 0.0, dx: 0.09 + 0.012 * m, dz: 0.087 + 0.012 * m, w: [[I(quadril), 0.25], [I(p.coxa), 0.75]] },
      { y: -0.12, dx: 0.083 + 0.012 * m, dz: 0.08 + 0.012 * m, fr: 0.01 * mu, w: [[I(p.coxa), 1]] },
      { y: -0.26, dx: 0.072 + 0.008 * m, dz: 0.068 + 0.008 * m, fr: 0.008 * mu, w: [[I(p.coxa), 1]] },
      { y: -0.39, dx: 0.054, dz: 0.054, w: [[I(p.coxa), 0.85], [I(p.joelho), 0.15]] },
      { y: -0.44, dx: 0.05, dz: 0.05, fr: 0.006, w: [[I(p.coxa), 0.45], [I(p.joelho), 0.55]] },
      { y: -0.5, dx: 0.05, dz: 0.049, w: [[I(p.joelho), 1]] },
      { y: -0.58, dx: 0.054, dz: 0.052, tr: 0.016 + 0.01 * mu, w: [[I(p.joelho), 1]] },
      { y: -0.7, dx: 0.044, dz: 0.042, tr: 0.006, w: [[I(p.joelho), 1]] },
      { y: -0.82, dx: 0.032, dz: 0.03, w: [[I(p.joelho), 0.8], [I(p.tornozelo), 0.2]] },
      { y: -0.86, dx: 0.03, dz: 0.03, w: [[I(p.joelho), 0.4], [I(p.tornozelo), 0.6]] },
    ].filter((a) => -a.y <= y1).map((a) => ({ c: V(J.x + 0.004 * s, J.y + a.y * s, J.z), dx: a.dx * s + off, dz: a.dz * s + off, fr: (a.fr || 0) * s, tr: (a.tr || 0) * s, w: a.w }));
  };
  const malha = (geo, mat) => { const sk = new THREE.SkinnedMesh(geo, mat); sk.bind(esq); sk.frustumCulled = false; sk.castShadow = true; sk.receiveShadow = true; raiz.add(sk); return sk; };
  // pele
  malha(tuboAneis(tronco(), { seg: 36, exp: 2.4 }), mPele);
  for (const b of [bL, bR]) malha(tuboAneis(brAneis(b), { seg: 24, exp: 2.05 }), mPele);
  for (const p of [pL, pR]) malha(tuboAneis(pnAneis(p), { seg: 26, exp: 2.05 }), mPele);
  // roupas por estilo (mesmos anéis, deslocados: deformam com o corpo)
  const est = R.estilo;
  malha(tuboAneis(tronco(0.009 * s, 0.84, 1.03), { seg: 36, exp: 2.4 }), mCalca);
  for (const p of [pL, pR]) malha(tuboAneis(pnAneis(p, (est === 'tatico' ? 0.014 : 0.01) * s, est === 'esportivo' ? 0.42 : 0.84), { seg: 26 }), mCalca);
  malha(tuboAneis(tronco((est === 'casual' ? 0.016 : 0.009) * s, est === 'casual' ? 0.88 : 0.97, 1.555), { seg: 36, exp: 2.4 }), mRoupa1);
  for (const b of [bL, bR]) malha(tuboAneis(brAneis(b, (est === 'casual' ? 0.012 : 0.007) * s, est === 'esportivo' ? 0.16 : est === 'tatico' ? 0.33 : 0.53), { seg: 24 }), mRoupa1);
  if (est === 'tatico') {
    malha(tuboAneis(tronco(0.022 * s, 1.1, 1.48), { seg: 36, exp: 2.3 }), mRoupa2);   // colete
    for (const lado of [-1, 1]) { const alca = new THREE.Mesh(new THREE.BoxGeometry(0.2 * s, 0.012 * s, 0.045 * s), mRoupa2); alca.position.set(0.0, 0.215 * s, 0.11 * s * lado); alca.rotation.z = 0.15; peito.add(alca); }
    for (const [y, z] of [[1.2, 0.06], [1.2, -0.06], [1.31, 0.075], [1.31, -0.075]]) {
      const pai = y > 1.25 ? peito : coluna;
      const b = new THREE.Mesh(new THREE.BoxGeometry(0.035 * s, 0.07 * s, 0.05 * s), mRoupa2);
      b.position.set(0.14 * s, Y(y) - wp(pai).y, z * s); pai.add(b);
    }
    for (const p of [pL, pR]) { const jo = new THREE.Mesh(new THREE.SphereGeometry(0.055 * s, 16, 10, 0, Math.PI * 2, 0, Math.PI / 2), mCouro); jo.rotation.z = -Math.PI / 2; jo.position.set(0.045 * s, 0, 0); jo.scale.set(1, 0.8, 1); p.joelho.add(jo); }
  }
  if (est === 'casual') { const capuz = new THREE.Mesh(new THREE.TorusGeometry(0.075 * s, 0.03 * s, 10, 24, Math.PI * 1.3), mRoupa1); capuz.rotation.set(Math.PI / 2, 0, Math.PI * 0.85); capuz.position.set(-0.03 * s, 0.29 * s, 0); peito.add(capuz); }
  const cinto = new THREE.Mesh(new THREE.TorusGeometry(0.15 * s, 0.013 * s, 8, 40), est === 'esportivo' ? mCalca : mCouro); cinto.rotation.x = Math.PI / 2; cinto.scale.set(0.72 + 0.12 * m, 1.08 + 0.12 * m, 1); cinto.position.y = 0.04 * s; quadril.add(cinto);
  // mãos (rígidas no osso do punho) com dedos articulados
  const mMao = est === 'tatico' ? new THREE.MeshStandardMaterial({ color: '#1a1a1a', roughness: 0.65 }) : mPele;
  const mao = (b) => {
    const palma = new THREE.Mesh(new THREE.BoxGeometry(0.03 * s, 0.085 * s, 0.075 * s), mMao); palma.position.y = -0.045 * s; b.pulso.add(palma);
    const dedos = [];
    for (let i = 0; i < 4; i++) {
      const base = new THREE.Group(); base.position.set(0.004 * s, -0.088 * s, (0.027 - i * 0.018) * s * -b.s); b.pulso.add(base);
      const L1 = (0.028 - (i === 3 ? 0.006 : 0)) * s;
      const f1 = new THREE.Mesh(new THREE.CapsuleGeometry(0.0078 * s, L1, 3, 8), mMao); f1.position.y = -L1 * 0.6; base.add(f1);
      const meio = new THREE.Group(); meio.position.y = -L1 * 1.25; base.add(meio);
      const f2 = new THREE.Mesh(new THREE.CapsuleGeometry(0.0072 * s, L1 * 0.85, 3, 8), mMao); f2.position.y = -L1 * 0.55; meio.add(f2);
      dedos.push({ base, meio });
    }
    const pol = new THREE.Group(); pol.position.set(0.012 * s, -0.04 * s, 0.035 * s * -b.s); b.pulso.add(pol);
    const fp = new THREE.Mesh(new THREE.CapsuleGeometry(0.009 * s, 0.03 * s, 3, 8), mMao); fp.position.y = -0.02 * s; fp.rotation.x = 0.5 * b.s; pol.add(fp);
    // dispositivo de pulso: sinal luminoso das ações (preparo, contato) — lido pelos dois jogadores
    const disp = new THREE.Mesh(new THREE.TorusGeometry(0.028 * s, 0.006 * s, 8, 24), mDisp.clone()); disp.rotation.x = Math.PI / 2; disp.position.y = 0.02 * s; b.pulso.add(disp);
    const orbe = new THREE.Group(); orbe.position.set(0.01 * s, -0.13 * s, 0); b.pulso.add(orbe);
    orbe.add(new THREE.Mesh(new THREE.SphereGeometry(0.035, 16, 12), new THREE.MeshBasicMaterial({ color: 0xffffff })));
    const halo = new THREE.Mesh(new THREE.SphereGeometry(0.08, 16, 12), new THREE.MeshBasicMaterial({ color: 0xffa040, transparent: true, opacity: 0.35, blending: THREE.AdditiveBlending, depthWrite: false })); orbe.add(halo);
    const faisc = [];
    for (let i = 0; i < 12; i++) { const f = new THREE.Mesh(new THREE.SphereGeometry(0.007, 6, 4), new THREE.MeshBasicMaterial({ color: 0xffe0b0, transparent: true, blending: THREE.AdditiveBlending, depthWrite: false })); orbe.add(f); faisc.push(f); }
    orbe.visible = false;
    Object.assign(b, { dedos, pol, emissor: disp, orbe, halo, faisc });
  };
  mao(bL); mao(bR);
  // calçados
  for (const p of [pL, pR]) {
    const sap = new THREE.Group(); p.tornozelo.add(sap);
    const corpo = new THREE.Mesh(new THREE.CapsuleGeometry(0.042 * s, 0.17 * s, 4, 12), mBota); corpo.rotation.z = Math.PI / 2; corpo.scale.set(0.75, 1, 1.15); corpo.position.set(0.05 * s, -0.035 * s, 0); sap.add(corpo);
    const sola = new THREE.Mesh(new THREE.BoxGeometry(0.27 * s, 0.022 * s, 0.1 * s), mSola); sola.position.set(0.05 * s, -0.064 * s, 0); sap.add(sola);
    if (est !== 'esportivo') { const cano = new THREE.Mesh(new THREE.CylinderGeometry(0.045 * s, 0.05 * s, 0.11 * s, 16), mBota); cano.position.set(0, 0.02 * s, 0); sap.add(cano); }
  }
  // cabeça, olhos, orelhas, cabelo
  const hGeo = cabecaGeo(g, s, pele, g.cabelo.cor);
  const cab = new THREE.Mesh(hGeo, mRosto); cab.position.set(0.012 * s, 0.075 * s, 0); cab.castShadow = true; cabeca.add(cab);
  const olhos = [];
  const mOlho = new THREE.MeshPhysicalMaterial({ map: T.iris(g.corOlhos), roughness: 0.1, clearcoat: 1, clearcoatRoughness: 0.02 });
  for (const lado of [-1, 1]) {
    const o = new THREE.Mesh(new THREE.SphereGeometry(0.0118 * s, 20, 14), mOlho);
    o.rotation.y = -Math.PI / 2;
    o.position.set(0.012 * s + 0.081 * s, 0.075 * s + 0.0165 * s, 0.032 * s * lado);
    cabeca.add(o); olhos.push(o);
    const pal = new THREE.Mesh(new THREE.SphereGeometry(0.0128 * s, 20, 10, 0, Math.PI * 2, 0, Math.PI * 0.42), mPele); pal.position.copy(o.position); pal.rotation.z = -0.35; cabeca.add(pal);
    const or = new THREE.Mesh(new THREE.TorusGeometry(0.018 * s, 0.006 * s, 8, 18, Math.PI * 1.4), mPele); or.position.set(0.012 * s - 0.005 * s, 0.075 * s + 0.0, 0.078 * s * lado); or.rotation.set(0, lado > 0 ? 0 : Math.PI, Math.PI * 0.8); or.scale.set(1, 1.35, 0.6); cabeca.add(or);
  }
  const mCabelo = new THREE.MeshStandardMaterial({ color: g.cabelo.cor, roughness: 0.55, metalness: 0.05, normalMap: T.fios, normalScale: new THREE.Vector2(0.8, 0.8), side: THREE.DoubleSide });
  const est2 = g.cabelo.estilo, vol = 0.004 + 0.008 * g.cabelo.volume;
  if (est2 >= 1) {
    const capa = new THREE.Mesh(new THREE.SphereGeometry(1, 48, 24, 0, Math.PI * 2, 0, Math.PI * (est2 === 1 ? 0.5 : 0.56)), mCabelo);
    capa.scale.set((0.098 + vol) * s * 1.02, (0.118 + vol) * s, (0.08 + vol) * s);
    capa.position.set(0.0, 0.075 * s + 0.006 * s, 0); capa.rotation.z = -0.42; cabeca.add(capa);
    if (est2 >= 3) {
      for (let i = 0; i < 22; i++) {
        const a = Math.PI * 0.35 + (i / 21) * Math.PI * 1.3, comp = (est2 === 3 ? 0.14 : 0.24) * s;
        const mecha = new THREE.Mesh(new THREE.PlaneGeometry(0.035 * s, comp, 1, 4), mCabelo);
        const pp = mecha.geometry.attributes.position; for (let k = 0; k < pp.count; k++) pp.setZ(k, -0.01 * ((pp.getY(k) / comp) - 0.5) ** 2 * 4);
        mecha.position.set(0.012 * s - Math.cos(a) * 0.085 * s, 0.075 * s - comp / 2 + 0.03 * s, Math.sin(a) * 0.075 * s);
        mecha.rotation.y = Math.PI / 2 - a; mecha.rotation.x = 0.1;
        cabeca.add(mecha);
      }
    }
    if (est2 === 4) { const coque = new THREE.Mesh(new THREE.SphereGeometry(0.04 * s, 18, 14), mCabelo); coque.position.set(-0.09 * s, 0.13 * s, 0); cabeca.add(coque); }
    if (est2 === 2) { const franja = new THREE.Mesh(new THREE.SphereGeometry(1, 24, 8, Math.PI - 0.7, 1.4, Math.PI * 0.25, Math.PI * 0.18), mCabelo); franja.scale.set(0.11 * s, 0.12 * s, 0.085 * s); franja.position.set(0.006 * s, 0.075 * s, 0); cabeca.add(franja); }
  }
  raiz.traverse((o) => { if (o.isMesh) { o.castShadow = true; o.receiveShadow = true; } });
  const dims = { coxa: 0.44 * s, canela: 0.42 * s, braco: 0.29 * s, ante: 0.29 * s, quadril: 0.97 * s, larg: Q + 0.015 * s, tornozelo: 0.065 * s, ombroY: 0.2 * s, escala: s };
  raiz.userData = {
    tipo: 'operador', humano: true, genoma: g, quadril, coluna, peito, pescoco, cabeca, bL, bR, pL, pR, olhos,
    emissores: [bL.emissor, bR.emissor], pl: mRoupa1, pl2: mRoupa2, emFio: null, faixas: [], extras: [], dims, anim: null,
  };
  return raiz;
}
