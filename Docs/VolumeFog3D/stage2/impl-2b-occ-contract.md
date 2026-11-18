# 2b-1 입력 가림(닻 채우기) GPU 구현 계약

작성: 2026-11-18 claude(감독). 근거: 등록 `stage2/prereg-2b-design.md` v3.1(117cc69b…) 1.1·1.2·B2-1·B2-3. **정답 명세 = 기준 도구** `Tools/FluidFX/fog3d_ref/fog3d_occlusion_ref.py`(232a9a8d…, 생성 판 c5cab351…) — 이어짐 규칙이 문서와 다르게 읽히면 도구가 이긴다. 기준 덤프 `Saved/FluidFX/Fog3D/ref_occ/`, 닻 판 재생 `Tools/FluidFX/fog3d_ref/replays_occ/{b4,r5,stage1,scenes}`, 숫자 파일 `Docs/VolumeFog3D/measurements/stage2b-occ/prereg-2b-occ-numbers.json`(1beb716f…). 앞 넘김 글 `Saved/AgentOps/20261117/fog3d-2b/occ-ref-a-handoff.md`·`occ-ref-b-handoff.md`.

## 0. 규칙
- 스위치 끔 = 지금과 바이트까지 같음: 새 GPU 길은 끔이면 RDG 패스 자체를 만들지 않고, 입력 셰이더의 기존 줄은 글자 그대로 두고 켬 분기만 더한다. 기존 시험 기대값은 바꾸지 않는다(허용 수정은 등록 B2-1 (다)의 하나뿐이고 이 조각과 무관).
- 새 코드에 주석 없음, TD 이름 규칙, 조기 리턴, 게임 로직은 C++, 런타임 동기 로드 금지.

## 1. 게임 쪽
- `FTDFog3DSettings`: `bool bOccludesInputBehindObstacles = false;` — 정본 JSON 의 Fog3D 설정 묶음에 같은 이름·기본값 false 한 줄만 더함(다른 줄 불변), 설정 시험(정본 = 헤더 기본값) 반영.
- `FTDFog3DInputSample`: `FVector OcclusionAnchorWorld = FVector::ZeroVector;` `bool bHasOcclusionAnchor = false;`. 구역 액터의 표본 만들기가 몸·칼 표본 모두에 `Pose.PelvisWorld`(없으면 폰 위치)를 넣는다.
- `PackSpheres`: 구 배열과 **같은 차례·같은 건너뜀**으로 닻 배열을 채운다 — CellX = clamp(floor((Anchor.X − 창 원점 X) / 칸 cm), 0, N−1)(64비트 double), CellY 같은 식, ZCm = float(Anchor.Z − FloorZ), Flags = 닻 있으면 1.
- 프레임 입력: `bOccludesInputBehindObstacles`, `TArray<FTDFluidGpuFog3DSphereAnchor> SphereAnchors`.
- 설정 로그 줄(`LogTDFog3DZone: Fog3D settings: …`)에 `occludesInput=0|1` 을 더한다.

## 2. GPU
- 구조체 `FTDFluidGpuFog3DSphereAnchor`(16바이트: int32 CellX, int32 CellY, float ZCm, uint32 Flags, static_assert). 구 구조체(48바이트)는 그대로.
- 이어짐 3D 텍스처 R16_UINT(N×N×8): 켬일 때만 만들고 걸음마다 0 으로 지운다.
- 새 패스 `Fog3DInputReachCS`(켬일 때만): 그룹 하나 = 구 하나(최대 16), 그룹 스레드가 상자 칸을 나눠 맡는다.
  1. 술어 상자: 기존 구 거르기 술어(`usf:903-908`, 같은 함수·같은 32비트 비교)를 만족하는 칸 중심의 i·j 범위, 층은 0~7 전부.
  2. 닻 칸: i·j = 버퍼 값, k = clamp(floor((ZCm − T) / 층 높이), 0, 7). T = 땅 따라가기면 (i, j) 열의 `ColumnTerrain.x`, 아니면 0.
  3. 채우기 상자 = 술어 상자와 닻 칸을 모두 담는 가장 작은 축 정렬 상자 ∩ 창. **허용**(술어 상자 안 모든 칸 비트 1): 상자가 48×48×8 을 넘음 / 닻 칸이 막힘(`CellBlocked`) / Flags 0.
  4. 공유 메모리 거리(칸당 uint8, 포화 255): 닻 칸 0, 나머지 255. 열린 면(면 표식의 그 방향 닫힘 비트가 0)인 이웃의 최소 + 1 로 고친다. 그룹 전체에 변화가 없을 때까지 돌린다(그룹 합의 표지 + 배리어).
  5. 이어짐 = d ≤ |Δi| + |Δj| + |Δk| + 2 → 이어짐 텍스처의 그 칸에 `InterlockedOr(1 << 구 번호)`. 상자 밖 칸은 비트 0.
- 입력 고리: 바람(`usf:1119` 근처)·깎기(`usf:1141` 근처) 구 고리에 켬일 때만 '이어짐 비트가 0 이면 그 구를 건너뜀'을 더한다(곱하지 않는다). 매개변수 `InputOcclusionEnabled`(uint32).
- 순서: 굽기(있는 걸음) → 이어짐 패스 → 입력 패스.
- D3D11(FXC·SM5) 공유 메모리 한도 32KB 안(48×48×8 = 18KB).

## 3. 재생·시험
- 재생 읽기: 구 `occlusion_anchor_cm`(선택 — 있으면 Flags 1, CellX·Y 는 64비트로 창 원점 기준 floor·자르기, ZCm = z − 바닥), 최상위 `input_occlusion`(bool, 기본 false).
- 덤프: 켬일 때 `.reach.bin` 을 더한다 — uint16 LE, 칸 순서 (x·N + y)·8 + z, 비트 s = 그 걸음 재생 구 s(기준 도구와 같은 꼴).
- 새 RHI 시험 `TDGame.FluidFXRhi.Fog3D.InputOcclusionReplayDumps`: `replays_occ/` 네 폴더의 매니페스트 장면을 재생 json 대로 돌려 `Saved/FluidFX/Fog3D/gpu_occ/<폴더>/on/` 에 덤프. 켬 = 끔 묶음(기준 도구의 묶음 표: b4 14 + r5 다섯)은 끔으로도 돌려 GPU 켬 = GPU 끔(장 덤프, `.reach.bin` 제외)을 시험 안에서 바이트로 확인. 매니페스트가 없으면 시험 실패.
- 대조: 바뀜 묶음은 `fog3d_compare_s2a.py --numbers <새 숫자 파일> --gpu-dir <gpu_occ/…/on> --ref-dir <ref_occ/…/on>` 와 이어짐 비트 바이트 대조(기준 도구에 명령이 없으면 `compare-reach` 를 더한다 — 장면·덤프마다 다른 칸 수, 빠진 파일은 실패).

## 4. 게임 시험
- 닻 포장 차례(건너뛴 구 포함 배치에서 구 배열과 같은 차례), 칸 식(64비트 floor, 창 밖은 자르기), 닻 없음 Flags 0, 설정 기본 false, 로그 줄에 `occludesInput`.

## 5. 빌드·검증 (RHI 마다 D3D12 `--rhi offscreen`, D3D11 `--rhi d3d11`)
- 셰이더 검사 `python Tools/FluidFX/check_fluid_gpu_shaders.py`, 비동기 로드 검사 `python Tools/check_sync_loads.py`.
- **B2-1**: 두 스위치 끔(정본 기본)에서 덤프 다섯 폴더 + `gpu_blend_b4` 가 기준선(`Saved/AgentOps/20261117/fog3d-blend-gpu/baseline_{d3d12,d3d11}` 와 `gpu_blend_b4_{d3d12,d3d11}_turnfix`)과 바이트 같음(`python -B Tools/FluidFX/fog3d_dump_snapshot.py snapshot|compare`, 다른 RHI 를 돌리기 전에 사본 — L-build-29), 2a 대조 506·0·0·덤프 41, B4 대조 1145·0·0·92, 1단계 대조는 1d 인수(L-repo-37), 게임 시험 `TDGame.FluidFX.Fog3D` 전부(손 등록 고정 시험 무수정 통과).
- **B2-3**: 바뀜 묶음 덤프마다 유체 표시 0·장 차이 ≤ X·이어짐 비트 같음·검사 수·덤프 수·건너뜀 0(L-repo-38). 켬 = 끔 묶음 GPU 켬 = 끔 바이트. 허용 수는 장면마다 기준과 같은지 적는다.

## 6. 넘김 글
`Saved/AgentOps/20261118/fog3d-2b/occ-gpu-handoff.md`: 기준별 결과 표(두 RHI), 고친 파일과 sha256, 줄 위치, 계약과 다르게 한 것과 까닭, 확인 못 한 것. 커밋·장부·에셋 저장 없음.
