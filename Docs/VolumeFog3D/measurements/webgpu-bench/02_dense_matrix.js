const FB = window.__fb; const out = [];
const cfgs = [
  {nx: 128, ny: 128, nz: 32, jac: 12, mode: 'dense'},
  {nx: 256, ny: 256, nz: 32, jac: 12, mode: 'dense'},
  {nx: 256, ny: 256, nz: 64, jac: 12, mode: 'dense'},
  {nx: 256, ny: 256, nz: 32, jac: 4, mode: 'dense'},
  {nx: 256, ny: 256, nz: 32, jac: 24, mode: 'dense'},
  {nx: 256, ny: 256, nz: 32, kernel: 'advect', mode: 'dense'},
  {nx: 256, ny: 256, nz: 32, kernel: 'jacobi', mode: 'dense'},
];
for (const c of cfgs) { try { out.push(await FB.runSim(Object.assign({targetMs: 600, reps: 7}, c))); } catch (e) { out.push({cfg: c, error: String(e && e.message || e).slice(0, 500)}); } }
out.map(r => r.error ? r : ({grid: r.grid, mode: r.mode, kernel: r.kernel, jac: r.jac, steps: r.steps, gpuMin: r.gpuMin, gpuMed: r.gpuMed, gpuMax: r.gpuMax, wallMed: r.wallMed, msPerMcellPass: r.msPerMcellPass, errors: r.errors}))