# H-01 회전 횡베기 v2 — 사용자 리그·몸통 회전 베기 재제작 (2026-10-08, claude)

교훈: L-anim-13
명령: `python Tools/Cascadeur/build_cascadeur_animation.py --spec AnimationSources/Cascadeur/AS_TD_Player_SwordSpinSlash01.motion.json --stages character,keys,scene,capture` → `python Tools/Cascadeur/check_cascadeur_animation.py --spec AnimationSources/Cascadeur/AS_TD_Player_SwordSpinSlash01.motion.json`

## 사용자 지적과 원인

- "리타겟이 이상함, 팔꿈치랑 손가락 등이 이상" → v1 근접 렌더에서 손가락이 갈퀴처럼 늘어나고 아래팔이 꽈배기로 보였다. 원인: 손가락 박스(FK 조작자)에 회전만 쓰고 위치를 안 써서 손가락이 기준 자세 자리에 남음(첫 마디 5.9cm → 최대 63.3cm). 아래팔 비틀림은 풀이 허용치 75°까지 쓰였다.
- 사용자 리그 `AnimationSources/Cascadeur/UE5_Manny.casc`(공식 샘플 리그, 루트 제약 포함)는 v1 리그와 관절·조작점 구성이 같고 조작점 위치가 최대 3.9cm 다르다. 이 리그의 clavicle_l `Position` 데이터는 0으로 읽혀 Transform 전역값으로 읽도록 바꿨다.
- "팔만 베기가 아니라 몸통 전체도 회전하면서 베기" → 팔을 몸 오른쪽 앞으로 편 뒤 타격 구간 동안 팔·칼을 가슴에 고정하고 골반·척추가 돌게 바꿨다.

## 결과 수치 (Cascadeur가 푼 관절 + 칼 쥐기, `H-01-sword-spin-slash-v2-check-2026-10-08.json`)

| 항목 | v2 |
|---|---|
| 타격 구간 | 20~23프레임, 칼 방위각 −37.3 → −13.3 → 10.7 → 32.8°(왼쪽 → 오른쪽), 정면 통과 22프레임 |
| 몸통 주도 | 타격 구간 칼 70.0° 회전 중 가슴 65.8° 회전, 가슴 기준 칼 방향 변화 4.2°(팔·칼 고정). 17~24프레임 몸통 156° 회전 |
| 칼끝 높이 | 122.4cm 일정(수평), 칼 전체 회전 359.4°, 칼끝 최고 39.1m/s |
| 팔·손목(풀이 보고) | 오른팔꿈치 타격 중 126~160°, 감기 85°, 왼팔꿈치 57~95°, 오른손목 꺾임 ≤29.5°·비틀림 ≤50° |
| 뼈 길이 | 최대 변화 0.07cm(v1 손가락 63.3cm) — 손 근접 렌더 `H-01-sword-spin-slash-v2-hand-closeups-2026-10-08.png`(v1: `…-v1-hand-closeups-…`) |
| 발·루트 | 볼 미끄러짐 왼발 0.95·오른발 0.78cm/프레임, 최저 관절 0.67cm, 루트(제약, 골반 투영) z −3.3 → 36.7cm(40cm 전진), 회전 추종 꺼짐 |
| 씬 | `AnimationSources/Cascadeur/AS_TD_Player_SwordSpinSlash01.casc` 54프레임(0~44 매 프레임 키 + 52 끝 + 유지 1), 사용자 원본 `UE5_Manny.casc`는 수정하지 않음 |

## 남은 한계

- FBX 내보내기는 여전히 라이선스(basic)로 막혀 UE 가져오기·PIE는 미실행(L-anim-08).
- 칼끝 39m/s는 빠른 편이다. 손목 꺾임·몸통 회전 속도는 사용자 시각 확인으로 조정한다.
