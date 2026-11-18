// Efeitos visuais como funções puras de (evento do servidor, tempo do servidor).
// Isso garante que a prévia da oficina, a repetição e as duas câmeras mostrem o mesmo quadro para o mesmo
// instante. Os sinais públicos (contorno do anúncio, setor da defesa, anel de recuperação) têm forma e
// contraste fixos; a cor personalizada só tinge a energia interna.
import * as THREE from 'three';

const NOISE = `
float h21(vec2 p){ p = fract(p*vec2(123.34, 456.21)); p += dot(p, p+45.32); return fract(p.x*p.y); }
float vn(vec2 p){ vec2 i=floor(p), f=fract(p); vec2 u=f*f*(3.0-2.0*f);
  return mix(mix(h21(i), h21(i+vec2(1,0)), u.x), mix(h21(i+vec2(0,1)), h21(i+vec2(1,1)), u.x), u.y); }
float fbm(vec2 p){ float s=0.0, a=0.5; for(int i=0;i<4;i++){ s+=a*vn(p); p*=2.03; a*=0.5; } return s; }`;

export const rnd = (seed, i) => { const x = Math.sin(seed * 12.9898 + i * 78.233) * 43758.5453; return x - Math.floor(x); };
const clamp01 = (x) => Math.max(0, Math.min(1, x));

// ---------- anúncio / contato de área ----------
function matAnuncio() {
  return new THREE.ShaderMaterial({
    transparent: true, depthWrite: false, side: THREE.DoubleSide,
    uniforms: { uProg: { value: 0 }, uFase: { value: 0 }, uTempo: { value: 0 }, uFlash: { value: 0 }, uInner: { value: 0 }, uCor: { value: new THREE.Color(0xffa040) }, uAlpha: { value: 1 }, uCinza: { value: 0 } },
    vertexShader: `varying vec2 vP; void main(){ vP = position.xy / 1.0; gl_Position = projectionMatrix*modelViewMatrix*vec4(position,1.0); }`,
    fragmentShader: `${NOISE}
      uniform float uProg, uFase, uTempo, uFlash, uInner, uAlpha, uCinza; uniform vec3 uCor; varying vec2 vP;
      void main(){
        float r = length(vP); if (r > 1.09) discard;
        float a = atan(vP.y, vP.x);
        float ang = fract((-a + 1.5707963) / 6.2831853);
        float dentro = step(uInner, r) * step(r, 1.0);
        float borda = 1.0 - smoothstep(0.012, 0.024, abs(r - 1.0));
        float halo = (1.0 - smoothstep(0.0, 0.05, abs(r - 1.045))) * 0.55;
        float n = fbm(vP * 5.0 + vec2(uTempo * 0.0007, -uTempo * 0.0005));
        float veios = smoothstep(0.55, 0.62, fbm(vP * 9.0 - uTempo * 0.0004)) * 0.6;
        float varre = step(ang, uProg) * dentro;
        float frente = (1.0 - smoothstep(0.0, 0.02, abs(ang - uProg))) * dentro * step(uProg, 0.999);
        float marcas = step(0.992, cos(a * 12.0)) * step(0.88, r) * step(r, 0.98);
        vec3 contorno = mix(vec3(1.0), vec3(1.0, 0.18, 0.12), step(0.5, uFase));
        vec3 energia = uCor * (0.35 + 0.9 * n) + vec3(1.0, 0.9, 0.7) * veios;
        vec3 c = vec3(0.0); float al = 0.0;
        al += dentro * (0.10 + 0.22 * varre + 0.25 * n * varre); c += energia * dentro * (0.4 + 0.8 * varre);
        c += vec3(1.0, 0.95, 0.85) * frente * 1.6; al += frente * 0.9;
        c += vec3(1.0) * marcas * 0.7; al += marcas * 0.5;
        c += vec3(1.0, 0.35, 0.15) * uFlash * dentro * (0.8 + 0.6 * n) * 2.2; al += uFlash * dentro * 0.75;
        c = mix(c, contorno * 1.4, borda); al = max(al, borda);
        c = mix(c, vec3(0.0), halo * (1.0 - borda)); al = max(al, halo * (1.0 - borda));
        c = mix(c, vec3(dot(c, vec3(0.33))), uCinza);
        gl_FragColor = vec4(c, al * uAlpha);
      }`,
  });
}
function matVolume() {
  return new THREE.ShaderMaterial({
    transparent: true, depthWrite: false, side: THREE.DoubleSide, blending: THREE.AdditiveBlending,
    uniforms: { uCor: { value: new THREE.Color(0xff6a2f) }, uA: { value: 0.2 }, uTempo: { value: 0 } },
    vertexShader: `varying vec2 vUv; void main(){ vUv = uv; gl_Position = projectionMatrix*modelViewMatrix*vec4(position,1.0); }`,
    fragmentShader: `${NOISE} uniform vec3 uCor; uniform float uA, uTempo; varying vec2 vUv;
      void main(){ float g = pow(1.0 - vUv.y, 2.2); float n = fbm(vec2(vUv.x * 24.0, vUv.y * 3.0 - uTempo * 0.002));
        gl_FragColor = vec4(uCor * (0.6 + 0.8 * n), g * uA * (0.6 + 0.6 * n)); }`,
  });
}
function matOnda() {
  return new THREE.ShaderMaterial({
    transparent: true, depthWrite: false, side: THREE.DoubleSide, blending: THREE.AdditiveBlending,
    uniforms: { uRaio: { value: 0.5 }, uLarg: { value: 0.08 }, uA: { value: 1 }, uCor: { value: new THREE.Color(0xffd0a0) } },
    vertexShader: `varying vec2 vP; void main(){ vP = position.xy; gl_Position = projectionMatrix*modelViewMatrix*vec4(position,1.0); }`,
    fragmentShader: `uniform float uRaio, uLarg, uA; uniform vec3 uCor; varying vec2 vP;
      void main(){ float r = length(vP); float b = exp(-pow((r - uRaio) / uLarg, 2.0)); float rastro = exp(-pow((r - uRaio * 0.85) / (uLarg * 3.0), 2.0)) * 0.25;
        gl_FragColor = vec4(uCor, (b + rastro) * uA); }`,
  });
}
function matDecal() {
  return new THREE.ShaderMaterial({
    transparent: true, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -2,
    uniforms: { uA: { value: 1 }, uSeed: { value: 0 } },
    vertexShader: `varying vec2 vP; void main(){ vP = position.xy; gl_Position = projectionMatrix*modelViewMatrix*vec4(position,1.0); }`,
    fragmentShader: `${NOISE} uniform float uA, uSeed; varying vec2 vP;
      void main(){ float r = length(vP); float n = fbm(vP * 6.0 + uSeed); float m = smoothstep(1.0, 0.55, r + (n - 0.5) * 0.5);
        float brasa = smoothstep(0.75, 0.95, n) * smoothstep(0.7, 0.3, r);
        gl_FragColor = vec4(mix(vec3(0.02, 0.018, 0.015), vec3(1.0, 0.35, 0.08), brasa * uA), m * 0.78 * uA); }`,
  });
}
function matRachadura() {
  return new THREE.ShaderMaterial({
    transparent: true, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -3,
    uniforms: { uCresce: { value: 0 }, uCalor: { value: 1 }, uA: { value: 1 }, uSeed: { value: 0 } },
    vertexShader: `varying vec2 vP; void main(){ vP = position.xy; gl_Position = projectionMatrix*modelViewMatrix*vec4(position,1.0); }`,
    fragmentShader: `${NOISE} uniform float uCresce, uCalor, uA, uSeed; varying vec2 vP;
      float trinca(float r, float a, float n, float s, float alcance){
        float ang = a / 6.2831853 * n + s; float id = floor(ang); float f = fract(ang) - 0.5;
        float jit = (vn(vec2(id * 3.1 + s, r * 7.0)) - 0.5) * 0.55 + (vn(vec2(id * 9.7, r * 23.0)) - 0.5) * 0.12;
        float d = abs(f - jit) * 6.2831853 * r / n;
        float w = 0.012 + 0.03 * (1.0 - r);
        float lim = alcance * (0.55 + 0.45 * h21(vec2(id, s)));
        return (1.0 - smoothstep(w, w * 2.2, d)) * step(r, lim) * smoothstep(0.08, 0.16, r);
      }
      void main(){
        float r = length(vP); if (r > 1.0) discard;
        float a = atan(vP.y, vP.x);
        float c = max(trinca(r, a, 9.0, uSeed, uCresce), trinca(r, a + 0.3, 17.0, uSeed + 4.0, uCresce * 0.7) * 0.7);
        float cratera = smoothstep(0.24, 0.12, r + (fbm(vP * 9.0 + uSeed) - 0.5) * 0.12);
        vec3 brasa = mix(vec3(1.0, 0.25, 0.05), vec3(1.0, 0.8, 0.45), uCalor) * (1.5 * uCalor);
        vec3 cor = mix(vec3(0.02), brasa, c * uCalor + cratera * uCalor * 0.6);
        float al = max(c, cratera * 0.85) * uA;
        gl_FragColor = vec4(cor, al);
      }`,
  });
}

function matPontos(cor, aditivo = true) {
  return new THREE.ShaderMaterial({
    transparent: true, depthWrite: false, blending: aditivo ? THREE.AdditiveBlending : THREE.NormalBlending,
    uniforms: { uCor: { value: new THREE.Color(cor) }, uEscala: { value: 380 } },
    vertexShader: `attribute float aTam; attribute float aA; varying float vA; uniform float uEscala;
      void main(){ vA = aA; vec4 mv = modelViewMatrix * vec4(position, 1.0); gl_PointSize = aTam * uEscala / -mv.z; gl_Position = projectionMatrix * mv; }`,
    fragmentShader: `uniform vec3 uCor; varying float vA; void main(){ vec2 d = gl_PointCoord - 0.5; float r = length(d); if (r > 0.5) discard; float s = smoothstep(0.5, 0.0, r);
      gl_FragColor = vec4(uCor * (0.7 + 0.6 * s), s * vA); }`,
  });
}
function nuvem(n, cor, aditivo) {
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(new Float32Array(n * 3), 3));
  g.setAttribute('aTam', new THREE.BufferAttribute(new Float32Array(n), 1));
  g.setAttribute('aA', new THREE.BufferAttribute(new Float32Array(n), 1));
  const p = new THREE.Points(g, matPontos(cor, aditivo));
  p.frustumCulled = false;
  return p;
}

// Área: anúncio (preparo), contato (clarão, onda, coluna, faíscas, detritos, poeira/respingo) e marca no chão.
export class EfeitoArea {
  constructor(a, { qualidade, cor, agua }) {
    this.a = a;
    this.seed = a.id * 7.31 + 1;
    this.alto = qualidade === 'alto';
    this.agua = agua;
    const R = a.raio / 100, H = (a.altura || 200) / 100;
    this.g = new THREE.Group();
    this.disco = new THREE.Mesh(new THREE.PlaneGeometry(2.2, 2.2), matAnuncio());
    this.disco.scale.set(R, R, 1);
    this.disco.rotation.x = -Math.PI / 2;
    this.disco.material.uniforms.uInner.value = a.espessura ? Math.max(0, (a.raio - a.espessura) / a.raio) : 0;
    if (cor) this.disco.material.uniforms.uCor.value.set(cor);
    this.vol = new THREE.Mesh(new THREE.CylinderGeometry(R, R, H, 64, 1, true), matVolume());
    this.vol.position.y = H / 2;
    this.onda = new THREE.Mesh(new THREE.PlaneGeometry(2 * (R + 2), 2 * (R + 2)), matOnda());
    this.onda.rotation.x = -Math.PI / 2; this.onda.position.y = 0.02;
    this.coluna = new THREE.Mesh(new THREE.CylinderGeometry(R * 0.25, R * 0.6, 4.5, 24, 1, true), matVolume());
    this.coluna.position.y = 2.25; this.coluna.material.uniforms.uCor.value.set(0xffc080);
    this.decal = new THREE.Mesh(new THREE.CircleGeometry(R * 0.9, 40), matDecal());
    this.decal.rotation.x = -Math.PI / 2; this.decal.position.y = 0.012; this.decal.material.uniforms.uSeed.value = this.seed;
    this.nF = this.alto ? 70 : 20; this.nD = this.alto ? 26 : 8; this.nP = this.alto ? 30 : 10;
    this.faiscas = nuvem(this.nF, 0xffb060, true);
    this.poeira = nuvem(this.nP, agua ? 0xcfe8ff : 0x8a8478, !agua ? false : true);
    this.detritos = new THREE.InstancedMesh(new THREE.DodecahedronGeometry(agua ? 0.018 : 0.03, 0), agua ? new THREE.MeshStandardMaterial({ color: 0xbfe0ff, roughness: 0.1, transparent: true, opacity: 0.8 }) : new THREE.MeshStandardMaterial({ color: 0x77736a, roughness: 0.9 }), this.nD);
    this.detritos.frustumCulled = false;
    this.ondas2 = [];
    if (agua) for (let i = 0; i < 3; i++) { const m = new THREE.Mesh(new THREE.PlaneGeometry(2 * (R + 3), 2 * (R + 3)), matOnda()); m.rotation.x = -Math.PI / 2; m.material.uniforms.uCor.value.set(0xcfeaff); this.ondas2.push(m); this.g.add(m); }
    this.rach = new THREE.Mesh(new THREE.PlaneGeometry(2 * R * 1.35, 2 * R * 1.35), matRachadura());
    this.rach.rotation.x = -Math.PI / 2; this.rach.position.y = 0.014; this.rach.material.uniforms.uSeed.value = this.seed % 10;
    this.nFl = this.alto ? 56 : 18;
    this.fluxo = nuvem(this.nFl, cor || 0xffb060, true);
    this.raio = null;
    this.g.add(this.disco, this.vol, this.onda, this.coluna, this.decal, this.rach, this.fluxo, this.faiscas, this.poeira, this.detritos);
    this._m = new THREE.Matrix4(); this._q = new THREE.Quaternion(); this._e = new THREE.Euler(); this._v = new THREE.Vector3(); this._s = new THREE.Vector3(1, 1, 1);
  }
  // fase: 'preparo' | 'contato' | 'fim' | 'cancelada'; tc = instante do contato; tf = fim
  atualizar(t, { fase, tc, tf, tcancel, volumes, mao }) {
    const a = this.a, R = a.raio / 100;
    const u = this.disco.material.uniforms;
    // fluxo de carga: energia sobe do anel em espiral até a mão do autor (ou até o centro, se a área está longe)
    const kp = Math.max(0, Math.min(1, (t - a.t) / Math.max(1, a.t_contato - a.t)));
    const fl = this.fluxo.geometry.attributes;
    const destino = mao ? mao.clone().sub(this.g.position) : new THREE.Vector3(0, 1.3, 0);
    const longe = destino.length() > R + 1.6;
    if (longe) destino.set(0, 0.25, 0);
    this.fluxo.visible = fase === 'preparo';
    if (this.fluxo.visible) for (let i = 0; i < this.nFl; i++) {
      const p = (kp * 2.4 + rnd(this.seed, i + 1000)) % 1;
      const th = rnd(this.seed, i + 1100) * Math.PI * 2 + t * 0.0012;
      const sx = Math.cos(th) * R, sz = Math.sin(th) * R;
      const e = p * p * (3 - 2 * p), giro = th + p * 7, rr = 0.28 * (1 - p);
      fl.position.setXYZ(i, sx + (destino.x - sx) * e + Math.cos(giro) * rr, 0.03 + destino.y * e + Math.sin(p * Math.PI) * 0.25, sz + (destino.z - sz) * e + Math.sin(giro) * rr);
      fl.aTam.setX(i, 0.025 + 0.03 * p);
      fl.aA.setX(i, Math.sin(p * Math.PI) * (0.25 + 0.75 * kp));
    }
    fl.position.needsUpdate = fl.aTam.needsUpdate = fl.aA.needsUpdate = true;
    // raio da mão até a área (ataque à distância), alguns ms antes e depois do contato
    const tcc = tc ?? a.t_contato;
    const mostraRaio = mao && longe && t >= tcc - 110 && t <= tcc + 130 && fase !== 'cancelada';
    if (this.raio) { this.g.remove(this.raio); this.raio.geometry.dispose(); this.raio = null; }
    if (mostraRaio) {
      const de = mao.clone().sub(this.g.position), ate = new THREE.Vector3(0, 0.05, 0);
      const pts = [];
      const bloco = Math.floor(t / 28);
      for (let i = 0; i <= 14; i++) {
        const k = i / 14, q = de.clone().lerp(ate, k);
        const amp = Math.sin(k * Math.PI) * 0.16;
        q.x += (rnd(this.seed + bloco, i) - 0.5) * amp; q.y += (rnd(this.seed + bloco, i + 50) - 0.5) * amp + Math.sin(k * Math.PI) * 0.35; q.z += (rnd(this.seed + bloco, i + 90) - 0.5) * amp;
        pts.push(q);
      }
      const k0 = 1 - Math.min(1, Math.abs(t - tcc) / 130);
      this.raio = new THREE.Mesh(new THREE.TubeGeometry(new THREE.CatmullRomCurve3(pts), 40, 0.012 + 0.02 * k0, 6), new THREE.MeshBasicMaterial({ color: 0xfff0d8, transparent: true, opacity: 0.6 + 0.4 * k0, blending: THREE.AdditiveBlending, depthWrite: false }));
      this.g.add(this.raio);
    }
    u.uTempo.value = t;
    const prep = Math.max(1, a.t_contato - a.t);
    u.uProg.value = clamp01((t - a.t) / prep);
    u.uFase.value = fase === 'preparo' ? 0 : 1;
    const dtc = t - (tc ?? a.t_contato);
    u.uFlash.value = dtc >= 0 ? 0.55 * Math.exp(-dtc / 90) + (dtc < (a.t_fim - a.t_contato) ? 0.18 : 0) : 0;
    const fimT = tf ?? a.t_fim;
    u.uAlpha.value = fase === 'fim' ? clamp01(1 - (t - fimT) / 350) : 1;
    if (fase === 'cancelada') { const k = clamp01((t - (tcancel ?? t)) / 400); u.uCinza.value = 1; u.uAlpha.value = 1 - k; this.disco.scale.setScalar(R * (1 - 0.4 * k)); }
    this.vol.visible = volumes || fase === 'contato';
    this.vol.material.uniforms.uTempo.value = t;
    this.vol.material.uniforms.uA.value = fase === 'preparo' ? 0.05 + 0.06 * u.uProg.value : Math.max(0.03, 0.22 * Math.exp(-Math.max(0, dtc) / 160));
    this.vol.material.uniforms.uCor.value.set(fase === 'preparo' ? 0xffc070 : 0xff5a2a);
    const contatou = fase !== 'preparo' && fase !== 'cancelada' && dtc >= 0;
    this.onda.visible = this.coluna.visible = this.decal.visible = this.faiscas.visible = this.detritos.visible = this.poeira.visible = this.rach.visible = contatou;
    for (const o of this.ondas2) o.visible = contatou;
    if (!contatou) return;
    const s = dtc / 1000;
    const ou = this.onda.material.uniforms;
    ou.uRaio.value = R + 1.8 * (1 - Math.exp(-s * 7)); ou.uLarg.value = 0.05 + 0.09 * s; ou.uA.value = Math.exp(-s * 3.6) * 0.9;
    this.coluna.material.uniforms.uA.value = Math.exp(-s * 12) * 0.45; this.coluna.material.uniforms.uTempo.value = t;
    const ru = this.rach.material.uniforms;
    ru.uCresce.value = this.agua ? 0 : 1 - Math.exp(-s * 22);
    ru.uCalor.value = Math.exp(-s * 1.4);
    ru.uA.value = this.agua ? 0 : Math.max(0, Math.min(1, 1 - (s - 2.5) / 2));
    this.decal.material.uniforms.uA.value = this.agua ? 0 : clamp01(1 - (s - 1.5) / 2.5);
    // faíscas: balística determinística
    const fp = this.faiscas.geometry.attributes;
    for (let i = 0; i < this.nF; i++) {
      const th = rnd(this.seed, i) * Math.PI * 2, el = 0.25 + rnd(this.seed, i + 99) * 0.9, v = 3 + rnd(this.seed, i + 7) * 6;
      const vida = 0.35 + rnd(this.seed, i + 3) * 0.45;
      const tt = Math.min(s, vida);
      const r0 = R * rnd(this.seed, i + 11);
      fp.position.setXYZ(i, Math.cos(th) * (r0 + v * Math.cos(el) * tt), Math.max(0.01, 0.1 + v * Math.sin(el) * tt - 4.9 * tt * tt), Math.sin(th) * (r0 + v * Math.cos(el) * tt));
      fp.aTam.setX(i, 0.05 + 0.04 * rnd(this.seed, i + 5));
      fp.aA.setX(i, s < vida ? 1 - s / vida : 0);
    }
    fp.position.needsUpdate = fp.aTam.needsUpdate = fp.aA.needsUpdate = true;
    // detritos (concreto) ou gotas (água): sobem, caem, quicam e assentam
    for (let i = 0; i < this.nD; i++) {
      const th = rnd(this.seed, i + 200) * Math.PI * 2, v = 1.5 + rnd(this.seed, i + 300) * 3.2, el = 0.6 + rnd(this.seed, i + 400) * 0.8;
      const vy = v * Math.sin(el), vh = v * Math.cos(el), tq = (2 * vy) / 9.8;
      let tt = s, y, h;
      if (tt < tq) { y = vy * tt - 4.9 * tt * tt; h = vh * tt; }
      else { const t2 = Math.min(tt - tq, tq * 0.45); y = this.agua ? -1 : vy * 0.35 * t2 - 4.9 * t2 * t2; h = vh * tq + vh * 0.4 * t2; if (y < 0) y = 0; }
      const r0 = R * 0.6 * rnd(this.seed, i + 500);
      this._v.set(Math.cos(th) * (r0 + h), Math.max(this.agua ? -0.5 : 0.015, y + 0.02), Math.sin(th) * (r0 + h));
      this._e.set(tt * 9 * rnd(this.seed, i), tt * 7, 0); this._q.setFromEuler(this._e);
      const vis = this.agua ? (tt < tq ? 1 : 0) : clamp01(1 - (s - 3) / 1.5);
      this._s.setScalar(vis * (0.6 + rnd(this.seed, i + 600)));
      this._m.compose(this._v, this._q, this._s);
      this.detritos.setMatrixAt(i, this._m);
    }
    this.detritos.instanceMatrix.needsUpdate = true;
    // poeira (ou névoa de água) que se expande e assenta
    const pp = this.poeira.geometry.attributes;
    for (let i = 0; i < this.nP; i++) {
      const th = rnd(this.seed, i + 700) * Math.PI * 2, d = R * (0.4 + 0.9 * rnd(this.seed, i + 800)) * (1 - Math.exp(-s * 2.2)) + R * 0.3;
      pp.position.setXYZ(i, Math.cos(th) * d, 0.15 + 0.6 * rnd(this.seed, i + 900) * (1 - Math.exp(-s * 1.5)), Math.sin(th) * d);
      pp.aTam.setX(i, (this.agua ? 0.25 : 0.45) + 0.5 * s);
      pp.aA.setX(i, (this.agua ? 0.35 : 0.22) * Math.exp(-s * 1.1));
    }
    pp.position.needsUpdate = pp.aTam.needsUpdate = pp.aA.needsUpdate = true;
    this.ondas2.forEach((o, i) => { const sl = s - i * 0.18; o.visible = sl > 0; const k = Math.max(0, sl); o.material.uniforms.uRaio.value = R * 0.5 + 1.4 * k; o.material.uniforms.uLarg.value = 0.03 + 0.05 * k; o.material.uniforms.uA.value = 0.9 * Math.exp(-k * 1.6); o.position.y = -0.03; });
  }
  vivo(t, tf) { return t - (tf ?? this.a.t_fim) < 4500; }
}

// ---------- barreira de defesa ----------
function matBarreira() {
  return new THREE.ShaderMaterial({
    transparent: true, depthWrite: false, side: THREE.DoubleSide, blending: THREE.AdditiveBlending,
    uniforms: { uForma: { value: 0 }, uDano: { value: 0 }, uTempo: { value: 0 }, uImpU: { value: 0.5 }, uImpT: { value: 9999 }, uCor: { value: new THREE.Color(0x5ff2ff) }, uA: { value: 1 } },
    vertexShader: `varying vec2 vUv; varying vec3 vN; varying vec3 vV; void main(){ vUv = uv; vN = normalize(normalMatrix * normal); vec4 mv = modelViewMatrix * vec4(position,1.0); vV = normalize(-mv.xyz); gl_Position = projectionMatrix * mv; }`,
    fragmentShader: `${NOISE} uniform float uForma, uDano, uTempo, uImpU, uImpT, uA; uniform vec3 uCor; varying vec2 vUv; varying vec3 vN; varying vec3 vV;
      float hex(vec2 p){ p.x *= 1.1547; p.y += mod(floor(p.x), 2.0) * 0.5; p = abs(fract(p) - 0.5); return abs(max(p.x * 1.5 + p.y, p.y * 2.0) - 1.0); }
      void main(){
        vec2 p = vec2(vUv.x * 9.0, vUv.y * 6.0);
        float h = 1.0 - smoothstep(0.0, 0.07, hex(p));
        float fres = pow(1.0 - abs(dot(vN, vV)), 2.0);
        float n = vn(vUv * 14.0);
        float forma = step(n, uForma * 1.05);
        float borda = (1.0 - smoothstep(0.0, 0.05, abs(n - uForma))) * step(uForma, 0.999);
        float d = length(vec2((vUv.x - uImpU) * 2.6, (vUv.y - 0.55)));
        float onda = sin(d * 38.0 - uImpT * 0.03) * exp(-uImpT / 260.0) * exp(-d * 3.0);
        float rach = smoothstep(0.93, 0.97, fbm(vUv * vec2(18.0, 10.0))) * uDano * 1.8;
        float scan = 0.5 + 0.5 * sin(vUv.y * 80.0 - uTempo * 0.006);
        float base = 0.03 + 0.3 * h * (0.35 + 0.65 * fres) + 0.75 * fres + 0.1 * scan * h;
        vec3 c = uCor * (base + max(onda, 0.0) * 1.6) + vec3(1.0) * (rach + borda * 1.5);
        float al = (base * 0.75 + max(onda, 0.0) + rach + borda) * forma * uA * smoothstep(0.0, 0.12, vUv.y) * smoothstep(1.0, 0.85, vUv.y);
        gl_FragColor = vec4(c, al);
      }`,
  });
}
function matSetor() {
  return new THREE.ShaderMaterial({
    transparent: true, depthWrite: false, side: THREE.DoubleSide,
    uniforms: { uAb: { value: 1.57 }, uAtivo: { value: 0 }, uTempo: { value: 0 }, uA: { value: 1 } },
    vertexShader: `varying vec2 vP; void main(){ vP = position.xy; gl_Position = projectionMatrix*modelViewMatrix*vec4(position,1.0); }`,
    fragmentShader: `uniform float uAb, uAtivo, uTempo, uA; varying vec2 vP;
      void main(){ float r = length(vP); float a = abs(atan(vP.y, vP.x)); if (r > 1.55 || a > uAb * 0.5 + 0.02) discard;
        float lado = 1.0 - smoothstep(0.0, 0.025, abs(a - uAb * 0.5) * r);
        float arco = 1.0 - smoothstep(0.0, 0.03, abs(r - 1.5));
        float tracej = mix(step(0.5, fract(r * 6.0 - uTempo * 0.002)), 1.0, uAtivo);
        float fill = (0.06 + 0.14 * uAtivo) * smoothstep(1.5, 0.2, r);
        float lin = max(lado, arco) * tracej;
        gl_FragColor = vec4(mix(vec3(0.37, 0.95, 1.0), vec3(0.85, 1.0, 1.0), lin), (fill + lin * (0.5 + 0.5 * uAtivo)) * uA); }`,
  });
}

export class EfeitoGuarda {
  constructor(g, { qualidade }) {
    this.gd = g;
    this.seed = g.id * 3.7 + 2;
    const ab = THREE.MathUtils.degToRad(g.abertura || 90);
    this.ab = ab;
    this.g = new THREE.Group();
    this.setor = new THREE.Mesh(new THREE.CircleGeometry(1.55, 64), matSetor());
    this.setor.rotation.x = -Math.PI / 2; this.setor.position.y = 0.02; this.setor.material.uniforms.uAb.value = ab;
    const seg = Math.max(8, Math.round(48 * ab / (Math.PI * 2)));
    this.parede = new THREE.Mesh(new THREE.CylinderGeometry(0.78, 0.78, 2.0, seg, 8, true, Math.PI / 2 - ab / 2, ab), matBarreira());
    this.parede.position.y = 1.0;
    this.n = qualidade === 'alto' ? 60 : 18;
    this.cacos = new THREE.InstancedMesh(new THREE.TetrahedronGeometry(0.05, 0), new THREE.MeshBasicMaterial({ color: 0xbff8ff, transparent: true, opacity: 0.85, blending: THREE.AdditiveBlending, depthWrite: false }), this.n);
    this.cacos.frustumCulled = false; this.cacos.visible = false;
    this.convergir = nuvem(this.n, 0x8ff6ff, true);
    this.g.add(this.setor, this.parede, this.convergir);
    this._m = new THREE.Matrix4(); this._q = new THREE.Quaternion(); this._v = new THREE.Vector3(); this._s = new THREE.Vector3(); this._e = new THREE.Euler();
  }
  atualizar(t, { absorcoes = [], fim, cargasIniciais }) {
    const g = this.gd;
    const prep = Math.max(1, g.t_ativa - g.t);
    const k = clamp01((t - g.t) / prep);
    const ativo = t >= g.t_ativa;
    const su = this.setor.material.uniforms, bu = this.parede.material.uniforms;
    su.uAtivo.value = ativo ? 1 : 0.15 * k; su.uTempo.value = t;
    bu.uTempo.value = t; bu.uForma.value = ativo ? 1 : k * 0.9;
    const ult = absorcoes.filter((x) => x.t <= t).pop();
    bu.uImpT.value = ult ? t - ult.t : 9999;
    if (ult) { const ang = ult.angulo_rel ?? 0; bu.uImpU.value = 0.5 + ang / (THREE.MathUtils.radToDeg(this.ab) || 90); }
    const usados = absorcoes.filter((x) => x.t <= t).length;
    bu.uDano.value = clamp01(usados / Math.max(1, cargasIniciais || 1));
    // partículas que convergem durante o gesto de preparo
    const cp = this.convergir.geometry.attributes;
    for (let i = 0; i < this.n; i++) {
      const th = (rnd(this.seed, i) - 0.5) * this.ab, y = 0.2 + 1.6 * rnd(this.seed, i + 50);
      const r = 0.78 + 1.4 * (1 - k) * rnd(this.seed, i + 9);
      cp.position.setXYZ(i, Math.cos(th) * r, y, -Math.sin(th) * r);
      cp.aTam.setX(i, 0.04); cp.aA.setX(i, ativo ? 0 : 0.8 * k);
    }
    cp.position.needsUpdate = cp.aA.needsUpdate = cp.aTam.needsUpdate = true;
    // fim: rompida (estilhaços) ou expirada (dissolve)
    if (fim && t >= fim.t) {
      const s = (t - fim.t) / 1000;
      if (fim.motivo === 'rompida') {
        this.parede.visible = s < 0.06; bu.uA.value = 2;
        this.cacos.visible = s < 0.9;
        for (let i = 0; i < this.n; i++) {
          const th = (rnd(this.seed, i + 100) - 0.5) * this.ab, y0 = 0.2 + 1.6 * rnd(this.seed, i + 150), v = 2 + 3 * rnd(this.seed, i + 200);
          this._v.set(Math.cos(th) * (0.78 + v * s), y0 + 1.5 * s - 4.9 * s * s, -Math.sin(th) * (0.78 + v * s));
          this._e.set(s * 12 * rnd(this.seed, i), s * 9, s * 5); this._q.setFromEuler(this._e); this._s.setScalar(Math.max(0, 1 - s / 0.9));
          this._m.compose(this._v, this._q, this._s); this.cacos.setMatrixAt(i, this._m);
        }
        this.cacos.instanceMatrix.needsUpdate = true;
        if (!this.cacos.parent) this.g.add(this.cacos);
      } else { bu.uForma.value = clamp01(1 - s / 0.3); }
      su.uA.value = clamp01(1 - s / 0.3);
    } else { this.parede.visible = true; su.uA.value = 1; bu.uA.value = 1; }
  }
  vivo(t, fim) { return !fim || t - fim.t < 1000; }
}

// ---------- deslocamento: fita de energia, marca de fuligem no chão, poeira e faíscas ----------
function fita(pontos, alt, larg) {
  const n = pontos.length;
  const pos = new Float32Array(n * 2 * 3), uv = new Float32Array(n * 2 * 2), idx = [];
  for (let i = 0; i < n; i++) {
    const p = pontos[i], q = pontos[Math.min(n - 1, i + 1)], o = pontos[Math.max(0, i - 1)];
    const dx = q.x - o.x, dz = q.z - o.z, L = Math.hypot(dx, dz) || 1;
    const nx = -dz / L * larg, nz = dx / L * larg;
    pos.set([p.x + nx, alt, p.z + nz, p.x - nx, alt, p.z - nz], i * 6);
    uv.set([i / (n - 1), 0, i / (n - 1), 1], i * 4);
    if (i < n - 1) idx.push(i * 2, i * 2 + 1, i * 2 + 2, i * 2 + 1, i * 2 + 3, i * 2 + 2);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(pos, 3)); g.setAttribute('uv', new THREE.BufferAttribute(uv, 2)); g.setIndex(idx);
  return g;
}
export class EfeitoRastro {
  constructor(mv, pontos, { cor = 0x8fe8ff, qualidade = 'alto', agua = false } = {}) {
    this.mv = mv;
    this.seed = (mv.t0 % 997) + 3;
    this.g = new THREE.Group();
    this.mat = new THREE.ShaderMaterial({
      transparent: true, depthWrite: false, side: THREE.DoubleSide, blending: THREE.AdditiveBlending,
      uniforms: { uCab: { value: 0 }, uA: { value: 1 }, uCor: { value: new THREE.Color(cor) } },
      vertexShader: `varying vec2 vUv; void main(){ vUv = uv; gl_Position = projectionMatrix*modelViewMatrix*vec4(position,1.0); }`,
      fragmentShader: `uniform float uCab, uA; uniform vec3 uCor; varying vec2 vUv; void main(){ if (vUv.x > uCab) discard; float atras = smoothstep(uCab - 0.75, uCab, vUv.x);
        float meio = 1.0 - abs(vUv.y - 0.5) * 2.0; gl_FragColor = vec4(uCor * (0.6 + meio), atras * meio * meio * 0.8 * uA); }`,
    });
    for (const [alt, larg] of [[0.95, 0.24], [0.55, 0.16], [1.35, 0.12]]) { const f = new THREE.Mesh(fita(pontos, alt, larg), this.mat); f.frustumCulled = false; this.g.add(f); }
    this.chao = new THREE.Mesh(fita(pontos, 0.016, 0.16), new THREE.ShaderMaterial({
      transparent: true, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -2,
      uniforms: { uCab: { value: 0 }, uA: { value: 1 }, uAgua: { value: agua ? 1 : 0 } },
      vertexShader: `varying vec2 vUv; void main(){ vUv = uv; gl_Position = projectionMatrix*modelViewMatrix*vec4(position,1.0); }`,
      fragmentShader: `${NOISE} uniform float uCab, uA, uAgua; varying vec2 vUv; void main(){ if (vUv.x > uCab) discard; float n = fbm(vec2(vUv.x * 30.0, vUv.y * 3.0));
        float m = (1.0 - abs(vUv.y - 0.5) * 2.0) * (0.5 + 0.5 * n); vec3 c = mix(vec3(0.03, 0.025, 0.02), vec3(0.8, 0.95, 1.0), uAgua); gl_FragColor = vec4(c, m * 0.6 * uA * (1.0 - uAgua * 0.7)); }`,
    }));
    this.chao.frustumCulled = false;
    this.g.add(this.chao);
    this.n = qualidade === 'alto' ? 34 : 12;
    this.poeira = nuvem(this.n * 2, agua ? 0xd8f0ff : 0x9a9488, agua);
    this.faiscas = nuvem(this.n, 0xffd9a0, true);
    this.g.add(this.poeira, this.faiscas);
    this.ini = pontos[0].clone(); this.fim = pontos[pontos.length - 1].clone();
    const d = this.fim.clone().sub(this.ini); d.y = 0; this.dir = d.lengthSq() > 1e-6 ? d.normalize() : new THREE.Vector3(1, 0, 0);
  }
  atualizar(t) {
    const mv = this.mv, u = Math.max(0, Math.min(1, (t - mv.t0) / Math.max(1, mv.t1 - mv.t0)));
    this.mat.uniforms.uCab.value = u;
    this.mat.uniforms.uA.value = t > mv.t1 ? Math.max(0, 1 - (t - mv.t1) / 350) : 1;
    this.chao.material.uniforms.uCab.value = u;
    this.chao.material.uniforms.uA.value = Math.max(0, Math.min(1, 1 - (t - mv.t1 - 1500) / 1500));
    // poeira: nuvem na arrancada e na chegada
    const pp = this.poeira.geometry.attributes;
    for (let i = 0; i < this.n * 2; i++) {
      const naChegada = i >= this.n, t0 = naChegada ? mv.t1 : mv.t0, s = (t - t0) / 1000;
      const base = naChegada ? this.fim : this.ini;
      const th = rnd(this.seed, i) * Math.PI * 2, v = 0.6 + 1.6 * rnd(this.seed, i + 40);
      const k = 1 - Math.exp(-Math.max(0, s) * 3);
      const empurra = naChegada ? 0.6 : -0.8;
      pp.position.setXYZ(i, base.x + Math.cos(th) * v * k * 0.5 + this.dir.x * empurra * k, 0.08 + 0.5 * rnd(this.seed, i + 80) * k, base.z + Math.sin(th) * v * k * 0.5 + this.dir.z * empurra * k);
      pp.aTam.setX(i, 0.18 + 0.5 * Math.max(0, s));
      pp.aA.setX(i, s >= 0 ? 0.28 * Math.exp(-s * 1.4) : 0);
    }
    pp.position.needsUpdate = pp.aTam.needsUpdate = pp.aA.needsUpdate = true;
    // faíscas do derrape na chegada
    const fp = this.faiscas.geometry.attributes;
    for (let i = 0; i < this.n; i++) {
      const s = (t - mv.t1) / 1000 - rnd(this.seed, i + 200) * 0.08;
      const lado = (rnd(this.seed, i + 300) - 0.5) * 2.2, v = 2 + 3 * rnd(this.seed, i + 400);
      const vx = this.dir.x * v + -this.dir.z * lado, vz = this.dir.z * v + this.dir.x * lado;
      const tt = Math.max(0, s);
      fp.position.setXYZ(i, this.fim.x + vx * tt * 0.6, Math.max(0.01, 0.05 + 1.6 * tt - 4.9 * tt * tt), this.fim.z + vz * tt * 0.6);
      fp.aTam.setX(i, 0.03);
      fp.aA.setX(i, s >= 0 && s < 0.4 ? 1 - s / 0.4 : 0);
    }
    fp.position.needsUpdate = fp.aTam.needsUpdate = fp.aA.needsUpdate = true;
  }
  vivo(t) { return t - this.mv.t1 < 3000; }
}

// ---------- ambiente: poeira no feixe da claraboia e goteiras no canal ----------
export class Ambiente {
  constructor(qualidade) {
    this.g = new THREE.Group();
    this.n = qualidade === 'alto' ? 380 : 90;
    this.poeira = nuvem(this.n, 0xfff2d8, true);
    this.poeira.material.uniforms.uEscala.value = 260;
    this.g.add(this.poeira);
    this.gotas = [];
    for (const [x, z, per, fase] of [[-3.2, 0.2, 2.7, 0], [5.4, -0.35, 3.4, 1.1], [-9.5, 0.4, 4.1, 2.3]]) {
      const gota = new THREE.Mesh(new THREE.SphereGeometry(0.012, 8, 6), new THREE.MeshPhysicalMaterial({ color: 0xcfe8ff, roughness: 0, transparent: true, opacity: 0.9 }));
      gota.scale.y = 2.2;
      const anel = new THREE.Mesh(new THREE.PlaneGeometry(1.2, 1.2), matOnda()); anel.rotation.x = -Math.PI / 2; anel.position.set(x, -0.055, z); anel.material.uniforms.uCor.value.set(0xd8f0ff);
      this.g.add(gota, anel);
      this.gotas.push({ gota, anel, x, z, per, fase });
    }
  }
  atualizar(tSeg) {
    const a = this.poeira.geometry.attributes;
    for (let i = 0; i < this.n; i++) {
      const fy = (rnd(5, i) + tSeg * (0.01 + 0.02 * rnd(6, i))) % 1;
      const ang = rnd(7, i) * Math.PI * 2, rad = Math.sqrt(rnd(8, i)) * (1.2 + 0.6 * fy);
      a.position.setXYZ(i, -0.25 + Math.cos(ang) * rad + 0.12 * Math.sin(tSeg * 0.3 + i), 0.2 + fy * 4.6, -0.15 + Math.sin(ang) * rad + 0.12 * Math.cos(tSeg * 0.27 + i));
      a.aTam.setX(i, 0.012 + 0.014 * rnd(9, i));
      a.aA.setX(i, 0.5 * Math.sin(fy * Math.PI) * (0.4 + 0.6 * Math.sin(tSeg * 2 + i)));
    }
    a.position.needsUpdate = a.aTam.needsUpdate = a.aA.needsUpdate = true;
    for (const d of this.gotas) {
      const c = ((tSeg + d.fase) % d.per) / d.per * d.per;
      const queda = 0.85;
      if (c < queda) { d.gota.visible = true; const k = c / queda; d.gota.position.set(d.x, 4.95 - 5.0 * k * k, d.z); } else d.gota.visible = false;
      const s = c - queda;
      const u = d.anel.material.uniforms;
      u.uRaio.value = 0.05 + 0.35 * Math.max(0, s); u.uLarg.value = 0.015 + 0.02 * Math.max(0, s); u.uA.value = s > 0 ? 0.8 * Math.exp(-s * 2.2) : 0;
    }
  }
}
