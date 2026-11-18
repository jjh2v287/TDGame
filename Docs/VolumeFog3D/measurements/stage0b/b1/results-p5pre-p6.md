# 0b-1 가 결과: 볼륨 렌더 타깃 읽기 예비(P5 예비)와 재질 비용(P6)

작성 2026-11-12 하위 에이전트(Sonnet 5.5). 측정 정의는 촬영 전에 `PREREG-b1.md` 에 적었다. 증거 사본: `Docs/VolumeFog3D/measurements/stage0b/b1/`.

## 만든 것 (직접 확인)

- 볼륨 렌더 타깃: `unreal.RenderingLibrary.create_render_target_volume(world, 128, 128, 64, RTF_RGBA16F, ClearColor(1,1,1,1), False, True)` (마지막 True = 계산 셰이더가 쓸 수 있는 옵션). 생성 때 지우기 색으로 전체를 채우므로 알파 1 상수. 그 뒤 `rename` 으로 임시 패키지로 옮겨 오브젝트 경로를 고정했다.
  **오브젝트 경로: `/Engine/Transient.Fog3dDensityVolumeRT`** (읽은 크기 128x128x64). UAV 비트 읽기는 파이썬에서 속성 이름이 안 보여 직접 확인 못 했다(옵션 True 로 넘김). 0 값 대조용 `/Engine/Transient.Fog3dDensityVolumeZeroRT` (알파 0)도 만들었다.
- 텍스처판 재질 `/Game/Repro/Fog3D/M_Fog3D_TexProbe`(메모리, 컴파일 오류 0): 3D 텍스처 파라미터 `DensityVolume` 을 UVW = (세계 - 상자 최소)/상자 크기 로 읽어 알파를 농도로 쓰고, 소광 0.25/m x 알파 x 안개 높이 0~240cm 구간. 높이 구간 자르기는 수식판과 같게 맞추려고 재질에도 넣었다. 기본 텍스처는 노이즈 볼륨 에셋이고 상자의 동적 재질 인스턴스에서 위 렌더 타깃으로 덮어썼다(읽어 확인).
- 스크립트: `Tools/scratch/fog3d/` 의 `editor_fog3d_tex_probe_setup.py`, `fog3d_tex_setup.py`, `make_b1_plan.py`, `measure_b1_p5pre.py`, `summarize_b1_cost.py`. 수식판 상자는 기존 `fog3d_setup.py create` 로 다시 만들었다(그 도구가 m3/setup.json 을 다시 씀, 값 같음).

## P5 예비 (3인칭, 최대 거리 20m, 격자 8x128, 소광 0.25, 장면당 8장 평균)

| 지표 | 값 |
|---|---|
| 영향 영역 M (|N-C| >= 2) | 화면의 18.62% (0a 3인칭 18.9% 와 같은 수준), D(N,C) = 7.865 (0a 7.91) |
| D(N, 텍스처 상수) | 0.132 |
| 잡음 바닥 D(N,N') / D(T,T') | 0.165 / 0.152 (사용 0.165) |
| 비 = D(N,T) / 잡음 바닥 | 0.80 (기준 3 이하) |
| D(T, 상자 없음 X) / D(N, X) | 54.70 / 54.64 (안개가 없으면 이만큼 달라짐) |
| M 안 평균 밝기 N / T / C / X | 87.56 / 87.61 / 79.69 / 33.40 |

**판정: 읽는다.** 구분 확인(안 읽으면 기본 텍스처가 읽혔을 가능성 배제): 알파 0 렌더 타깃을 넣은 상자 Z 는 X 와 같다. D(Z,X) = 0.248, D(N,Z) = 54.8, 같은 세션에서 D(N,T) = 0.128 (`p5zero_judge.json`). 즉 상자가 실제로 렌더 타깃의 알파 값을 따른다.
한계: 상수 내용만 확인했다(무늬 있는 내용은 0b-3). 엔진 시간 섞기 때문에 렌더 타깃을 바꿨을 때의 반응 속도는 이번에 재지 않았다(P7 은 이 임무 밖).

## P6 재질 비용 (열 GPU/VolumetricFog, 구도마다 F T T F T F F T 여덟 구간, 구간당 csvprofile 400프레임에서 앞 60행 버려 340프레임, 최대 거리 20m)

| 구도 | 수식판 중앙값 ms | 텍스처판 중앙값 ms | 차(텍스처 - 수식) | 평균 차 | 구간 중앙값끼리 차 | 판정(0.10 ms 이하) |
|---|---|---|---|---|---|---|
| 3인칭 | 1.5115 | 1.5231 | +0.0115 | +0.036 | +0.0125 | 통과 |
| 탑다운 | 1.5522 | 1.5572 | +0.0050 | +0.025 | +0.0031 | 통과 |

같은 재질 구간끼리 중앙값 퍼짐: 3인칭 수식 0.008·텍스처 0.003, 탑다운 수식 0.006·텍스처 0.004 ms. 차가 퍼짐과 같은 수준이라 "있다 해도 0.02 ms 이내"로 읽는다. 구간마다 GPU 시간 최대값은 2.2~3.2 ms 로 튀는 프레임이 있다(전 구간 비슷).
한계: 뷰포트 크기를 맞추는 `r.ScreenPercentage` 조정은 하지 않았다(짝 비교라 영향이 작다고 보나 절대값은 선행 사례 1.1~1.5 ms 와 다른 조건). 숨긴 상자가 안개 비용에 0 이라는 것은 P5 에서 숨긴 상자가 안개를 못 낸다는 것으로만 본 추정이다. 상자는 한 번에 하나만 보이고 나머지는 게임 숨김.

## 겪은 문제

1. `unreal.get_transient_package` 가 없다 -> `unreal.find_object(None, "/Engine/Transient")` 로 임시 패키지를 얻어 `rename`. 첫 시도에서 이미 생긴 렌더 타깃(이름 `TextureRenderTargetVolume_0`)은 다음 시도에서 찾아 재사용했다.
2. 촬영 도구가 끝나도 PIE 가 켜진 채 남는다. 그 상태에서는 `get_all_level_actors` 가 PIE 월드 기준이라 편집 월드의 상자가 안 보인다(add-zero 가 "텍스처판이 없다"로 실패). `editor_request_end_play()` 뒤 6초 기다려 해결. 메모리 재질은 `does_asset_exist` 가 False 이므로 `find_object` 로 찾는다.
3. 콘솔 한글 깨짐은 출력만의 문제.
4. 장부(Worklog·NOW·Lessons)는 쓰지 않았다(부모 세션 몫). 후보 교훈: 위 2번.

## 뒤처리 (직접 확인)

PIE 종료 요청, 격자 8x128·시간 섞기 0.9·`r.TDFogScattering` 1·GPU CSV 통계 0 복원(읽은 값 8/128/0.9/1). 두 상자·두 재질·두 렌더 타깃 제거 확인. 비어 있던 `Content/Repro/Fog3D`·`Content/Repro` 폴더 삭제. `git status` 의 Content·Config·Source 목록은 시작 때와 같다(내 변경 0). 레벨 저장 안 함. 에디터는 열어 둠.
