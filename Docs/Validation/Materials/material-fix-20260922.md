# Goblin·Hyena 재질 수정 검증

- 날짜·담당: 2026-09-22 codex, 사용자 원본 백업 후 수정 승인.
- Goblin: 빈 FBX 재질의 부모를 신규 M_TD_Goblin_PBR로 변경. BaseColor·Normal·Roughness·Metallic 연결. Roughness·Metallic의 sRGB 해제. Height는 별도 변위 요구가 없어 미연결 유지.
- Hyena: Hyenas_A1 인스턴스 Translucent → Masked, 양면 렌더링, OpacityMaskMap에 기존 Opacity 텍스처 연결, 마스크 가중치 1, 발광 맵 가중치 0. Opacity sRGB 해제.
- 수정 에셋 5개: Goblin, goblin_base_DefaultMaterial_Roughness, goblin_base_DefaultMaterial_Metallic, Hyenas_A1, Hyenas_A1_Opacity. 신규 에셋 1개: M_TD_Goblin_PBR.
- 기존 메시·스켈레톤·애니메이션·엔진 공용 부모 재질은 변경하지 않음. 두 Hyena 메시가 같은 재질을 참조하므로 함께 반영.
- 검증: 신규 부모 셰이더 재컴파일 성공, 재질 3개 디스크 재로드 성공, 입력·색공간·마스크·양면·공유 참조 assertion 통과, 미저장 패키지 없음. 고블린 섬네일 및 Hyenas_A11·AllMotion 메시 에디터 정면에서 시각 확인. PIE 실행 및 모든 애니메이션/각도 검증은 미실시.
- 캡처: goblin-after-20260922.png, hyena-after-20260922.png. 구조 검증: material-fix-20260922.json.
- 원본 백업: Tools/scratch/20260922_codex_materials/backup (Git 제외). 변경 전후 SHA256: backup-manifest-20260922.json.
- 도구: 기존 run_in_editor.py·uemcp.py 사용, 일회성 스크립트는 Tools/scratch에만 위치. 재질 파라미터 setter 반환 False는 UE 5.8 엔진 구현상 고정값이므로 getter로 실제 변경을 검증함.
- Git: 대상 에셋은 사용자 미추적 상태를 유지. LFS 속성 확인, 스테이징·커밋하지 않음.
