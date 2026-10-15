# H-01 Cascadeur 제작 파이프라인과 플레이어 회전 횡베기 시험 (2026-10-08, claude)

교훈: L-anim-08, L-anim-09, L-anim-10, L-anim-11, L-anim-12, L-repo-09
명령: `python Tools/Cascadeur/build_cascadeur_animation.py --spec AnimationSources/Cascadeur/AS_TD_Player_SwordSpinSlash01.motion.json` → `python Tools/Cascadeur/check_cascadeur_animation.py --spec AnimationSources/Cascadeur/AS_TD_Player_SwordSpinSlash01.motion.json`

## 환경

- Cascadeur 2026.2.3, 라이선스 basic(무료): `is_export_available=false`, `is_pro_features_available=false`
- 내장 MCP 스크립트 서버 0.3.1(`127.0.0.1:8765/mcp`, 도구 `run_script`)이 Cascadeur 시작 때 자동 기동(`setup_cascadeur.py` → `td_events/scene_idle`)
- MCP 흐름 시험: initialize(서버 프로토콜 2024-11-05)·initialized 알림·tools/list·tools/call 모두 200 응답, `ping`만 미구현(-32601)

## 단계별 결과

| 단계 | 결과 |
|---|---|
| UE 캐릭터 FBX 내보내기 | `SKM_Manny_Simple` → `Saved/Cascadeur/Characters/SKM_Manny_Simple.fbx`(2.9MB) |
| Cascadeur 가져오기·UE5 퀵 리깅(Python만, 대화상자 없음) | 관절 89·점 43·박스 69·레이어 13, 13.7초. 공식 샘플 `UE5_Manny.casc`와 구성 일치 |
| 텍스트 요청 → 동작 명세 | `AnimationSources/Cascadeur/AS_TD_Player_SwordSpinSlash01.motion.json`(지정 키 15, 생성 키 5 = 20키, 53프레임 + 유지 1) |
| 키 풀이(FK·2관절 IK·손목 인지 칼 쥐기 탐색) | 손 도달 오차 최대 0.7cm(16프레임, 팔을 완전히 펴도 모자람 — 손 위치 탐색이 손목 부담과 바꾼 결과), 오른손목 꺾임 최대 66.6°(17프레임), 아래팔 비틀림 최대 75.3° |
| Cascadeur 씬(키·clamped Bezier) | `AnimationSources/Cascadeur/AS_TD_Player_SwordSpinSlash01.casc`, 54프레임, 키 역읽기 최대 오차 0.40cm |
| 3방향 Cascadeur 렌더 | 정면·측면 접촉 시트, 위 GIF(이 폴더) |
| FBX 내보내기 | **라이선스로 중단**: Free(basic)는 FBX·DAE 내보내기 불가(Indie 이상 필요) |
| UE 가져오기·PIE | 미실행(FBX 없음). 가져오기 단계 코드는 기존 `import_animation_fbx` 경로 재사용 |

## Cascadeur 결과 점검(Cascadeur가 푼 관절 + 칼 쥐기 좌표로 계산)

- 타격 구간 17~20프레임, 칼 방위각 −90.3 → −35.1 → 14.9 → 59.8°: 왼쪽 → 오른쪽, 정면 통과 19프레임
- 칼끝 높이 123.6~131.4cm(가슴 높이 수평), 칼 전체 회전 403.1°(등 뒤 → 왼쪽 → 정면 → 오른쪽 → 오른쪽 뒤)
- 칼끝 최고 속도 43.0m/s(17프레임)
- 루트: (0,0,0) → (0,0,45)cm 정면 이동, 옆 이동·회전 없음(몸 회전은 골반·척추)
- 발: 볼 미끄러짐 왼발 1.54cm/프레임(30프레임 착지), 오른발 0.74cm/프레임 — 허용치 2cm/프레임 안(`foot_slide_within_tolerance=true`), 최저 관절 높이 0.54cm(지면 관통 없음)
- 독립 반박 검증(td-refuter, `Saved/AgentOps/20261008/refute-H-01.md`): 키 풀이 재실행 바이트 동일, 저장 .casc 재계산 수치 동일, 내보내기 중단·MCP 흐름 재현. 지적 4건(본 문서 도달 오차 표기, 남은 FBX를 성공으로 보던 판정, 미사용 허용치 상수, docstring 셸 표기)은 수정 후 재확인

## 해석과 한계

- 요청 해석: "왼쪽으로 뒤돌며"는 반시계 감기(약 110°, 등이 대상 쪽), "회전하듯 좌→우 횡베기"는 시계 방향으로 풀며 정면을 왼쪽에서 오른쪽으로 지나는 백핸드 수평 베기로 보았다. 오른손 칼을 펴고 반시계로만 돌면 칼은 오른쪽 → 왼쪽으로 지나므로 두 조건을 함께 만족하는 해석은 감기-풀기다.
- AI 모션 생성(`View.Inbetweening_RunMotionGeneration`)을 회수 구간에 시험했더니 무료 등급에서도 동작했지만 키를 덮어써 경계 튐(칼끝 22 → 38cm/프레임)과 발 미끄러짐(17.7cm/프레임)이 생겨 최종본에는 쓰지 않았다(L-anim-11).
- 품질 판단은 사용자 시각 확인이 필요하다. 손목 꺾임 66.6°는 백핸드 손목 젖힘 범위 안이지만 큰 편이다.
