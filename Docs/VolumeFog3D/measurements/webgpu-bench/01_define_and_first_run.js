window.__fb = {};
(() => {
const FB = window.__fb;
const TU = GPUTextureUsage, BU = GPUBufferUsage;
FB.init = async () => {
  if (FB.device) return {already: true, hasTs: FB.hasTs};
  const adapter = await navigator.gpu.requestAdapter({powerPreference: 'high-performance'});
  const feats = ['timestamp-query', 'float32-filterable'].filter(f => adapter.features.has(f));
  const dev = await adapter.requestDevice({requiredFeatures: feats, requiredLimits: {maxComputeInvocationsPerWorkgroup: 512}});
  FB.device = dev; FB.hasTs = feats.includes('timestamp-query'); FB.errors = [];
  dev.addEventListener('uncapturederror', e => FB.errors.push(String(e.error && e.error.message).slice(0, 400)));
  if (FB.hasTs) {
    FB.qs = dev.createQuerySet({type: 'timestamp', count: 2});
    FB.resolveBuf = dev.createBuffer({size: 16, usage: BU.QUERY_RESOLVE | BU.COPY_SRC});
    FB.readBuf = dev.createBuffer({size: 16, usage: BU.COPY_DST | BU.MAP_READ});
  }
  return {feats, vendor: adapter.info && adapter.info.vendor, arch: adapter.info && adapter.info.architecture};
};
FB.timeIt = async (encodeFn) => {
  const dev = FB.device;
  const enc = dev.createCommandEncoder();
  const desc = FB.hasTs ? {timestampWrites: {querySet: FB.qs, beginningOfPassWriteIndex: 0, endOfPassWriteIndex: 1}} : {};
  const pass = enc.beginComputePass(desc);
  encodeFn(pass);
  pass.end();
  if (FB.hasTs) { enc.resolveQuerySet(FB.qs, 0, 2, FB.resolveBuf, 0); enc.copyBufferToBuffer(FB.resolveBuf, 0, FB.readBuf, 0, 16); }
  const t0 = performance.now();
  dev.queue.submit([enc.finish()]);
  await dev.queue.onSubmittedWorkDone();
  const wall = performance.now() - t0;
  let gpu = null;
  if (FB.hasTs) { await FB.readBuf.mapAsync(GPUMapMode.READ); const a = new BigInt64Array(FB.readBuf.getMappedRange().slice(0)); FB.readBuf.unmap(); gpu = Number(a[1] - a[0]) / 1e6; }
  return {wall, gpu};
};
FB.mkPipe = async (code) => {
  const dev = FB.device;
  const module = dev.createShaderModule({code});
  const info = await module.getCompilationInfo();
  const errs = info.messages.filter(m => m.type === 'error').map(m => m.lineNum + ':' + m.message);
  if (errs.length) throw new Error('WGSL ' + errs.join(' | ').slice(0, 700));
  return dev.createComputePipelineAsync({layout: 'auto', compute: {module, entryPoint: 'main'}});
};
const declP = `struct Stamp { posRad: vec4f, velStr: vec4f };
struct Params { dims: vec4<u32>, invDims: vec4f, misc: vec4f, stamps: array<Stamp, 8> };
@group(0) @binding(0) var<uniform> P: Params;
`;
const declB = `@group(0) @binding(7) var<storage, read> bricks: array<u32>;
`;
const pre = (mode, needP) => ((mode === 'dense' || needP) ? declP : '') + (mode === 'sparse' ? declB : '');
const entry = (mode) => mode === 'dense' ? `@compute @workgroup_size(8,8,4)
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  if (any(gid >= P.dims.xyz)) { return; }
  let cell = vec3<i32>(gid);
` : `@compute @workgroup_size(8,8,8)
fn main(@builtin(workgroup_id) wid: vec3<u32>, @builtin(local_invocation_id) lid: vec3<u32>) {
  let pk = bricks[wid.x];
  let cell = vec3<i32>(vec3<u32>(pk & 1023u, (pk >> 10u) & 1023u, (pk >> 20u) & 1023u) * 8u + lid);
`;
const nb6 = (tex, ch, solidVal) => `
  let xp = select(textureLoad(${tex}, cell + vec3<i32>(1,0,0), 0).${ch[0]}, ${solidVal}, (m & 1u) != 0u);
  let xm = select(textureLoad(${tex}, cell - vec3<i32>(1,0,0), 0).${ch[0]}, ${solidVal}, (m & 2u) != 0u);
  let yp = select(textureLoad(${tex}, cell + vec3<i32>(0,1,0), 0).${ch[1]}, ${solidVal}, (m & 4u) != 0u);
  let ym = select(textureLoad(${tex}, cell - vec3<i32>(0,1,0), 0).${ch[1]}, ${solidVal}, (m & 8u) != 0u);
  let zp = select(textureLoad(${tex}, cell + vec3<i32>(0,0,1), 0).${ch[2]}, ${solidVal}, (m & 16u) != 0u);
  let zm = select(textureLoad(${tex}, cell - vec3<i32>(0,0,1), 0).${ch[2]}, ${solidVal}, (m & 32u) != 0u);
`;
FB.advWGSL = (mode) => pre(mode, true) + `
@group(0) @binding(1) var src: texture_3d<f32>;
@group(0) @binding(2) var samp: sampler;
@group(0) @binding(3) var nbr: texture_3d<u32>;
@group(0) @binding(4) var dst: texture_storage_3d<rgba16float, write>;
` + entry(mode) + `
  let m = textureLoad(nbr, cell, 0).r;
  if ((m & 64u) != 0u) { textureStore(dst, cell, vec4f(0.0)); return; }
  let cur = textureLoad(src, cell, 0);
  let wp = vec3f(cell) + 0.5;
  let back = (wp - cur.xyz * P.invDims.w) * P.invDims.xyz;
  let q = textureSampleLevel(src, samp, back, 0.0);
  var v = q.xyz; var d = q.w;
  let n = P.dims.w;
  for (var i = 0u; i < n; i++) {
    let s = P.stamps[i];
    let r = wp - s.posRad.xyz;
    let f = exp(-dot(r, r) * s.posRad.w);
    v += s.velStr.xyz * f;
    d -= d * min(1.0, s.velStr.w * f);
  }
  d = mix(d, 1.0, P.misc.x);
  v *= P.misc.y;
  textureStore(dst, cell, vec4f(v, d));
}`;
FB.divWGSL = (mode) => pre(mode, false) + `
@group(0) @binding(1) var vel: texture_3d<f32>;
@group(0) @binding(3) var nbr: texture_3d<u32>;
@group(0) @binding(4) var dst: texture_storage_3d<r32float, write>;
` + entry(mode) + `
  let m = textureLoad(nbr, cell, 0).r;` + nb6('vel', ['x', 'y', 'z'], '0.0') + `
  textureStore(dst, cell, vec4f(0.5 * ((xp - xm) + (yp - ym) + (zp - zm)), 0.0, 0.0, 0.0));
}`;
FB.jacWGSL = (mode) => pre(mode, false) + `
@group(0) @binding(1) var pin: texture_3d<f32>;
@group(0) @binding(2) var dvg: texture_3d<f32>;
@group(0) @binding(3) var nbr: texture_3d<u32>;
@group(0) @binding(4) var dst: texture_storage_3d<r32float, write>;
` + entry(mode) + `
  let m = textureLoad(nbr, cell, 0).r;
  let pc = textureLoad(pin, cell, 0).r;` + nb6('pin', ['r', 'r', 'r'], 'pc') + `
  let d = textureLoad(dvg, cell, 0).r;
  textureStore(dst, cell, vec4f((xp + xm + yp + ym + zp + zm - d) * (1.0 / 6.0), 0.0, 0.0, 0.0));
}`;
FB.gradWGSL = (mode) => pre(mode, false) + `
@group(0) @binding(1) var vel: texture_3d<f32>;
@group(0) @binding(2) var prs: texture_3d<f32>;
@group(0) @binding(3) var nbr: texture_3d<u32>;
@group(0) @binding(4) var dst: texture_storage_3d<rgba16float, write>;
` + entry(mode) + `
  let m = textureLoad(nbr, cell, 0).r;
  let q = textureLoad(vel, cell, 0);
  let pc = textureLoad(prs, cell, 0).r;` + nb6('prs', ['r', 'r', 'r'], 'pc') + `
  var v = q.xyz - 0.5 * vec3f(xp - xm, yp - ym, zp - zm);
  if ((m & 1u) != 0u) { v.x = min(v.x, 0.0); }
  if ((m & 2u) != 0u) { v.x = max(v.x, 0.0); }
  if ((m & 4u) != 0u) { v.y = min(v.y, 0.0); }
  if ((m & 8u) != 0u) { v.y = max(v.y, 0.0); }
  if ((m & 16u) != 0u) { v.z = min(v.z, 0.0); }
  if ((m & 32u) != 0u) { v.z = max(v.z, 0.0); }
  textureStore(dst, cell, vec4f(v, q.w));
}`;
FB.runSim = async (cfg) => {
  const dev = FB.device;
  const {nx, ny, nz, jac = 12, mode = 'dense', frac = 1, kernel = 'step', targetMs = 150, reps = 5} = cfg;
  FB.errors.length = 0;
  const mk3 = (format, usage) => dev.createTexture({size: [nx, ny, nz], dimension: '3d', format, usage});
  const SU = TU.STORAGE_BINDING | TU.TEXTURE_BINDING;
  const velA = mk3('rgba16float', SU), velB = mk3('rgba16float', SU);
  const pA = mk3('r32float', SU), pB = mk3('r32float', SU), dvT = mk3('r32float', SU);
  const nbrT = mk3('r8uint', TU.TEXTURE_BINDING | TU.COPY_DST);
  const N = nx * ny * nz;
  const solid = new Uint8Array(N);
  const hOf = (x, y) => nz * 0.22 + nz * 0.10 * Math.sin(x * 12.566 / nx) * Math.cos(y * 9.42 / ny);
  for (let y = 0; y < ny; y++) for (let x = 0; x < nx; x++) {
    const h = Math.floor(hOf(x, y));
    for (let z = 0; z <= h && z < nz; z++) solid[x + nx * (y + ny * z)] = 1;
  }
  for (let b = 0; b < 12; b++) {
    const cx = Math.floor(nx * (0.15 + 0.7 * ((b * 37) % 100) / 100)), cy = Math.floor(ny * (0.15 + 0.7 * ((b * 61) % 100) / 100));
    const s = 3 + (b % 4) * 2, hgt = 4 + (b % 5) * 2, h0 = Math.floor(hOf(cx, cy));
    for (let z = h0; z < Math.min(nz, h0 + hgt); z++) for (let y = Math.max(0, cy - s); y < Math.min(ny, cy + s); y++) for (let x = Math.max(0, cx - s); x < Math.min(nx, cx + s); x++) solid[x + nx * (y + ny * z)] = 1;
  }
  let solidCount = 0; for (let i = 0; i < N; i++) solidCount += solid[i];
  const nbr = new Uint8Array(N);
  const S = (x, y, z) => (x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz) ? 1 : solid[x + nx * (y + ny * z)];
  for (let z = 0; z < nz; z++) for (let y = 0; y < ny; y++) for (let x = 0; x < nx; x++) {
    nbr[x + nx * (y + ny * z)] = S(x + 1, y, z) | (S(x - 1, y, z) << 1) | (S(x, y + 1, z) << 2) | (S(x, y - 1, z) << 3) | (S(x, y, z + 1) << 4) | (S(x, y, z - 1) << 5) | (solid[x + nx * (y + ny * z)] << 6);
  }
  dev.queue.writeTexture({texture: nbrT}, nbr, {bytesPerRow: nx, rowsPerImage: ny}, [nx, ny, nz]);
  const bx = nx / 8, by = ny / 8, bz = nz / 8, nb = bx * by * bz;
  const zb = (nz * 0.22) / 8;
  const keys = [];
  for (let z = 0; z < bz; z++) for (let y = 0; y < by; y++) for (let x = 0; x < bx; x++) {
    const dx = x + 0.5 - bx / 2, dy = y + 0.5 - by / 2, dz = (z + 0.5 - zb) * 4;
    keys.push([dx * dx + dy * dy + dz * dz, x | (y << 10) | (z << 20)]);
  }
  keys.sort((a, b) => a[0] - b[0]);
  const K = Math.max(1, Math.round(nb * frac));
  const brickArr = new Uint32Array(K);
  for (let i = 0; i < K; i++) brickArr[i] = keys[i][1];
  const brickBuf = dev.createBuffer({size: K * 4, usage: BU.STORAGE | BU.COPY_DST});
  dev.queue.writeBuffer(brickBuf, 0, brickArr);
  const ub = new ArrayBuffer(304), u32 = new Uint32Array(ub), f32 = new Float32Array(ub);
  u32[0] = nx; u32[1] = ny; u32[2] = nz; u32[3] = 8;
  f32[4] = 1 / nx; f32[5] = 1 / ny; f32[6] = 1 / nz; f32[7] = 1.0;
  f32[8] = 0.02; f32[9] = 0.995;
  for (let i = 0; i < 8; i++) {
    const o = 12 + i * 8, a = i * 0.785;
    f32[o] = nx / 2 + Math.cos(a) * nx * 0.12; f32[o + 1] = ny / 2 + Math.sin(a) * ny * 0.12; f32[o + 2] = nz * 0.22 + 6; f32[o + 3] = 1 / 50;
    f32[o + 4] = -Math.sin(a) * 0.6; f32[o + 5] = Math.cos(a) * 0.6; f32[o + 6] = 0.1; f32[o + 7] = 0.2;
  }
  const ubuf = dev.createBuffer({size: 304, usage: BU.UNIFORM | BU.COPY_DST});
  dev.queue.writeBuffer(ubuf, 0, ub);
  const advP = await FB.mkPipe(FB.advWGSL(mode)), divP = await FB.mkPipe(FB.divWGSL(mode)), jacP = await FB.mkPipe(FB.jacWGSL(mode)), gradP = await FB.mkPipe(FB.gradWGSL(mode));
  const samp = dev.createSampler({magFilter: 'linear', minFilter: 'linear', addressModeU: 'clamp-to-edge', addressModeV: 'clamp-to-edge', addressModeW: 'clamp-to-edge'});
  const E = (binding, resource) => ({binding, resource});
  const base = (needP) => [...((mode === 'dense' || needP) ? [E(0, {buffer: ubuf})] : []), ...(mode === 'sparse' ? [E(7, {buffer: brickBuf})] : [])];
  const bg = (pipe, entries) => dev.createBindGroup({layout: pipe.getBindGroupLayout(0), entries});
  const nV = nbrT.createView();
  const advAB = bg(advP, [...base(true), E(1, velA.createView()), E(2, samp), E(3, nV), E(4, velB.createView())]);
  const advBA = bg(advP, [...base(true), E(1, velB.createView()), E(2, samp), E(3, nV), E(4, velA.createView())]);
  const divBG = bg(divP, [...base(false), E(1, velB.createView()), E(3, nV), E(4, dvT.createView())]);
  const jacAB = bg(jacP, [...base(false), E(1, pA.createView()), E(2, dvT.createView()), E(3, nV), E(4, pB.createView())]);
  const jacBA = bg(jacP, [...base(false), E(1, pB.createView()), E(2, dvT.createView()), E(3, nV), E(4, pA.createView())]);
  const gradBG = bg(gradP, [...base(false), E(1, velB.createView()), E(2, pA.createView()), E(3, nV), E(4, velA.createView())]);
  const disp = (pass) => mode === 'dense' ? pass.dispatchWorkgroups(nx / 8, ny / 8, nz / 4) : pass.dispatchWorkgroups(K, 1, 1);
  const encode = (pass, steps) => {
    for (let s = 0; s < steps; s++) {
      if (kernel === 'step') {
        pass.setPipeline(advP); pass.setBindGroup(0, advAB); disp(pass);
        pass.setPipeline(divP); pass.setBindGroup(0, divBG); disp(pass);
        pass.setPipeline(jacP);
        for (let j = 0; j < jac; j++) { pass.setBindGroup(0, (j % 2 === 0) ? jacAB : jacBA); disp(pass); }
        pass.setPipeline(gradP); pass.setBindGroup(0, gradBG); disp(pass);
      } else if (kernel === 'advect') {
        pass.setPipeline(advP); pass.setBindGroup(0, (s % 2 === 0) ? advAB : advBA); disp(pass);
      } else {
        pass.setPipeline(jacP); pass.setBindGroup(0, (s % 2 === 0) ? jacAB : jacBA); disp(pass);
      }
    }
  };
  await FB.timeIt(p => encode(p, 2));
  const est = await FB.timeIt(p => encode(p, 2));
  const per = Math.max(0.005, (est.gpu ?? est.wall) / 2);
  const steps = Math.max(2, Math.min(kernel === 'step' ? 60 : 600, Math.round(targetMs / per)));
  const g = [], w = [];
  for (let r = 0; r < reps; r++) { const t = await FB.timeIt(p => encode(p, steps)); g.push((t.gpu ?? t.wall) / steps); w.push(t.wall / steps); }
  g.sort((a, b) => a - b); w.sort((a, b) => a - b);
  [velA, velB, pA, pB, dvT, nbrT].forEach(t => t.destroy()); brickBuf.destroy(); ubuf.destroy();
  const passes = kernel === 'step' ? 3 + jac : 1;
  const activeCells = mode === 'dense' ? N : K * 512;
  const med = g[Math.floor(reps / 2)];
  return {grid: nx + 'x' + ny + 'x' + nz, cells: N, solidPct: +(100 * solidCount / N).toFixed(1), mode, frac, kernel, jac, activeCells, steps, gpuMin: +g[0].toFixed(4), gpuMed: +med.toFixed(4), gpuMax: +g[reps - 1].toFixed(4), wallMed: +w[Math.floor(reps / 2)].toFixed(4), msPerMcellPass: +(med / passes / (activeCells / 1e6)).toFixed(4), hasTs: FB.hasTs, errors: FB.errors.slice(0, 3)};
};
FB.marchWGSL = `struct R { res: vec4<u32>, ro: vec4f, fwd: vec4f, right: vec4f, up: vec4f, dims: vec4f, tanHalf: vec4f };
@group(0) @binding(0) var<uniform> U: R;
@group(0) @binding(1) var vol: texture_3d<f32>;
@group(0) @binding(2) var samp: sampler;
@group(0) @binding(3) var outT: texture_storage_2d<rgba16float, write>;
@compute @workgroup_size(8,8,1)
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  if (gid.x >= U.res.x || gid.y >= U.res.y) { return; }
  let uv = (vec2f(gid.xy) + 0.5) / vec2f(U.res.xy) * 2.0 - 1.0;
  let dir = normalize(U.fwd.xyz + U.right.xyz * (uv.x * U.tanHalf.x) - U.up.xyz * (uv.y * U.tanHalf.y));
  let inv = 1.0 / dir;
  let ta = (vec3f(0.0) - U.ro.xyz) * inv;
  let tb = (U.dims.xyz - U.ro.xyz) * inv;
  let tmin = min(ta, tb); let tmax = max(ta, tb);
  let t0 = max(max(tmin.x, tmin.y), max(tmin.z, 0.0));
  let t1 = min(min(tmax.x, tmax.y), tmax.z);
  var T = 1.0; var L = 0.0;
  if (t1 > t0) {
    let n = U.res.z;
    let dtt = (t1 - t0) / f32(n);
    for (var i = 0u; i < n; i++) {
      let p = U.ro.xyz + dir * (t0 + (f32(i) + 0.5) * dtt);
      let s = textureSampleLevel(vol, samp, p / U.dims.xyz, 0.0);
      let a = s.w * U.dims.w * dtt;
      L += T * a;
      T *= exp(-a);
    }
  }
  textureStore(outT, vec2<i32>(gid.xy), vec4f(L, L, L, 1.0 - T));
}`;
FB.runMarch = async (cfg) => {
  const dev = FB.device;
  const {nx = 256, ny = 256, nz = 32, W = 960, H = 540, steps = 24, view = 'top', reps = 5, cpm = 8} = cfg;
  FB.errors.length = 0;
  const vol = dev.createTexture({size: [nx, ny, nz], dimension: '3d', format: 'rgba16float', usage: TU.TEXTURE_BINDING | TU.COPY_DST});
  const data = new Uint16Array(nx * ny * nz * 4);
  for (let z = 0; z < nz; z++) for (let y = 0; y < ny; y++) for (let x = 0; x < nx; x++) {
    const i = 4 * (x + nx * (y + ny * z));
    const inFog = z >= nz * 0.2 && z < nz * 0.9;
    data[i + 3] = inFog ? ((((x >> 3) + (y >> 3) + (z >> 2)) & 1) ? 0x3C00 : 0x3800) : 0;
  }
  dev.queue.writeTexture({texture: vol}, data, {bytesPerRow: nx * 8, rowsPerImage: ny}, [nx, ny, nz]);
  const outT = dev.createTexture({size: [W, H], format: 'rgba16float', usage: TU.STORAGE_BINDING | TU.TEXTURE_BINDING});
  const ub = new ArrayBuffer(112), u32 = new Uint32Array(ub), f32 = new Float32Array(ub);
  u32[0] = W; u32[1] = H; u32[2] = steps; u32[3] = 0;
  const g0 = nz * 0.22;
  let ro, fwd, up;
  if (view === 'top') { ro = [nx / 2, ny / 2 - 4 * cpm, g0 + 6.93 * cpm]; fwd = [0, 0.5, -0.866]; up = [0, 0.866, 0.5]; }
  else { ro = [nx / 2, ny * 0.2, g0 + 1.6 * cpm]; fwd = [0, 0.985, -0.174]; up = [0, 0.174, 0.985]; }
  f32.set([ro[0], ro[1], ro[2], 0], 4); f32.set([fwd[0], fwd[1], fwd[2], 0], 8); f32.set([1, 0, 0, 0], 12); f32.set([up[0], up[1], up[2], 0], 16); f32.set([nx, ny, nz, 0.05], 20); f32.set([1, H / W, 0, 0], 24);
  const ubuf = dev.createBuffer({size: 112, usage: BU.UNIFORM | BU.COPY_DST});
  dev.queue.writeBuffer(ubuf, 0, ub);
  const pipe = await FB.mkPipe(FB.marchWGSL);
  const samp = dev.createSampler({magFilter: 'linear', minFilter: 'linear'});
  const bgp = dev.createBindGroup({layout: pipe.getBindGroupLayout(0), entries: [{binding: 0, resource: {buffer: ubuf}}, {binding: 1, resource: vol.createView()}, {binding: 2, resource: samp}, {binding: 3, resource: outT.createView()}]});
  const encode = (pass, n) => { pass.setPipeline(pipe); pass.setBindGroup(0, bgp); for (let i = 0; i < n; i++) pass.dispatchWorkgroups(Math.ceil(W / 8), Math.ceil(H / 8), 1); };
  await FB.timeIt(p => encode(p, 2));
  const est = await FB.timeIt(p => encode(p, 2));
  const per = Math.max(0.005, (est.gpu ?? est.wall) / 2);
  const n = Math.max(2, Math.min(400, Math.round(120 / per)));
  const g = [];
  for (let r = 0; r < reps; r++) { const t = await FB.timeIt(p => encode(p, n)); g.push((t.gpu ?? t.wall) / n); }
  g.sort((a, b) => a - b);
  vol.destroy(); outT.destroy(); ubuf.destroy();
  return {view, vol: nx + 'x' + ny + 'x' + nz, W, H, steps, frames: n, gpuMin: +g[0].toFixed(4), gpuMed: +g[Math.floor(reps / 2)].toFixed(4), errors: FB.errors.slice(0, 3)};
};
})();
const __init = await window.__fb.init();
let __first;
try { __first = await window.__fb.runSim({nx: 128, ny: 128, nz: 32, jac: 12, mode: 'dense'}); } catch (e) { __first = {error: String(e && e.message || e).slice(0, 900)}; }
({init: __init, first: __first, errors: window.__fb.errors})