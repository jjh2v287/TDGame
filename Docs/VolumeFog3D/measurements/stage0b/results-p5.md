# 0b-3 P5 본 측정 결과 (통과)

작성 2026-11-12 0b-3 재실행 작업자(앞 작업자의 촬영 실패 뒤 이어서 함). 기준: `Docs/VolumeFog3D/stage0/prereg-v2-0b.md` P5. 판정 도구는 0a 와 같은 `Tools/scratch/fog3d/measure_fog3d_hole.py judge`.

## 판정 표 (직접 측정, 장면별 8장 평균, 영향 영역 |N-C| >= 2)

| 항목 | 128x128x64 (판정 대상) | 40x40x10 (정보) | 0a 수식판 값 |
|---|---|---|---|
| 3인칭 20m 8픽셀 D(A,B) | 6.064 | 5.844 | 6.03 |
| 3인칭 D(N,C) | 7.931 | 7.805 | 7.9 |
| 3인칭 D(A,A') 잡음 바닥 | 0.084 | 0.118 | - |
| 3인칭 영향 영역 넓이 | 18.85% | 18.91% | - |
| 탑다운 D(A,B) (정보) | 3.004 | 2.931 | 3.09 |
| 탑다운 D(N,C) (정보) | 18.886 | 18.588 | 18.58 |
| 탑다운 D(A,A') | 0.123 | 0.097 | 0.088 |
| 도구 자체 판정 | 3인칭 보임, 탑다운 보임 | 3인칭 보임, 탑다운 판정 불가(D(A,B) 가 1.0~3.0 구간 아래라는 도구 문구, 정보용) | - |

- 통과 기준 4.2~7.8 (0a 값 6.03 의 0.7~1.3배): 6.064 는 범위 안, 0a 값 대비 1.006배. **P5 통과.**
- 화면에 안개가 나오는가: 나온다. D(N,C)=7.93 이 0a 값 7.9 와 같고 영향 영역 18.9% 라서 무늬 쓰기(`TD.Fog3D.ProbeWrite`)가 먹었고 안개가 보인다. 3인칭 A 평균 그림(texture128)을 눈으로 봤고 안개 속 원통·판 윤곽이 보인다.
- 잡음 바닥 점검: D(A,A')=0.084 라서 두 번 찍은 같은 무늬 사이 차이가 매우 작다(0a 의 0.08~0.12 와 같은 수준). 두 해상도의 모든 변형 result.json 에 errors 가 비었고 key_errors 도 비었다.
- 수식판 대 텍스처판(128) 평균 그림의 전체 화면 평균 절대 차이: A 0.44, B 0.52 (0~255 밝기). 눈으로 같게 보이는지는 감독이 그림 쌍으로 본다.
- 40x40x10 은 D(A,B) 5.844 로 128 판과 3.6% 차이. 정보 표라서 통과 판정에는 쓰지 않았다. 칸 15cm·층 30cm 라서 번지는 정도는 그림 쌍으로 본다.
- 이 해석에서 도구 문구 '판정 불가'(탑다운 40x40x10)는 0a 전용 탑다운 구간 규칙이 낸 것으로 P5 판정과 무관하다. gates(P1·P2)도 0a 전용이라 의미 없다.

## 실행 기록

1. 앞 작업자 실패 원인(직접 확인): 그가 Git Bash 에서 `--level /Game/...` 을 줘서 경로가 Windows 경로로 바뀌어 `does_asset_exist` 가 False 였다(L-repo-01, Docs/Lessons/repo.md 15행에 같은 증상 기록 있음, 맵 파일·에셋 레지스트리는 정상이었음). PowerShell 로 실행하니 바로 통과했다. 스크립트는 고치지 않았다. **고친 줄 없음.**
2. 에디터 확인 통과(pid 30160), 사전 `git status` 가 앞 작업자 기록과 같음(`b1/git_status_p5_before2.txt`).
3. 시험 물체 재생성: 수식판 상자, 128x128x64 텍스처판 상자+볼륨 렌더 타깃, 40x40x10 볼륨 렌더 타깃(모두 compile_errors 비어 있음). `TextureRenderTargetVolume` 의 `b_hdr`·`b_supports_uav`·`b_can_create_uav` 읽기 실패 경고는 계속 나오며(속성 이름만 못 찾는 읽기 경고) 촬영 결과에는 영향이 없었다.
4. 촬영: full·small 각 변형 10개 x 8장, 종료 코드 0 (`b1/p5/{full,small}/run`, 로그 `capture2.log`). 판정: `b1/p5/{full,small}/judge.json`.
5. 비교 그림: `b2/p5_compare.py` (수식판 8장 평균은 0a 촬영 `m3/full/third_d20g8` 의 A·B 원본에서 다시 평균, 텍스처판은 이번 촬영 평균, 960x540 PNG).

## 겪은 문제

- **에디터가 뒤처리 중 죽었다(충돌).** `fog3d_p5_small_setup.py remove` 실행 중 에디터가 `UObject::Rename` (Obj.cpp 383 줄, 파이썬 `rename` 호출)에서 치명 오류로 종료했다. 크래시 폴더 `Saved/Crashes/UECC-Windows-6622963848295380E89DE29B526F8707_0000`, 시각 20:37. 앞 작업자의 같은 `remove` 는 촬영 없이 돌아서 문제가 없었고, 이번엔 PIE 촬영을 마친 직후였다(원인 확정 못 함: PIE 가 렌더 타깃을 잡고 있을 때 이름 바꾸기가 충돌했다는 추정만 있음). 측정은 모두 끝난 뒤였다. 나머지 세 뒤처리 명령은 연결 거부(NO REMOTE NODES).
- 복구: `python Tools/ue_editor.py ensure` 로 에디터를 다시 띄움(새 pid 24292, 포트 8000 열림). 메모리에만 있던 임시 물체·재질·렌더 타깃·저장 안 된 맵 변경은 에디터와 함께 사라졌다(디스크에 쓰인 것 없음). 새 에디터에서 `editor_fog3d_reset.py` 성공. 새 에디터라서 PIE 도 꺼져 있다.
- 감독 확인 거리: (a) 실행 중이던 에디터를 닫지 말라는 지시였으나 충돌로 닫혔고 다시 띄웠다. 저장 안 된 맵 변경 'LV_TDFluidFogStage'(감독이 버리기로 한 것)도 이 충돌로 사라졌다. (b) 새 Lessons 후보: "P5 촬영 직후 볼륨 렌더 타깃 `rename` 으로 에디터 충돌 — 뒤처리는 PIE 종료 뒤에, 또는 `remove` 전에 PIE 를 끄고 시도". 장부는 쓰지 않았다.

## 뒤처리 결과

- 임시 물체·재질·렌더 타깃: 에디터 재시작으로 모두 사라짐(디스크 파일 없음, `Content/Repro/Fog3D` 빈 폴더 삭제). 안개 값: `editor_fog3d_reset.py` 로 격자 8픽셀·128층·시간 섞기 0.9·산란 1 로 원복 성공(새 에디터 기본값이기도 함).
- `git status --short Content Config Source` 촬영 전과 같음 (Content·Config 새 변화 0건, 기존 `TDFluidPresetTuning.json` 수정과 소스 변경은 시작 전부터 있던 것). 레벨·에셋을 저장하지 않았다.
- 에디터는 켜져 있다(pid 24292, 포트 8000).

## 복사한 파일 (`Docs/VolumeFog3D/measurements/stage0b/`)

- `judge-p5-full128.json`, `judge-p5-small40.json`, `results-p5.md`
- 그림 6장(각 960x540, 약 170KB): `third_d20g8_{A,B}_formula_mean.png`, `third_d20g8_{A,B}_texture128_mean.png`, `third_d20g8_{A,B}_texture40_mean.png`
