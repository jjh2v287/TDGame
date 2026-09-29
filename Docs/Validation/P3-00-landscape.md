# P3-00 랜드스케이프 생성 파이프라인 검증 (2026-09-30 claude)

정본: numpy 생성기 + raw 입력 C++ 함수(사용자 결정 2026-09-30, `Docs/Tasks/decisions.md`). 절차: `Tools/WorldGen/README.md` "지형·도로·강·바이옴 마스크 절차".

## 1. 같은 시드 2회 생성 해시 (시드 7, 출력 폴더 A·B)

| 파일 | SHA-256 앞 16자 | A = B |
|---|---|---|
| height.r16 (2,036,162 B) | 3ee87b317681e885 | 같음 |
| layer_Bedrock.r8 | dd217c09da686e68 | 같음 |
| layer_Moss.r8 | 7f2807bcf10f9a1f | 같음 |
| layer_Mud.r8 | a636f6fa7248e6bf | 같음 |
| layer_Road.r8 | 0409ffb4dc50759e | 같음 |
| layer_Rock.r8 | 3b4726f034e9ffb9 | 같음 |
| layer_Soil.r8 | f7224f4b7dd761c9 | 같음 |
| layout.json | 511317a76c70122d(출력 경로 문자열 정규화 후) | 같음 |

현재 베이크 원본 `Saved/WorldGen/AshenVale/height.r16`도 시드 7 결과와 같다.

## 2. 에디터 측정 (`/Game/Level/LV_DarkFantasy_OpenWorld`)

| 항목 | 값 |
|---|---|
| Landscape 액터 | 1 (스케일 100) |
| LandscapeStreamingProxy | 64 (8×8, 월드 파티션 그리드 2) |
| 랜드스케이프 컴포넌트 합 | 256 (16×16) |
| 프록시 합집합 XY 범위 | −50,400 ~ 50,400 cm = 1,008 m × 1,008 m (조건 500 m × 500 m 이상) |

측정 스크립트: 에디터 Python으로 레벨 로드 후 `LandscapeStreamingProxy` 바운드·`LandscapeComponent` 수 집계(2026-09-30).
