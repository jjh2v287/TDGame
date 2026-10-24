# 02 부동소수점 결정론·C++ 생성 속도 (갈래 02, 2026-10-23, claude 하위 에이전트)

질문: Game 타깃(`/fp:fast`)과 Editor 타깃(`/fp:precise`)의 차이가 무한 세계 생성 결과와 저장 차분의 기준값을 바꾸는가. C++로 옮긴 생성기는 청크당 몇 ms인가. UE 빌드 없이 `cl.exe`만 썼고, TDGame 에디터·빌드·Source·Config·Content·장부는 건드리지 않았다.

## 요약

1. **바뀐다.** UE 5.8 Game 기본 플래그(`/fp:fast`, `/arch` 없음)로 빌드하면 높이 표본의 46~48%가 Python float64·Editor 빌드와 비트가 다르다. 최대 차이는 1.85e-13 m다. Editor 기본 플래그(`/fp:precise`)와 `/fp:strict`는 SSE2·AVX2 모두 Python과 115만 표본에서 비트 단위로 같다(높이·온도·습도·바이옴 전부). [검증, `fpdet/compare.json`]
2. **차이의 크기는 0.2 pm(피코미터)급이고, 1 mm·1 cm 정수 양자화 뒤에는 0건이었다**(변형마다 115만 표본, 바이옴 뒤집힘도 0건). 다만 0은 확률이 낮다는 뜻이지 보장이 아니다. 1 mm 경계를 넘을 기대 횟수는 Game 기본 기준 표본당 4.7e-12, `/arch:AVX2`의 FMA(곱셈·덧셈 융합 명령) 경로에서는 1.4e-11이다. [검증·계산]
3. **`/fp:precise`만으로 다 막히지는 않는다.** 같은 실행 파일 안에서 CRT(C 런타임 라이브러리) 배정밀도 `sin`·`cos`·`tan`·`exp`·`pow`는 FMA3 경로를 켜고 끌 때 결과가 달라진다(100만 표본 중 0.12~2.6%). CPU 기능에 따라 같은 바이너리라도 결과가 갈릴 수 있다는 뜻이다. [검증, `fpdet/crt_fma3_probe.json`] 지금 생성기는 초월 함수를 쓰지 않아 영향이 없다.
4. **C++ 이식 속도.** 단일 스레드 중앙값 기준 64 m@1 m 청크는 높이만 1.4~1.6 ms, 기후 포함 2.0~2.1 ms다. 64 m@0.5 m는 8.2~8.3 ms이고, numpy보다 7.7~8.5배 빠르다. 8스레드에서는 64 m@1 m를 초당 2,689~2,901청크(1스레드 대비 6.1~6.3배), 64 m@0.5 m를 초당 657~674청크 처리한다. 다른 세션의 UnrealEditor가 CPU를 30~85% 쓰는 상태에서 잰 값이다. [검증, `fpdet/timing_rounds.json`·`timing.json`]
5. **권장.** 생성 모듈(`TDWorldGen`, Runtime 모듈 `TDGame.uproject:17-19`)에 `FPSemantics = FPSemanticsMode.Precise;`를 둔다. 실행 비용은 측정 잡음 수준(0~11%, 평균 약 +6%)이고, 빌드 비용은 이 모듈 전용 PCH(미리 컴파일된 헤더) 변형 하나다. 정수 양자화는 그 대신이 아니라 함께 쓰는 보조다. 저장·식별·존재 판정에 쓰는 값은 정수로 둔다.

## 1. 실험 구성

### 1.1 이식
- `Tools/scratch/infinite_world/fpdet/chunkgen_port.cpp`(C++17 단일 파일): `chunkgen.py`의 다음 요소를 같은 연산 순서로 옮겼다. splitmix64 정수 해시 그래디언트 노이즈, fBm(프랙탈 브라운 운동, 옥타브 합), 도메인 왜곡(60 m, 3옥타브), 높이(8옥타브)·대륙(2옥타브)·기후(온도·습도 각 3옥타브), 휘터커 바이옴. 구조물(`apply_structure`)은 옮기지 않았다.
  - 옥타브 수는 템플릿 상수로 두었다. 그래서 `amplitude_sum`이 컴파일 시점 상수가 되어, `/fp:fast`가 나눗셈을 역수 곱셈으로 바꿀 수 있는 자연스러운 C++ 형태가 된다.
  - 표본마다 스칼라로 계산한다. 64비트 정수 곱 해시는 SSE2·AVX2로 벡터화되지 않는다.
- 세계 시드 20261023(`run_proto.py:24`와 같음).
- 구동기 `Tools/scratch/infinite_world/fpdet/run_fpdet.py`: 변형 빌드 → 어셈블리 명령 수 집계 → 덤프 → Python 기준값 비교 → 시간 측정 → 표 생성을 한 번에 한다. `--rounds N`은 교차 순서 반복 측정이다.

### 1.2 변형과 UE 대응 [검증]

| 변형 | 플래그 | UE 대응 |
|---|---|---|
| precise_sse2 / fast_sse2 / strict_sse2 | `/O2 /fp:{precise,fast,strict}` | 과제 지정 |
| precise_avx2 / fast_avx2 / strict_avx2 | 위 + `/arch:AVX2` | `MinCpuArchX64=AVX2` |
| precise_contract_avx2 | `/O2 /fp:precise /fp:contract /arch:AVX2` | FMA 융합만 분리 |
| **ue_game_default** | `/Ox /Ot /Oi /GF /Gw /Gy /GS- /permissive- /Zc:inline /fp:fast` | **TDGame Game 타깃 실제 조합** |
| **ue_editor_default** | 같은 공통 플래그 + `/fp:precise` | **TDGameEditor 타깃 실제 조합** |
| ue_game_avx / ue_game_avx2 | 공통 + `/fp:fast /arch:AVX{,2}` | Game + `MinCpuArchX64=AVX/AVX2` |

근거:
- **FP 모드 결정.** `VCToolChain.cs:1313-1347`(UE_5.8 Engine/Source/Programs/UnrealBuildTool/Platform/Windows): `Default`이고 `DefaultBuildSettings ≥ V7`이면 Editor·Program 타깃만 Precise(`:1324-1330`)가 되고, 나머지는 `/fp:fast`(`:1334-1335`)다. TDGame 두 타깃은 모두 V7이다(`Source/TDGame.Target.cs:11`, `Source/TDGameEditor.Target.cs:11`). 프로젝트 `.cs`에서 `FPSemantics`·`MinCpuArchX64`를 지정한 곳은 0건이다(grep).
- **CPU 아키텍처.** `TargetRules.cs:2650`의 `MinCpuArchX64` 기본값은 `Default`이고, `System/CppCompileEnvironment.cs:124` 열거형에서 `Default = None`이다. 그래서 `/arch`가 붙지 않는다(`VCToolChain.cs:1085-1101`은 None이 아닐 때만 `/arch:` 추가). x64 MSVC 기본은 SSE2이므로 **UE 기본 Game 빌드에서는 FMA 명령이 나올 수 없다**.
- **최적화 플래그.** `VCToolChain.cs:616`(/Oi), `:676`(/Gw), `:698`(/Gy), `:719`(/permissive-), `:739`(/Zc:inline), `:977-986`(/Ox + 속도 우선 /Ot), `:1501`(/GS-).
- **컴파일러.** `cl` 19.38.33135, 도구 집합 14.38(`fpdet/build_log.txt`). UE 최소 요구는 14.38.33130(`Engine/Config/Windows/Windows_SDK.json:46`).

### 1.3 표본
- 과제 지정 청크 (0,0), (-1,0), (1e7,-3e6), (2^40,5)를 각각 64 m@1 m(65²)와 64 m@0.5 m(129²)로 생성했다. 합계 83,464표본이다.
- 드문 뒤집힘을 보려고 넓은 영역도 더했다. 청크 -4..3 × -4..3을 64 m@0.5 m로 생성해 1,065,024표본이다.
- Python 기준값은 `chunkgen.generate_chunk(..., with_structures=False)`의 높이·바이옴과, `classify_biome`과 같은 식으로 계산한 온도·습도다.

## 2. 결정론 결과

### 2.1 Python(float64) 대비 높이 [검증, `fpdet/compare.json`·`tables.md`]

| 변형 | 4청크 8묶음: 다른 표본 | 넓은 영역: 다른 표본 | 최대 절대 차이(m) | 1 mm 후 다름 | 1 cm 후 다름 | 바이옴 다름 | 1 mm 기대 뒤집힘/표본 |
|---|---|---|---|---|---|---|---|
| precise_sse2, strict_sse2, precise_avx2, strict_avx2, **ue_editor_default** | 0 / 83,464 | 0 / 1,065,024 | 0 | 0 | 0 | 0 | 0 |
| **ue_game_default** = fast_sse2 = ue_game_avx(해시 동일) | 38,630 (46.3%) | 508,292 (47.7%) | 1.85e-13 | 0 | 0 | 0 | 4.7e-12 |
| fast_avx2 = ue_game_avx2(해시 동일) | 70,124 (84.0%) | 900,679 (84.6%) | 2.34e-13 | 0 | 0 | 0 | 1.4e-11 |
| precise_contract_avx2 | 69,072 (82.8%) | 869,284 (81.6%) | 2.34e-13 | 0 | 0 | 0 | 1.3e-11 |

- 온도·습도도 같은 양상이다. fast 계열의 온도 최대 차이는 2.1e-14 °C, 습도는 2.2e-16이다. 정밀 계열은 0이다.
- 먼 좌표의 영향은 없다. (2^40,5) 청크(원점에서 약 7×10^10 km)도 정밀 계열은 비트 일치하고, fast 계열의 차이 크기는 원점 청크와 같다(4.97e-14~7.11e-14). 위치를 정수 단위로 다루고 소수부를 정수 나머지에서 만드는 설계 덕이다.
- 최대 ULP(마지막 자리 단위)가 수십만~177만으로 크게 나오는 것은 0 m 근처 높이(ULP가 아주 작음) 때문이다. 절대 차이로 판단한다.
- 정밀 계열은 Python 원본과 비트 단위로 같다. 그래서 **Python 프로토가 C++ 생성기의 골든 해시를 만드는 기준값 노릇을 할 수 있다**(`compare.json`의 `python_reference`, 예: (0,0)@1 m 높이 SHA-256 앞 16자 `1b119c286034be7b`).

### 2.2 원인 [검증 일부·추정 일부]
어셈블리 명령 수는 생성 함수 범위에서 셌다(`fpdet/asm_stats.json`).

| 변형 | FMA | 나눗셈 | 곱셈 |
|---|---|---|---|
| precise_sse2 / ue_editor_default | 0 | 25 | 60 / 47 |
| fast_sse2 / ue_game_default | 0 | 10 | 80 / 67 |
| precise_avx2 | 0 | 25 | 60 |
| fast_avx2 / ue_game_avx2 | 40 / 33 | 10 | 40 / 34 |
| precise_contract_avx2 | 37 | 25 | 23 |

- **VS2022 `/fp:precise`는 AVX2에서도 FMA를 융합하지 않는다**(FMA 0개, Python과 비트 일치). 융합은 `/fp:contract`나 `/fp:fast`를 줄 때만 생긴다. 그래서 Precise라면 나중에 `MinCpuArchX64=AVX2`로 올려도 결과가 그대로다. [검증]
- SSE2 `/fp:fast`의 차이는 FMA가 아니라 나눗셈 25개 → 10개(역수 곱셈 치환)와 곱셈 증가(재결합)에서 온다. 역수 치환만으로 Python 모형을 만들어 보면 fast 출력과 표본의 57~67%만 일치한다(손 검사). 다른 재결합도 섞여 있다는 뜻이다. [검증: 치환 존재, 추정: 나머지 변환 종류]
- 같은 `/fp:fast`라도 `/O2`와 UE 플래그, SSE2와 `/arch:AVX` 사이의 해시는 이번 코드에서 같았다. 그러나 `/fp:fast`의 변환은 주변 코드, 인라인 여부, 컴파일러 버전에 따라 달라질 수 있다. **생성 코드를 건드리지 않은 패치에서도 기준값이 바뀔 수 있다**는 점이 핵심 위험이다. [추정]
- **이음매.** 모든 변형에서 (-1,0)의 오른쪽 열과 (0,0)의 왼쪽 열이 비트 일치하고, 1 m 격자와 0.5 m 격자의 짝수 표본도 일치한다(`fpdet/seam_check.json`). 비평 V1 ③이 걱정한 "벡터화 본체와 나머지 루프의 경로 차이"는 스칼라 이식에서는 생기지 않는다. 나중에 SIMD(단일 명령 다중 데이터)로 직접 벡터화하면 fast에서 다시 위험해진다. [검증·추정]

## 3. `/fp:precise`로 막지 못하는 것 [검증·추정 구분]

| 출처 | 결과 | 판정 |
|---|---|---|
| CRT 배정밀도 초월 함수 (`crt_fma3_probe.cpp`, `/fp:precise`, 같은 실행 파일에서 `_set_FMA3_enable(1/0)`, 100만 표본) | sin 9,370 · cos 9,524 · tan 26,491 · exp 1,498 · pow 1,197개 다름(상대 차이 ≤2.2e-16). log·atan2·sqrt·sinf·expf·powf는 0 | [검증] FMA3를 지원하지 않는 CPU나 FMA3를 가린 가상 머신에서는 같은 바이너리라도 다른 비트가 나올 수 있다 [추정: 시작 플래그는 CPU 지원으로 정해짐, 이 기계의 초기값 1] |
| UE `FMath::InvSqrtEst(float)` | `_mm_rsqrt_ss` 하드웨어 추정치 + 뉴턴 1회(`Runtime/Core/Public/Math/UnrealPlatformMathSSE.h:46-63`). 추정치는 CPU 제조사마다 다르다 | [검증: 구현, 추정: 제조사 차이] `InvSqrt`는 sqrt+div라 안전하다(`:24-44`) |
| 엔진 비인라인 함수 | `FPSemantics`는 모듈 단위다(`ModuleRules.cs:784-790`, `UEBuildModuleCPP.cs:2699`). 헤더 인라인 코드는 생성 모듈의 번역 단위에서 Precise로 컴파일되지만, Core 등의 `.cpp` 함수는 Game에서 `/fp:fast`로 남는다 | [검증: 모듈 단위, 추정: 영향 범위] |
| 벡터화된 초월 함수 호출 | MSVC 자동 벡터화가 루프 안의 `sin` 등을 벡터 수학 함수로 바꾸면 본체와 나머지 루프의 결과가 다를 수 있다 | [추정, 미측정] |
| Clang(리눅스 서버·콘솔) | Default와 Precise 모두 `-ffp-contract=off`(`Platform/Clang/ClangToolChain.cs:789-797`), clang-cl도 같다(`VCToolChain.cs:1339-1341`) | [검증] 리눅스 전용 서버(정밀)와 Windows Game 클라이언트(fast)는 지금 설정으로는 비트가 다르다 |

## 4. 속도 결과

### 4.1 단일 스레드 청크당 시간

조건: 교차 순서 6회 반복, 각 회는 청크 81~301개의 중앙값이다. 표 값은 "회별 중앙값의 중앙값 / 최솟값"(ms)이다. [검증, `fpdet/timing_rounds.json`·`timing_rounds.md`]

| 변형 | 32m@1m 높이 | 64m@1m 높이 | 64m@0.5m 높이 | 128m@1m 높이 | 32m@1m +기후 | 64m@1m +기후 | 64m@0.5m +기후 | 128m@1m +기후 |
|---|---|---|---|---|---|---|---|---|
| ue_game_default | 0.363 / 0.329 | 1.416 / 1.345 | 5.976 / 4.981 | 5.993 / 5.048 | 0.508 / 0.420 | 2.012 / 1.840 | 8.167 / 7.458 | 8.278 / 7.518 |
| ue_editor_default | 0.380 / 0.344 | 1.574 / 1.479 | 6.297 / 6.113 | 6.535 / 5.664 | 0.544 / 0.505 | 2.073 / 1.946 | 8.290 / 7.789 | 8.666 / 7.076 |
| precise_sse2(/O2) | 0.389 / 0.346 | 1.726 / 1.415 | 6.414 / 5.967 | 6.502 / 5.129 | 0.571 / 0.434 | 2.086 / 1.795 | 9.090 / 8.293 | 8.859 / 8.229 |
| precise_avx2 | 0.350 / 0.295 | 1.339 / 1.197 | 5.794 / 4.987 | 5.479 / 4.822 | 0.452 / 0.397 | 1.863 / 1.587 | 7.363 / 6.456 | 7.479 / 6.645 |
| ue_game_avx2 | 0.323 / 0.274 | 1.276 / 1.087 | 5.114 / 4.382 | 5.319 / 4.530 | 0.463 / 0.361 | 1.782 / 1.445 | 7.180 / 5.967 | 7.341 / 6.907 |

- **Precise 비용(ue_editor_default ÷ ue_game_default).** 여덟 칸에서 +1.5~+11.2%, 평균 약 +6%다. 같은 Precise끼리인 precise_sse2와 ue_editor_default도 최대 10% 차이가 나므로, 측정 잡음과 구분되지 않는다. 상한을 10%로 본다.
- **표본당 비용.** 높이만 약 335~370 ns, 기후 포함 약 475~490 ns(64 m@1 m)다. 표본당 격자 해시는 높이 64회, 기후 포함 88회다.
- **AVX2.** precise_avx2는 비트 일치를 유지하면서 ue_editor_default보다 약 10% 빠르다. 다만 `MinCpuArchX64=AVX2`는 AVX2가 없는 CPU에서 해당 코드가 비정상 종료되게 만든다(`TargetRules.cs:2643-2650` 주석). 선택 사항이다.
- **numpy 대비**(같은 시각, `fpdet/python_timing.json`). 기후 포함 64 m@1 m 15.97 ms 대 2.07 ms(7.7배), 64 m@0.5 m 68.9 대 8.29(8.3배), 128 m@1 m 73.7 대 8.67(8.5배), 32 m@1 m 8.21 대 0.544(15배).

### 4.2 다중 스레드 처리량

조건: 청크/초, 기후 포함, 고정 작업량 1,600/400청크. [검증, `fpdet/timing.json`]

| 변형 | 설정 | 1 | 2 | 4 | 8 | 16 |
|---|---|---|---|---|---|---|
| ue_game_default | 64m@1m | 472 | 888 | 1,679 | **2,901** | 4,276 |
| ue_editor_default | 64m@1m | 430 | 877 | 1,597 | **2,689** | 3,669 |
| ue_game_default | 64m@0.5m | 129 | 230 | 417 | **657** | 898 |
| ue_editor_default | 64m@0.5m | 119 | 223 | 404 | **674** | 951 |

- 8스레드는 1스레드의 5.1~6.3배, 16스레드(SMT, 동시 멀티스레딩)는 다시 약 1.4배다.
- 예산 대비: 1단의 50 m/s 작업자 예산은 청크당 183 ms다(`01-proto-budget.md:10`). C++ 64 m@1 m는 그 1.1%, 64 m@0.5 m는 4.5%다. 보행(초당 최대 1.33청크, `01-proto-chunkgen.md:13`)에서는 코어 하나의 0.3%다. **생성 수학은 병목이 아니라는 1단 결론이 C++에서도 확인됐다.** 남은 미지수는 엔진 객체화·충돌·내비 비용이다.
- 최적화 여지: 왜곡을 받지 않는 층(왜곡·대륙·기후)은 격자 모서리를 청크 단위로 캐시할 수 있다. 해시의 약 40%가 줄 것으로 본다. [추정, 미측정]

## 5. 결론과 권장

1. **`TDWorldGen.Build.cs` 생성자에 `FPSemantics = FPSemanticsMode.Precise;`를 둔다.**
   - 이렇게 해야 Editor·PIE(에디터 안 플레이)·헤드리스 시험(Editor 타깃)과 패키지 Game이 같은 비트를 낸다.
   - 속성은 `ModuleRules.cs:784-790`, 값 열거형은 `TargetRules.cs:400-419`(`Default`/`Precise`/`Imprecise`)에 있다.
   - 실행 비용은 0~11%(잡음 수준)이고, 생성 모듈에만 적용된다.
   - 빌드 비용: 공유 PCH를 쓰지 못하고(`UEBuildModuleCPP.cs:1819-1822`) `.FPPrecise` 변형 PCH가 하나 생긴다(`:1913-1916`). 작은 모듈이라 컴파일 한 번 분량이다. [검증: 규칙, 추정: 시간]
   - 같은 이유로 결정론 전투 시뮬레이터의 골든 해시(비평 V1)도 시뮬 코드가 있는 모듈에 같은 조치가 필요하다. 이 갈래의 범위 밖이므로 부모 판단에 맡긴다.
2. **정수 양자화만으로는 부족하다. 보조로 함께 쓴다.**
   - 이번 측정에서 1 mm 양자화 뒤 불일치는 0이었다. 그러나 표본당 4.7e-12~1.4e-11의 확률은 남는다.
   - 양자화로 막을 수 없는 것도 있다. 부동소수 배열 자체의 해시 비교, 그리고 높이에 기댄 이산 판정(물 여부 `height<0`, 고산 경계, 구조물 광장 높이)은 경계 근처에서 뒤집힐 수 있다.
   - 반대로 Precise만 믿으면 CRT 초월 함수나 컴파일러·플랫폼 변경에 약하다.
   - 따라서 둘 다 쓴다. 생성물의 정체성(존재 여부, 칸, 순번, ID)은 지금처럼 정수 해시로만 정하고, 저장하거나 비교하는 높이는 정수(예: int32 mm)로 양자화한다.
3. **저장 차분 설계에 주는 함의.**
   - 편집된 칸은 기준값 대비 float 차이가 아니라 **양자화된 절대값**이나 정수 매개변수 편집 연산(예: 정수 좌표·반지름의 굴착)으로 저장한다.
   - 그러면 기준값이 미세하게 달라져도 편집 칸은 그대로다. 영향은 편집 경계의 1e-13 m급 이음매뿐이라 보이지 않는다.
   - 세이브 검증 해시는 양자화한 값으로 계산한다.
4. **생성 코드 금지 목록**(Precise와 함께 지킬 규칙):
   - CRT 초월 함수 `sin`·`cos`·`tan`·`exp`·`pow`를 쓰지 않는다. 필요하면 다항식으로 직접 구현하거나 정수 표를 쓴다.
   - `FMath::*Est`, `VectorReciprocal*Estimate`를 쓰지 않는다.
   - 생성 결과에 들어가는 엔진 비인라인 수학 함수를 쓰지 않는다.
   - 전역 난수를 쓰지 않는다(D-47, 결정론 규칙과 같음).
   - 쓸 수 있는 연산: `+ - * / sqrt floor`와 정수 연산. 이 범위는 IEEE에서 정확히 반올림되므로 컴파일러·CPU·플랫폼이 달라도 같다.
5. **골든 시험.** Python 프로토 해시(`compare.json`의 `python_reference`)를 C++ 자동화 시험의 기대값으로 쓴다. Editor와 Game 빌드 양쪽에서 같은 해시가 나오면 1·4항이 지켜지고 있다는 증거가 된다(비평 실험 E2의 판정 기준).
6. **결정 항목 제안**(부모가 `Docs/Tasks/decisions.md`에 proposed로 기록할 후보): "생성 모듈 `FPSemantics=Precise` + 생성 코드 금지 목록 + 저장값 정수 양자화". 편차 없음. `MinCpuArchX64=AVX2`는 최소 사양 결정이므로 별도 항목으로 둔다(이득 약 10%).

## 6. 한계와 미측정
- UE 빌드 결과물이 아니라 같은 플래그로 만든 단독 `cl` 실행 파일이다. UE의 PCH, 단위 빌드(unity build), 인라인 맥락에서는 `/fp:fast` 변환이 다를 수 있다. 그래도 정밀 계열의 비트 일치 결론은 IEEE 의미론이라 그대로 유지된다. [추정]
- 구조물 단계(`sqrt`, 블렌드)는 이식하지 않았다. `sqrt`는 IEEE에서 정확하지만, fast의 재결합 영향은 측정하지 않았다.
- 시간은 다른 세션의 UnrealEditor 부하(CPU 30~85%) 아래에서 쟀다. 변형 간 ±10%는 잡음이다. 첫 일괄 측정(`timing.json` 단일 스레드 부분)은 더 흔들렸으므로 교차 반복 값(`timing_rounds.json`)을 쓴다.
- FMA3가 없는 CPU에서 직접 실행해 보지는 않았다. 같은 바이너리 안에서 경로를 바꿔 본 것으로 대신했다.
- 도구 집합 14.39와 Clang으로는 빌드하지 않았다.

## 7. 재현과 파일
- 일괄 실행: `python Tools/scratch/infinite_world/fpdet/run_fpdet.py --work <임시 폴더>`(약 3분). 교차 재측정: `--rounds 6`(빌드된 상태에서 약 3분).
- CRT 탐침: `crt_fma3_probe.cpp` 첫머리 주석의 명령을 쓴다. 실행 파일은 전체 경로로 호출한다. 이 세션 셸에서는 현재 폴더 실행이 막혀 있었다.
- 소스: `Tools/scratch/infinite_world/fpdet/{chunkgen_port.cpp, run_fpdet.py, crt_fma3_probe.cpp}`
- 원자료: `Saved/AgentOps/20261023/infinite-world/fpdet/{build_log.txt, asm_stats.json, compare.json, seam_check.json, timing.json, timing_rounds.json, timing_rounds.md, python_timing.json, crt_fma3_probe.json, environment.json, tables.md}`
- 덤프 원본(약 130 MB)은 세션 임시 폴더에만 두었다. 재실행하면 다시 만들어진다.
