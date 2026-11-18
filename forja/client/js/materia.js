// Matéria: aparência paramétrica dos poderes (fora do digest; não muda regras nem sinais públicos).
// Eixos contínuos: viscosidade, elasticidade, toxicidade, temperatura (-1 gelo … +1 fogo), eletricidade,
// fumaça, brilho e duas cores. Cada camada de efeito pesa pelo seu eixo, então misturas surgem sozinhas
// (veneno em chamas, gelo elétrico, gosma elástica…). Predefinições são só pontos desse espaço.
// Tudo é função pura de (evento, tempo): mesma imagem no autor, no oponente, na prévia e na repetição.
import * as THREE from 'three';

export const EIXOS = [
  ['visc', 'viscosidade', 0, 1], ['elast', 'elasticidade', 0, 1], ['tox', 'toxicidade', 0, 1], ['temp', 'temperatura', -1, 1],
  ['elet', 'eletricidade', 0, 1], ['fumaca', 'fumaça', 0, 1], ['brilho', 'brilho', 0, 2],
];
export const PRESETS = {
  Energia: { cor: '#ffb347', cor2: '#ff5a1f', visc: 0, elast: 0, tox: 0, temp: 0.15, elet: 0.1, fumaca: 0.15, brilho: 1 },
  Veneno: { cor: '#86ff2e', cor2: '#1f5a0c', visc: 0.85, elast: 0.15, tox: 1, temp: 0.05, elet: 0, fumaca: 0.35, brilho: 0.9 },
  Elástico: { cor: '#ff4fb0', cor2: '#7a1fff', visc: 0.3, elast: 1, tox: 0, temp: 0, elet: 0, fumaca: 0, brilho: 0.9 },
  Brasa: { cor: '#ffb020', cor2: '#ff2a00', visc: 0, elast: 0, tox: 0, temp: 1, elet: 0, fumaca: 0.6, brilho: 1.3 },
  Gelo: { cor: '#bff0ff', cor2: '#3a9cff', visc: 0.1, elast: 0, tox: 0, temp: -1, elet: 0, fumaca: 0.25, brilho: 1 },
  Tempestade: { cor: '#a8d8ff', cor2: '#5a4cff', visc: 0, elast: 0, tox: 0, temp: 0, elet: 1, fumaca: 0.2, brilho: 1.4 },
  Fumaça: { cor: '#8a8a96', cor2: '#26262c', visc: 0.2, elast: 0, tox: 0.2, temp: 0.1, elet: 0, fumaca: 1, brilho: 0.4 },
  Mercúrio: { cor: '#d8e0e8', cor2: '#606a78', visc: 0.7, elast: 0.55, tox: 0.1, temp: 0, elet: 0.15, fumaca: 0, brilho: 0.7 },
};
export function normalizar(m) {
  const b = PRESETS.Energia;
  const o = { ...b, ...(m || {}) };
  for (const [k, , mn, mx] of EIXOS) o[k] = Math.max(mn, Math.min(mx, Number(o[k] ?? b[k])));
  return o;
}
export function aleatoria(semente) {
  const r = (i) => { const x = Math.sin(semente * 91.7 + i * 47.3) * 43758.5453; return x - Math.floor(x); };
  const hsl = (h, s, l) => '#' + new THREE.Color().setHSL(h, s, l).getHexString();
  const h = r(1);
  return normalizar({ cor: hsl(h, 0.85, 0.6), cor2: hsl((h + 0.08 + r(2) * 0.2) % 1, 0.9, 0.35), visc: r(3) ** 2, elast: r(4) ** 2, tox: r(5) ** 2, temp: (r(6) - 0.5) * 2, elet: r(7) ** 3, fumaca: r(8) * 0.8, brilho: 0.6 + r(9) });
}

const rnd = (s, i) => { const x = Math.sin(s * 12.9898 + i * 78.233) * 43758.5453; return x - Math.floor(x); };
const c01 = (x) => Math.max(0, Math.min(1, x));
const NOISE = `
float h21(vec2 p){ p = fract(p*vec2(123.34, 456.21)); p += dot(p, p+45.32); return fract(p.x*p.y); }
float vn(vec2 p){ vec2 i=floor(p), f=fract(p); vec2 u=f*f*(3.0-2.0*f);
  return mix(mix(h21(i), h21(i+vec2(1,0)), u.x), mix(h21(i+vec2(0,1)), h21(i+vec2(1,1)), u.x), u.y); }
float fbm(vec2 p){ float s=0.0, a=0.5; for(int i=0;i<4;i++){ s+=a*vn(p); p*=2.03; a*=0.5; } return s; }
float voro(vec2 p){ vec2 i=floor(p), f=fract(p); float d1=8.0, d2=8.0; for(int y=-1;y<=1;y++) for(int x=-1;x<=1;x++){ vec2 g=vec2(x,y); vec2 o=vec2(h21(i+g), h21(i+g+7.0)); float d=length(g+o-f); if(d<d1){d2=d1; d1=d;} else if(d<d2) d2=d; } return d2-d1; }`;

// ---------- partículas com cor e tamanho por vértice ----------
function pontos(n, { aditivo = true, escala = 380 } = {}) {
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(new Float32Array(n * 3), 3));
  g.setAttribute('aCor', new THREE.BufferAttribute(new Float32Array(n * 3), 3));
  g.setAttribute('aTam', new THREE.BufferAttribute(new Float32Array(n), 1));
  g.setAttribute('aA', new THREE.BufferAttribute(new Float32Array(n), 1));
  const m = new THREE.ShaderMaterial({
    transparent: true, depthWrite: false, blending: aditivo ? THREE.AdditiveBlending : THREE.NormalBlending,
    uniforms: { uEscala: { value: escala } },
    vertexShader: `attribute vec3 aCor; attribute float aTam; attribute float aA; varying vec3 vC; varying float vA; uniform float uEscala;
      void main(){ vC = aCor; vA = aA; vec4 mv = modelViewMatrix * vec4(position, 1.0); gl_PointSize = aTam * uEscala / -mv.z; gl_Position = projectionMatrix * mv; }`,
    fragmentShader: `varying vec3 vC; varying float vA; void main(){ vec2 d = gl_PointCoord - 0.5; float r = length(d); if (r > 0.5) discard; float s = smoothstep(0.5, 0.0, r);
      gl_FragColor = vec4(vC * (0.6 + 0.7 * s), s * s * vA); }`,
  });
  const p = new THREE.Points(g, m);
  p.frustumCulled = false;
  p.n = n;
  return p;
}
function set(p, i, x, y, z, c, tam, a) {
  const at = p.geometry.attributes;
  at.position.setXYZ(i, x, y, z); at.aCor.setXYZ(i, c.r, c.g, c.b); at.aTam.setX(i, tam); at.aA.setX(i, a);
}
function sujo(p) { const at = p.geometry.attributes; at.position.needsUpdate = at.aCor.needsUpdate = at.aTam.needsUpdate = at.aA.needsUpdate = true; }

const _m4 = new THREE.Matrix4(), _q = new THREE.Quaternion(), _v = new THREE.Vector3(), _s = new THREE.Vector3(), _e = new THREE.Euler();
const C = (h) => new THREE.Color(h);

// ---------- decalque de matéria (poça tóxica, geada, brasa, mancha elástica) ----------
function matPoca() {
  return new THREE.ShaderMaterial({
    transparent: true, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -4,
    uniforms: { uT: { value: 0 }, uA: { value: 0 }, uCor: { value: C('#86ff2e') }, uCor2: { value: C('#1f5a0c') }, uTox: { value: 0 }, uFrio: { value: 0 }, uCalor: { value: 0 }, uVisc: { value: 0 }, uSeed: { value: 0 } },
    vertexShader: `varying vec2 vP; void main(){ vP = position.xy; gl_Position = projectionMatrix*modelViewMatrix*vec4(position,1.0); }`,
    fragmentShader: `${NOISE} uniform float uT, uA, uTox, uFrio, uCalor, uVisc, uSeed; uniform vec3 uCor, uCor2; varying vec2 vP;
      void main(){
        float r = length(vP); float n = fbm(vP * 3.0 + uSeed);
        float borda = smoothstep(1.0, 0.75, r + (n - 0.5) * 0.6);
        // gosma: bolhas que nascem e estouram (células de Voronoi animadas)
        float cel = voro(vP * 7.0 + vec2(uSeed, uT * 0.15 * (1.0 - uVisc * 0.6)));
        float bolha = smoothstep(0.06, 0.0, abs(cel - 0.18 - 0.08 * sin(uT * 2.0 + n * 9.0))) * uTox;
        vec3 gosma = mix(uCor2, uCor, 0.4 + 0.6 * n) * (0.55 + 0.6 * bolha);
        // geada: arestas cristalinas
        float cris = smoothstep(0.08, 0.0, voro(vP * 5.0 + uSeed * 3.0)) + smoothstep(0.04, 0.0, voro(vP * 13.0 - uSeed));
        vec3 geada = mix(vec3(0.75, 0.9, 1.0), vec3(1.0), cris);
        vec3 brasa = mix(vec3(0.03), vec3(1.0, 0.35, 0.05), smoothstep(0.7, 0.95, n) * uCalor * 1.5);
        float wG = max(uTox, uVisc * 0.6), wF = uFrio, wB = uCalor;
        float wt = max(1e-3, wG + wF + wB);
        vec3 c = (gosma * wG + geada * wF + brasa * wB) / wt;
        float al = borda * uA * (0.55 + 0.35 * wG + 0.3 * cris * wF);
        gl_FragColor = vec4(c, al);
      }`,
  });
}

// ---------- camadas de contato por matéria ----------
export class CamadasMateria {
  constructor(m, R, { qualidade = 'alto', seed = 1, agua = false } = {}) {
    this.m = normalizar(m);
    this.R = R; this.seed = seed; this.agua = agua;
    const k = qualidade === 'alto' ? 1 : 0.35;
    this.g = new THREE.Group();
    const M = this.m;
    this.c1 = C(M.cor); this.c2 = C(M.cor2);
    // gosma (toxicidade/viscosidade): bolhas balísticas que esticam com a velocidade
    this.nGo = Math.round(28 * k * c01(M.tox + M.visc));
    if (this.nGo) {
      this.gosma = new THREE.InstancedMesh(new THREE.SphereGeometry(0.035, 12, 8), new THREE.MeshPhysicalMaterial({ color: this.c1, roughness: 0.12, clearcoat: 1, clearcoatRoughness: 0.05, emissive: this.c1, emissiveIntensity: 0.35 * M.brilho, transparent: true, opacity: 0.92 }), this.nGo);
      this.gosma.frustumCulled = false; this.g.add(this.gosma);
    }
    this.vapor = pontos(Math.round(40 * k * c01(M.tox + M.fumaca * 0.5)) || 1, { aditivo: false });
    this.g.add(this.vapor);
    // fogo: línguas de chama ascendentes e brasas
    this.nFo = Math.round(70 * k * c01(M.temp));
    this.chama = pontos(this.nFo || 1); this.g.add(this.chama);
    // gelo: espinhos de cristal que irrompem em anel e se partem
    this.nGe = Math.round(22 * k * c01(-M.temp));
    if (this.nGe) {
      const geo = new THREE.OctahedronGeometry(0.08, 0); geo.scale(0.6, 3.2, 0.6); geo.translate(0, 0.2, 0);
      this.gelo = new THREE.InstancedMesh(geo, new THREE.MeshPhysicalMaterial({ color: this.c1, roughness: 0.05, metalness: 0, transparent: true, opacity: 0.78, clearcoat: 1, emissive: this.c2, emissiveIntensity: 0.25 * M.brilho }), this.nGe);
      this.gelo.frustumCulled = false; this.g.add(this.gelo);
    }
    // eletricidade: arcos irregulares que piscam
    this.nAr = Math.round(6 * c01(M.elet * 1.2));
    if (this.nAr) {
      this.arcos = new THREE.LineSegments(new THREE.BufferGeometry(), new THREE.LineBasicMaterial({ color: this.c1, transparent: true, blending: THREE.AdditiveBlending, depthWrite: false }));
      this.arcos.geometry.setAttribute('position', new THREE.BufferAttribute(new Float32Array(this.nAr * 12 * 2 * 3), 3));
      this.arcos.frustumCulled = false; this.g.add(this.arcos);
      this.arcos2 = this.arcos.clone(); this.arcos2.material = this.arcos.material.clone(); this.arcos2.material.color = C('#ffffff'); this.g.add(this.arcos2);
    }
    // fumaça densa
    this.fum = pontos(Math.round(26 * k * c01(M.fumaca)) || 1, { aditivo: false }); this.g.add(this.fum);
    // anéis elásticos (oscilam e ultrapassam)
    this.nEl = M.elast > 0.15 ? 3 : 0;
    this.elos = [];
    for (let i = 0; i < this.nEl; i++) {
      const r = new THREE.Mesh(new THREE.TorusGeometry(1, 0.025, 8, 64), new THREE.MeshBasicMaterial({ color: i % 2 ? this.c2 : this.c1, transparent: true, blending: THREE.AdditiveBlending, depthWrite: false }));
      r.rotation.x = Math.PI / 2; this.elos.push(r); this.g.add(r);
    }
    // poça/geada/brasa persistente (cosmética)
    this.poca = new THREE.Mesh(new THREE.CircleGeometry(R * 1.25, 48), matPoca());
    this.poca.rotation.x = -Math.PI / 2; this.poca.position.y = 0.018;
    const u = this.poca.material.uniforms;
    u.uCor.value = this.c1; u.uCor2.value = this.c2; u.uTox.value = M.tox; u.uVisc.value = M.visc; u.uFrio.value = c01(-M.temp); u.uCalor.value = c01(M.temp) * 0.6; u.uSeed.value = seed % 17;
    this.g.add(this.poca);
  }
  // s: segundos desde o contato (negativo = antes)
  atualizar(s, tAbs) {
    const M = this.m, R = this.R;
    const vis = s >= 0;
    this.g.visible = vis;
    if (!vis) return;
    // gosma
    if (this.gosma) {
      for (let i = 0; i < this.nGo; i++) {
        const th = rnd(this.seed, i) * 6.283, v = (1.2 + 2.6 * rnd(this.seed, i + 9)) * (1 - 0.45 * M.visc), el = 0.5 + 0.9 * rnd(this.seed, i + 19);
        const vy = v * Math.sin(el), vh = v * Math.cos(el), tq = 2 * vy / 9.8;
        const t = Math.min(s, tq);
        const y = vy * t - 4.9 * t * t, h = vh * t + (s > tq ? 0 : 0);
        const vz = vy - 9.8 * t, sp = Math.hypot(vh, vz);
        _v.set(Math.cos(th) * (R * 0.35 * rnd(this.seed, i + 3) + h), Math.max(0.02, y + 0.03), Math.sin(th) * (R * 0.35 * rnd(this.seed, i + 3) + h));
        const estica = s < tq ? 1 + sp * 0.18 * (0.5 + M.visc) : 0.35;
        const tam = (0.6 + 0.8 * rnd(this.seed, i + 5)) * (s < tq ? 1 : Math.max(0, 1 - (s - tq) / 1.5));
        _q.setFromUnitVectors(new THREE.Vector3(0, 1, 0), new THREE.Vector3(Math.cos(th) * vh, vz, Math.sin(th) * vh).normalize());
        _s.set(tam / Math.sqrt(estica), tam * estica, tam / Math.sqrt(estica));
        if (s >= tq) { _s.set(tam * 1.8, tam * 0.35, tam * 1.8); _q.identity(); _v.y = 0.02; }
        _m4.compose(_v, _q, _s); this.gosma.setMatrixAt(i, _m4);
      }
      this.gosma.instanceMatrix.needsUpdate = true;
    }
    // vapor tóxico subindo
    for (let i = 0; i < this.vapor.n; i++) {
      const life = 2.2, ti = (s + rnd(this.seed, i + 30) * life) % life, nasce = s > rnd(this.seed, i + 40) * 0.6;
      const th = rnd(this.seed, i + 50) * 6.283, rr = R * Math.sqrt(rnd(this.seed, i + 60));
      const cor = this.c1.clone().lerp(C('#ffffff'), 0.25);
      set(this.vapor, i, Math.cos(th) * rr + Math.sin(ti * 2 + i) * 0.1, 0.05 + ti * 0.55, Math.sin(th) * rr, cor, 0.25 + ti * 0.35, nasce ? 0.22 * Math.sin(Math.PI * ti / life) * c01(M.tox + 0.3) * Math.exp(-s / 6) : 0);
    }
    sujo(this.vapor);
    // chamas
    for (let i = 0; i < this.chama.n; i++) {
      if (!this.nFo) { set(this.chama, i, 0, -9, 0, this.c1, 0, 0); continue; }
      const life = 0.55 + 0.4 * rnd(this.seed, i + 70), ti = (s + rnd(this.seed, i + 80) * life) % life, f = ti / life;
      const th = rnd(this.seed, i + 90) * 6.283, rr = R * (0.2 + 0.85 * Math.sqrt(rnd(this.seed, i + 100)));
      const cor = C('#fff2b0').lerp(this.c1, c01(f * 1.6)).lerp(this.c2, c01(f * 1.6 - 0.6));
      const forca = Math.exp(-s / 2.2) * (s < 0.12 ? 2 : 1);
      set(this.chama, i, Math.cos(th) * rr * (1 - f * 0.3), 0.05 + f * (0.6 + 1.2 * forca * rnd(this.seed, i)), Math.sin(th) * rr * (1 - f * 0.3), cor, (0.16 + 0.22 * (1 - f)) * (0.6 + 0.6 * forca), (1 - f) * c01(M.temp) * Math.min(1, forca * 1.2));
    }
    sujo(this.chama);
    // gelo
    if (this.gelo) {
      for (let i = 0; i < this.nGe; i++) {
        const th = (i / this.nGe) * 6.283 + rnd(this.seed, i) * 0.4, rr = R * (0.45 + 0.6 * rnd(this.seed, i + 3));
        const nasce = c01(s / 0.09), quebra = c01((s - 1.6 - rnd(this.seed, i) * 0.6) / 0.35);
        const alt = (0.6 + 0.9 * rnd(this.seed, i + 5)) * nasce * (1 - quebra);
        _v.set(Math.cos(th) * rr, -0.05 - quebra * 0.2, Math.sin(th) * rr);
        _e.set(Math.sin(th) * 0.5 * (rr / R), rnd(this.seed, i + 7) * 3, -Math.cos(th) * 0.5 * (rr / R)); _q.setFromEuler(_e);
        _s.set(Math.max(0.01, alt * 1.1), Math.max(0.01, alt), Math.max(0.01, alt * 1.1));
        _m4.compose(_v, _q, _s); this.gelo.setMatrixAt(i, _m4);
      }
      this.gelo.instanceMatrix.needsUpdate = true;
    }
    // arcos elétricos (redesenhados a cada 30 ms, determinísticos pelo bloco de tempo)
    if (this.arcos) {
      const pos = this.arcos.geometry.attributes.position, bloco = Math.floor((tAbs || 0) / 30);
      const vivo = s < 0.9 || rnd(bloco, 3) < 0.25 * c01(M.elet);
      let k = 0;
      for (let a = 0; a < this.nAr; a++) {
        const th = rnd(this.seed + bloco, a) * 6.283;
        const p0 = new THREE.Vector3(Math.cos(th) * R, 0.05, Math.sin(th) * R);
        const p1 = rnd(bloco, a + 9) < 0.5 ? new THREE.Vector3(0, 1.2 + rnd(bloco, a) * 0.6, 0) : new THREE.Vector3(Math.cos(th + 2) * R * 0.6, 0.1, Math.sin(th + 2) * R * 0.6);
        let prev = p0;
        for (let j = 1; j <= 12; j++) {
          const q = p0.clone().lerp(p1, j / 12);
          const amp = Math.sin(Math.PI * j / 12) * 0.22;
          q.x += (rnd(bloco + a, j) - 0.5) * amp; q.y += (rnd(bloco + a, j + 20) - 0.5) * amp; q.z += (rnd(bloco + a, j + 40) - 0.5) * amp;
          pos.setXYZ(k++, prev.x, prev.y, prev.z); pos.setXYZ(k++, q.x, q.y, q.z); prev = q;
        }
      }
      pos.needsUpdate = true;
      this.arcos.material.opacity = vivo ? 0.9 * Math.exp(-s / 1.4) : 0;
      this.arcos2.material.opacity = vivo ? 0.6 * Math.exp(-s / 0.6) : 0;
      this.arcos2.position.y = 0.01;
    }
    // fumaça
    for (let i = 0; i < this.fum.n; i++) {
      const th = rnd(this.seed, i + 200) * 6.283, d = R * (0.3 + 0.9 * rnd(this.seed, i + 210)) * (1 - Math.exp(-s * 1.5)) + 0.2;
      const cor = this.c2.clone().lerp(C('#3a3a40'), 0.6);
      set(this.fum, i, Math.cos(th) * d, 0.2 + s * (0.25 + 0.4 * rnd(this.seed, i + 220)), Math.sin(th) * d, cor, 0.6 + 0.9 * s, 0.32 * c01(M.fumaca) * Math.exp(-s / 2.5) * c01(s * 6));
    }
    sujo(this.fum);
    // anéis elásticos
    this.elos.forEach((r, i) => {
      const si = s - i * 0.08;
      const w = Math.exp(-Math.max(0, si) * 3.2) * Math.sin(Math.max(0, si) * 26);
      r.scale.setScalar(R * (1 + 0.35 * M.elast * w + i * 0.12));
      r.position.y = 0.05 + 0.08 * Math.abs(w);
      r.material.opacity = si > 0 ? 0.8 * Math.exp(-si * 1.5) * c01(M.elast) : 0;
    });
    // poça/geada persistente
    const u = this.poca.material.uniforms;
    u.uT.value = s;
    const pesoPoca = c01(M.tox + M.visc * 0.6 + c01(-M.temp) + c01(M.temp) * 0.5);
    u.uA.value = (this.agua ? 0.4 : 1) * pesoPoca * c01(s * 8) * c01(1 - (s - 3.2) / 1.6);
  }
}

// ---------- projétil do arremesso (da mão até a área) ----------
export class ProjetilMateria {
  constructor(m, { seed = 1 } = {}) {
    this.m = normalizar(m);
    this.seed = seed;
    const M = this.m;
    this.c1 = C(M.cor); this.c2 = C(M.cor2);
    const pesos = { gosma: M.visc * 0.5 + M.tox, fogo: c01(M.temp) * 1.2, gelo: c01(-M.temp) * 1.2, raio: M.elet * 1.3, elastico: M.elast * 1.1 };
    this.tipo = Object.entries(pesos).sort((a, b) => b[1] - a[1])[0];
    this.tipo = this.tipo[1] > 0.35 ? this.tipo[0] : 'feixe';
    this.g = new THREE.Group();
    this.nucleo = new THREE.Mesh(new THREE.SphereGeometry(0.07, 18, 12), new THREE.MeshBasicMaterial({ color: C('#ffffff').lerp(this.c1, 0.4) }));
    this.halo = new THREE.Mesh(new THREE.SphereGeometry(0.16, 16, 12), new THREE.MeshBasicMaterial({ color: this.c1, transparent: true, opacity: 0.45, blending: THREE.AdditiveBlending, depthWrite: false }));
    this.cauda = pontos(40);
    if (this.tipo === 'gelo') { const geo = new THREE.OctahedronGeometry(0.07, 0); geo.scale(0.5, 4, 0.5); this.nucleo.geometry = geo; this.nucleo.material = new THREE.MeshPhysicalMaterial({ color: this.c1, roughness: 0.05, clearcoat: 1, transparent: true, opacity: 0.85, emissive: this.c2, emissiveIntensity: 0.6 }); }
    if (this.tipo === 'gosma') { this.nucleo.material = new THREE.MeshPhysicalMaterial({ color: this.c1, roughness: 0.1, clearcoat: 1, emissive: this.c1, emissiveIntensity: 0.4 }); this.gotas = new THREE.InstancedMesh(new THREE.SphereGeometry(0.03, 10, 8), this.nucleo.material, 10); this.gotas.frustumCulled = false; this.g.add(this.gotas); }
    this.g.add(this.nucleo, this.halo, this.cauda);
    this.raio = null;
  }
  // de/para no espaço do grupo; p em [0,1] (0 = mão, 1 = área); visível só no trajeto
  atualizar(de, para, p, tAbs, k0) {
    const vivo = p >= 0 && p <= 1.05;
    this.g.visible = vivo || (this.tipo === 'raio' && Math.abs(p - 1) < 0.6);
    if (this.raio) { this.g.remove(this.raio); this.raio.geometry.dispose(); this.raio = null; }
    if (!this.g.visible) return;
    const arco = (u) => { const q = de.clone().lerp(para, u); q.y += Math.sin(Math.PI * u) * (this.tipo === 'gosma' ? 0.7 : 0.25); return q; };
    const pc = Math.min(1, Math.max(0, p));
    const pos = arco(pc);
    this.nucleo.position.copy(pos); this.halo.position.copy(pos);
    const dirv = arco(Math.min(1, pc + 0.02)).sub(arco(Math.max(0, pc - 0.02))).normalize();
    this.nucleo.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dirv);
    const pulso = 1 + 0.15 * Math.sin((tAbs || 0) / 25);
    this.halo.scale.setScalar(pulso * (this.tipo === 'fogo' ? 1.5 : 1));
    this.nucleo.visible = this.halo.visible = this.tipo !== 'raio' && this.tipo !== 'elastico' && this.tipo !== 'feixe';
    for (let i = 0; i < 40; i++) {
      const u = pc - (i / 40) * 0.35;
      if (u < 0 || this.tipo === 'raio' || this.tipo === 'elastico') { set(this.cauda, i, 0, -9, 0, this.c1, 0, 0); continue; }
      const q = arco(u);
      const j = (rnd(this.seed + Math.floor((tAbs || 0) / 40), i) - 0.5) * 0.08 * (1 + i / 10);
      const cor = this.tipo === 'fogo' ? C('#fff0b0').lerp(this.c1, i / 20).lerp(this.c2, Math.max(0, i / 40 - 0.4)) : this.c1.clone().lerp(this.c2, i / 40);
      set(this.cauda, i, q.x + j, q.y + j * 0.6 - (this.tipo === 'gosma' ? i * 0.004 : 0), q.z - j, cor, 0.09 * (1 - i / 40) + 0.02, (1 - i / 40) * 0.85);
    }
    sujo(this.cauda);
    if (this.gotas) {
      for (let i = 0; i < 10; i++) {
        const u = Math.max(0, pc - 0.05 - i * 0.04), q = arco(u); q.y -= (i * 0.02) ** 1.4;
        _m4.compose(q, _q.identity(), _s.setScalar(u > 0 ? 1 - i * 0.07 : 0)); this.gotas.setMatrixAt(i, _m4);
      }
      this.gotas.instanceMatrix.needsUpdate = true;
    }
    if (this.tipo === 'raio' || this.tipo === 'feixe') {
      const pts = [], bloco = Math.floor((tAbs || 0) / 28);
      for (let i = 0; i <= 16; i++) {
        const u = i / 16, q = arco(u), amp = Math.sin(u * Math.PI) * (this.tipo === 'raio' ? 0.22 : 0.08);
        q.x += (rnd(this.seed + bloco, i) - 0.5) * amp; q.y += (rnd(this.seed + bloco, i + 50) - 0.5) * amp; q.z += (rnd(this.seed + bloco, i + 90) - 0.5) * amp;
        pts.push(q);
      }
      this.raio = new THREE.Mesh(new THREE.TubeGeometry(new THREE.CatmullRomCurve3(pts), 48, 0.01 + 0.025 * k0, 6), new THREE.MeshBasicMaterial({ color: C('#ffffff').lerp(this.c1, 0.5), transparent: true, opacity: 0.55 + 0.45 * k0, blending: THREE.AdditiveBlending, depthWrite: false }));
      this.g.add(this.raio);
    }
  }
}

// ---------- aura na mão durante a carga ----------
export class AuraMateria {
  constructor(m, { seed = 1 } = {}) {
    this.m = normalizar(m); this.seed = seed;
    this.c1 = C(this.m.cor); this.c2 = C(this.m.cor2);
    this.p = pontos(36);
    this.g = new THREE.Group(); this.g.add(this.p);
  }
  atualizar(mao, k, tAbs) {
    const M = this.m;
    this.g.visible = k > 0.02;
    if (!this.g.visible || !mao) return;
    const t = (tAbs || 0) / 1000;
    for (let i = 0; i < 36; i++) {
      const fase = (t * (1.2 + rnd(this.seed, i)) + rnd(this.seed, i + 5)) % 1;
      let x = 0, y = 0, z = 0, tam = 0.03, a = k, cor = this.c1;
      if (i % 3 === 0 && (M.tox + M.visc) > 0.4) { x = (rnd(this.seed, i + 9) - 0.5) * 0.06; y = -fase * 0.9; z = (rnd(this.seed, i + 11) - 0.5) * 0.06; tam = 0.035; cor = this.c1; a = k * (1 - fase); }       // gotas que pingam
      else if (i % 3 === 1 && M.temp > 0.3) { const th = i * 2.1 + t * 3; x = Math.cos(th) * 0.06; y = fase * 0.5; z = Math.sin(th) * 0.06; cor = C('#fff0b0').lerp(this.c2, fase); tam = 0.05 * (1 - fase); a = k * (1 - fase); } // chamas
      else if (M.temp < -0.3 && i % 3 === 1) { const th = i * 2.4; x = Math.cos(th) * 0.12 * fase; y = -0.05 * fase; z = Math.sin(th) * 0.12 * fase; cor = C('#ffffff'); tam = 0.02; a = k * (1 - fase); }        // névoa fria
      else { const th = i * 1.7 + t * (5 + 4 * M.elet); const r = 0.1 + 0.05 * Math.sin(t * 9 + i); x = Math.cos(th) * r; y = Math.sin(th * 1.3) * r * 0.7; z = Math.sin(th) * r; cor = this.c1.clone().lerp(C('#ffffff'), 0.3); a = k * (0.5 + 0.5 * Math.sin(t * 20 + i)); }
      set(this.p, i, mao.x + x, mao.y + y, mao.z + z, cor, tam, a * c01(M.brilho));
    }
    sujo(this.p);
  }
}
