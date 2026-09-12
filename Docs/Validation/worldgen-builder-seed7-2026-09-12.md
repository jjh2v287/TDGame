# TDWorldGenBuilder Forest seed 7

- 결과: PASS, 점수 100.0 / 100
- 오류 0, 경고 0, 정보 6

| 심각도 | 코드 | 메시지 | 위치(cm) | 관련 |
|---|---|---|---|---|
| Info | entrance_spacing | 입구 4개, 최소 간격 18847 cm (기준 ≥ 15000 cm) | (0, 0, 0) | None |
| Info | poi_density | POI 9개 (범위 7~11), 최소 간격 5911 cm (기준 ≥ 4000 cm) | (0, 0, 0) | None |
| Info | road_reach | 대상 19개 중 19개 도달 (도로 점 271개, 허용 4000 cm) | (0, 0, 0) | None |
| Info | terrain_fit | 대상 20개 중 20개 적합 (최대 경사 25°, 수면 -175 cm) | (0, 0, 0) | None |
| Info | streaming_safety | 슬롯 3개, 로딩 범위 12800 cm (필요 간격 ≥ 25600 cm, 필드 여유 ≥ 20000 cm) | (0, 0, 0) | None |
| Info | play_density | 콘텐츠 12000 cm 밖 도로 구간 2개, 최장 3949 cm (허용 ≤ 18000 cm) | (0, 0, 0) | None |

## 입력

- 월드 정의: /Game/World/Definitions/DA_TDWorld_Main.DA_TDWorld_Main
- 지역 정의: /Game/World/Definitions/DA_TDRegion_Forest.DA_TDRegion_Forest
- 아틀라스: /Game/World/Definitions/DA_TDDungeonAtlas_Main.DA_TDDungeonAtlas_Main
- 범위(cm): (-50400, -50400) - (50400, 50400)
- 앵커 4, 잠긴 POI 0, 잠긴 입구 0, 잠긴 도로 0
- 지역 필터: (전체)

## 결과

- POI 15, 입구 4, 도로 25, 배제 19
- 레이아웃 해시: 1238411181
- 검증(-Validate): PASS (생성 시 지형 검증 PASS, 독립 검증 점수 100.0 PASS)
- 베이크(-Bake): 실행 안 함
