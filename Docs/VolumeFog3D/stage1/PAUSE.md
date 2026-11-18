# 1단계 설계 — 일시 중지 지점 (2026-11-13 02:23, 사용자 재부팅)

**재개함(2026-11-13 09:40).** 아래 "먼저 정할 것" 1·2 는 덧붙임 규칙 `prereg-design-2b.md` 로 정했고, 이어 하기 워크플로 wf_c18bef5f-a41 을 돌렸다. 이후 진행은 `Docs/VolumeFog3D/progress.md` 의 "재개" 절을 본다. 이 파일은 기록으로 남긴다. (아래에 나오는 명세 초안 `spec-v1-draft.md` 는 확정본 `spec.md` 로 대체되어 2026-11-13 에 지웠다.)

## 어디서 끊었나

| 묶음 | 상태 |
|---|---|
| 1차(기존 경로 조사, 압력 실험, 기준 모의, 구현 지도, 반박 2건) | 끝. 보고서 `Saved/AgentOps/20261112/fog3d/stage1/` |
| 반박 지적 판정, 2차 규칙 `prereg-design-2.md`(sha256 앞 16자 3a0ddd47b0f5b729) | 끝(실험 전 등록) |
| 명세 초안 `spec-v1-draft.md`, 1a 통과 기준 초안 `prereg-1a-draft.md`, 계획서 7절 다시 짜기 | 끝(초안) |
| 2차 워크플로 `wf_b92e5c19-96e` — K1a 핵심 계산 v2 | 끝. 결과가 일지에 기록됨. `Tools/scratch/fog3d/stage1/r2/fog3d_core2.py`, 정의서 `Saved/AgentOps/20261113/fog3d/stage1r2/k1a-core.md` |
| 같은 워크플로 — K1b 판정 장면 모듈 | 끝. 결과가 일지에 기록됨. `r2/gates2.py`, `r2/tuned_solvers.json`, 보고서 `stage1r2/k1b-gates.md` |
| 같은 워크플로 — 선택 실험 3건(EA 압력 / EB 옮기기·막힌 칸 / EE 층 배치) | 시작 15초 뒤 중지. 만든 파일 없음. 다시 돌려야 한다 |
| 최종 통합(K3), 수치 반박(V3), 명세 검토(V4) | 시작 전 |

끊을 때 확인한 것: 워크플로 중지됨(TaskStop), 실험 파이썬 프로세스 없음(남은 python 2개는 Serena 서버), 빌드 보류 파일 없음, 언리얼 에디터는 켜져 있으나 저장 안 한 패키지 0(이번 설계 작업은 에디터를 쓰지 않음). 커밋하지 않았다.

끊을 때 파일 지문(sha256 앞 16자): 아래 "재개 방법" 1번에서 다시 대조한다.
- `r2/tuned_solvers.json` — 압력 후보의 가속 계수·순환 꼴(판정 전에 고정된 값). 지문은 `Docs/VolumeFog3D/progress.md` 에 적음.

## 재개 때 감독이 먼저 정할 것 (K1b 가 찾은 것)

1. **왕복 시험(D-G12c) 실패**: 정밀 기준 구성에서도 두 주기 뒤 차이값 최대 0.0423(기준 0.04), S8 은 0.055. 압력과 무관하고, 안개 윗면 흐림 층에서 주기마다 오차가 커진다. 규칙상 "식의 결함 — 원인과 함께 적고 감독이 다룬다(판정선은 그대로)". 할 일: 원인(차이값 보간의 번짐인지, 2판 식의 φ 전환 때문인지)을 작은 확인 1건으로 가리고, 고칠지·실패로 기록하고 갈지 정한다.
2. **깎기 식의 구멍**: 칸 가운데가 붓 반지름 밖이면(점 가중치 0) 칸의 높이 범위가 붓과 겹쳐도 깎지 않는다. 반지름 20cm 붓에서 높이 평균 가중치가 0 보다 큰 칸 12개 중 4개만 깎인다. 할 일: 머문 시간 비율을 "가로 거리"로만 재도록 정의를 보완하고(덧붙임 문서를 실험 전에 등록), 핵심 모듈을 고친 뒤 깎기 높이 연속성 확인을 더한다.
3. K1a 가 적은 한계: 성분별 옮기기(A3)에서 맨 위 열린 면의 바람이 맨 위 층 가운데에서 읽힌다(윗면 흐름이 약해질 수 있음, 크기 미측정). 새 띠 처리는 "들어온 계산에서 옮기기 결과를 0"으로 해석했다.

## 재개 방법

1. 지문 대조: `sha256sum Tools/scratch/fog3d/stage1/r2/*.py Tools/scratch/fog3d/stage1/r2/tuned_solvers.json` 이 progress.md 에 적은 값과 같은지 본다.
2. 위 1·2 를 정한다.
   - 핵심 모듈을 고치지 않기로 하면: `Workflow({scriptPath: "C:\\Users\\jjh\\.claude\\projects\\C--Project-TDGame\\39141651-0a11-4fe1-a255-76c9013907ac\\workflows\\scripts\\fog3d-stage1-design-r2-wf_b92e5c19-96e.js", resumeFromRunId: "wf_b92e5c19-96e"})`. K1a·K1b 는 저장된 결과로 넘어가고 선택 실험부터 실제로 돈다. 재개 직후 일지(`...\subagents\workflows\wf_b92e5c19-96e\journal.jsonl`) 끝의 started 가 EA·EB·EE 인지 확인하고, K1a·K1b 가 다시 started 면 바로 멈춘다.
   - 핵심 모듈을 고치기로 하면(깎기 식 보완 등): 덧붙임 규칙을 먼저 등록 → Opus 1명이 `fog3d_core2.py`·`gates2.py` 수정과 자체 시험 → 위 스크립트에서 K1a·K1b 호출을 "보고서 경로를 담은 결과 상수"로 바꾼 이어 하기 스크립트로 선택 실험부터 돈다(resumeFromRunId 는 쓰지 않는다).
3. 워크플로가 끝나면: `stage1r2/k3-final.md`·`v3-refute-numerics.md`·`v4-review-spec.md` 를 읽고 → 명세 v1 확정(`spec.md` 를 초안 내용 + 실험이 정한 칸으로 교체) → `prereg-1a.md` 숫자 채워 잠금 → 결정 기록(`Docs/Tasks/decisions.md`) → Worklog 완료 항목 → NOW → 그림과 함께 사용자 보고.

## 사용자에게 아직 보고하지 않은 것

- 1차 실험의 자세한 결과와 그림(1차 그림은 `Docs/VolumeFog3D/measurements/stage1-design/`). 최종 보고는 2차가 끝난 뒤 한 번에 한다.
- K1b 기본 확인(정밀 기준 구성): 내려찍기 고리 바람 +24.7 cm/s·기둥 농도 −71%, 높이 구분 2.20(기준 2.0), 다시 차오름 2.31초, 이음매 누수·달리기 창 이동·먼 좌표·빈 곳 유지 통과.
