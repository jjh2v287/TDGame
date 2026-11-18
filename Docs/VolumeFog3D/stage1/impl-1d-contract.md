# 1d 구현 계약 — 바람 옮기기 N2 (GPU 구현자가 이 문서만 보고 짠다)

작성: 2026-11-14 K8(claude 하위 에이전트). GPU 를 돌리기 전에 썼다. 통과 기준은 `prereg-1d.md`(고치지 않음), 설계 근거는 `prereg-1d-design.md`·`Saved/AgentOps/20261114/fog3d/stage1d/k7-design.md`, 반박 `v13a-refute.md`·`v13b-review.md`. 명세 덧붙임은 `spec-add-1d.md`.
표기: [실측] 직접 돌린 값, [확인: 파일:줄] 열어 본 것, [추정] 추론. 기준 모의는 `Tools/FluidFX/fog3d_ref/fog3d_core_v2.py`(advect="N2", solid="S9"), 기준 자료 도구는 `fog3d_replay_v2.py`·`fog3d_compare_v2.py`. 모두 `Saved/AgentOps/20261114/fog3d/stage1d/ref-v2-lock.txt` 로 잠갔다.

## 0. 한 장 요약

- 바꾸는 곳은 `TDFluidGpuFog3D.usf` 의 옮기기 패스(`Fog3DAdvectCS` → `AdvectFog3DFluidCell`)뿐이다. 패스 수·텍스처·셰이더 인수·C++ 파라미터 구조체는 그대로다.
- 밀린 거리·채움(d·g): 지금 길과 8칸 읽기를 그대로 쓰되, 출발 바람만 칸 가운데 바람 V_c(양쪽 면 평균)로 바꾼다. 8칸 읽기의 바람 결과는 버린다.
- 바람 세 성분: 자기 칸의 +x·+y·윗면마다 따로, 면 가운데에서 그 면 자리 바람 V_f 로 거슬러 올라가 길 따라 멈추고, 끝 자리에서 그 성분의 면 격자 8마디를 읽는다(다시 나누지 않음, 면 이어짐 규칙 적용).
- 반박이 낸 꼭 고칠 것 다섯의 결론: (가) 맨 위 층 w 면 식 = §2.2 (나) 면 축 기준 마디 상한 = 가로 N−1·높이 7(§2.6) (다) 면 이어짐 규칙은 **남긴다** — 결과를 바꾸지 않는다는 증명이 실패했고, 결과를 바꾸는 장면을 찾아 검증 장면 `face_rule` 에 넣었다(§3) (라) 명세 덧붙임 `spec-add-1d.md` (마) 기준 숫자와 덤프 목록의 지문은 `ref-v2-lock.txt` 와 숫자 파일의 `stage1d` 절.
- 패스는 나누지 않는다(한 패스). 근거 §4.

## 1. 바뀌지 않는 것

- 텍스처 꼴: Wind = (자기 +x 면 u, +y 면 v, 윗면 w, 넷째 = 유체 표시), DisplacementFill = (dx, dy, dz, g). 옮기기는 `WindIn`(지난 장, SRV)·`DisplacementFillIn`·`FaceFlagsIn` 을 읽고 `WindOut`·`DisplacementFillOut`·`OutputVolume` 세 쓰기 뷰에 쓴다 [확인: TDFluidGpuFog3D.usf:729-765]. 쓰기 뷰를 늘리지 않고, 형식 있는 UAV 읽기를 새로 만들지 않는다.
- 한 번 계산의 순서(명세 3.3): 옮기기 → 입력(구 바람 섞기·깎기) → 닫힌 면 0 → 압력 → 막힌 칸 정리. 옮기기 패스 안의 뒷부분(되돌림 계수 곱 → 가장자리 띠 곱 → 새 띠 0 → 구 바람 섞기 → 채움 깎기 → 닫힌 면 0 → 쓰기, 넷째 채널 베끼기)은 내용과 순서가 지금과 같다 [확인: :704-717, 761-764].
- 자기 칸이 막힘이면 바람 0·그리기용 d·g 채우기 후 끝 [확인: :742-750]. 모드 1 은 그대로(바람 없음).
- 길 따라가기 규칙(묶기 먼저 → 경계 시점 → 칸 옮김, 엄격한 `<`, x→y→z, 24번 상한, 이동 상한 → 겹침 자르기 순서), 8칸 기준 자르기, 이어짐 비트 식(`FindFog3DDisconnectedCorners`), "a(1−t)+b·t" 섞기 꼴.
- 압력·몰림·기울기·굽기·정리·덮어쓰기 패스, C++ 쪽 인수와 디스패치.

## 2. 옮기기 패스의 단계별 식 (모드 2, 막히지 않은 칸 l)

기호: h = (CellSizeCm, CellSizeCm, LayerHeightCm) = (15.625, 15.625, 30). N = Resolution. e_a = 축 a 의 단위 정수 벡터. B[c].a = 칸 c 텍셀의 성분 a(= 칸 c 의 +a 면 값). "창 밖 이웃"은 지역 좌표가 [0, N)×[0, N)×[0, 8) 밖인 칸이며 그 값은 0 으로 읽는다(저장 자리 감김 금지, 지금 `ReadFog3DWestWind` 의 꼴 [확인: :82-92]). Low·High = 지금 `ClampFog3DTargetToOverlap(l, ·)` 안의 LowCm·HighCm(칸 l 가운데를 원점으로 한 겹침 상자) [확인: :308-317].

새 띠 칸(`IsFog3DCellInNewBand`)은 결과가 0 으로 덮이므로 아래 1~3 을 건너뛰고 0 을 써도 된다(결과 비트 같음, 반박 V13a F4: 창 이동 계산의 새 띠 칸은 길이 16칸까지 간다). 아래 식은 새 띠가 아닌 칸에 대한 것이다.

### 2.1 칸 가운데 바람과 d·g (가운데 길)

1. V_c = 0.5f × (B[l].xyz + (B[l−e_x].x, B[l−e_y].y, B[l−e_z].z)), 창 밖·층 0 아래 이웃 값은 0. 막힌 이웃은 그 면이 닫혀 값이 0 이므로 그대로 읽는다. 기준 모의는 64비트로 더한 뒤 반으로 나눈다(`center_velocity`) [확인: fog3d_core.py:880-888]. 32비트 덧셈의 반올림 한 번 차는 허용 안에 든다 [추정].
2. Target = `ClampFog3DTargetToOverlap(l, LimitFog3DMove(−V_c × DeltaSeconds))`, 길 = 지금 `FollowFog3DPath(l, Target, Sample)`, 읽기 = 지금 `SampleFog3DEightCells(l, EndOffset, Sample, DisplacementFill)`. 그 결과의 `.DisplacementFill` 만 쓰고 `.Wind` 는 버린다. 되돌림의 `(d − Sample)` 의 Sample 도 이 가운데 길의 것이다.

### 2.2 면 자리 바람 V_f (성분 a = x, y, z 마다, 면 = 칸 l 의 +a 면)

기준 모의의 정의 [확인: fog3d_core_v2.py:66-73, fog3d_core.py:739-793]: 면 자리 f(칸 l 가운데 원점으로 ½h_a·e_a)를 겹침 상자로 자른 점 q = clamp(f, Low, High) 에서 엇갈린 격자 3선형 보간. 성분마다 쓸 값(모두 32비트, 무게는 0·0.5·1 이라 곱이 정확하다):

- 자기 성분(V_f.a):
  - 보통: B[l].a.
  - q_a 가 잘린 경우 = 칸 l 이 a 축의 겹침 상한 칸일 때(가로: l.a = OverlapMax.a, 높이: **l.z = 7, 곧 맨 위 층의 윗면**): 0.5·B[l−e_a].a + 0.5·B[l].a (창 밖 이웃 0). **맨 위 층 w 면은 ½(w₆ + w₇)** 이다(F1).
- 다른 성분 b(≠ a):
  - 보통: 0.5·(0.5·B[l−e_b].b + 0.5·B[l].b) + 0.5·(0.5·B[l+e_a−e_b].b + 0.5·B[l+e_a].b). 안쪽 괄호(b 축 짝)를 먼저 섞고 a 축으로 섞는다. 단 a = z 이고 b 가 가로일 때 섞는 순서는 "높이 먼저": 0.5·(0.5·B[l−e_b].b + 0.5·B[l−e_b+e_z].b) + 0.5·(0.5·B[l].b + 0.5·B[l+e_z].b). 순서 규칙은 하나다: 기준 모의 `_gather` 처럼 높이(z) → 세로(y) → 가로(x) 순으로 짝을 섞는다. 무게가 0.5 라 순서를 지키면 값이 비트까지 같다.
  - q_a 가 잘린 경우(위와 같은 칸): a 축 짝 대신 칸 l 쪽 값만 쓴다. 곧 0.5·B[l−e_b].b + 0.5·B[l].b. **맨 위 층 w 면의 u·v 는 같은 층(7) 두 값의 평균**이다(위층 8 을 0 으로 넣어 ¼(두 값)으로 만들면 틀린다: 변이 `top_w_draft`).
  - b 축 쪽 이웃 B[l−e_b] 가 창 밖이거나(가로 0 열) 층 0 아래면 0.
- 구현은 위 닫힌 식(빠른 길)으로 쓴다. 면이 잘리는 경우는 "a 축 상한 칸" 하나뿐이다(새 띠 칸을 건너뛰면 b·c 축은 잘리지 않는다: 칸 l 의 가운데가 겹침 상자 안이므로).

### 2.3 이동·목표·시작 칸

1. Move = LimitFog3DMove(−V_f × DeltaSeconds) (지금 함수 그대로, 상한 125/125/120cm, 비율 곱).
2. **면 축 이동 상한(F2 관련 정보)**: 이동 상한은 세 축 모두 같은 함수로 곱한다. 면 축이라고 다른 상한을 쓰지 않는다.
3. Target(면 가운데 원점) = clamp(Move, Low − f, High − f), f = ½h_a·e_a. 곧 겹침 상자의 하한·상한에서 반 칸을 빼고 Move 를 자른다(G2: Move + f 를 자른 뒤 f 를 빼면 반올림이 하나 더 생긴다). 맨 위 층 w 면은 High_z − f = −15cm 라 Target.z ≤ −15 가 늘 성립한다(그 면의 길은 늘 아래로 간다).
4. 시작 칸 s = (Target.a > 0) ? 1 : 0, 시작 칸 = l + s·e_a. Target.a 가 0(−0 포함)이면 s = 0 이고 그 축은 처음부터 묶인다(`int3(sign(Target))`). s = 1 은 그 칸이 늘 창 안이다(Target.a > 0 이면 칸 l 은 a 축 상한 칸이 아니다).

### 2.4 길 따라 멈추기(면 길)

지금 `FollowFog3DPath` 를 시작 칸 오프셋 S = s·e_a 와 가운데 오프셋 C0 = (s − 0.5)·h_a·e_a 를 받게 일반화한다. 원점은 면 가운데다.
- PathCell = l + S + EndOffset, PathFlags = `ReadFog3DPathFaceFlags(PathCell)`.
- CenterCm = EndOffset·h + C0, EdgeCm = CenterCm + Direction·0.5·h. 모두 h/2 의 정수배라 32비트에서 정확하다.
- 묶기: `BindFog3DPathAxisAtClosedFace(IsFog3DFaceClosed(PathCell, PathFlags, 축, Direction>0), Target, CenterCm, …)` 그대로(지나쳤으면 CenterCm, 아니면 Target).
- 시점: `EdgeCm / Target`(32비트), 엄격한 `<`, x→y→z, `> 1` 이면 끝, 24번 상한. 기준 모의도 같은 원점에서 32비트로 나눈다(`_crossing_time32`, 분자 = 경계 − 시작은 정확, 분모 = 64비트 이동을 자른 뒤 32비트로 바꾼 값) [확인: fog3d_core.py:1064-1076, fog3d_core_v2.py:121-161].
- 결과: 끝 칸 E = l + S + EndOffset, 표본 Sample(면 가운데 원점). Sample 은 늘 E 의 닫힌 상자 안이다.

### 2.5 칸 묶음과 이어짐(면 이어짐 규칙, §3 에 따라 남긴다)

1. 묶음 기준(칸 격자): 면 축은 칸 l 가운데 원점으로 바꾼 값 Sample.a + 0.5·h_a 를, 다른 두 축은 Sample 을 그대로 `SplitFog3DSampleAxis(·, h, l, 가로 N−2 / 높이 6)` 에 넣어 기준 칸 Bc 를 얻는다(G1: 원점을 반 칸 옮기는 것을 빠뜨리면 변이 `rule_half_shift`).
2. 8칸 유체 표시(Wind.w)로 막힘 마스크, 끝 칸 오프셋 E − Bc 로 지금 `FindFog3DDisconnectedCorners` 를 그대로 부른다(끝 칸이 묶음 밖이면 0 = 지울 것 없음). 이 마스크 D 는 "끝 칸과 이어지지 않은 유체 칸"이다.
3. 끝 칸은 **면 길의 끝 칸 E** 다(가운데 길의 끝 칸을 쓰면 변이 `rule_center_end`).

### 2.6 면 격자 8마디 읽기

1. 면 축(a): 마디 기준 n0 = (l.a + 1) + floor(Sample.a / h_a), 무게 t_a. `SplitFog3DSampleAxis(Sample.a, h_a, l.a + 1, BaseLimit, n0, t_a)` 로 얻고 **BaseLimit = 가로 N − 1, 높이 7**(F2. 칸 격자의 N−2·6 을 쓰면 표본 높이 210~225cm 에서 윗면 마디 대신 w₆ 만 읽는다: 반박 변이 M6z). 마디 n 은 칸 n−1 과 n 사이의 면이며 값은 B[n−1].a(칸 n−1 의 +a 면), n − 1 < 0 이면 0(감김 금지). n0 ≤ N−1(높이 7)이라 n−1 은 창 안이다.
2. 다른 두 축: 칸 격자 그대로 `SplitFog3DSampleAxis(Sample.b, h_b, l.b, N−2 또는 6)` 의 기준 Bc.b·무게 t_b(§2.5-1 과 같은 값).
3. 마디 d(비트 4·x + 2·y + z)의 값: 면 축 마디 n = n0 + (d 의 a 비트), 다른 축 칸 = Bc + (d 의 그 축 비트). 묶음 칸 cc_a = (Bc.a ≤ n ≤ Bc.a + 1 이고 n ≤ N_a − 1) ? n : n − 1, cc 의 모서리 번호 = d 의 다른 축 비트 + a 비트 (cc_a − Bc.a)(늘 0 또는 1: 증명 k7-design §1.3 (i), 실측 묶음 밖 0번). cc 가 D 에 들면 그 마디 값을 0 으로 고른다. 막힌 cc 는 마디 면이 닫혀 값이 이미 0 이다. 다시 나누지 않는다.
4. 섞기: 높이 → 세로 → 가로 순 "a·(1−t)+b·t"(`lerp` 금지). 결과가 새 성분 a.

### 2.7 뒤처리와 순서

- 새 바람 = (2.6 의 x, y, z 결과). 그 뒤 `Result.Wind = Wind × WindRefillFactor × EdgeBandFactor`, d·g 는 2.1 의 결과로 지금 식, 새 띠 0, `BlendFog3DSphereWind`, `ErodeFog3DFill`, `SelectFog3DOpenFaceWind`(닫힌 면 0), 쓰기(넷째 채널 = 원래 Wind.w) — 지금 순서 그대로.
- 닫힌 면(FaceFlags 의 +a 비트 또는 창 끝)은 2.2~2.6 을 건너뛰고 0 을 넣어도 된다(마지막에 0 으로 덮이므로 결과 같음) [확인: fog3d_core.py:1468-1470].
- 입력과의 순서: 구 바람 섞기는 새 바람(2.7 첫 줄 뒤)에 대해 면 가운데 자리에서 한다(지금과 같음).

## 3. 면 이어짐 규칙: 증명 시도와 반례 (꼭 고칠 것 다)

### 3.1 증명하려 한 것과 성립하는 부분
"무게 > 0 이고 값 ≠ 0 인 마디의 묶음 칸 cc 는 늘 끝 칸 E 와 묶음 안에서 이어진다." 성립하는 부분:
1. 면이 열렸다는 것은 두 칸이 모두 유체라는 뜻이다(`ou = fluid & fluid`) [확인: fog3d_core.py:612-618]. 그러니 cc 와 마디의 다른 칸은 유체다.
2. 표본은 E 의 닫힌 상자 안이므로 면 축의 무게 > 0 마디는 E 자신의 두 a 면이다. cc 가 a 축에서 E 와 다르면 그 마디 면을 거쳐 (E.a, b', c') 로 줄일 수 있다.
3. b' = E.b 이거나 c' = E.c 면 cc 는 E 와 한 축만 다른 유체 칸이라 이어진다.
4. 둘 다 다르면(cc0 = (E.a, E.b+s_b, E.c+s_c)): s_b 가 이동 방향과 같으면 길이 끝 칸에서 그 축의 앞 면을 검사했고(묶이지 않았으므로 열림) (E.a, E.b+s_b, E.c) 가 유체 → 이어진다. c 도 같다.
5. 남는 경우는 s_b·s_c 가 모두 "지나온 쪽"인 경우다. 길은 칸마다 묶이지 않은 모든 축의 앞 면을 검사하므로 지나온 칸들의 앞 이웃이 유체로 강제되고, 대부분의 경우 그 칸들이 cc0 와 E 를 묶음 안에서 잇는다.

### 3.2 실패 지점과 반례 [실측]
5 의 "대부분"이 전부가 아니다. 면 축 이동이 5칸 이상이고, b·c 의 마지막 칸 넘기가 면 축 넘기 사이사이에 끼며, 길이 묶음에 들어온 칸이 cc0 의 이웃이 아닐 때 cc0 가 묶음 안에서 고립될 수 있다. 시작 칸 c0 = (0,0,0) 기준(축 a·b·c = x·y·z, 이동 (5.4h, 0.943h, 0.714·30cm), 넘기 시점 x 0.185/0.370, y 0.53, x 0.556, z 0.70, x 0.741/0.926):
- 길: (0,0,0)→(1,0,0)→(2,0,0)→(2,1,0)→(3,1,0)→(3,1,1)→(4,1,1)→(5,1,1)=E, 묶음 {4,5}×{0,1}×{0,1}.
- 막힌 칸 (4,0,0), (5,1,0), (5,0,1) 세 개만 두면 cc0 = (5,0,0) 이 유체인데 묶음 안에서 고립되고, 마디 (면 (5,0,0)|(6,0,0), 무게 0.4 × 0.057 × 0.286 ≈ 0.0065)가 규칙으로 지워진다.
- 기준 모의 확인 [실측]: 128칸, dt 1/20, 균일 바람 (−1687.5, −294.8, −428.6)cm/s, 압력·되돌림·띠 끔, 한 번 계산. 규칙을 켜고 끈 차이가 정확히 예측한 면 하나(u (59,62,2))에서 10.92cm/s(−1137.13 대 −1148.05), 다른 모든 장 0.

결론: **규칙을 남긴다.** 결과를 바꾸는 경우는 면 축으로 한 계산에 5칸 넘게 가는 센 바람(dt 1/20 에서 초속 약 17m)과 특정한 막힌 칸 꼴이 겹칠 때뿐이라 드물고 작다(면 하나에 바람의 1% 안팎). 그래서 장 차이 판정(0.2%)으로는 잡히지 않는다. 검증 장면 `face_rule`(위 꼴 10벌, 장애물 30개)에 국소 판정 "규칙이 바꾸는 면들에서 |GPU − 기준| ≤ 1e-3 × 바람 최댓값"을 둔다(숫자 파일 `stage1d.face_rule`). 이 국소 판정은 prereg-1d 0절의 "결과를 바꾸는 장면을 검증 장면에 넣는다"를 실행하는 것으로, E2 의 장 차이 판정에 더해지는 것이다(느슨하게 하지 않는다).

## 4. 패스를 나눌지

한 패스로 둔다. 반박 V13b 의 실측: 합친 패스 임시 레지스터 39, 바람만 하는 패스 38 로 최대치가 거의 줄지 않고, 자기 칸·면 표시·구 읽기가 두 벌, 디스패치가 하나 는다 [확인: v13b-review.md:50, 66-72]. 나눌 때는 둘째 패스도 지난 바람(`Wind[Read]`)을 읽어야 한다(새 바람 장을 읽으면 틀린다). 쓰는 장: 지금과 같은 SRV 셋(WindIn·DisplacementFillIn·FaceFlagsIn, 필요하면 CellBlockedIn)과 UAV 셋.

## 5. 정밀도와 동률 (기준 모의와 가까이 맞추기)

- 면 바람의 0.5 무게 섞기는 §2.2 의 순서를 지키면 비트까지 같다. V_c 는 반올림 한 번 차가 있을 수 있다.
- 이동·목표: 기준 모의는 64비트 이동을 자른 뒤 시점 분모만 32비트로 바꾼다. GPU 는 전부 32비트다. 1a 와 같은 종류의 차이며 허용(0.2%) 안에 든다 [추정: 1a 의 같은 구조에서 장 차이 1e-7~1e-6].
- 시작 칸 동률: Target.a 가 정확히 0 이면 s = 0, 그 축 묶임. 경계 시점이 같으면 x→y→z.
- 이동 상한 → 겹침 자르기 순서.
- 묶음 기준의 Sample.a + 0.5·h_a 는 32비트 덧셈 하나다. 기준 모의는 절대 좌표 64비트로 쪼갠다. 경계에 정확히 놓이는 경우(묶인 축 = 칸 가운데)는 h/2 의 배수라 정확하다.

## 6. 셰이더 함정 (1a 계약 3.1·6절에서 이어짐)

- 반복문 안에서 벡터 성분을 변수 번호로 대입하지 않는다(FXC "l-value 번호"): 면 축은 상수 인수를 받는 함수를 세 번 부르거나 `[unroll]` 로 펼친다. 끝 칸 옮기기는 `select(int3(0,1,2) == 축, 방향, 0)`, 마디의 a 비트도 `select`·`dot` 으로 만든다. V13b 뼈대 셰이더가 FXC·dxc 에서 오류 없이 통과했다 [확인: v13b-review.md:74].
- `lerp` 금지, `a*(1-t)+b*t`. `[loop]` 24번 상한은 길마다(칸마다 길 4개).
- .usf·.ush 주석 금지, 클래스 선언에 `final` 금지.
- 레지스터: 8칸 배열과 면 3 × 8마디를 한꺼번에 들지 말고 성분마다 읽고 섞은 뒤 버린다. 정적 읽기는 지금 32 → 약 101, FXC 컴파일 2.3 → 18.5초(CPU 바쁠 때 수십 초) [확인: v13b-review.md:66-75].
- 셰이더를 바꾼 뒤 SM5 전역 셰이더 쿠킹 1회(L-build-20·24).
- 이웃 바람은 SRV 지난 장에서 읽는다(같은 패스가 쓰는 UAV 에서 읽지 않는다).

## 7. 시험 대응표

| 무엇 | 명령(저장소 루트, 모두 `PYTHONUTF8=1 python -B`) | 판정 |
|---|---|---|
| E1 장 차이 62장면 | `Tools/FluidFX/fog3d_ref/fog3d_compare_v2.py e1 --gpu Saved/FluidFX/Fog3D/gpu --gpu-add1 Saved/FluidFX/Fog3D/gpu_add1` | 모든 덤프에서 바람·밀린 거리·채움 장 차이 각각 ≤ 0.002(띠 포함), 유체 표시 같음 |
| E1 숫자형(1a) | `Tools/FluidFX/fog3d_ref/fog3d_compare.py --gpu Saved/FluidFX/Fog3D/gpu --ref Saved/FluidFX/Fog3D/ref_v2 --shake Saved/FluidFX/Fog3D/ref_v2_shake --numbers Docs/VolumeFog3D/measurements/stage1d-ref/prereg-1d-numbers.json --out Saved/FluidFX/Fog3D/compare/verdict_1d_a.json --extras` | 잠긴 도구의 A3·A4·A5·A7·A8~A13·A15 를 새 숫자로 |
| E1 숫자형(1b 덧붙임) | `Tools/FluidFX/fog3d_ref/fog3d_compare_add1.py --gpu Saved/FluidFX/Fog3D/gpu_add1 --ref Saved/FluidFX/Fog3D/ref_add1_v2 --numbers Docs/VolumeFog3D/measurements/stage1d-ref/prereg-1d-numbers.json --out Saved/FluidFX/Fog3D/compare/verdict_1d_add1.json` | 덧붙임 판정을 새 숫자로 |
| E2 새 장면 | `Tools/FluidFX/fog3d_ref/fog3d_compare_v2.py e2 --gpu Saved/FluidFX/Fog3D/gpu_1d` | 장 차이 ≤ 0.002·유체 같음 + face_rule 국소 판정 |
| E3 대칭 | `Tools/FluidFX/fog3d_ref/fog3d_compare_v2.py e3 --gpu Saved/FluidFX/Fog3D/gpu_1d` | 네 방향 치우침 ≤ 0.03, 세기 ±5%, 비스듬한 방향 = 기준 ± 0.01 |
| E4 판별력(기준 모의) | `fog3d_replay_v2.py mutant --mutant <변이> --scene <…> --out Saved/AgentOps/20261114/fog3d/stage1d/k8/mut/<변이>` 뒤 `fog3d_compare_v2.py mutants` | §8 표 |
| E5·E6 | `python Tools/build_and_test.py`(기존 절차) | prereg-1d 2절 |

GPU 덤프: 기존 장면은 지금 `ReplayDumps`(59)·`AddendumReplayDumps`(3)가 `gpu`·`gpu_add1` 에 쓰는 그대로. 새 장면 9개(`replays_v2/`)는 새 시험이 필요하다(지금 시험은 파일 수 59·3 을 상수로 박아 두었다 [확인: v13b-review.md:52]): 예 `TDGame.FluidFXRhi.Fog3D.Stage1dReplayDumps` 가 `Tools/FluidFX/fog3d_ref/replays_v2/*.json` 9개를 읽어 `Saved/FluidFX/Fog3D/gpu_1d/<장면>__<덤프>.json/.f32` 로 쓴다(형식은 기존 `fog3d-dump-1`). 새 장면이 쓰는 입력은 기존 재생 형식 안의 것뿐이다(균일 바람·소용돌이 덮어쓰기, 상자 장애물, 쓸린 구, 압력·되돌림·띠 손잡이).

기대값이 바뀌는 C++ 시험: 값을 박아 둔 Fog3D 시험은 없다. `ReplayDumps`·`AddendumReplayDumps` 는 덤프만 쓰고 판정은 파이썬이 새 기준으로 한다. 성질 시험(`RestStaysZero`·`Determinism`·`FarOrigin`·`WindowShiftOutput`·`ClosedFaces`·`SealedColumn`·`BlockedCellDrawFill*`·`PreviousWindowCheck`·`ModuleSmoke`)은 그대로 통과해야 한다. `StepTiming` 은 시간이 늘어난다(판정 아님, E7 에 적는다). `SealedColumn`(바람 ≤ 3000)의 C++ 장면 자체는 기준 모의로 재지 않았다. 같은 성질의 재생 장면 sealed_cells_600 은 새 방식에서 끝 바람 최댓값 1837cm/s 다(§9).

## 8. 판별력(E4) — 기준 모의로 미리 증명

변이 정의는 `fog3d_replay_v2.py` 의 `MutantSim`, 결과 `Saved/AgentOps/20261114/fog3d/stage1d/k8/e4-mutants.json`. 칸 값 = (변이 덤프와 기준 덤프의 장 차이 최댓값) ÷ 허용 0.002, "L" = face_rule 국소 판정의 (최대 |차|) ÷ 허용 [실측].

| 변이 | 뜻 | obstacle_swap | face_rule | k2 | k3 | k7 | leak_room | top_wface | wall_self | 가장 잘 잡는 곳 |
|---|---|---|---|---|---|---|---|---|---|---|
| old_u | u 성분만 옛 방식(칸 가운데 길, 자기 텍셀 바람) | 165.8 | 48.4 (L8.9) | 123.2 | 52.2 | 118.4 | 61.4 | 57.0 | 130.8 | 166배 |
| old_v | v 성분만 옛 방식 | 61.8 | 3.4 | 34.2 | 48.1 | 37.2 | 1.1 | 0.0 | 251.2 | 251배 |
| old_w | w 성분만 옛 방식 | 125.3 | 0.8 | 71.8 | 43.2 | 74.8 | 0.8 | 97.8 | 197.6 | 198배 |
| old_dg | 밀린 거리·채움만 옛 출발 바람(자기 텍셀) | 300.1 | 31.3 | 206.0 | 101.4 | 236.5 | 25.2 | 250.7 | 107.5 | 300배 |
| top_w_draft | 맨 위 층 w 면을 계약 초안대로(자기 = w₇, u·v = ¼(두 값)) | 0.0 | 0.0 | 0.0 | 1.1 | 0.0 | 0.0 | **36.5** | 0.2 | top_wface 36배 (s1 9.6배) |
| rule_off | 면 이어짐 규칙 뺌 | 0.0 | 0.0 (**L8.9**) | 0 | 0 | 0 | 0 | 0 | 0 | face_rule 국소 8.9배 (s1 6.5배) |
| rule_half_shift | 묶음 기준의 반 칸 원점 옮김 빠뜨림 | 0 | 0.0 (**L8.9**, s2) | 0 | 0 | 0 | 0 | 0 | 0 | face_rule s2 국소 8.9배 |
| rule_center_end | 이어짐의 끝 칸 = 가운데 길의 끝 칸 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | **가르지 못함** |

- E4 의 두 등록 변이(한 성분 옛 방식·맨 위 층 w 면 초안)는 모두 3배를 넘는다(최소: top_w_draft 의 top_wface 36배).
- 면 이어짐 규칙의 기준 칸 실수 두 가지 가운데 "반 칸 원점"은 face_rule 의 s2 꼴(표본이 끝 칸의 앞쪽 반)로 갈린다. "가운데 길의 끝 칸"은 가르지 못했다: 균일 바람에서 가운데 길은 면 길과 같은 칸이나 그 칸의 지나온 쪽 면 이웃(같은 덩어리)에서 끝나 이어짐 판정이 같아진다. 둘이 갈리려면 가운데 바람과 면 바람이 크게 다른 국소 흐름이 반례 꼴과 겹쳐야 한다. 구현 뒤 반박은 이 변이를 셰이더 읽기로 확인한다(코드 검토 항목).
- face_rule 은 장 차이(0.2%)로는 규칙을 보지 못한다(0.02~0.03배). 국소 판정이 이 장면의 판별 수단이다.

## 9. 기준 자료

- 기존 장면 기준 덤프: `Saved/FluidFX/Fog3D/ref_v2/`(59장면), 흔든 덤프 `ref_v2_shake/`(민감도 10장면), 덧붙임 `ref_add1_v2/`(+`proof.json`)·`ref_add1_v2_shake/`. 폴더마다 `manifest.json`(파일 이름·크기·sha256, 목록 지문, 코드 지문).
- 새 장면: 재생 `Tools/FluidFX/fog3d_ref/replays_v2/`, 기준 덤프 `Saved/FluidFX/Fog3D/ref_v2_new/`.
- 새 장면 9개: `walk_px`·`walk_py`·`walk_mx`·`walk_my`(대칭 걷기, 148계산, 덤프 w70·end), `walk_a11`·`walk_a22`(11.25°·22.5°), `wall_self`(얇은 판 두 장 옆을 초속 24m 구가 지나간 뒤 48계산 동안 바람이 스스로 옮겨짐, 덤프 s12·s30·end), `top_wface`(소용돌이 덮어쓰기 뒤 압력·되돌림 끄고 7계산, 덤프 s1·end), `face_rule`(면 이어짐 반례 꼴 두 가지 9벌, 균일 바람 두 번, 덤프 s1·s2).
- 기준 모의 값 [실측]: 대칭 걷기 네 방향 치우침 2.8e-5, 세기 14.1983(넷 차 0.0001% 안), 비스듬한 방향 치우침 11.25° 0.05163·22.5° 0.04421(반박 V13a 와 같음). walk_px·walk_mx 의 상태 해시는 설계 실험·반박 독립 구현과 같다(586d8998e9b18f11, 7d211a916c4c712c). 민감도(입력 1e-4cm 흔들기) 최대 5.2e-6 이라 0.2% 판정은 민감도의 약 400배 여유다. 밀폐 칸 600번 끝의 바람 최댓값 1837cm/s(≤ 3000), 모든 값 유한. A11 소용돌이 끝 0.02517(옛 0.03141).
- **덧붙임 ADD-a(이어짐 넷째 단계 장면 add1_connect4_wall45)는 새 방식에서 값이 정의되지 않는다** [실측]: 그 장면의 마지막 계산에서 넷째 단계가 무게 > 0 으로 쓰인 칸이 0개라 재는 칸 목록(measure_mask)이 비고, 기준값이 NaN 이 되어 잠긴 `fog3d_compare_add1.py` 의 ADD-a 는 늘 실패한다(기준 덤프를 GPU 덤프로 넣은 자체 점검에서도 12검사 중 이것 하나 실패). 가운데 길의 출발 바람이 바뀌어 길 끝이 달라졌기 때문이다 [추정]. 기준을 바꾸는 일이라 이 작업에서 정하지 않았다: 감독 결정 필요(이 장면은 E1 장 차이로만 볼지, 넷째 단계를 쓰는 새 장면을 등록할지).
- 기준 숫자: `Docs/VolumeFog3D/measurements/stage1d-ref/prereg-1d-numbers.json`(잠긴 두 대조 도구의 키 + `add1_meta` + `stage1d`(새 방식 코드 sha256, 덤프 폴더 목록 지문, 새 장면 sha256, E3 기준값, face_rule 면 목록)). 잠긴 도구가 적는 `code` 키는 옛 core 의 지문이라(F5) 새 방식의 묶음은 `stage1d` 절과 잠금 파일이 맡는다.
