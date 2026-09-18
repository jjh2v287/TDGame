# Lessons — build (빌드·컴파일)

[← 인덱스로](../AgentCollaboration_Plan.md)
종류: 교훈 · 형식은 `Docs/AgentRules.md` 3절 A. 색인: `rg -n "^### L-" --glob '*.md' Docs/Lessons`

### L-build-01 새 C++ 모듈·클래스는 라이브 코딩이 안 된다
- 증상: 새 모듈·새 UCLASS를 추가한 뒤 라이브 코딩 컴파일이 성공해도 에디터에 반영되지 않거나 실패한다.
- 원인: 라이브 코딩은 기존 모듈 안의 함수 본문 변경만 패치한다.
- 해결: `python Tools/ue_editor.py restart` (에디터 닫고 Build.bat 후 재기동).
- 범위: UE 5.8, Source/TDGame·TDGameEditor·TDWorldGen
- 증거: Tools/README.md 5절(이관 전 기록) — 미검증(증거 파일 없음)
- 날짜·상태: 2026-09-18 active (원 기록 2026-09-12 이전)
- 발견: gemini/claude (이관: claude)

### L-build-02 컴파일 오류 원인은 로그의 들여쓴 다음 줄에 있다
- 증상: 머티리얼·파이썬 스크립트 오류가 났는데 마지막 줄만 보면 원인이 안 보인다.
- 해결: `Saved/Logs/TDGame.log`에서 `LogMaterial`/`LogPython` 줄 바로 다음의 들여쓴 줄을 읽는다.
- 범위: UE 5.8 에디터 로그
- 증거: 미검증
- 날짜·상태: 2026-09-18 active
- 발견: gemini (이관: claude)
