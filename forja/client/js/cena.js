// Cena 3D do protótipo: galeria de drenagem em escala real, operadores articulados, efeitos derivados dos
// eventos do servidor (funções puras do tempo), luzes de impacto, pós-processamento e áudio espacial.
// Coordenadas do jogo (cm, X frente, Y direita, Z cima — iguais às da Unreal) -> Three.js (m, Y cima):
//   three.x = x/100, three.z = y/100, three.y = z/100. Yaw do jogo -> rotation.y = -yaw.
// Os sinais públicos (anúncio de área, contato, frente protegida, recuperação, cancelamento) têm forma,
// escala e contraste fixos; a personalização (cor, material, ritmo) só tinge o preenchimento.
import * as THREE from 'three';
import { EffectComposer } from '../vendor/addons/postprocessing/EffectComposer.js';
import { RenderPass } from '../vendor/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from '../vendor/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from '../vendor/addons/postprocessing/OutputPass.js';
import { Ambiente, EfeitoArea, EfeitoGuarda, EfeitoRastro } from './efeitos.js';
import { AuraMateria } from './materia.js';
import { bezier } from './modelo.js';
import { animar, criarAlvo, criarOperador, posarDeslocamento } from './personagem.js';
import { criarHumano, genomaDe } from './humano.js';

const M = (cm) => cm / 100;
export const g2t = (x, y, z = 0) => new THREE.Vector3(M(x), M(z), M(y));

// ---------- texturas procedurais (sem assets externos) ----------
function hash(x, y, s) { let h = (x * 374761393 + y * 668265263 + s * 2147483647) | 0; h = (h ^ (h >>> 13)) * 1274126177; return ((h ^ (h >>> 16)) >>> 0) / 4294967295; }
function vnoise(x, y, s) {
  const xi = Math.floor(x), yi = Math.floor(y), xf = x - xi, yf = y - yi;
  const u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
  const a = hash(xi, yi, s), b = hash(xi + 1, yi, s), c = hash(xi, yi + 1, s), d = hash(xi + 1, yi + 1, s);
  return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
function fbm(x, y, s, oct = 5) { let f = 0, a = 0.5, fr = 1; for (let i = 0; i < oct; i++) { f += a * vnoise(x * fr, y * fr, s + i * 17); fr *= 2.03; a *= 0.5; } return f; }

function canvasTex(n, fn, srgb = true) {
  const c = document.createElement('canvas');
  c.width = c.height = n;
  const ctx = c.getContext('2d');
  const img = ctx.createImageData(n, n);
  for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
    const [r, g, b] = fn(x / n, y / n);
    const i = (y * n + x) * 4;
    img.data[i] = r; img.data[i + 1] = g; img.data[i + 2] = b; img.data[i + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
  const t = new THREE.CanvasTexture(c);
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.anisotropy = 4;
  if (srgb) t.colorSpace = THREE.SRGBColorSpace;
  return t;
}
function normalFromHeight(n, hfn, strength = 2) {
  const h = new Float32Array(n * n);
  for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) h[y * n + x] = hfn(x / n, y / n);
  return canvasTex(n, (u, v) => {
    const x = Math.floor(u * n), y = Math.floor(v * n);
    const l = h[y * n + ((x - 1 + n) % n)], r = h[y * n + ((x + 1) % n)], t = h[((y - 1 + n) % n) * n + x], b = h[((y + 1) % n) * n + x];
    let nx = (l - r) * strength, ny = (t - b) * strength, nz = 1;
    const k = 1 / Math.hypot(nx, ny, nz);
    return [(nx * k * 0.5 + 0.5) * 255, (ny * k * 0.5 + 0.5) * 255, (nz * k * 0.5 + 0.5) * 255];
  }, false);
}

function materiais(n) {
  const concH = (u, v) => fbm(u * 8, v * 8, 3) * 0.7 + fbm(u * 40, v * 40, 9, 3) * 0.3;
  const concreto = new THREE.MeshStandardMaterial({
    map: canvasTex(n, (u, v) => {
      const base = 96 + 50 * (fbm(u * 6, v * 6, 1) - 0.5) + 25 * (fbm(u * 50, v * 50, 2, 2) - 0.5);
      const mancha = fbm(u * 3, v * 3, 7) > 0.62 ? -22 : 0;
      const k = base + mancha;
      return [k * 0.96, k * 0.97, k * 0.95];
    }),
    roughnessMap: canvasTex(n, (u, v) => { const w = fbm(u * 2.5, v * 2.5, 11); const r = w > 0.55 ? 70 : 215 + 30 * (fbm(u * 30, v * 30, 5, 2) - 0.5); return [r, r, r]; }, false),
    normalMap: normalFromHeight(n, concH, 3),
    roughness: 1, metalness: 0,
  });
  const parede = concreto.clone();
  parede.map = canvasTex(n, (u, v) => {
    const k = 112 + 55 * (fbm(u * 5, v * 9, 21) - 0.5) - 30 * Math.max(0, fbm(u * 40, v * 1.2, 4, 3) - 0.55) * 3; // escorrimentos verticais
    const linha = Math.abs((v * 4) % 1 - 0.5) < 0.006 ? -25 : 0; // juntas de concretagem
    const furo = ((Math.abs((u * 6) % 1 - 0.5) < 0.012) && (Math.abs((v * 4) % 1 - 0.25) < 0.012)) ? -60 : 0; // furos de tirante
    const c = k + linha + furo;
    return [c * 0.95, c * 0.97, c * 0.96];
  });
  parede.roughnessMap = canvasTex(n, (u, v) => { const w = v > 0.8 ? 80 : 200; const r = w + 40 * (fbm(u * 8, v * 8, 13) - 0.5); return [r, r, r]; }, false);
  const metal = new THREE.MeshStandardMaterial({
    map: canvasTex(n / 2, (u, v) => { const ferr = fbm(u * 10, v * 10, 31) > 0.6; const k = 140 + 30 * (fbm(u * 200, v * 3, 3, 2) - 0.5); return ferr ? [120, 70, 40] : [k, k * 1.01, k * 1.03]; }),
    roughnessMap: canvasTex(n / 2, (u, v) => { const r = fbm(u * 10, v * 10, 31) > 0.6 ? 230 : 90 + 40 * fbm(u * 200, v * 3, 3, 2); return [r, r, r]; }, false),
    metalness: 0.9, roughness: 1,
  });
  const aguaN = normalFromHeight(n / 2, (u, v) => fbm(u * 10, v * 10, 41, 4), 4);
  const agua = new THREE.MeshPhysicalMaterial({ color: 0x223a3a, roughness: 0.05, metalness: 0.0, transmission: 0.0, transparent: true, opacity: 0.82, normalMap: aguaN, normalScale: new THREE.Vector2(0.35, 0.35), clearcoat: 1, clearcoatRoughness: 0.08 });
  const lodo = new THREE.MeshStandardMaterial({ color: 0x2e2b22, roughness: 0.55, metalness: 0 });
  return { concreto, parede, metal, agua, lodo };
}

// ---------- áudio espacial derivado dos eventos ----------
class Audio {
  constructor() { this.ctx = null; this.ativo = false; }
  ligar() { if (this.ctx) return; try { this.ctx = new AudioContext(); this.ativo = true; } catch { this.ativo = false; } }
  _pan(pos) { const p = this.ctx.createPanner(); p.panningModel = 'HRTF'; p.distanceModel = 'inverse'; p.refDistance = 2; p.positionX.value = pos.x; p.positionY.value = pos.y; p.positionZ.value = pos.z; p.connect(this.ctx.destination); return p; }
  ouvinte(cam) {
    if (!this.ativo) return;
    const L = this.ctx.listener, f = new THREE.Vector3(); cam.getWorldDirection(f);
    if (L.positionX) { L.positionX.value = cam.position.x; L.positionY.value = cam.position.y; L.positionZ.value = cam.position.z; L.forwardX.value = f.x; L.forwardY.value = f.y; L.forwardZ.value = f.z; L.upX.value = 0; L.upY.value = 1; L.upZ.value = 0; }
  }
  tom(pos, f0, f1, dur, vol = 0.12, tipo = 'sine') {
    if (!this.ativo) return;
    const t = this.ctx.currentTime, o = this.ctx.createOscillator(), g = this.ctx.createGain();
    o.type = tipo; o.frequency.setValueAtTime(f0, t); o.frequency.exponentialRampToValueAtTime(Math.max(20, f1), t + dur);
    g.gain.setValueAtTime(0.0001, t); g.gain.exponentialRampToValueAtTime(vol, t + 0.02); g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
    o.connect(g); g.connect(this._pan(pos)); o.start(t); o.stop(t + dur + 0.05);
  }
  ruido(pos, dur, freq, vol = 0.25) {
    if (!this.ativo) return;
    const t = this.ctx.currentTime, n = Math.floor(this.ctx.sampleRate * dur), b = this.ctx.createBuffer(1, n, this.ctx.sampleRate), d = b.getChannelData(0);
    for (let i = 0; i < n; i++) d[i] = (Math.random() * 2 - 1) * (1 - i / n) ** 2;
    const s = this.ctx.createBufferSource(); s.buffer = b;
    const f = this.ctx.createBiquadFilter(); f.type = 'bandpass'; f.frequency.value = freq; f.Q.value = 0.8;
    const g = this.ctx.createGain(); g.gain.value = vol;
    s.connect(f); f.connect(g); g.connect(this._pan(pos)); s.start(t);
  }
}

export class Cena {
  constructor(canvas, { qualidade = 'alto', editor = false } = {}) {
    this.canvas = canvas;
    this.editor = editor;
    this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance', preserveDrawingBuffer: true });
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.1;
    this.scene = new THREE.Scene();
    this.scene.fog = new THREE.FogExp2(0x0b0d0f, 0.03);
    this.scene.background = new THREE.Color(0x07090b);
    this.camera = new THREE.PerspectiveCamera(60, 16 / 9, 0.05, 120);
    this.camera.position.set(-9, 3, -2);
    this.modoCamera = editor ? 'editor' : 'jogador';
    this.orbita = { yaw: -0.6, pitch: 0.75, dist: 9, alvo: new THREE.Vector3(-4, 0, -2.5) };
    this.audio = new Audio();
    this.objetos = { ent: new Map(), areas: new Map(), guardas: new Map(), rastros: new Map(), marcas: new Map(), gizmos: new THREE.Group() };
    this.scene.add(this.objetos.gizmos);
    this.depuracao = { trajetoria: true, volumes: true, duracao: true };
    this.aparencias = new Map();
    this.estiloPersonagem = 'humano';
    this.contatos = [];
    this.luzes = [];
    for (let i = 0; i < 3; i++) { const l = new THREE.PointLight(0xff8a4a, 0, 9, 2); this.scene.add(l); this.luzes.push(l); }
    // luz de preenchimento que acompanha a câmera (leitura do personagem em qualquer canto da galeria)
    this.preench = new THREE.DirectionalLight(0xc8d8ff, 1.1);
    this.recorte = new THREE.DirectionalLight(0xffe2c0, 1.6);
    this.scene.add(this.preench, this.preench.target, this.recorte, this.recorte.target);
    this.qualidade = null;
    this.setQualidade(qualidade);
    this.relogio = new THREE.Clock();
    this._dt = 0.016;
    this.stats = { quadros: 0, t0: performance.now(), fps: 0 };
  }

  setQualidade(q) {
    if (q === this.qualidade) return;
    this.qualidade = q;
    const alto = q === 'alto';
    this.renderer.shadowMap.enabled = alto;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.renderer.setPixelRatio(alto ? Math.min(2, window.devicePixelRatio || 1) : 0.75);
    if (this.galeria) { this.scene.remove(this.galeria); this.galeria.traverse((o) => { if (o.geometry) o.geometry.dispose(); }); }
    if (this.ambiente) this.scene.remove(this.ambiente.g);
    this.mats = materiais(alto ? 512 : 128);
    if (!alto) for (const m of Object.values(this.mats)) { m.normalMap = null; m.roughnessMap = null; m.needsUpdate = true; }
    this.construirGaleria();
    this.ambiente = new Ambiente(q);
    this.scene.add(this.ambiente.g);
    for (const [, o] of this.objetos.ent) o.traverse((x) => { if (x.isMesh) x.castShadow = alto; });
    this.limparDinamicos();
    this._montarPos();
  }

  _montarPos() {
    this.composer = null;
    if (this.qualidade !== 'alto') return;
    const w = this.canvas.clientWidth || 1280, h = this.canvas.clientHeight || 720;
    this.composer = new EffectComposer(this.renderer);
    this.composer.addPass(new RenderPass(this.scene, this.camera));
    this.bloom = new UnrealBloomPass(new THREE.Vector2(w, h), 0.38, 0.45, 0.9);
    this.composer.addPass(this.bloom);
    this.composer.addPass(new OutputPass());
    this.composer.setSize(w, h);
  }

  construirGaleria() {
    const G = new THREE.Group();
    this.galeria = G;
    const { concreto, parede, metal, agua, lodo } = this.mats;
    const rep = (m, rx, ry) => { const c = m.clone(); for (const k of ['map', 'roughnessMap', 'normalMap']) if (c[k]) { c[k] = c[k].clone(); c[k].repeat.set(rx, ry); c[k].needsUpdate = true; } return c; };
    const L = 28, W = 9, H = 5, CW = 1.4, CD = 0.25;
    // piso em duas faixas + canal rebaixado
    const lado = (W - CW) / 2;
    for (const s of [-1, 1]) {
      const p = new THREE.Mesh(new THREE.BoxGeometry(L, 0.3, lado), rep(concreto, 10, 1.4));
      p.position.set(0, -0.15, s * (CW / 2 + lado / 2)); p.receiveShadow = true; G.add(p);
      const borda = new THREE.Mesh(new THREE.BoxGeometry(L, CD, 0.04), rep(concreto, 10, 0.2));
      borda.position.set(0, -CD / 2, s * (CW / 2 + 0.02)); G.add(borda);
    }
    const fundo = new THREE.Mesh(new THREE.BoxGeometry(L, 0.1, CW), lodo); fundo.position.set(0, -CD - 0.05, 0); G.add(fundo);
    this.agua = new THREE.Mesh(new THREE.PlaneGeometry(L, CW, 1, 1), agua);
    this.agua.rotation.x = -Math.PI / 2; this.agua.position.y = -0.06; this.agua.receiveShadow = true;
    if (agua.normalMap) agua.normalMap.repeat.set(14, 0.7);
    G.add(this.agua);
    // paredes, teto com claraboia
    for (const s of [-1, 1]) {
      const w = new THREE.Mesh(new THREE.BoxGeometry(L, H, 0.3), rep(parede, 6, 1)); w.position.set(0, H / 2, s * (W / 2 + 0.15)); w.receiveShadow = true; w.castShadow = true; G.add(w);
      const ch = new THREE.Mesh(new THREE.BoxGeometry(L, 0.5, 0.7), rep(parede, 6, 0.1)); ch.position.set(0, H - 0.25, s * (W / 2 - 0.3)); ch.rotation.x = s * 0.6; ch.userData.teto = true; G.add(ch);
      const e = new THREE.Mesh(new THREE.BoxGeometry(0.3, H, W), rep(parede, 2, 1)); e.position.set(s * (L / 2 + 0.15), H / 2, 0); G.add(e);
    }
    const tetoA = new THREE.Mesh(new THREE.BoxGeometry(L / 2 - 1.5, 0.35, W), rep(parede, 3, 1)); tetoA.position.set(-(L / 4 + 0.75), H + 0.17, 0); tetoA.castShadow = true; tetoA.userData.teto = true; G.add(tetoA);
    const tetoB = tetoA.clone(); tetoB.position.x = L / 4 + 0.75; tetoB.userData.teto = true; G.add(tetoB);
    for (const s of [-1, 1]) { const t = new THREE.Mesh(new THREE.BoxGeometry(3, 0.35, (W - 2.4) / 2), rep(parede, 1, 1)); t.position.set(0, H + 0.17, s * (1.2 + (W - 2.4) / 4)); t.castShadow = true; t.userData.teto = true; G.add(t); }
    const grade = new THREE.Mesh(new THREE.BoxGeometry(3, 0.04, 2.4), new THREE.MeshStandardMaterial({ color: 0x1a1c1e, metalness: 0.8, roughness: 0.5, transparent: true, opacity: 0.35 })); grade.position.set(0, H + 0.05, 0); grade.userData.teto = true; G.add(grade);
    // juntas metálicas de dilatação (x = ±7 m), atravessando o piso e o canal
    for (const s of [-1, 1]) {
      const j = new THREE.Mesh(new THREE.BoxGeometry(0.2, 0.02, W), metal); j.position.set(s * 7, 0.005, 0); j.receiveShadow = true; G.add(j);
      const jc = new THREE.Mesh(new THREE.BoxGeometry(0.2, 0.02, CW), metal); jc.position.set(s * 7, -0.07, 0); G.add(jc);
      for (const k of [-1, 1]) { const pa = new THREE.Mesh(new THREE.BoxGeometry(0.22, H, 0.06), metal); pa.position.set(s * 7, H / 2, k * (W / 2 - 0.02)); G.add(pa); }
    }
    // tubulação e luminárias de serviço
    const tubo = new THREE.Mesh(new THREE.CylinderGeometry(0.16, 0.16, L, 16, 1, true), new THREE.MeshStandardMaterial({ color: 0x4a4038, metalness: 0.6, roughness: 0.6 }));
    tubo.rotation.z = Math.PI / 2; tubo.position.set(0, 3.9, W / 2 - 0.45); tubo.castShadow = true; tubo.userData.teto = true; G.add(tubo);
    // luz: claraboia (sol filtrado) + luminárias frias
    // luz do dia entra só pela claraboia: spot estreito com sombra; o resto da galeria é iluminado pelas luminárias
    const sol = new THREE.SpotLight(0xfff1d6, this.qualidade === 'alto' ? 900 : 700, 30, 0.2, 0.55, 1.6);
    sol.position.set(1.2, 16, 1.6); sol.target.position.set(-0.5, 0, -0.3);
    sol.castShadow = this.qualidade === 'alto'; sol.shadow.mapSize.set(1024, 1024); sol.shadow.bias = -0.0004; sol.shadow.camera.near = 8; sol.shadow.camera.far = 20;
    G.add(sol, sol.target);
    if (this.qualidade === 'alto') {
      const feixe = new THREE.Mesh(new THREE.CylinderGeometry(1.25, 1.9, H, 32, 1, true), new THREE.ShaderMaterial({
        transparent: true, depthWrite: false, side: THREE.DoubleSide, blending: THREE.AdditiveBlending,
        uniforms: { uCor: { value: new THREE.Color(0xfff1d6) } },
        vertexShader: 'varying vec3 vN; varying vec3 vV; varying float vY; void main(){ vN = normalize(normalMatrix*normal); vec4 mv = modelViewMatrix*vec4(position,1.0); vV = normalize(-mv.xyz); vY = uv.y; gl_Position = projectionMatrix*mv; }',
        fragmentShader: 'uniform vec3 uCor; varying vec3 vN; varying vec3 vV; varying float vY; void main(){ float c = pow(abs(dot(vN, vV)), 3.0); float g = 0.35 + 0.65 * vY; gl_FragColor = vec4(uCor, 0.075 * c * g); }',
      }));
      feixe.position.set(-0.25, H / 2, -0.15); feixe.rotation.z = 0.12; feixe.userData.teto = true; G.add(feixe);
    }
    G.add(new THREE.HemisphereLight(0x8fa3b3, 0x1a1712, 0.35));
    for (const x of [-11, -4, 4, 11]) {
      const l = new THREE.PointLight(0xbfd4ff, this.qualidade === 'alto' ? 14 : 11, 13, 2); l.position.set(x, 4.2, -W / 2 + 0.6); G.add(l);
      const lum = new THREE.Mesh(new THREE.BoxGeometry(1.0, 0.06, 0.16), new THREE.MeshStandardMaterial({ color: 0xffffff, emissive: 0xcfe0ff, emissiveIntensity: 3 })); lum.position.copy(l.position); G.add(lum);
    }
    // na oficina a galeria aparece em corte (sem teto) para a câmera orbitar por cima
    if (this.editor) G.traverse((o) => { if (o.userData && o.userData.teto) o.visible = false; });
    this.scene.add(G);
    // ambiente para reflexos coerentes (PMREM de uma cena simples com a claraboia)
    if (this.qualidade === 'alto') {
      const pm = new THREE.PMREMGenerator(this.renderer);
      const env = new THREE.Scene();
      env.background = new THREE.Color(0x0c0e10);
      const sky = new THREE.Mesh(new THREE.PlaneGeometry(3, 3), new THREE.MeshBasicMaterial({ color: 0xffffff })); sky.position.set(0, 5, 0); sky.rotation.x = Math.PI / 2; env.add(sky);
      this.scene.environment = pm.fromScene(env, 0.04).texture;
      this.scene.environmentIntensity = 0.6;
    } else this.scene.environment = null;
  }


  limparDinamicos() {
    for (const k of ['areas', 'guardas', 'rastros', 'marcas']) { for (const [, o] of this.objetos[k]) this.scene.remove(o.grupo || o.g || o); this.objetos[k].clear(); }
    this.contatos = [];
  }

  resize(w, h) {
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h; this.camera.updateProjectionMatrix();
    if (this.composer) this.composer.setSize(w, h);
  }

  // ---------- entidades (operadores e alvos) ----------
  _ent(id, info) {
    let o = this.objetos.ent.get(id);
    if (!o) {
      o = info.tipo === 'alvo' ? criarAlvo() : this.estiloPersonagem === 'operador' ? criarOperador(info.time) : criarHumano(info.forma || genomaDe(info.nome || 'jogador' + id));
      o.userData.id = id;
      o.traverse((x) => { if (x.isMesh) x.castShadow = this.qualidade === 'alto'; });
      const aberto = new THREE.Mesh(new THREE.RingGeometry(0.42, 0.5, 40), new THREE.MeshBasicMaterial({ color: 0xff3b2f, transparent: true, opacity: 0, depthWrite: false, side: THREE.DoubleSide }));
      aberto.rotation.x = -Math.PI / 2; aberto.position.y = 0.025;
      o.add(aberto);
      o.userData.aberto = aberto;
      for (const x of o.userData.extras || []) this.scene.add(x);
      this.scene.add(o);
      this.objetos.ent.set(id, o);
    }
    return o;
  }

  sincronizar(mundo, tServ) {
    const dt = this._dt;
    const vistos = new Set();
    const areas = [...mundo.areas.values()];
    const guardas = [...mundo.guardas.values()];
    for (const [id, e] of mundo.ent) {
      if (e.ativo === false) continue;
      const p = mundo.posicao(id, tServ);
      if (!p) continue;
      vistos.add(id);
      const o = this._ent(id, e);
      const u = o.userData;
      const alvoPos = g2t(p[0], p[1], 0);
      const operador = u.tipo === 'operador';
      if (!operador && Math.abs(p[1]) < 70) alvoPos.y = -0.2;
      const yaw = mundo.yaw(id);
      const yr = THREE.MathUtils.degToRad(yaw);
      // velocidade no mundo (m/s), suavizada; o controlador de passos planta os pés a partir dela
      const vw = u.ultPos ? alvoPos.clone().sub(u.ultPos).divideScalar(Math.max(0.001, dt)) : new THREE.Vector3();
      if (vw.length() > 40) vw.set(0, 0, 0);
      u.ultPos = alvoPos.clone();
      u.velW = (u.velW || new THREE.Vector3()).lerp(vw, 0.3);
      o.position.copy(alvoPos);
      o.rotation.y = -yr;
      const inf = mundo.info ? mundo.info(id) : {};
      const mv = mundo.movs.get(id);
      const minhas = areas.filter((a) => a.autor === id);
      const prepA = minhas.filter((a) => tServ < a.t_contato && a.fase === 'preparo').pop();
      const contA = minhas.filter((a) => tServ >= (a.t_contato_real ?? a.t_contato)).pop();
      const g = guardas.find((x) => x.ent === id && !x.fim);
      const imp = (mundo.impactos || []).filter((x) => !x.absorvido && (x.alvo === id) && x.t <= tServ).pop();
      const c3 = (c) => (c ? g2t(c[0], c[1], 0) : null);
      let olhar = null;
      for (const [id2, e2] of mundo.ent) if (id2 !== id && e2.tipo === 'jogador' && e2.ativo !== false) { const q = mundo.posicao(id2, tServ); if (q) { const w = g2t(q[0], q[1], 0); if (!olhar || w.distanceTo(alvoPos) < olhar.distanceTo(alvoPos)) olhar = w; } }
      if (olhar && olhar.distanceTo(alvoPos) > 9) olhar = null;
      const est = {
        tServ, velW: u.velW, folego: inf.folego, olhar, solo: (x, z) => (Math.abs(z) < 0.7 ? -0.22 : 0),
        mov: mv && tServ >= mv.t0 && tServ <= mv.t1 + 40 ? { u: (tServ - mv.t0) / Math.max(1, mv.t1 - mv.t0), recuo: ((mv.para[0] - mv.de[0]) * Math.cos(yr) + (mv.para[1] - mv.de[1]) * Math.sin(yr)) < -30 } : null,
        prep: prepA ? { k: (tServ - prepA.t) / Math.max(1, prepA.t_contato - prepA.t), centro: c3(prepA.centro), materia: this._materia(prepA) } : null,
        contato: contA ? { dt: tServ - (contA.t_contato_real ?? contA.t_contato), centro: c3(contA.centro), materia: this._materia(contA) } : null,
        recup: inf.recup && tServ < inf.recup,
        guarda: g ? { gesto: g.gesto, k: (tServ - g.t) / Math.max(1, g.t_ativa - g.t), absorveDt: g.ultima_absorcao ? tServ - g.ultima_absorcao.t : undefined, fonte: g.ultima_absorcao ? c3(g.ultima_absorcao.fonte) : null } : null,
        golpe: imp ? { dt: tServ - imp.t, fonte: c3(imp.centro) } : null,
      };
      const parada = (est.contato && est.contato.dt >= 0 && est.contato.dt < 70) || (est.golpe && est.golpe.dt >= 0 && est.golpe.dt < 90);
      // a pausa de impacto vale para o autor do golpe no chão e para a vítima; o braço elástico também congela esticado
      animar(o, est, parada ? dt * 0.06 : dt);
      if (u.bR) { u.bR.pulso.getWorldPosition(u.maoW || (u.maoW = new THREE.Vector3())); }
      // sinal público de recuperação: anel vermelho pulsante nos pés (“aberto”)
      u.aberto.material.opacity = est.recup ? 0.55 + 0.35 * Math.sin(tServ / 70) : Math.max(0, u.aberto.material.opacity - dt * 3);
    }
    for (const [id, o] of this.objetos.ent) if (!vistos.has(id)) { this.scene.remove(o); for (const x of o.userData.extras || []) this.scene.remove(x); this.objetos.ent.delete(id); }
    this._areas(areas, tServ, mundo);
    this._guardas(guardas, tServ, mundo);
    this._rastros(mundo, tServ);
    this._marcas(mundo.marcas || [], tServ);
    this._luzesImpacto(tServ);
  }

  _materia(x) { return x.materia || (this.aparencias.get(undefined) || {}).materia || null; }

  _areas(lista, tServ, mundo) {
    for (const a of lista) {
      let ef = this.objetos.areas.get(a.id);
      const agua = a.superficie === 'agua' || Math.abs(a.centro[1]) < 70;
      if (!ef) {
        const ap = this.aparencias.get(a.exec_digest) || this.aparencias.get(undefined) || {};
        ef = new EfeitoArea(a, { qualidade: this.qualidade, cor: ap.cor, agua, materia: this._materia(a) });
        ef.aura = new AuraMateria(ef.materia, { seed: a.id });
        this.scene.add(ef.aura.g);
        ef.ultimo = a;
        this.scene.add(ef.g);
        this.objetos.areas.set(a.id, ef);
        this.audio.tom(g2t(a.centro[0], a.centro[1], 50), 160 + 4000 / Math.max(30, a.raio), 360 + 6000 / Math.max(30, a.raio), Math.max(0.08, (a.t_contato - a.t) / 1000), 0.08);
      }
      ef.ultimo = a;
      if (a.fase === 'contato' && !ef.somou) {
        ef.somou = true;
        const acertou = a.resultados.some((x) => x.resultado === 'atingido');
        this.audio.ruido(ef.g.position, 0.22, acertou ? 140 : 380, acertou ? 0.55 : 0.25);
        if (agua) this.audio.ruido(ef.g.position, 0.5, 1900, 0.3);
        this.contatos.push({ t: a.t_contato_real ?? a.t_contato, pos: ef.g.position.clone(), raio: a.raio, agua });
        if (this.contatos.length > 8) this.contatos.shift();
      }
    }
    for (const [id, ef] of this.objetos.areas) {
      const a = ef.ultimo;
      let c = a.centro;
      if (a.segue && a.fase === 'preparo' && mundo) { const p = mundo.posicao(a.autor, tServ); if (p) c = p; }
      ef.g.position.copy(g2t(c[0], c[1], 2));
      if (ef.agua) ef.g.position.y = -0.045;
      const fase = a.fase === 'preparo' && tServ >= a.t_contato ? 'contato' : a.fase === 'contato' && tServ >= a.t_fim ? 'fim' : a.fase;
      const autor = this.objetos.ent.get(a.autor);
      ef.atualizar(tServ, { fase, tc: a.t_contato_real, tf: a.t_fim_real, tcancel: a.t_fim_real, volumes: this.depuracao.volumes, mao: autor && autor.userData.maoW ? autor.userData.maoW : null });
      if (ef.aura) ef.aura.atualizar(autor && autor.userData.maoW, fase === 'preparo' ? Math.min(1, (tServ - a.t) / Math.max(1, a.t_contato - a.t)) : 0, tServ);
      if (!ef.vivo(tServ, a.t_fim_real) || (a.fase === 'cancelada' && tServ - (a.t_fim_real || 0) > 500)) { this.scene.remove(ef.g); if (ef.aura) this.scene.remove(ef.aura.g); this.objetos.areas.delete(id); }
    }
  }

  _guardas(lista, tServ, mundo) {
    for (const g of lista) {
      let ef = this.objetos.guardas.get(g.id);
      if (!ef) { ef = new EfeitoGuarda(g, { qualidade: this.qualidade, materia: this._materia(g) }); ef.cargas0 = g.cargas; this.scene.add(ef.g); this.objetos.guardas.set(g.id, ef); this.audio.tom(g2t(0, 0, 100), 500, 900, 0.18, 0.05, 'triangle'); }
      ef.ultimo = g;
      if (g.absorcoes && g.absorcoes.length > (ef.nAbs || 0)) { ef.nAbs = g.absorcoes.length; this.audio.tom(ef.g.position, 1800, 2700, 0.25, 0.15, 'square'); }
      if (g.fim && !ef.somFim) { ef.somFim = true; if (g.fim.motivo === 'rompida') this.audio.ruido(ef.g.position, 0.4, 2600, 0.35); }
    }
    for (const [id, ef] of this.objetos.guardas) {
      const g = ef.ultimo;
      const p = mundo.posicao(g.ent, tServ);
      if (p) { ef.g.position.copy(g2t(p[0], p[1], 0)); if (Math.abs(p[1]) < 70) ef.g.position.y = -0.2; }
      const eixo = g.segue === false ? g.eixo_yaw : mundo.yaw(g.ent) + (g.orientacao || 0);
      ef.g.rotation.y = -THREE.MathUtils.degToRad(eixo);
      ef.atualizar(tServ, { absorcoes: g.absorcoes || [], fim: g.fim, cargasIniciais: ef.cargas0 || g.cargas || 1 });
      if (!ef.vivo(tServ, g.fim) || (!g.fim && !lista.includes(g))) { this.scene.remove(ef.g); this.objetos.guardas.delete(id); }
    }
  }

  _rastros(mundo, tServ) {
    for (const [ent, mv] of mundo.movs) {
      const k = ent + ':' + mv.t0;
      if (this.objetos.rastros.has(k)) continue;
      const pts = [];
      for (let i = 0; i <= 28; i++) { const q = bezier(mv.de, mv.ctrl, mv.para, i / 28); pts.push(g2t(q[0], q[1], 0)); }
      const yb = this.objetos.ent.get(ent) ? -this.objetos.ent.get(ent).rotation.y : 0;
      const recuo = ((mv.para[0] - mv.de[0]) * Math.cos(yb) + (mv.para[1] - mv.de[1]) * Math.sin(yb)) < -30;
      const ef = new EfeitoRastro(mv, pts, { cor: recuo ? 0xc58bff : 0x8fe8ff, qualidade: this.qualidade, agua: pts.some((p) => Math.abs(p.z) < 0.7), materia: recuo ? null : this._materia(mv) });
      const grupo = new THREE.Group();
      grupo.add(ef.g);
      ef.fantasmas = [];
      const base = this.objetos.ent.get(ent);
      if (base && base.userData.tipo === 'operador' && this.depuracao.trajetoria !== false) {
        const matF = new THREE.MeshBasicMaterial({ color: recuo ? 0xc58bff : (this._materia(mv) ? new THREE.Color(this._materia(mv).cor) : 0x8fe8ff), transparent: true, opacity: 0.35, blending: THREE.AdditiveBlending, depthWrite: false });
        for (const f of [0.2, 0.4, 0.6, 0.8]) {
          const fan = base.userData.humano ? criarHumano(base.userData.genoma) : criarOperador(base.userData.time, { simples: true });
          const q0 = bezier(mv.de, mv.ctrl, mv.para, f), q1 = bezier(mv.de, mv.ctrl, mv.para, Math.min(1, f + 0.05));
          fan.position.copy(g2t(q0[0], q0[1], 0));
          fan.rotation.y = -Math.atan2(q1[1] - q0[1], q1[0] - q0[0]);
          posarDeslocamento(fan, f);
          const m = matF.clone();
          fan.traverse((x) => { if (x.isMesh) { x.material = m; x.castShadow = false; } });
          fan.userData.f = f; fan.userData.mat = m;
          grupo.add(fan);
          ef.fantasmas.push(fan);
        }
      }
      ef.grupo = grupo;
      this.scene.add(grupo);
      this.objetos.rastros.set(k, ef);
      this.audio.ruido(pts[0], (mv.t1 - mv.t0) / 1000 + 0.12, 900, 0.14);
    }
    for (const [k, ef] of this.objetos.rastros) {
      ef.atualizar(tServ);
      for (const fan of ef.fantasmas) {
        const tf = ef.mv.t0 + fan.userData.f * (ef.mv.t1 - ef.mv.t0);
        fan.visible = tServ >= tf;
        fan.userData.mat.opacity = tServ >= tf ? 0.42 * Math.exp(-(tServ - tf) / 170) : 0;
      }
      if (!ef.vivo(tServ)) { this.scene.remove(ef.grupo); this.objetos.rastros.delete(k); }
    }
  }

  _marcas(lista, tServ) {
    for (const m of lista) {
      const k = m.exec + ':' + m.no + ':' + m.t;
      if (this.objetos.marcas.has(k)) continue;
      const g = new THREE.Group();
      const haste = new THREE.Mesh(new THREE.CylinderGeometry(0.015, 0.015, 0.9, 8), new THREE.MeshStandardMaterial({ color: 0x222222, emissive: 0xc58bff, emissiveIntensity: 2 }));
      haste.position.y = 0.45;
      const anel = new THREE.Mesh(new THREE.RingGeometry(0.16, 0.2, 32), new THREE.MeshBasicMaterial({ color: 0xc58bff, transparent: true, opacity: 0.8, side: THREE.DoubleSide, depthWrite: false }));
      anel.rotation.x = -Math.PI / 2; anel.position.y = 0.02;
      g.add(haste, anel);
      g.position.copy(g2t(m.ponto[0], m.ponto[1], 0));
      g.userData = { t: m.t, anel };
      this.scene.add(g);
      this.objetos.marcas.set(k, g);
    }
    for (const [k, g] of this.objetos.marcas) {
      const s = (tServ - g.userData.t) / 1000;
      g.userData.anel.scale.setScalar(1 + 0.15 * Math.sin(tServ / 120));
      g.visible = s >= 0;
      if (s > 3.5) { this.scene.remove(g); this.objetos.marcas.delete(k); }
    }
  }

  _luzesImpacto(tServ) {
    const rec = this.contatos.filter((c) => tServ - c.t >= 0 && tServ - c.t < 600).slice(-this.luzes.length);
    this.luzes.forEach((l, i) => {
      const c = rec[i];
      if (!c) { l.intensity = 0; return; }
      const s = tServ - c.t;
      l.position.copy(c.pos).add(new THREE.Vector3(0, 0.7, 0));
      l.color.set(c.agua ? 0xbfe6ff : 0xff8a4a);
      l.intensity = 28 * Math.exp(-s / 120);
    });
  }

  // ---------- câmeras ----------
  _tremor(tServ) {
    let a = 0;
    for (const c of this.contatos) { const s = tServ - c.t; if (s >= 0 && s < 400) a += 0.08 * Math.exp(-s / 120) * Math.min(1, 6 / Math.max(1, c.pos.distanceTo(this.camera.position))); }
    return new THREE.Vector3(Math.sin(tServ * 0.09) * a, Math.sin(tServ * 0.13 + 1) * a * 0.6, Math.cos(tServ * 0.11) * a);
  }

  posicionarCamera(mundo, tServ, alvoEnt, yawCam) {
    if (this.modoCamera === 'cima') { this.camera.position.set(0, 22, 0.01); this.camera.lookAt(0, 0, 0); return; }
    if (this.modoCamera === 'editor' || alvoEnt == null || !mundo) {
      const o = this.orbita;
      this.camera.position.set(o.alvo.x + o.dist * Math.cos(o.pitch) * Math.cos(o.yaw), o.alvo.y + o.dist * Math.sin(o.pitch), o.alvo.z + o.dist * Math.cos(o.pitch) * Math.sin(o.yaw));
      this.camera.lookAt(o.alvo);
      if (tServ) this.camera.position.add(this._tremor(tServ));
      return;
    }
    const p = mundo.posicao(alvoEnt, tServ);
    if (!p) return;
    const yaw = THREE.MathUtils.degToRad(yawCam ?? mundo.yaw(alvoEnt));
    const f = new THREE.Vector3(Math.cos(yaw), 0, Math.sin(yaw));
    const base = g2t(p[0], p[1], 0);
    const lado = new THREE.Vector3(-f.z, 0, f.x);
    const desejada = base.clone().addScaledVector(f, -3.2).addScaledVector(lado, 0.55).add(new THREE.Vector3(0, 1.95, 0));
    this.camera.position.lerp(desejada, 0.25);
    this.camera.position.add(this._tremor(tServ));
    this.camera.lookAt(base.clone().addScaledVector(f, 2.4).addScaledVector(lado, 0.35).add(new THREE.Vector3(0, 1.0, 0)));
  }

  chao(ndcX, ndcY) {
    const r = new THREE.Raycaster();
    r.setFromCamera(new THREE.Vector2(ndcX, ndcY), this.camera);
    const p = new THREE.Vector3();
    if (!r.ray.intersectPlane(new THREE.Plane(new THREE.Vector3(0, 1, 0), 0), p)) return null;
    return [p.x * 100, p.z * 100];
  }

  render() {
    this._dt = Math.min(0.05, this.relogio.getDelta() || 0.016);
    { // preenchimento vem da câmera; recorte vem de trás-acima do alvo da câmera
      const f = new THREE.Vector3(); this.camera.getWorldDirection(f);
      const alvo = this.camera.position.clone().addScaledVector(f, 4);
      this.preench.position.copy(this.camera.position).add(new THREE.Vector3(0, 1.2, 0)); this.preench.target.position.copy(alvo);
      this.recorte.position.copy(alvo).addScaledVector(f, 3).add(new THREE.Vector3(0, 3, 0)); this.recorte.target.position.copy(alvo);
    }
    const tSeg = performance.now() / 1000;
    if (this.agua && this.agua.material.normalMap) this.agua.material.normalMap.offset.x = tSeg * 0.03;
    if (this.ambiente) this.ambiente.atualizar(tSeg);
    this.audio.ouvinte(this.camera);
    if (this.composer) this.composer.render(); else this.renderer.render(this.scene, this.camera);
    this.stats.quadros++;
    const el = performance.now() - this.stats.t0;
    if (el > 1000) { this.stats.fps = (this.stats.quadros * 1000) / el; this.stats.quadros = 0; this.stats.t0 = performance.now(); }
  }
}
