# 01. 무한·초대형 절차적 세계와 변경 저장: 실제 게임 구현 사례 (웹 조사)

작성: claude 하위 에이전트, 2026-10-23. 갈래: 웹 자료 조사(사례 정리). 범위 밖: 엔진 내부 설계안(다른 갈래).

## 0. 근거 표기 규칙

- [검증] = 원문을 직접 읽었고, (두 출처가 일치) 또는 (개발사 공식 글·공식 패치 노트·공식/표준 위키·엔진 소스 같은 1차 출처)인 경우.
- [추정] = 검색 결과 요약만 보았거나, 단일 2차 출처(커뮤니티 글·호스팅 업체 문서), 또는 내 계산·추론.
- 웹 출처는 URL, 엔진은 `UE_5.8 경로:줄`.

## 1. 결론 먼저 (이 갈래의 답)

1. 실제 무한 세계 게임은 모두 같은 세 기둥을 쓴다. (가) 생성은 `f(시드, 좌표)`의 순수 함수여서 이미 놓인 것을 모른다, (나) 세계를 고정 크기 청크(조각)로 나눠 플레이어 주변만 생성·적재·시뮬레이션한다, (다) 저장은 "방문한 청크 전체 스냅샷"이거나 "시드 + 변경분(델타)" 중 하나다. Factorio 개발 글이 (가)를 명시한다: "The terrain generator can't know anything about what is already placed." [검증] https://factorio.com/blog/post/fff-390
2. "무한"의 실제 한계는 수치다. Minecraft 는 노이즈 정수 오버플로로 12,550,824 에서 Far Lands 가 생겼고(Beta 1.8 에서 수정), 지금은 ±30,000,000 블록 경계를 둔다 [검증]. UE5 는 위치가 배정밀도(double)이고 최대 세계 크기가 약 8,796만 km 이므로(엔진 소스) 걷는 탑다운 게임에서 좌표 한계는 사실상 도달 불가하다 [검증/추정 혼합, 6절].
3. 저장 전략의 교훈: 전체 스냅샷(Minecraft·Luanti·Vintage Story·Factorio)은 단순하지만 탐험량에 비례해 세이브가 커진다. 시드+델타(No Man's Sky)는 작지만 상한(cap)이 생기고 생성기가 버전 간 바뀌면 델타가 엉뚱한 바탕에 적용된다. No Man's Sky 는 실제로 지형 편집 상한·재생성 문제를 여러 패치에 걸쳐 고쳤다 [검증].
4. 유한을 택한 게임(Terraria·Valheim·Core Keeper·Enshrouded)의 공통 이유는 "기술 불가"가 아니라 설계다: 전 세계를 보고 배치하는 생성 단계(Terraria 100개 이상 패스), 중심에서의 거리 = 진행도(Valheim·Core Keeper), "장소감·이야기할 수 있는 장소"(Enshrouded 개발자 발언) [검증/추정 혼합, 5절].
5. TDGame 에 가장 가까운 선례는 탑다운 무한 2D 인 Factorio(32×32 타일 청크, 노이즈 식, 결정론 락스텝)다. 권고 골격은 "청크 순수 함수 생성 + 정수 해시 난수 + 손대지 않은 청크는 저장 안 함, 손댄 청크는 통째 저장(청크별 생성기 버전 기록)"의 혼합형이다 [추정, 7절].

## 2. 사례별 구현

### 2.1 Minecraft (Java Edition) — 3D 복셀 무한 세계의 표준

청크와 로딩
- 청크는 16×16 블록, 바닥부터 건축 한계까지, 구획(section)은 16 블록 높이. 오버월드 높이 384. [검증] https://minecraft.wiki/w/Chunk
- 로딩은 "티켓" 기반 적재 수준(load level) 22~44. 31 이하 = 엔티티 틱, 32 = 블록 틱, 33 = 경계(접근만 가능), 34 이상 = 접근 불가지만 생성은 진행. 티켓에서 청크 1개마다 수준이 1씩 증가하며 퍼진다. [검증] 같은 URL
- 티켓 종류 예: 플레이어 적재(31), 강제 적재 `/forceload`(31, 세이브에 영속), 포털(30, 반경 3, 15초 만료, 영속), 엔더 진주(2초). [검증] 같은 URL
- 시뮬레이션 거리 s 에 대해 엔티티 틱 영역 한 변 = min(2s+1, 63) 청크. 렌더 거리와 시뮬레이션 거리가 분리되어 있다. 300 게임 틱 동안 유지 티켓이 없으면 차원 처리 중단. [검증] 같은 URL
- 함의: "보이는 범위"와 "돌아가는 범위"를 분리하고, 원격 시설은 명시적 티켓으로만 살린다. 무한 세계에서 시뮬레이션 비용을 유계로 묶는 핵심 장치다. [추정]

생성 파이프라인
- 청크 생성 단계(status): empty → structure_starts → structure_references → biomes → terrain → features → initialize_light → light → spawn → full. full 이전은 "proto-chunk". (예전 noise/surface/carvers 는 26.3 snapshot 10 에서 terrain 으로 통합) [검증] https://minecraft.wiki/w/Chunk_format
- 노이즈 라우터 = 밀도 함수(density function) 묶음. `final_density > 0` 이면 기본 블록(돌), 아니면 공기/유체(대수층 논리). temperature·vegetation·continents·erosion·depth·ridges 는 생물군계 배치용이며 "지형 모양에는 영향 없음". 1.18.2-pre1 에서 도입. [검증] https://minecraft.wiki/w/Noise_router
- 밀도 함수 `interpolated` 는 셀 단위로 값을 구하고 셀 내부를 보간(셀 크기 = size_horizontal×4, size_vertical×4), `cache`(구 cache_once)는 같은 위치 중복 계산 제거, `spline` 은 3차 스플라인. 26.3-snap7 에서 밀도 함수가 배정밀도에서 단정밀도로 바뀜. [검증] https://minecraft.wiki/w/Density_function
- 구조물 배치 `random_spread`: 청크 0,0 기준 spacing×spacing 격자 칸마다 1회 시도, 칸 안 오프셋은 0~(spacing−separation−1). 마을 spacing 34, separation 8, salt 10387312, 이웃 시도 간 최대 거리 2×spacing−separation = 60 청크. `concentric_rings`(요새: distance 32, count 128, spread 3), `exclusion_zone`(약탈자 전초기지는 마을에서 10 청크 배제), `frequency`. [검증] https://minecraft.wiki/w/Structure_set
- 함의: 격자 칸 + salt 해시로 "이 칸에 구조물이 있나"를 이웃 정보 없이 국소 계산한다. 무한 세계에서 전역 배치 문제를 국소 문제로 바꾸는 표준 기법. [추정]

난수와 시드
- 시드는 64비트 부호 정수, 문자열 시드는 `.hashCode()` 로 32비트. 생성 알고리즘이 바뀌면 같은 시드라도 같은 지형이 아니다. "Deleted chunks can regenerate if the seed and generator remain the same." [검증] https://minecraft.wiki/w/Seed_(level_generation)
- 1.18 부터 Xoroshiro 기반 위치 난수 팩토리(`XoroshiroPositionalRandomFactory`, 좌표로 난수원 분기)를 쓴다. 64비트 시드를 SplitMix64 로 128비트 상태로 확장한다는 제3자 설명이 있다. [추정] https://mappings.dev/1.18.2/net/minecraft/world/level/levelgen/XoroshiroRandomSource$XoroshiroPositionalRandomFactory.html , https://packagist.org/packages/axolotl-pm/xoroshiro128pp

저장
- 지역 파일 `r.x.z.mca` 하나에 32×32 = 1024 청크(512×512 블록). 지역 좌표 = 청크 좌표 >> 5. 4 KiB 섹터 정렬, 헤더 = 위치표 4 KiB + 타임스탬프표 4 KiB. 청크당 최대 255 섹터(1020 KiB), 초과 시 외부 `c.x.z.mcc`. 압축: Zlib(기본), 무압축, LZ4(24w04a~, 서버 설정). 청크 내용은 NBT. [검증] https://minecraft.wiki/w/Region_file_format
- 청크 NBT 는 구획마다 팔레트 + 4096 인덱스 비트 압축 배열(최소 4비트)을 통째로 저장, `InhabitedTime`(플레이어 체류 틱 누적, 지역 난이도용), `block_ticks`·`fluid_ticks`(대기 중 갱신), 구조물 starts/references, `DataVersion`. 델타가 아니라 전체 상태 저장. [검증] https://minecraft.wiki/w/Chunk_format
- 생성기 변경 대응: `blend_density` 가 옛 버전에서 생성된 청크로의 매끄러운 전이에 쓰인다. [검증] https://minecraft.wiki/w/Density_function

수치 한계
- Far Lands: 저·고 노이즈 생성기의 정수 오버플로로 12,550,824 에서 지형 붕괴. Beta 1.8 Pre-release 에서 수정. 별개로 부동소수점 정밀도 손실이 2의 거듭제곱마다 커지며 2^24 = 16,777,216 에서 악화. 선택자 노이즈는 1,004,065,924 에서 붕괴("Farther Lands"). [검증] https://minecraft.wiki/w/Far_Lands
- 세계 경계: 기본 각 면이 원점에서 29,999,984, 설정과 무관하게 30,000,000 에 하드 경계. 기본값이 1 청크 짧은 이유는 갑작스러운 낭떠러지 방지(Dinnerbone). [검증] https://minecraft.wiki/w/World_border

### 2.2 Factorio — 탑다운 무한 2D (TDGame 에 가장 가까운 선례)

- 청크 = 32×32 타일(1,024 타일), 1 타일 ≈ 1 m. 맵 최대 한 변 200만 타일(총 4조 타일). 각 플레이어 20 청크 안을 점진 생성, 미생성 청크를 드러내면 즉시 생성, 오염·포자도 청크를 (드러내지 않고) 생성시킬 수 있음. 아무 일 없는 청크는 비활성화(물고기·대기 적 이동 정지). [검증] https://wiki.factorio.com/Map_structure
- 생성 = 노이즈 식(noise expression). C++ 에서 추상 구문 트리(AST)로 표현, 지표(surface) 생성 시 "noise program" 으로 컴파일(2.0 기준 타일·절벽·엔티티+장식 3개 프로그램), 다중 스레드·SIMD(단일 명령 다중 데이터) 최적화. 기저 노이즈(Perlin 계열) + 다중 옥타브(대륙·섬·해안선), 자원은 스폿 노이즈(원뿔 + 기저 노이즈 교란). 최적화로 청크당 18.35 ms → 2.83 ms. [검증] https://factorio.com/blog/post/fff-390 (기저 노이즈 자체는 https://factorio.com/blog/post/fff-112)
- 멀티플레이 = 결정론 락스텝. 행동(입력)만 전송, 모든 피어가 전체 시뮬레이션 수행, 가장 느린 플레이어가 게임 속도를 제한, 지연 버퍼만큼 로컬 행동이 늦어짐. [검증] https://factorio.com/blog/post/fff-76
- 비결정 원인 대응: 직원 Rseding91 이 Windows/Linux 간 `math.sin` 결과 차이를 보고받고 "게임 내부는 대부분의 수학 함수를 자체 구현"한다고 답함. FMA(융합 곱셈-덧셈) 소프트웨어 구현이 비정규수에서 하드웨어와 다를 수 있다는 경고도 있음. [추정] https://forums.factorio.com/viewtopic.php?p=380372 , https://forums.factorio.com/viewtopic.php?p=703180
- 탈동기화(desync) 시 진단 보고서(서버·클라이언트 상태)를 비교, 매 틱 CRC 검사로 개발 중 결정론 버그를 찾음. [추정] https://factorio.com/blog/post/fff-188 , https://factorio.com/blog/post/fff-55
- 세이브 크기: 생성된 청크는 모두 저장되므로 탐험이 세이브 증가의 주요 원인. 기본 게임에 정리 도구가 없어 "미사용 청크 제거" 모드(기본 1시간마다 건물·플레이어 근처가 아닌 청크 삭제, 삭제 중 수 초 멈춤)가 쓰인다. 삭제된 청크는 같은 시드로 재생성된다. [추정] https://forums.factorio.com/viewtopic.php?p=242632 , https://mods.factorio.com/mod/unused_chunk_removal_rework
- 적 확장: 4~60분 간격(진화도 가중 무작위), 기존 둥지 7 청크 반경의 육지 청크를 점수화(3 청크 안에 구조물 없으면 1.0), 5~20 유닛 정착대가 도착해 하나씩 둥지·벌레로 변함. 공격은 오염 구름이 둥지에 닿을 때만. [검증] https://wiki.factorio.com/Enemies
- 함의: 무한 세계에서 "살아 있는 세계" 시뮬레이션은 생성된·활성 청크 집합 안에서만, 규칙은 청크 단위 점수로 국소화. [추정]

### 2.3 Valheim — 유한 원판 + 오브젝트 단위 저장

- 세계 = 원판, 반경 약 10 km(면적 약 314 km²), 바다 끝 가장자리는 약 10,500 에서 해류가 낭떠러지로 끌어감. 출처 간 10,000/10,500 수치가 섞여 있음(생물군계 반경 vs 가장자리로 추정). [검증: 두 출처 이상 일치] https://mein-mmo.de/valheim-map-groesse/ , https://cybrancee.com/blog/how-does-valheim-world-generation-work/ , https://www.pcgamer.com/au/valheim-player-discovers-whats-beyond-the-edge-of-its-enormous-map
- 구역(zone) 64×64 m 가 생성 단위. 플레이어 주변 9×9 구역 생성, 5×5 적재, 0.1초마다 1구역 추가, 범위 이탈 10초 후 해제. 출처는 커뮤니티 위키 기반이며 개발사 비공개 수치. [검증: 두 출처 일치] https://gamever.io/knowledge-base/how-valheim-loads-the-world-zones-zdos-and-why-big-bases-cost-performance , https://valheim.fandom.com/wiki/Zones (검색 요약)
- ZDO(Zone Data Object) = 네트워크 동기화 + 세이브 직렬화 단위. 위치·회전·섹터·프리팹 해시·소유자·리비전 + 타입별 키-값 사전. 벽·바닥 아이템·길들인 생물이 각각 ZDO. 위치(던전 등)는 서버가 완전 생성 후 "유령 모드"로 파괴하고 ZDO 만 남김. 비용은 맵 크기가 아니라 네트워크 객체 수에 비례. [검증: 두 출처 일치] 위 gamever URL, https://edgegap.com/blog/valheim-multiplayer-game-backend-deep-dive (검색 요약)
- 2021년 4월 패치로 "New terrain modification system" 도입, 목적은 "reduce the number of network instances and make loading faster and smoother", 지형 수정이 건물보다 먼저 적재되도록 우선순위 변경. 즉 지형 편집을 편집마다 객체로 두던 방식이 비용 문제를 일으켜 구역 단위로 묶었다고 해석된다. [검증(패치 문구) / 추정(해석)] https://www.gamingonlinux.com/2021/04/valheim-gets-a-needed-patch-new-terrain-modification-system-and-no-more-draugr-in-stones/page=1
- 세이브 = `.fwl`(메타) + `.db`(지형·건물·생물). [추정] https://xgamingserver.com/docs/valheim/world-save-files
- 왜 유한인가: 개발사 공식 설명을 찾지 못함. 중심에서 거리별 생물군계 고리가 진행도 역할을 하고, 커뮤니티는 생성 방식상 무한 확장이 어렵다고 본다. [추정] https://steamcommunity.com/app/892970/discussions/2/4353373240374550773

### 2.4 No Man's Sky — 결정론 생성 + 변경 저장의 한계

- 64비트 시드로 1,800경(18 quintillion) 이상 행성. [추정] https://en.wikipedia.org/wiki/No_Man%27s_Sky (검색 요약)
- GDC 2017 "Continuous World Generation in No Man's Sky"(Innes McKendrick): 복셀 기반 생성 → 다각형화(polygonization) → 텍스처링 → 개체 배치·시뮬레이션의 단계별 파이프라인. 본문은 유료 회원 전용. [추정] https://www.gdcvault.com/play/1024265/Continuous-World-Generation-in-No (검색 요약)
- 지형 편집 저장은 상한이 있는 델타 방식이며 공식 패치 노트가 여러 번 손댔다. [검증: 공식 패치 노트 + 커뮤니티 일치]
  - 1.60 개발 업데이트: 기지 "terrain complexity cap" 시각화 추가(기지 부품이 판 지형만 해당). https://www.nomanssky.com/2018/09/development-update-1-60/
  - NEXT: 한도 도달 시 옛 편집을 덮어쓰지 않고 편집을 멈춤. https://www.nomanssky.com/next-update/
  - Synthesis: 기지 범위 안 지형 편집은 재생성에서 보호, 보호 편집 상한 확대. https://www.nomanssky.com/synthesis-update/
  - Sentinel(3.8): 저장 가능한 지형 편집 최대 수 대폭 증가. https://www.nomanssky.com/sentinel-update/
  - 커뮤니티 보고: 세이브당 약 15,000~20,000+ 편집, 기지 밖 편집은 오래된 것부터 버려져 지형이 되살아남(재현 불규칙). [추정] https://steamcommunity.com/app/275850/discussions/0/3790380404154265262
- 함의: "시드 + 델타"는 세이브가 작지만, 델타 수 상한·우선순위(보호 영역)·생성기 버전 고정이 반드시 필요하다. 무한 세계에서 변경 저장을 무제한 약속하면 결국 상한 정책을 다시 만들게 된다. [추정]

### 2.5 Luanti(구 Minetest)·Vintage Story — 소스/형식 공개 복셀 엔진

Luanti
- 맵블록 = 16×16×16 노드, `mapgen_limit` 기본 31000. [검증: 두 출처] https://rubenwardy.com/minetest_modding_book/en/map/environment.html , https://docs.luanti.org/for-players/world-boundaries/
- 세계 = 한 변 약 60,000 노드의 정육면체, 기본 X·Y·Z 범위 −30912 ~ 30927. 이론 한계는 부호 있는 16비트 정수이나 경계 버그를 줄이려 짧게 자름. 경계 밖은 Ignore 노드의 빈 공간. `mapgen_limit` 은 맵청크 단위로 반올림, 새 생성에만 적용. [검증] https://docs.luanti.org/for-players/world-boundaries/
- 저장 = SQLite 테이블 하나에 블록 블롭, 키는 좌표를 해시한 정수. 즉 생성된 맵블록 전체를 저장. [추정] https://gogs.librecmc.org/OWEALs/minetest/raw/f99a1a7c7c1a3d96332169978b63ffb10c56dbb9/doc/world_format.md
- 옛 코드 주석: 생성기는 80×80×80 노드(5×5×5 맵블록) 묶음 단위. 최신판 확인 필요. [추정] https://code.moparisthebest.com/moparisthebest/minetest/commit/ec796b8e814864b433aea75119c307f44b2b33e8

Vintage Story
- 세이브 `.vcdbs` = SQLite, 대부분 protobuf 블롭. 테이블 chunk(32×32×32 블록), mapchunk(청크 기둥별 2D), mapregion(16×16 청크 영역 2D), gamedata, playerdata. 1.19.8 기준. [추정] https://wiki.vintagestory.at/Modding:VCDBS_format
- 기둥당 mapchunk 배열이 약 15 KB 로 chunk 테이블보다 큰 경우가 많다는 커뮤니티 측정. [추정] https://www.vintagestory.at/forums/topic/19778-understanding-and-improving-the-games-savefiles-serialization-and-compression/
- 기본 세계 1,024,000×1,024,000 블록(MapSizeX/Y/Z). [추정] https://www.gameserverkings.com/knowledge-base/vintage-story/getting-started-with-your-vintage-story-server.md
- 함의: "실용적 무한"이라도 대부분 설정 가능한 큰 유한 경계를 둔다(Minecraft ±30M, Luanti ±31K, Vintage Story 약 1M). 경계는 수치 버그 예방 장치다. [추정]

### 2.6 유한을 택한 게임

- Terraria: 소(4200×1200)·중(6400×1800)·대(8400×2400) 타일 고정. [검증] https://terraria.wiki.gg/wiki/World_size . 생성은 새 세계 생성 직후 100개 이상 패스("Reset"~"Final Cleanup")로 세계 전체를 채우며, 일부 개수가 세계 크기에 비례(지옥 대장간 21/32/42). [검증] https://terraria.wiki.gg/wiki/World_generation . 전역 패스 구조가 무한화와 맞지 않는다는 것은 내 해석. [추정]
- Core Keeper: 절차적이나 무한 아님. 중심(코어)에서 고정 거리(약 1250/1550/1750 타일 등)에 진행 요소를 두는 설계로 보고됨. [추정] https://steamcommunity.com/app/1621690/discussions/0/3189116970733388358 , https://www.gamepur.com/guides/how-big-is-the-map-in-core-keeper
- Enshrouded(복셀 지형, 건축·채굴): 손으로 만든 고정 세계. 개발자: Portal Knights 경험에서 절차적 세계는 "장소" 같지 않았고, 플레이어가 장소와 발견을 이야기할 수 있도록 고정. 알고리즘은 보조 도구로만. 앞서 해보기 약 24~25 km², 정식 목표 64 km². [검증: 두 출처] https://wccftech.com/enshrouded-qa-keen-games-explains-all-about-its-zelda-meets-valheim-game/amp/ , https://www.gamewatcher.com/enshrouded-world-map-procedurally-generated-handcrafted
- 공통 교훈: 유한 선택의 이유는 (1) 전역 지식이 필요한 생성 단계, (2) 거리 = 진행도의 의도된 곡선, (3) 장소 정체성·공유 가능한 지리. 무한 세계를 택하면 이 셋을 국소 규칙(격자 칸 배치, 거리 기반 난이도 함수, 시드 공유)으로 대체해야 한다. [추정]

### 2.7 Kerbal Space Program — 부동 원점(floating origin)

- Krakensbane 은 환경 크기 때문에 생기는 부동소수점 정밀도 오류를 극복하는 시스템이며, 모든 객체를 주기적으로 재배치해 원점을 활성 비행체에 유지한다. [추정: 공식 위키·포럼 검색 요약, 원문은 봇 차단으로 못 읽음] https://wiki.kerbalspaceprogram.com/wiki/API:Krakensbane , https://forum.kerbalspaceprogram.com/topic/207101-question-on-krakensbane
- UE5 대비: UE5 는 큰 세계 좌표(LWC, Large World Coordinates)로 CPU 위치를 배정밀도로 바꿨다. 엔진 상수 `UE_LARGE_WORLD_MAX 8796093022208.0` (주석 "Approx 87,960,930.2 km across, or 43,980,465.1 km from the origin"), UE4 값은 `UE_OLD_WORLD_MAX 2097152.0`, `UE_USE_UE4_WORLD_MAX` 기본 0. [검증] `C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Public/EngineDefines.h:37,41,45-53`
- GPU 는 double 매개변수 미지원, 렌더링은 단정밀도 두 개로 배정밀도를 흉내 내며 머티리얼 LWC 에 추가 비용. [추정: Epic 문서 검색 요약] https://dev.epicgames.com/documentation/en-us/unreal-engine/large-world-coordinates-in-unreal-engine-5 , https://dev.epicgames.com/documentation/unreal-engine/large-world-coordinates-rendering-in-unreal-engine-5

## 3. 결정론 노이즈를 CPU·플랫폼 간 동일하게

- 부동소수점은 "같은 바이너리·같은 컴파일러·같은 명령어 집합·엄격 모드면 결정론"이지 임의 빌드·기기 간 재현은 아니다. 원인: 최적화, FMA 반올림 차이, x87 80비트 vs SSE, sin/cos 등 초월 함수의 라이브러리·제조사 차이, DLL 이 바꾸는 반올림 모드, 역수·역제곱근 근사 명령. 대책: /fp:strict, 시작 시 FPU 상태 설정 후 매 틱 확인(Gas Powered Games), 초월 함수는 자체 구현/비최적화 래핑(Pandemic). [검증: 다수 개발자 증언 모음, 원문 직접 읽음] https://gafferongames.com/post/floating_point_determinism/
- Factorio 는 수학 함수 대부분을 자체 구현(2.2절). [추정]
- 정수 해시 난수: Squirrel Eiserloh GDC 2017 "Noise-Based RNG" — 상태 있는 난수 생성기 대신 `noise(위치, 시드)` 해시를 쓰면 순서 무관 접근, 재시드 용이, 기록·재생, 네트워크 손실 내성, 잠금 없는 병렬화. [추정: GDC Vault 소개문 검색 요약] https://gdcvault.com/play/1024365/Math-for-Game-Programmers-Noise , 구현 이식 예 https://swiftpackageregistry.com/heckj/Squirrel3
- 위치 분기 난수: Minecraft 1.18+ `XoroshiroPositionalRandomFactory` 가 좌표로 난수원을 분기한다(2.1절). 구조물 배치는 (시드, 격자 칸, salt) 로 결정. [추정/검증 혼합]
- 고정소수점: Photon Quantum 의 `FP` = 정수부 48비트 + 소수부 16비트, 역코사인 등은 16비트 정밀도 조회표. BEPUphysics 고정소수점 이식판은 부동소수점판보다 약 4배 느렸다는 보고, SIMD 와의 결합이 어렵다. [추정] https://doc-api.photonengine.com/en/quantum/v3/namespace_photon_1_1_deterministic.html , https://forum.bepuentertainment.com/viewtopic.php?p=14015
- 실무 결론 [추정]:
  1. 생성 입력은 정수(청크 좌표 int64, 셀 좌표 int32)와 시드만. 난수는 정수 해시(예: Squirrel 계열, SplitMix64 마무리)로 `hash(seed, salt, cx, cy, i)`.
  2. 연속 노이즈는 격자 꼭짓점 기울기를 정수 해시로 고르고, 보간 가중치만 부동소수점으로 계산. 이때 입력을 "청크 정수 좌표 + 청크 내부 국소 소수"로 나눠 큰 좌표에서도 소수 정밀도가 유지되게 한다(Far Lands·2^24 정밀도 악화 회피).
  3. 생성 결과가 시뮬레이션(전투·경로)에 들어가는 값이면 플랫폼 간 동일성이 필요하다. 이 경우 sin/cos/pow 같은 초월 함수 금지 또는 자체 구현, FMA 축약 금지 컴파일 옵션, 가능하면 생성 결과를 정수 격자(높이 단계, 타일 종류)로 양자화해 비교한다.
  4. 생성 결과가 시각 전용(장식·풀)이면 플랫폼 간 미세 차이는 허용 가능. 결정론 요구 범위를 데이터 종류별로 나눈다.

## 4. 변경분만 저장하는 세이브 전략 비교

| 전략 | 사례 | 장점 | 비용·함정 | 근거 |
|---|---|---|---|---|
| 방문 청크 전체 스냅샷 | Minecraft(.mca), Luanti(SQLite 블롭), Vintage Story(.vcdbs), Factorio | 생성기 변경에 강함(저장된 청크는 재생성 안 함), 구현 단순 | 탐험량 비례 증가 → 정리 도구·모드 필요, 생성기 변경 시 새/옛 청크 이음매(Minecraft `blend_density`) | 2.1·2.2·2.5절 [검증/추정] |
| 시드 + 델타 | No Man's Sky 지형 편집 | 세이브 작음 | 델타 상한·우선순위 정책 필요, 생성기 버전 고정 필수(바뀌면 델타가 다른 바탕에 적용), 재생성 현상 | 2.4절 [검증] |
| 오브젝트 레코드 | Valheim ZDO | 설치·철거(수준 B)에 자연스러움, 네트워크와 세이브 형식 공유 | 객체 수 비례 비용, 편집당 객체는 폭증 → 구역 단위 집계로 전환 | 2.3절 [검증/추정] |

권고 혼합형(수준 A·B·C 공통) [추정]:
- 손대지 않은 청크 = 저장하지 않음(시드로 재생성, Minecraft "Deleted chunks can regenerate"와 같은 원리).
- 수준 B(설치·철거): 청크별 오브젝트 레코드 목록(ZDO 식 키-값) + 생성물 철거는 "생성 ID 무효화 비트셋"으로 표현(생성된 나무 #17 이 베어짐). 생성물 ID 는 (청크, 생성 순번) 결정론이어야 함.
- 수준 C(지형 변형): 청크가 처음 변형되는 순간 그 청크의 지형 데이터를 통째로 저장으로 승격(이후 생성기에 의존 안 함). NMS 식 편집 로그 누적은 상한 문제를 부른다.
- 청크마다 `생성기 버전`과 `DataVersion` 기록, 지역 파일은 Minecraft 처럼 32×32 청크 묶음 + 4 KiB 섹터 + 압축으로 파일 수 폭증을 막는다.
- Factorio 처럼 "건물·플레이어 근처가 아닌 오래된 청크"를 주기적으로 버려 세이브를 유계로 유지하는 선택지도 있다(단, 수준 C 변형 청크는 제외).

## 5. TDGame 에 대입한 수치 (계산, [추정])

- 카메라 시야 바닥 면적(스프링암 1100 cm, 피치 −50°, 수평 시야각 90°, 화면비 16:9 가정): 카메라 높이 842.6 cm, 수직 반시야각 29.4° → 화면 위 가장자리 광선 내림각 20.6°, 아래 79.4°. 바닥에서 앞뒤 약 20.8 m(폰 앞 15.3 m, 뒤 5.5 m), 폭은 가까운 쪽 약 15 m ~ 먼 쪽 약 42 m 의 사다리꼴. 폰에서 가장 먼 화면 모서리까지 약 26 m.
- 이동 600 cm/s = 21.6 km/h. Valheim 반경 10 km 를 약 28분, 100 km 를 약 4.6시간에 직진. Minecraft 경계 3천만 m 는 약 58일 연속 보행 → 걷는 탑다운에서는 수치 경계에 사실상 도달 불가. UE5 LWC(double) 위치면 좌표 자체는 문제없고, 남는 위험은 단정밀도 float 로 계산하는 노이즈·셰이더·물리 입력이다. float32 단위 최소 간격(ulp)은 원점에서 10 km(10^6 cm)일 때 약 0.06 cm, 100 km 일 때 약 1 cm, 1,000 km 일 때 약 8 cm.
- 청크 크기 감: Minecraft 16 m, Factorio 32 m, Valheim 64 m. 화면 반경 약 26 m + 적재 지연 여유(6 m/s × 지연 초)를 감안하면 32~64 m 청크에 반경 2~3 청크 생성 고리, 그 안쪽 1~2 청크만 시뮬레이션(Minecraft 렌더/시뮬 거리 분리)이 합리적인 출발점.

## 6. 다른 갈래로 넘길 질문

- 월드 파티션(에디터 시점 셀 분할 스트리밍)은 런타임 생성 무한 콘텐츠를 셀로 받지 못할 가능성이 높다. 런타임 청크 액터·인스턴스 메시·동적 내비메시(D-06) 타일 생성 비용을 엔진 소스로 확인 필요.
- 현재 메인 월드 생성기(numpy → C++ 랜드스케이프 → 에디터 베이크)는 전역 패스(Terraria 형) 성격일 수 있다. 청크 국소 순수 함수로 재작성 가능한지 검토 필요.
- 결정론 전투 시뮬레이터(전역 난수 금지)와 생성 난수(정수 해시)를 같은 규약으로 묶을지.
- D-42 연속 보행 이계 지역과 무한 생성 영역의 이음매 설계.
