const FB = window.__fb; const out = [];
const cfgs = [
  {nx: 256, ny: 256, nz: 32, jac: 12, mode: 'sparse', frac: 1.0},
  {nx: 256, ny: 256, nz: 32, jac: 12, mode: 'sparse', frac: 0.5},
  {nx: 256, ny: 256, nz: 32, jac: 12, mode: 'sparse', frac: 0.25},
  {nx: 256, ny: 256, nz: 32, jac: 12, mode: 'sparse', frac: 0.1},
  {nx: 256, ny: 256, nz: 32, jac: 12, mode: 'sparse', frac: 0.05},
  {nx: 256, ny: 256, nz: 64, jac: 12, mode: 'sparse', frac: 0.1},
  {nx: 512, ny: 512, nz: 64, jac: 12, mode: 'sparse', frac: 0.03},
];
for (const c of cfgs) { try { out.push(await FB.runSim(Object.assign({targetMs: 600, reps: 7}, c))); } catch (e) { out.push({cfg: c, error: String(e && e.message || e).slice(0, 500)}); } }
const sim = out.map(r => r.error ? r : ({grid: r.grid, mode: r.mode, frac: r.frac, activeCells: r.activeCells, jac: r.jac, steps: r.steps, gpuMin: r.gpuMin, gpuMed: r.gpuMed, gpuMax: r.gpuMax, msPerMcellPass: r.msPerMcellPass, errors: r.errors}));
const march = [];
const mc = [
  {view: 'top', W: 960, H: 540, steps: 16}, {view: 'top', W: 960, H: 540, steps: 32}, {view: 'top', W: 1920, H: 1080, steps: 16}, {view: 'top', W: 1920, H: 1080, steps: 32},
  {view: 'third', W: 960, H: 540, steps: 32}, {view: 'third', W: 960, H: 540, steps: 64}, {view: 'third', W: 1920, H: 1080, steps: 32}, {view: 'third', W: 1920, H: 1080, steps: 64},
];
for (const c of mc) { try { march.push(await FB.runMarch(c)); } catch (e) { march.push({cfg: c, error: String(e && e.message || e).slice(0, 500)}); } }
({sim, march})