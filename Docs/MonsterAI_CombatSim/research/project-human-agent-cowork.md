# 조사 노트: 사람과 AI 에이전트가 몬스터·전투를 함께 만드는 방식 (역할 cowork)

작성 2026-09-23 claude(하위 조사자). 상태 표기: **검증**(파일·웹에서 확인) / **가정**(추론·계산, 실측 전) / **미확인**.
범위: 사람(1인 개발자: 재미 판단·방향 결정) + 에이전트(제작·검증). 몬스터 AI 결정 D1~D38은 바꾸지 않고 그 위에 얹는다.

## 0. 결론 다섯 줄

1. 제작 루프(D37: 스키마 덤프 → JSON → 검증기 3단 → 200시드 배치 → 30줄 요약)는 **설계만 있고 구현은 0**이다(Phase 0 미착수). 재미 층을 얹을 비용이 가장 싼 시점이 지금이다.
2. 현재 완료 조건은 "승률 목표 밴드 안"뿐이다. **균형은 재미가 아니다.** 사용자가 원하는 "관계의 동사·조작 중심" 재미를 검사하는 입력(의도)·지표·시각 자료가 모두 없다.
3. 재미는 둘로 쪼갠다. (가) 시뮬로 반증 가능한 **재미 가설**(에이전트가 자동 반복) / (나) 체감 판정(사람만). 에이전트는 영상으로 재미를 판정하지 않는다(시각-언어 모델의 몰입 예측이 기준선 수준, 2026 논문).
4. 사람의 입력은 "명세 쓰기"보다 "변형 2~3개 중 고르기 + 한 줄 이유"가 싸다. 비교 페이지·도식 리플레이 GIF·모먼트 클립은 결정론 스크립트가 만든다.
5. 고정 시점은 제약이자 이점이다. 화면 구도가 늘 같아서 **엔진 없이 그린 2D 도식 리플레이가 실제 화면과 거의 같은 구도**가 되고, "화면 밖에서 맞은 피해" 같은 공정성 지표를 정확히 계산할 수 있다.

## 1. (a) 이미 있는 절차·도구·역할

| 구분 | 내용 | 상태 | 근거 |
|---|---|---|---|
| 공통 장부 | NOW·Worklog·대장·Lessons, 청구·종료·보고 2줄, OP-01~32 | 운영 중 | `AGENTS.md` 15절, `Docs/AgentRules.md` |
| Claude 위임 | 오케스트레이터 + 6역할: td-scout(haiku 위치), td-researcher(sonnet 조사), td-builder(sonnet 명세 구현), td-refuter(opus 독립 반박), td-debugger(opus 원인), td-test-runner(haiku 실행·실패 첫 줄) | 운영 중 | `.claude/agents/`, `research/claude-subagents.md` |
| 위임 계약 | Goal/Scope/Allowed/Verification/Do not/Known facts/Output/Limit/Stop, 큰 산출물은 `Saved/AgentOps/<날짜>/` | 운영 중 | `CLAUDE.md` |
| 몬스터 제작 루프 | 스키마 덤프 3파일(약 2,600토큰) → `<Id>.json` → `TDMonsterAIValidate`(1 스키마·이름 유사도 힌트, 2 정적 규칙, 3 5초 생존 시뮬·해시 2회) → `run_batch.py` → `summarize_batch.py` 30줄 → `propose_tweaks.py` 힌트. 반복당 약 5천 토큰, 종당 컴파일 0회 | **설계만** | 02 §5.7·5.8·7, 05 §6.1 |
| 사람 승인 지점 | 골든 해시는 에이전트가 생성만, 승인은 사람 | 설계만 | 06 §4-1 |
| 분석 스크립트 | `analyze_decisions`(행동 점유·0점 원인), `diff_runs`(최초 이탈 스텝), `summarize_batch`, `propose_tweaks` | 설계만 | 04 §11.2 |
| 대리 플레이어 | 페르소나 JSON(반응 지연·회피 확률 등, D12), 실플레이 텔레메트리 형식(06 §4-4, `summarize_batch --live`) | 설계만 | 02 §6, 06 §4-4 |
| 시각화 | 기획자용 `render_report.py`(장비×몬스터 승률 히트맵·추세) 제안 | 미반영 | 06 §4-5(M13) |
| 시각 검토 선례 | Blender 공격 모션 3면 GIF·게임 GIF(`Docs/Validation/BlenderAnimation/`), `compose_*`·`render_*` 스크립트, PIE 캡처 `editor_pie_capture_sword_slash.py`. 캡처는 MCP `CaptureViewport`만 신뢰 | **구현됨** | `Tools/BlenderAnimation/`, L-editor-03 |
| 범용 실행 | `ue_editor.py`, `run_in_editor.py`, `uemcp.py`, `check_automation_tests.py`, `pie_profile.py` | 구현됨 | `Tools/README.md` 1절 |
| 조사 방법 | 조사 16 → 주장 검증 → 비평 → 설계안 4 → 심사 3 → 결정 기록 → 병렬 작성 → 검토 | 기록됨 | Plan §8 |

교훈 제목(Docs/Lessons): build 2, editor 6, repo 7, anim 3. 전투·재미 관련 교훈은 아직 0건(combat 파일 없음).

## 2. (b) '재미'를 다루기 위해 빠진 것

### 2.1 진단
- 완료 조건(02 §7.1, 05 §6.1 7단계)이 균형 지표만 본다. 06 B4도 "시뮬은 지표를 낼 뿐 균형을 정의하지 않는다"고 적었는데, 재미는 그보다 한 단계 더 비어 있다. 결과적으로 에이전트는 "밴드 안 + 검증 통과 = 완료"로 멈춘다(Anthropic 문서: 검사 수단이 없으면 "끝나 보이는 것"이 유일한 신호).
- 결정 로그 이벤트 필드(`t, ev, src, dst, amount, ability`, 04 §11.1)로는 **누가 무엇 때문에 죽었는지(원인)·어디서 맞았는지(위치)·예고가 있었는지(텔레그래프)**를 알 수 없다. 조작 처치 비율, 화면 밖 피해, 불공정 피해를 계산할 수 없다.
- 관계 동사(보호·포식·합체·빙의·밀기)를 시뮬에서 재려면 해당 행동 원시가 `bSimulatable`이어야 한다(actions.md 필드). 넉백처럼 순수 함수(`FTDKnockback::Integrate`)로 만들지 않으면 헤드리스 측정이 불가능하고 PIE로만 확인된다.
- 대리 플레이어가 "광역기 반복" 한 종류뿐이면, 대화의 핵심 주장(대량 러시는 광역기 반복으로 수렴)을 **반증할 비교 대상**이 없다.

### 2.2 빠진 것 목록

| 분류 | 빠진 것 | 제안 |
|---|---|---|
| 사람이 판단할 재료 | 종·조우의 의도(무엇을 느끼게 할지)가 적힌 곳 | 의도 카드(6절) |
| | 장면을 보는 수단(현재는 CSV·30줄 텍스트뿐) | 도식 리플레이 GIF + PIE 모먼트 클립 |
| | 선택지 비교(한 번에 한 안만 나옴) | 한 축만 다른 변형 2~3개 비교 페이지 |
| 에이전트가 검증할 지표 | 재미 가설 → 지표 매핑 | 재미 지표 등록표(3절) + 가설 판정표 |
| | 로그 필드 | `cause`(direct/push/hazard/friendly/consume/merge), 위치 샘플(8스텝=8Hz), `telegraph_start_step`, 장판·시체 생성/소멸 이벤트 |
| | 전략 비교 | 페르소나 최소 2종(광역 반복형, 조작형) + 같은 시드 쌍 비교(06 §5 14번 `--paired`) |
| 시각 확인 수단 | 조우 장면 GIF, 로스터 전체 지도 | `render_schematic_replay.py`, `plot_roster_range.py` |
| 누적 학습 | 사람 판정이 기록되지 않음 | 판정 카드 → `verdicts.jsonl` → 지표-판정 상관 |

### 2.3 고정 시점이 주는 수치 (가정: 카메라 FOV 미설정이므로 엔진 기본 90도, 화면 16:9, 캡슐 반높이 96cm)
- 근거: 스프링암 800cm, 피치 -60도, 절대 회전(`TDGameCharacter.cpp:56-62`), `TopDownCameraComponent`에 FOV 설정 없음(검증).
- 계산 결과(파이썬 계산): 발밑 기준 화면 **위쪽 약 9.3m, 아래쪽 약 3.9m**가 보인다. 폭은 화면 아래 끝 약 13.7m, 플레이어 위치 약 17.7m, 위 끝 약 27m인 사다리꼴. 21:9 화면이면 위 6.5m·아래 3.1m.
- 뜻: 화면 아래(카메라 쪽)에서 오는 위협은 보이는 거리가 위쪽의 약 0.4배다. 초속 4m 몬스터라면 1초 미만 경고. → 지표 `offscreen_damage_share`(피해 시점 가해자가 사다리꼴 밖), 스폰·접근 규칙(아래쪽 접근 시 예고 강화), 도식 리플레이에 사다리꼴을 그려 사람이 즉시 확인.
- 이점: 카메라가 돌지 않으므로 시뮬 좌표를 같은 투영으로 그리면 도식과 실제 화면의 구도가 같다. 원근·애니메이션 차이만 남는다(가정).

## 3. 재미 지표 후보 (에이전트가 자동 계산, 사람 판정으로 보정)

| 지표 | 정의(시뮬 이벤트에서 계산) | 재는 가설 | 근거·선례 |
|---|---|---|---|
| `manipulation_kill_share` | 적 사망 중 `cause ≠ direct` 비율 | "조작이 죽이는 것보다 재밌다" | Into the Breach(GDC 2019) |
| `strategy_gap` | 같은 시드 쌍에서 조작형 페르소나 대 광역 반복형의 처치 시간(TTK) 비·승률 차 | "관계가 광역 반복 수렴을 깬다" | 대화 핵심 주장의 반증 시험 |
| `verb_consequence_rate` | 밀기·당기기 사용 후 3초 안에 피해·처치·관계 이벤트가 난 비율(헛동작 비율의 반대) | 동사가 의미 있다 | 가정 |
| `relation_event_variety` | 조우당 서로 다른 관계 이벤트 종류 수와 엔트로피 | 상황이 매번 다르다 | 대화의 "종류 조합이 아닌 관계" |
| `pressure_peaks`, `relax_ratio` | 초당 강도(받은 피해 + 반경 내 적 수 가중)의 피크 수, 저강도 구간 비율 | 긴장-이완 리듬 | Left 4 Dead 감독 AI 강도(Booth, GDC 2009) |
| `comeback_rate` | 최저 체력비 ≤ 0.3 이후 승리 비율 | 역전 가능성(드라마) | Browne Ludi 미학 지표(2008~2011) |
| `killer_move_count` | 단일 행동 후 5초 안에 적 체력 합의 X% 이상 제거한 사건 수 | 한 수의 쾌감 | Browne "killer moves" |
| `unreadable_hit_share` | 예고 시작~적중이 반응 시간(기본 제안 0.4초) 미만인 피해 비율 | 죽음이 내 탓이다 | Into the Breach 원칙의 실시간 번역(임계값 가정) |
| `offscreen_damage_share` | 2.3절 사다리꼴 밖 가해자의 피해 비율 | 고정 시점 공정성 | 계산(가정) |
| `simultaneous_mechanics_peak` | 화면 안 동시에 활성인 서로 다른 메커니즘(장판 종류·예고 종류·관계 상태) 최댓값 | 과복잡 방지 | 대화의 PoE Archnemesis 교훈 |

기존 균형 지표(승률·TTK 분위수·리썰 위험·교체율·첫 공격, 04 §10.2)는 그대로 가드레일로 쓴다.

## 4. (c) 한 번 만들어 반복 실행할 결정론 도구 후보 (`Tools/CombatSim/`, 시스템 파이썬)

| 도구 | 입력 → 출력 | 에이전트 토큰 절감 | 비고 |
|---|---|---|---|
| `summarize_batch.py --fun <intent>` | 결과·이벤트 + 의도 카드 → 가설별 통과/실패/측정 불가 **15줄 이하** | 에이전트는 로그 대신 15줄만 읽음 | 새 도구보다 기존 계획 확장(OP-12) |
| `find_moments.py` | 이벤트 → `moments.json`(시드·스텝 범위·이유·점수) 상위 5: 최대 강도, 최대 역전, 조작 처치 연쇄, 불공정 피해, 교착 | 사람이 볼 장면을 기계가 고름 | |
| `render_schematic_replay.py` | 위치 샘플 + 이벤트 + 모먼트 → 5~10초 GIF/SVG, 시야 사다리꼴·예고 범위·관계 선 표시(PIL) | 엔진 기동 없음, 수 초 | 2.3절 투영 상수 공유 |
| `editor_capture_moment.py` | 같은 시나리오·시드를 PIE에서 표시 켜고 재생, 스텝 범위 연속 `CaptureViewport` → GIF | 최종 체감 확인용만 | B단계 일치(D32) 전제, `compose_*` GIF 코드 재사용 |
| `render_report.py --review` | 변형 2~3개 → 단일 HTML: 의도 요약, 지표 표(밴드 이탈 색), GIF, 판정 칸 | 사람 검토 시간 단축 | 06 §4-5 제안 확장 |
| `check_intent_card.py` | 의도 카드 → 필수 필드·동사 어휘·지표 이름이 등록표에 있는지 | 모호한 카드가 루프에 못 들어감 | 스키마 덤프에 `fun-metrics.md` 추가(문서=코드) |
| `plot_roster_range.py` | 전 종·조우를 지표 두 축에 찍은 히트맵 + 근접 중복 경고 | 로스터 단위 판단을 한 장으로 | 표현 범위 분석(Smith & Whitehead 2010) |
| `record_verdict.py` + `calibrate_fun_metrics.py` | 판정 카드 → `verdicts.jsonl` 추가, 20~30건 뒤 지표와 판정의 순위 상관 | 어떤 지표를 게이트로 쓸지 데이터로 결정 | 1인 표본이라 약한 신호(가정) |

## 5. (d) 사람-에이전트 역할 분담표 초안

| 단계 | 사람 | 오케스트레이터(상위 모델) | 하위 에이전트 | 결정론 도구 |
|---|---|---|---|---|
| 1 방향 | 짧은 의도 5줄(6.1) | 질문 3개 이하로 인터뷰 → 의도 카드 전체형 작성 | — | `check_intent_card` |
| 2 승인 | 카드 승인·수정(1~2분) | — | — | — |
| 3 제작 | — | 변형 축 1개 정해 명세 | td-builder: JSON 변형 2~3개. 새 원시 필요 시 별도 C++ 명세 | 검증기 3단 |
| 4 측정 | — | — | td-test-runner: 배치 실행 | `summarize --fun`, `find_moments`, `render_schematic_replay` |
| 5 반박 | — | 판정 | td-refuter: 퇴화 전략(광역 반복·도망 반복) 페르소나로 가설 반박 시도 | `--paired` |
| 6 제시 | — | 비교 페이지 1장 | — | `render_report --review` |
| 7 판정 | 변형 선택 + 태그 + 한 줄(2~5분) | — | — | `record_verdict` |
| 8 체감 | 주 1회 PIE 20분 플레이 | 실플레이 요약과 시뮬 요약 비교 | — | `summarize --live`, `editor_capture_moment` |
| 9 기록 | — | 장부 1회 | 장부 쓰지 않음 | — |

사람 전용: 재미 판정, 의도 카드 승인, 골든 해시 승인, KPI 밴드 개정, 결정 기록 변경. 에이전트는 재미 항목을 `done`으로 바꾸지 않고 `decision`(사유 "재미 판정 대기")으로 둔다(새 상태 추가 없이 기존 어휘 사용).
타 벤더(Codex·Gemini)도 같은 루프를 돌 수 있도록 전부 CLI 스크립트로 둔다(Claude 전용 스킬에 넣지 않는다).
층 구분: L0 검증기 → L1 균형 밴드 → L2 재미 가설 → L3 모먼트·GIF까지 에이전트가 사람 없이 진행, L4 판정과 L5 체감만 사람.

## 6. (e) 모호한 지시를 줄이는 입력 양식 초안

### 6.1 짧은 의도(사람이 쓰는 5줄)
```
대상: 종 | 조우 <이름>
느낌: <한 문장> [미학 태그 1~2: 도전·발견·표현·감각·판타지·서사·동료·몰입]
플레이어 동사: <밀기|당기기|유도|끊기|…> (최대 2)
몬스터가 만드는 상황: <관계 동사 한 문장: 보호|포식|합체|빙의|밀림|…>
피하고 싶은 것: <한 문장>
```

### 6.2 의도 카드 전체형(에이전트 작성, 사람 승인)
```
id / kind(species|encounter) / owner(user)
fantasy: 한 문장
aesthetics: [태그]
player_verbs: [등록된 동사 이름]
relation_verbs: [등록된 관계 원시 이름]
telegraph: {무엇을, 최소 예고 ms, 화면 아래쪽 접근 시 처리}
counterplay: 한 문장
hypotheses:
  - 문장: "슬라임 근처에서 고블린을 밀면 처치가 빨라진다"(예시)
    metric: strategy_gap
    compare: 조작형 대 광역 반복형, 같은 시드 200쌍
    pass: TTK p50 비 ≤ 0.8
guardrails: kpi.md 밴드 참조, unreadable_hit_share ≤ 0.1, simultaneous_mechanics_peak ≤ N
vary_axis: 한 축만(예: 밀림 거리)
review: [요청 모먼트 종류]
out_of_scope: [하지 않을 것]
```

### 6.3 판정 카드(사람, 1~3줄)
`<변형> keep|tweak|cut #태그(느림|불공정|안 읽힘|지루|과함|좋음) — 한 줄 이유 → 다음 축: <축>`

### 6.4 모호어 치환 규칙(오케스트레이터가 인터뷰에서 적용)

| 사람이 쓴 말 | 에이전트가 되묻는 형태 |
|---|---|
| 재밌게 | 어떤 가설 + 어떤 지표 + 무엇과 비교 |
| 강하게 / 약하게 | 승률·TTK 밴드 중 무엇을 몇으로 |
| 자연스럽게 | 예고 ms, 교체율 상한 |
| 가끔 / 자주 | 쿨다운 초 또는 확률 |
| 많이 | 마릿수 또는 화면 동시 수 |
| 똑똑하게 | 어떤 관계 동사를 어떤 조건에서 |

## 7. 위험과 한계
- 지표는 대리 플레이어 품질에 묶인다. 조작형 페르소나가 서툴면 `strategy_gap`이 거짓 음성이 된다 → 사람 실플레이 텔레메트리와 같은 요약으로 교차 확인.
- 재미 지표를 게이트로 너무 일찍 고정하면 에이전트가 지표를 맞추는 쪽으로 과적합된다. 20~30건 판정 전까지는 경고로만 쓴다(가정).
- 도식 리플레이는 애니메이션·타격감·소리를 보여주지 못한다. 타격감 판정은 PIE 클립으로만.
- 시각-언어 모델로 버그성 화면(겹침·가려짐)을 거르는 것은 가능성이 있으나(VideoGameQA-Bench 2025), 재미 판정에는 쓰지 않는다.
- 로그 필드 확장은 해시 체인·결과 스키마 버전(06 §5 12번)에 영향을 준다. Phase 1 명세 단계에서 넣어야 골든 재생성 비용이 없다.

## 8. 미결 질문(사용자 결정)
1. 의도 카드 위치: `Docs/MonsterAI_CombatSim/intents/<Id>.md`(사람 소유 문서) 또는 `Content/MonsterAI/Intents/<Id>.json`(검증기가 읽음).
2. 재미 판정 대기를 기존 `decision` 상태로 겸용할지, 새 상태를 둘지(규칙 변경은 사용자 승인).
3. 비교 페이지를 로컬 HTML로만 둘지(모든 벤더 호환), claude.ai 아티팩트로도 발행할지.
4. 조작형 페르소나를 규칙으로 먼저 쓸지, 사용자 플레이 녹화를 먼저 모을지.
5. 대화의 Reaction Rule 표(원천 태그·대상 태그·사건·반응)를 전역 파일로 도입할지. 평면 표라 LLM 저작에 맞지만 D6(종당 네 표) 밖의 새 파일 유형이라 결정 기록이 필요하다.
6. 임계값 초기값: 최소 예고 ms, 동시 메커니즘 상한, 화면 아래쪽 접근 처리.
7. 로그 필드(`cause`·위치 샘플·예고 시작)를 M1-09/M1-10 명세에 선반영할지.

## 9. 출처

인용은 한 개만 쓴다: "Give Claude a check it can run" (Anthropic Claude Code 모범 사례).

| # | 출처 | 연도 | 쓴 곳 |
|---|---|---|---|
| W1 | Anthropic, Best practices for Claude Code — https://code.claude.com/docs/en/best-practices (확인 2026-09-23) | 2026 | 검증 수단, 인터뷰 후 명세, 증거 제시, 독립 검토 |
| W2 | Anthropic, Effective context engineering for AI agents — https://www.anthropic.com/engineering/effective-context-engineering-for-ai-agents | 2025 | 작은 고신호 문맥 |
| W3 | Anthropic, Writing effective tools for agents — https://www.anthropic.com/engineering/writing-tools-for-agents | 2025 | 도구 응답은 고신호만, 실행 가능한 오류 문구 |
| W4 | Anthropic, Building effective agents — https://www.anthropic.com/engineering/building-effective-agents | 2024 | 평가 기준이 주관적이면 평가자-최적화 루프 부적합 |
| W5 | Wang 외, Do Vision-Language Models Understand Human Engagement in Games? arXiv 2603.18480 | 2026 | 영상 기반 몰입 예측이 기준선 수준 |
| W6 | VideoGameQA-Bench, arXiv 2505.15952 | 2025 | 시각-언어 모델의 게임 QA 용도 |
| W7 | RuleSmith, arXiv 2602.06232 | 2026 | LLM 자체 대국 + 베이즈 최적화 균형 |
| W8 | Politowski 외, Assessing Video Game Balance using Autonomous Agents, arXiv 2304.08699 | 2023 | 에이전트 플레이테스트 |
| W9 | Smith & Whitehead, Analyzing the expressive range of a level generator (PCG 워크숍) | 2010 | 표현 범위 분석 |
| W10 | Yannakakis·Liapis·Alexopoulos, Mixed-initiative co-creativity (FDG) | 2014 | 사람이 고르고 기계가 제안 |
| W11 | Browne, Evolutionary Game Design(Springer) / Ludi | 2011 | 드라마·불확실성·킬러 무브, 인간 순위와 일치 선례 |
| W12 | Booth, The AI Systems of Left 4 Dead (GDC) | 2009 | 강도 곡선 |
| W13 | Davis, Into the Breach Design Postmortem (GDC) | 2019 | 예고와 "내 탓인 죽음" |
| W14 | Librande, One-Page Designs (GDC) | 2010 | 한 장 설계 문서 |
| W15 | Hunicke·LeBlanc·Zubek, MDA | 2004 | 미학 8종 어휘 |
| W16 | Ubisoft Ghostwriter (GDC, gamedeveloper.com) | 2023 | 사람이 상황 작성, AI가 변형, 사람이 선택 |
| W17 | Epic, Visual Logger / Rewind Debugger 문서(UE 5.8) | 2026 | 개발자용 녹화·되감기 디버그 |

프로젝트 근거: `Docs/MonsterAI_CombatSim/02-architecture-and-definition-format.md` §5.7·5.8·7, `05-ml-and-generative-ai.md` §6, `04-combat-simulator.md` §10·11, `06-beyond-the-ask.md` §1·4, `Docs/MonsterAI_CombatSim_Plan.md` §5·8, `.claude/agents/*.md`, `Docs/AgentCollaboration/research/claude-subagents.md`, `Tools/README.md`, `Docs/Lessons/*.md`, `Source/TDGame/Characters/TDGameCharacter.cpp:56-62`, 사용자 제공 대화 파일(Reaction Rule 표, 아이콘 인식 테스트 비판).
