"""
실행 환경: Python 3.12 + numpy 2.2 + scipy 1.17 (추가 설치 없음), 저장소 루트에서 실행. 같은 폴더의 잠긴 fog3d_core.py 를 import 한다
실행 명령: 직접 실행하지 않는다. fog3d_gates_v2.py 가 import 한다
출력: 없음
상태: 1d 바람 옮기기 후보 N1·N2(Docs/VolumeFog3D/stage1/prereg-1d-design.md) 설계 실험판, 2026-11-14. advect 가 A1·A2·A3 이면 잠긴 판 그대로
"""
import math

import numpy as np

import fog3d_core as core
from fog3d_core import F32, PATH_CLAMP, CONNECTED_COUNT, CONNECTED_READ, BLOCK_CONNECTED, PATH_MAX_ITER, PATH_CAP_H_CM, PATH_CAP_V_CM

NEW_ADVECT = ("N1", "N2")
FACE_KINDS = ("u", "v", "w")


class Sim(core.Sim):

    def _advect(self):
        p = self.p
        if p.advect not in NEW_ADVECT:
            return super()._advect()
        if not self.v3:
            raise ValueError("N1·N2 는 식 3판 전용")
        dt = p.dt
        X = self.xc[:, None, None]
        Y = self.yc[None, :, None]
        Z = self.zc[None, None, :]
        shape = self.u.shape
        center_vel = self.center_velocity()
        if p.solid in PATH_CLAMP:
            dep = self._path_clamp(center_vel, dt)
        else:
            d = self._clip_closed(-center_vel[0] * dt, -center_vel[1] * dt, -center_vel[2] * dt)
            dep = (X + d[0], Y + d[1], Z + d[2])
        st = self._setup("c", *self._clamp(*dep))
        zero = None
        if p.solid in CONNECTED_COUNT:
            self._disconnected = self._disconnected_corner(st)
            if p.solid in CONNECTED_READ:
                zero = self._disconnected
        if p.advect == "N1":
            newu = self._gather(self.corners8(self.u), st, zero)
            newv = self._gather(self.corners8(self.v), st, zero)
            neww = self._gather(self.corners8(self.w), st, zero)
        else:
            center_end = getattr(self, "_end_cell", None)
            newu, newv, neww = self._face_advect(dt)
            self._end_cell = center_end
        dep = tuple(np.broadcast_to(c, shape) for c in dep)
        cx, cy, cz = self._clamp(*dep)
        wd = math.exp(-p.wind_relax_per_sec * dt)
        band = self.band if p.band_on else 1.0
        self._advect_state(newu, newv, neww, (X, Y, Z), (cx, cy, cz), wd, band)
        self._count_props(st, zero)

    def _face_positions(self, kind):
        shape = self.u.shape
        X = self.xc[:, None, None]
        Y = self.yc[None, :, None]
        Z = self.zc[None, None, :]
        pos = {"u": (self.xf[:, None, None], Y, Z), "v": (X, self.yf[None, :, None], Z), "w": (X, Y, self.zt[None, None, :])}[kind]
        return tuple(np.broadcast_to(c, shape) for c in pos)

    def _face_velocity(self, kind, pos, up, vp, wp):
        cache = self.__dict__.setdefault("_face_setups", {})
        key = (kind, self.overlap)
        if key not in cache:
            cpos = self._clamp(*pos)
            cache[key] = (self._setup("u", *cpos), self._setup("v", *cpos), self._setup("w", *cpos))
        su, sv, sw = cache[key]
        return [self._gather(up, su), self._gather(vp, sv), self._gather(wp, sw)]

    def _face_advect(self, dt):
        up, vp, wp = self._padded()
        flats = {"u": up, "v": vp, "w": wp}
        out = []
        for axis, kind in enumerate(FACE_KINDS):
            pos = self._face_positions(kind)
            vel = self._face_velocity(kind, pos, up, vp, wp)
            if self.p.solid in PATH_CLAMP:
                sample = self._face_walk(axis, pos, vel, dt)
            else:
                sample = self._clamp(pos[0] - vel[0] * dt, pos[1] - vel[1] * dt, pos[2] - vel[2] * dt)
            sf = self._setup(kind, *sample)
            zero = None
            if self.p.solid in CONNECTED_READ and self.p.solid in BLOCK_CONNECTED:
                zero = self._face_disconnected(axis, kind, sample, sf)
            out.append(self._gather(flats[kind], sf, zero))
        return out

    def _face_start_cell(self, axis, start_index, start, target):
        n = (self.nx, self.ny, self.nz)[axis]
        upward = target > start
        return np.minimum(np.where(upward, start_index + 1, start_index), n - 1).astype(np.int64)

    def _face_walk(self, axis, pos, vel, dt):
        shape = self.u.shape
        start = list(pos)
        move = [-vel[0] * dt, -vel[1] * dt, -vel[2] * dt]
        caps = (PATH_CAP_H_CM, PATH_CAP_H_CM, PATH_CAP_V_CM)
        scale = np.ones(shape)
        for a in range(3):
            size = np.abs(move[a])
            nonzero = size > 0
            scale = np.minimum(scale, np.where(nonzero, caps[a] / np.where(nonzero, size, 1.0), 1.0))
        move = [m * scale for m in move]
        target = [np.broadcast_to(t, shape) for t in self._clamp(start[0] + move[0], start[1] + move[1], start[2] + move[2])]
        cell = [np.broadcast_to(np.arange(self.nx)[:, None, None], shape).astype(np.int64),
                np.broadcast_to(np.arange(self.ny)[None, :, None], shape).astype(np.int64),
                np.broadcast_to(np.arange(self.nz)[None, None, :], shape).astype(np.int64)]
        cell[axis] = self._face_start_cell(axis, cell[axis], start[axis], target[axis])
        sample, end, moves, capped = self._walk(start, cell, target, move)
        hist = self.__dict__.setdefault("face_path_hist", np.zeros(PATH_MAX_ITER + 1, np.int64))
        hist += np.bincount(moves[self.fluid].ravel(), minlength=PATH_MAX_ITER + 1)[:PATH_MAX_ITER + 1]
        self.face_path_cap_hits = getattr(self, "face_path_cap_hits", 0) + int((capped & self.fluid).sum())
        self._end_cell = end
        return tuple(sample)

    def _walk(self, start, cell, target, move):
        shape = self.u.shape
        sign = [np.sign(target[a] - start[a]).astype(np.int64) for a in range(3)]
        sample = [t.copy() for t in target]
        cell = [c.copy() for c in cell]
        bound = [sign[a] == 0 for a in range(3)]
        local_span = self._local_target_span32(start, move) if self.p.path_t32 else None
        active = np.ones(shape, bool)
        moves = np.zeros(shape, np.int64)
        for _ in range(PATH_MAX_ITER):
            for a in range(3):
                candidate = active & ~bound[a]
                if not candidate.any():
                    continue
                hit = candidate & self._face_closed(a, cell, sign[a])
                if not hit.any():
                    continue
                center = self._cell_center(a, cell[a])
                passed = (target[a] - center) * sign[a] > 0
                sample[a] = np.where(hit, np.where(passed, center, target[a]), sample[a])
                bound[a] = bound[a] | hit
            t_min = np.full(shape, np.inf)
            first_axis = np.full(shape, -1, np.int64)
            for a in range(3):
                free = active & ~bound[a]
                edge = self._cell_boundary(a, cell[a], sign[a])
                if local_span is None:
                    span = target[a] - start[a]
                    t = np.where(free, (edge - start[a]) / np.where(free, span, 1.0), np.inf)
                else:
                    t = self._crossing_time32(edge - start[a], local_span[a], free)
                earlier = t < t_min
                t_min = np.where(earlier, t, t_min)
                first_axis = np.where(earlier, a, first_axis)
            active = active & (t_min <= 1.0)
            if not active.any():
                break
            for a in range(3):
                cell[a] = cell[a] + np.where(active & (first_axis == a), sign[a], 0)
            moves += active
        return sample, cell, moves, active

    def _face_disconnected(self, axis, kind, sample, sf):
        sc = self._setup("c", *sample)
        reach, inside, _ = self._block_reach(sc)
        base_c = sc[4]
        base_f = sf[4]
        n_axis = (self.nx, self.ny, self.nz)[axis]
        zero = np.zeros(reach.shape, bool)
        misses = 0
        for d in range(8):
            off = ((d >> 2) & 1, (d >> 1) & 1, d & 1)
            node = base_f[axis] + off[axis]
            in_block = (node >= base_c[axis]) & (node <= base_c[axis] + 1) & (node <= n_axis - 1)
            cell_axis = np.where(in_block, node, node - 1)
            o_axis = cell_axis - base_c[axis]
            misses += int(((o_axis < 0) | (o_axis > 1)).sum())
            o = [np.full(o_axis.shape, off[0]), np.full(o_axis.shape, off[1]), np.full(o_axis.shape, off[2])]
            o[axis] = np.clip(o_axis, 0, 1)
            corner = (o[0] * 4 + o[1] * 2 + o[2])[None]
            reached = np.take_along_axis(reach, corner, axis=0)[0]
            zero[d] = inside & ~reached
        stats = self.__dict__.setdefault("face_rule", {k: dict(cells=0, out_of_block=0, end_outside=0) for k in FACE_KINDS})
        stats[kind]["cells"] += int((zero.any(axis=0) & self.fluid).sum())
        stats[kind]["out_of_block"] += misses
        stats[kind]["end_outside"] += int((~inside & self.fluid).sum())
        if getattr(self, "prop_check", False):
            self._face_prop_check(kind, sf, zero)
        return zero

    def _face_prop_check(self, kind, sf, zero):
        from scipy import ndimage
        labels, _ = ndimage.label(self.fluid)
        end = self._end_cell
        end_label = labels[end[0], end[1], end[2]]
        axis = FACE_KINDS.index(kind)
        idx, _, _, _, base = sf
        w = self._base_weights(sf)
        n_axis = (self.nx, self.ny, self.nz)[axis]
        bad = 0
        for d in range(8):
            off = ((d >> 2) & 1, (d >> 1) & 1, d & 1)
            node = [base[a] + off[a] for a in range(3)]
            lo = [node[a] for a in range(3)]
            hi = [node[a] for a in range(3)]
            lo[axis] = np.clip(node[axis] - 1, 0, n_axis - 1)
            hi[axis] = np.clip(node[axis], 0, n_axis - 1)
            open_face = (node[axis] >= 1) & (node[axis] <= (n_axis if kind == "w" else n_axis - 1))
            lab = labels[lo[0], lo[1], lo[2]]
            read = (w[d] > 0) & ~zero[d] & open_face & self.fluid[lo[0], lo[1], lo[2]] & self.fluid[hi[0], hi[1], hi[2]]
            bad += int((read & (lab != end_label) & self.fluid).sum())
        totals = self.__dict__.setdefault("face_prop_totals", {k: 0 for k in FACE_KINDS})
        totals[kind] += bad

    def face_stats(self):
        hist = getattr(self, "face_path_hist", np.zeros(1, np.int64))
        used = np.nonzero(hist)[0]
        top = int(used.max()) if len(used) else 0
        return dict(face_rule=getattr(self, "face_rule", None), face_prop_other_component=getattr(self, "face_prop_totals", None),
                    face_path_hist=[int(v) for v in hist[:top + 1]], face_path_cap_hits=int(getattr(self, "face_path_cap_hits", 0)))
