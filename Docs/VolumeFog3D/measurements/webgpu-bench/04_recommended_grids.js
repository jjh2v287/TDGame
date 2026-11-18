const FB = window.__fb; const out = [];
if (!FB || !FB.runSim) { throw new Error('bench not defined'); }
const cfgs = [
  {nx: 256, ny: 256, nz: 8, jac: 4, mode: 'dense'},
  {nx: 256, ny: 256, nz: 8, jac: 8, mode: 'dense'},
  {nx: 256, ny: 256, nz: 8, jac: 12, mode: 'dense'},
  {nx: 256, ny: 256, nz: 16, jac: 4, mode: 'dense'},
  {nx: 256, ny: 256, nz: 16, jac: 12, mode: 'dense'},
  {nx: 256, ny: 256, nz: 16, jac: 12, mode: 'sparse', frac: 0.25},
  {nx: 256, ny: 256, nz: 16, jac: 12, mode: 'sparse', frac: 0.1},
  {nx: 256, ny: 256, nz: 8, jac: 8, mode: 'sparse', frac: 0.25},
];
for (const c of cfgs) { try { out.push(await FB.runSim(Object.assign({targetMs: 600, reps: 7}, c))); } catch (e) { out.push({cfg: c, error: String(e && e.message || e).slice(0, 500)}); } }
out.map(r => r.error ? r : ({grid: r.grid, cells: r.cells, solidPct: r.solidPct, mode: r.mode, frac: r.frac, activeCells: r.activeCells, jac: r.jac, steps: r.steps, gpuMin: r.gpuMin, gpuMed: r.gpuMed, gpuMax: r.gpuMax, msPerMcellPass: r.msPerMcellPass, errors: r.errors}))