# 500마리 몬스터의 애니메이션 성능·품질·생산성 연구

### 요약

PC·콘솔에서 한 화면에 보이는 몬스터 약 500마리를 60fps로 처리하려면, **대부분의 반복 동작은 저렴하게 처리하고, 눈에 띄는 개체와 정확한 상호작용에만 독립적인 포즈 계산을 배정하는 방식**을 우선 검증하는 것이 합리적이다. 기존 애니메이션 공유의 품질 문제는 공유 자체가 메시 품질을 떨어뜨려서가 아니라, 서로 다른 개체의 **재생 시간, 이동 조건, 전투 반응을 소수의 동일한 포즈로 표현할 때** 발생한다.

**기존 공유보다 나은 후보는 있다.** 현재 프로젝트에 설치된 UE 5.8.2에는 `Instanced Skinned Mesh`와 GPU 애니메이션 재생 경로가 존재한다. 특히 `AnimSequenceTransformProvider`는 원본 시퀀스를 사용하는 개체별 재생·블렌딩 후보여서, 수작업 VAT 제작을 기본 공정으로 삼지 않고도 반복감과 개별 반응을 개선할 가능성이 있다. 다만 엔진 코드의 기능 존재와 TDGame에서의 품질·60fps 달성은 별개의 검증이다. 지형 접촉, CPU 본 조회, 전투 노티파이, 플랫폼별 패키징까지 확인한 뒤 채택해야 한다.[^1][^2][^21]

이 연구의 권고는 두 가지다. **안정적인 비교 기준은 ‘대표 포즈 공유 + 필요한 개체의 독립 평가·보정’으로 만들고, 가장 먼저 경쟁시킬 대안은 ‘개체별 GPU 시퀀스 재생 + 스켈레탈 인스턴싱’으로 삼는다.** UAF 전체 도입, Mass 전환, VAT 파이프라인 구축은 해당 병목과 편익이 확인된 다음 판단한다.

### 접근 방식

- 목표는 `500마리 동시 가시성`, `PC·콘솔`, `60fps`다. 동작 반복감, 발 미끄러짐·지형·회전, 공격·피격의 독립성을 모두 품질 평가에 포함한다.
- 애니메이션 평가, 포즈 전달, GPU 스키닝, 드로 제출, 그림자, 게임플레이 시뮬레이션을 분리한다. 한 항목의 절감을 전체 프레임 절감으로 바꾸어 말하지 않는다.
- Epic의 공식 문서·API와 설치된 UE 5.8.2 소스를 대조한다. 옛 군중 연구는 원리 설명에 사용하고 현대 플랫폼의 성능 수치로 환산하지 않는다.
- 기능은 `확인된 엔진 기능`, `TDGame 적용을 위한 설계 제안`, `실험이 필요한 가설`로 구분한다. 문서에 제시한 개체 수·평가 주기·오차 기준은 별도 표기가 없으면 실험 시작값이다.
- 게임 규칙, 중요도 결정, 시간 관리, 애니메이션 선택·보정 로직은 C++ 구현을 전제로 한다. 엔진 문서의 Blueprint 예제를 그대로 프로젝트 구현으로 채택하지 않는다.
- 이번 결과물은 연구와 검증 설계다. 게임 코드·에셋은 변경하지 않았고, 500마리 런타임 벤치마크나 콘솔 실기기 검증은 수행하지 않았다.

### 설명 1. 적용 범위와 확인 수준

조사 기준일은 2026년 9월 9일이다. `TDGame.uproject`의 엔진 연결은 `5.8`이며, 설치 엔진의 `Build.version`은 `5.8.2`, `Changelist 56702186`이다. 따라서 UE 5.6의 초기 UAF 소개나 UE 5.7의 Animation Bank 설명만으로 현재 기능을 판단하지 않는다.[^20]

| 항목 | 현재 확인한 내용 | 남은 결정·측정 |
|---|---|---|
| 동시 개체 수 | 화면에 약 500마리 | 실제 최고 밀도, 겹침, 시야 가림 비율 |
| 목표 | PC·콘솔 60fps, 프레임 주기 약 16.67ms | 최소 PC 사양, 콘솔 기종, 출력·내부 해상도 |
| 품질 문제 | 반복감, 접지·회전, 개별 전투 반응 모두 중요 | 실제 카메라에서 허용 가능한 오차와 비교 영상 |
| 콘텐츠 | 기존 스켈레탈 애니메이션 제작 흐름 존중 | 종별 스켈레톤, 본·버텍스·섹션 수, 동시 등장 분포 |
| 기존 경험 | 공유의 성능·생산성이 좋았으나 품질 불만 | 이전 구현이 Epic 플러그인인지 자체 포즈 복사인지, 당시 설정·측정치 |
| 현재 프로젝트 | C++ 전투·몬스터 코드 존재 | 실제 몬스터 에셋의 그래프·LOD·소켓 구성 |

이전 공유 구현의 상세 설정과 성능 캡처가 제공되지 않았으므로, 특정 설정 실수 때문에 품질이 나빴다고 단정할 수 없다. 대신 같은 증상을 만드는 구조적 원인을 나누어 설명한다. 또한 500마리가 모두 화면에 있다는 조건에서는 화면 밖 틱 중지만으로 목표를 해결할 수 없다.

현재 C++·설정·모듈 의존성에서는 Animation Sharing·ABA·Significance·Mass·Leader Pose·Copy Pose의 통합 근거를 찾지 못했다. 바이너리 에셋 내부까지 검사한 결과는 아니므로 미사용을 확정하지 않는다. 기존 엔진 가이드의 모듈형 파츠 공유와 UKGame의 URO·VAT 참고 기록도 이번 500마리 전투의 측정 근거로 사용하지 않았다.[^29]

### 설명 2. ‘애니메이션 복사·공유’가 줄이는 비용

| 방식 | 재사용하는 대상 | 주로 줄이는 비용 | 여전히 남는 비용·제약 |
|---|---|---|---|
| 같은 `AnimSequence` 에셋 참조 | 원본 애니메이션 데이터 | 에셋 중복 메모리 | 개체마다 평가하면 계산은 계속 발생 |
| `Save Cached Pose` 등 인스턴스 내부 캐시 | 한 애니메이션 그래프 내부 결과 | 같은 그래프에서의 중복 계산 | 그 자체로 500개 인스턴스 간 공유가 되지는 않음 |
| `Leader Pose` | 대표 컴포넌트의 본 변환 | follower의 독립 애니메이션 평가 | 독립 포즈 제한, 렌더링·섹션 비용 유지 |
| `Animation Sharing` | 상태별 대표 컴포넌트의 포즈 | 개체 수보다 작은 수의 대표 평가 | 상태 선택·포즈 전달·전환·렌더링 비용 |
| `Copy Pose From Mesh` | 다른 메시의 평가 결과 | 원본 동작 계산 재사용 | 목적지 평가·복사·보정·본 변환 비용 |
| 스켈레탈 인스턴싱 | 같은 메시의 렌더 자원·제출 구조 | 컴포넌트·드로 처리의 일부 | 독립 포즈를 어떻게 만드는지는 별도 문제 |
| GPU 시퀀스 평가 | GPU에서 시퀀스 샘플링·블렌딩 | CPU 애니메이션 평가·전달의 일부 | GPU 계산·대역폭, CPU와의 의미 연결 |
| VAT·본 애니메이션 텍스처 | 사전 변환한 재생 데이터 | CPU 평가, 인스턴싱을 쓰면 제출 비용 | 변환 공정·메모리·기능 연결·품질 관리 |

Epic의 Animation Sharing은 상태 버킷과 Leader Pose를 사용하는 방식이다. Leader Pose를 쓰더라도 개별 컴포넌트와 섹션의 렌더링이 자동으로 합쳐지는 것은 아니다. Copy Pose는 후속 개별 동작을 구성할 수 있지만 목적지의 처리가 생긴다.[^3][^4]

따라서 ‘한 마리만 계산하므로 500마리도 한 마리 비용’이라는 설명은 정확하지 않다. 공유는 **애니메이션 평가 횟수**를 크게 줄일 수 있지만, 500개의 위치·가시성·렌더링·공격 주체를 없애지는 않는다.

개념적으로 CPU 애니메이션 일량은 다음처럼 볼 수 있다. 이는 실측 모델이나 엔진 내부의 정확한 합산식이 아니다.

`전체 일량 ≈ 대표 포즈 평가 + 독립 포즈 평가 + 개체별 전달·보정 + 전환·시간 관리`

예를 들어 같은 스켈레톤·같은 이동 상태의 500마리를 위상 4개 × 속도군 3개, 총 12개의 대표로 표현하면 기본 포즈 평가 개수는 500개에서 12개로 줄어든다. **약 41.7배라는 값은 이 예시의 평가 개수 비율일 뿐, 프레임 속도 향상 배수가 아니다.** 종·상태·보정·전환이 추가되면 대표 수와 개체별 비용이 달라진다.

프레임 시간은 Game Thread, Render Thread, GPU 시간을 단순히 전부 더한 값도 아니다. 병렬 작업과 프레임 파이프라인의 임계 경로, 대기·동기화가 영향을 준다. 워커 스레드의 누적 CPU 작업시간이 줄어든 것과 화면 프레임이 빨라진 것은 별도로 보고해야 한다.

### 설명 3. 공유할수록 품질이 나빠지는 세 가지 원인

| 관찰되는 문제 | 원인 | 공유만 조정해 개선 가능한 부분 | 개별 처리가 필요한 부분 |
|---|---|---|---|
| 군무처럼 동시에 움직임 | 동일 클립·위상·재생 속도 | 대표 위상·클립 변형 증가, 안정적인 배정 | 완전히 독립적인 시간·행동 이력 |
| 발 미끄러짐 | 이동 속도와 보행 거리 불일치 | 속도군, 이동 클립 선택, 대표 재생 속도 조절 | 개체별 보폭·발 고정·가감속 대응 |
| 경사·계단에서 발이 뜸 | 개체마다 다른 지면을 동일 포즈로 표현 | 먼 개체의 단순 경사 분류 | 발별 접촉·골반·지형 보정 |
| 회전 시 몸이 꺾이거나 미끄러짐 | 이동 방향·시선·발 지지 상태 불일치 | 방향별 클립, 회전 상태 분리 | 개별 회전 보정·시작·정지 동작 |
| 공격·피격이 늦거나 중간부터 시작 | 개별 이벤트를 공유 재생에 합류 | 비중요 반복 반응의 제한된 공유 | 정확한 공격 시계, 개별 중단·우선순위 |
| 등급 변경 때 팝 | 서로 다른 포즈·위상·보정 상태로 교체 | 인접 위상 선택·제한된 블렌드 | 재생 상태와 발 고정 상태의 인계 |

**같은 현재 포즈를 직접 참조하는 follower에게 완전히 다른 재생 시간을 부여할 수는 없다.** 다른 시간이 필요하면 다른 대표를 선택하거나, 복수 샘플을 보간하거나, 개별 포즈를 평가해야 한다. 품질 개선은 자유도를 복구하는 작업이며, 그 자유도에는 저장·계산·관리 비용이 따른다.

이 한계는 모든 개체를 저품질로 만들어야 한다는 뜻은 아니다. 최종 포즈 전체를 공유하지 않고 공통 보행 부분만 재사용하거나, 원본 시퀀스 데이터는 공유하면서 GPU에서 개체별 시간을 평가하는 식으로 공유 대상을 바꿀 수 있다. 군중 연구에서도 이동·애니메이션·렌더링의 LOD를 함께 고려하고 발 미끄러짐을 별도 문제로 다룬다.[^5]

### 설명 4. 기존 Animation Sharing으로 먼저 개선할 부분

**4.1 위상 다양성은 대표 수와 함께 관리한다.**

UE 5.8.2의 `Num Randomized Instances`는 모든 follower에게 독립적인 임의 시간을 주는 장치가 아니다. 엔진은 여러 대표 컴포넌트에 서로 다른 시작 오프셋을 주고, 개체를 그중 하나에 배정한다. 소스에서는 대표 수에 따른 오프셋 계산과 무작위 컴포넌트 선택을 확인할 수 있다.[^22]

권장 실험은 대표 위상 수 `1 → 4 → 8` 비교다. 이동 루프에서 먼저 시험하고, 공격 시작 시점을 무작위화하는 용도로 사용하지 않는다. 평가 기준은 ‘전체적으로 다르게 보이는가’와 ‘서로 이웃한 개체가 반복적으로 동기화되는가’다.

추가로, 최초 배정 결과를 개체의 수명 동안 가능한 한 유지하고 상태 복귀 때 가까운 보행 위상을 선택하는 정책을 제안한다. 이것은 엔진 기본 랜덤 배정 이상의 C++ 정책이다. 매 프레임 무작위 재배정하면 다양성이 아니라 위상 튐이 생긴다. 주변 개체와의 중복을 줄이는 공간 배정은 단순한 안정 배정으로도 반복감이 충분히 줄지 않을 때만 검토한다.

**4.2 속도군은 적게 만들고 실제 점유율을 측정한다.**

같은 뛰기 클립이라도 실제 이동 속도가 다르면 접지감이 달라진다. 대표 포즈를 저속·중속·고속처럼 제한된 군으로 분리하면 오차를 줄일 수 있다. 속도군과 개별 재생 속도 제어가 Animation Sharing에서 자동으로 해결된다고 가정해서는 안 된다. 상태 선택과 대표 재생 정책을 구현해야 한다.[^22]

군 전환에는 히스테리시스를 둔다. 속도가 경계값 주변에서 흔들릴 때 매 프레임 다른 군으로 이동하면 블렌드 비용과 시각적 흔들림이 함께 증가한다. 이동 속도에는 최근 추세를 반영하되, 급정지·돌진 같은 전투 입력을 과도하게 지연시키지 않는다.

스켈레톤 수 × 상태 수 × 속도군 × 위상 수 × 변형 수를 모두 곱해 상시 생성하는 구성은 피한다. 예를 들어 4종 × 6상태 × 3속도 × 4위상만으로 288개 조합이다. 이는 동시 사용되는 대표 수의 확정값은 아니지만, 공유가 빠르게 비싸질 수 있음을 보여준다. 실제 생성·활성 대표 수와 follower 수의 분포를 기록하고, 같은 상태를 사용하는 개체가 적으면 독립 평가가 더 단순할 수 있다.

**4.3 Additive와 On Demand는 무료 다양성 옵션이 아니다.**

공유 시스템에는 전환·On Demand·Additive 기능이 있다. 하지만 On Demand의 동시 인스턴스가 고갈되면 이미 재생 중인 인스턴스로 합류하는 경로가 있고, Additive는 개체별 슬롯·평가 비용이 생긴다. 500개 독립 피격 반응에 작은 풀을 사용하면서 모두 정확한 시작 시간을 갖는다고 기대할 수 없다.[^3][^22]

상체 흔들림이나 호흡 같은 비중요 표현과, 공격 예고·실제 타격·강한 경직은 정책을 나눈다. 전자는 제한된 변형 공유를 허용할 수 있지만 후자는 개체의 이벤트 시간과 중단 여부를 보존해야 한다. 부족한 표현 예산을 이유로 게임플레이 공격을 누락하거나 늦추는 정책은 이 연구에서 권장하지 않는다.

**4.4 제한된 확장을 위해 엔진 포크부터 만들 필요는 없다.**

로컬 플러그인에는 `DeterminePermutationIndex`, `CreateAnimSharingInstance`의 가상 함수와 커스텀 manager factory를 받는 생성 경로가 있다. 대표 배정 확장은 native 파생 클래스에서 검토할 수 있다. 다만 이 진입점이 임의의 속도·지형·전투 정책을 자동 제공하는 것은 아니다.[^22]

공식 설정 예제는 상태·전환·Additive용 Animation Blueprint 구성을 사용한다. TDGame에서는 로직을 C++로 구현해야 하므로 native 애니메이션 proxy/node 구성과 생명주기 연결의 개발비를 포함해야 한다. ‘State Processor만 C++로 바꾸면 나머지 모든 애니메이션 로직도 C++ 정책을 충족한다’고 가정하지 않는다.

### 설명 5. 접지·회전 품질을 되찾는 방법

**5.1 공유 포즈 뒤의 보정은 목적지에 독립 포즈가 있을 때 가능하다.**

Leader Pose follower는 대표 포즈를 사용하는 경로이므로, 그 상태를 유지한 채 각 follower에 서로 다른 발 IK를 추가하면 해결된다는 설계는 성립하지 않는다. 개별 보정이 필요한 개체는 대표와의 Leader 연결에서 벗어나 자체 목적지 포즈를 만들고, Copy Pose 또는 독립 시퀀스 평가 뒤에서 보정해야 한다.[^4][^23]

권장 비교는 다음 두 가지다.

- 공통 보행 계산이 복잡할 때: `공유 기본 포즈 → 목적지 복사 → 개별 보폭·회전·접지 보정`.
- 기본 동작이 단순한 단일 시퀀스일 때: `독립 시퀀스 평가 → 개별 보정`.

두 번째가 항상 비싸다고 가정하지 않는다. 첫 번째에는 복사, 좌표계 처리, 대표 평가 순서 의존성이 있다. 공유의 편익이 작은 상태에서는 단순한 독립 평가가 오히려 관리하기 쉬우며 성능도 경쟁력이 있을 수 있다. 같은 콘텐츠로 두 경로를 비교해야 한다.

**5.2 발 미끄러짐은 속도 일치부터 해결한다.**

개체의 실제 이동 속도와 클립의 보행 속도를 먼저 맞춘 뒤, 제한된 보폭 조절을 사용한다. 속도 차이가 큰데 재생 속도만 계속 올리면 발걸음 빈도가 부자연스러워지고, 보폭만 크게 늘리면 다리가 과도하게 펴진다. 다른 이동 클립으로 바꿀 구간과 작은 보정으로 처리할 구간을 콘텐츠별로 정한다.

Pose Warping에는 방향·보폭·경사 보정이 있다. 그러나 이 노드들이 모든 지형·전투 문제를 자동 해결하지는 않는다. 특히 그래프 구동 방식은 필요한 root-motion 속성·입력을 요구한다.[^6]

UE 5.8.2의 Copy Pose는 커브·커스텀 속성 복사가 기본적으로 꺼져 있으며, Stride Warping에는 root motion이 없을 때 비활성화하는 설정이 있다. 따라서 ‘Copy Pose 뒤에 Stride Warping을 연결하면 바로 작동한다’는 계획은 불충분하다. 어떤 속도·위상·발 접촉 정보를 C++에서 전달할지와 좌표계가 무엇인지 먼저 정해야 한다.[^23]

**5.3 발 고정은 보행 이력과 지형 정보를 요구한다.**

현재 발을 아래로 내리는 IK만으로는 지지 중인 발이 바닥을 미끄러지는 문제를 해결하지 못한다. 접지 구간의 발 위치를 유지하고, 보행 전환에서 고정을 풀며, 골반 높이를 조절하는 이력이 필요하다. 경사면 전체의 기울기와 각 발이 놓인 계단 높이는 서로 다른 입력이다.

보정의 우선순위는 발별 접촉과 골반, 보폭·방향, 상체 시선, 꼬리·장식의 순으로 실제 화면에서 검토한다. 지면 트레이스는 필요한 개체·발에 한정하고, 접지 정보 갱신과 보정 포즈 평가의 주기를 따로 측정한다. 작은 장애물과 빠른 움직임에서는 낮은 빈도의 트레이스 캐시가 부정확해질 수 있다.

엔진의 Foot Placement 노드는 로컬 소스에서 실험적 기능으로 표시된다. 이를 즉시 표준 의존성으로 정하지 않고, 필요한 접지 동작을 만족하는지와 C++ 통합 범위를 먼저 검증한다.[^23]

**5.4 회전은 캐릭터 전체 yaw와 포즈의 관계를 맞춘다.**

이동 방향, 몸이 바라보는 방향, 공격 대상 방향을 하나의 yaw 값으로 취급하지 않는다. 작은 방향 차이는 보정으로 흡수할 수 있지만 급격한 반전·제자리 회전·빠른 시작과 정지는 해당 동작이 필요할 수 있다. 공유 대표를 계속 바꾸는 방법만으로 처리하면 발 접촉 위상과 방향 전환이 충돌한다.

Motion Matching은 좋은 동작을 찾는 품질 도구이며, 검색 비용 자체를 없애는 군중 최적화 기술은 아니다. 필요한 클립·전환 품질을 소규모 상태 선택으로 달성할 수 있으면 먼저 그것을 쓴다. 다양하고 민감한 이동이 필요한 보스·주목 개체에 대해서만 검색 주기와 데이터베이스를 포함해 비교한다.[^7]

### 설명 6. UE 5.8.2의 우선 검증 대안: 개체별 GPU 재생

**6.1 인스턴싱, 포즈 공급, 애니메이션 시스템을 분리한다.**

| 계층 | 역할 | 혼동하면 안 되는 점 |
|---|---|---|
| `UInstancedSkinnedMeshComponent` | 다수 인스턴스의 메시·변환·렌더링 구성 | 이것만으로 각 개체의 애니메이션 상태가 만들어지지 않음 |
| `AnimSequenceTransformProvider` | 시퀀스를 사용하는 포즈 공급·재생 경로 | 기존 AnimInstance의 모든 기능을 대체한다고 볼 수 없음 |
| `Animation Bank` | 애니메이션 데이터의 GPU 재사용을 위한 별도 관련 경로 | 이름이 비슷해도 위 provider와 동일 API로 취급하지 않음 |
| `AnimRuntimeTransformProvider` | CPU에서 만든 포즈를 track별 공급할 수 있는 경로 | CPU 평가·변환·업로드 비용이 사라지지 않음 |
| `UAF` | 애니메이션 실행·구성을 위한 새 프레임워크 | GPU 시퀀스 재생을 조사하기 위해 전체 게임을 UAF로 바꿀 필요는 없음 |
| `Nanite` | 지원되는 메시의 지오메트리·렌더 처리 | 개별 IK·공격 시계의 해결책과는 다른 계층 |

로컬 엔진에서 `InstancedSkinnedMesh`의 Nanite와 비 Nanite 렌더 경로를 확인할 수 있다. 따라서 ‘인스턴싱은 무조건 Nanite 전용’이라는 전제로 비교 범위를 제한하지 않는다. 단, 경로 존재가 모든 재질·LOD·플랫폼에서 같은 기능·성능을 보장한다는 뜻은 아니다.[^21]

**6.2 가장 중요한 차이는 ‘데이터 공유 + 개체별 시간’이다.**

`AnimSequenceTransformProviderDataInstance`에는 track별 자동·수동 재생, 재생 속도·위치, Blend Space 관련 API가 있다. 로컬 헤더에는 레이어와 마스크·블렌딩 관련 데이터 구조도 존재한다. 이것은 모든 개체가 동일 시점의 대표 포즈를 따라야 하는 구조보다 표현 자유도가 높다는 근거다.[^2][^21]

여기서 **기본 Data asset과 런타임 DataInstance를 구분하는 것이 핵심**이다. 기본 경로는 시퀀스 수를 기준으로 포즈를 공유하지만 DataInstance는 track pool의 항목별로 포즈를 공급한다. 후자의 메시 `AnimationIndex`는 단순한 원본 클립 번호가 아니라 track을 가리킨다. 개별 위상을 시험할 때 여러 개체에 같은 track을 배정하면 다시 같은 재생을 공유하게 된다.[^21]

이 경로의 실용적 가설은 다음과 같다. 애니메이터는 기존 스켈레톤·시퀀스를 수정하고, C++ 시스템이 개체별 클립·시간·속도·반응 상태를 제출한다. 필요한 GPU 데이터 변환·쿠킹은 공정에 포함하되, 매번 정점 텍스처와 머티리얼 연결을 수작업으로 관리하는 것을 기본 작업으로 만들지 않는다.

특히 동일 클립에 각기 다른 시작 위상과 실제 이동에 맞는 재생 속도를 부여하기 쉬워지므로, 반복감과 속도 불일치에 유리할 가능성이 있다. 하지만 GPU도 개별 샘플링·블렌딩을 수행하며 레이어 수가 증가하면 비용이 증가한다. CPU 공유보다 항상 빠르다는 결론은 아직 내릴 수 없다.

이 경로도 **데이터 전처리가 없는 시스템은 아니다.** 확인한 renderer 구현은 cooked 시퀀스를 CPU에서 풀어 GPU에 샘플 데이터를 올리고, GPU compute로 포즈를 평가한다. 로컬 기본값은 프레임당 업로드 예산 32개 시퀀스 프레임, 최대 샘플링 주파수 30, 단계적 quarter·half·full 업로드다. 이는 클립당 30개의 프레임만 저장한다는 뜻이나 GPU 평가를 항상 30Hz로 제한한다는 뜻은 아니다.[^30]

따라서 안정 상태뿐 아니라 처음 보는 몬스터·클립이 등장할 때의 CPU 스파이크, 업로드 완료시간, 초기 저밀도 샘플의 품질을 측정해야 한다. 60fps로 출력하더라도 빠른 검·발 동작이 원본 시간 해상도를 충분히 보존하는지 따로 비교한다. 샘플링 빈도·정밀도를 높이면 메모리·업로드 비용이 늘 수 있으므로 기본 설정 변경을 곧바로 권장하지 않는다.

**6.3 GPU에서 잘 재생되는 것과 게임에서 잘 동작하는 것은 다르다.**

| 검증 대상 | 첫 실험에서 확인할 질문 |
|---|---|
| 개별 시간 | 500개의 위상·속도가 서로 독립적인가, 상태 변경이 시간을 초기화하지 않는가 |
| 전환·레이어 | 이동 중 공격·피격·반응 중단이 의도한 블렌드·마스크로 나타나는가 |
| 본·소켓 | CPU에서 필요한 무기·이펙트 위치를 어떤 포즈와 시점으로 얻는가 |
| 이벤트 | 기존 Notify Begin/Tick/End를 누가 어떤 개체 시간에 실행하는가 |
| Root Motion | 이동 주체가 CPU라면 추출·소비 경로와 시간 일치가 가능한가 |
| 접지 | 개체별 발 IK를 어디서 계산하고 어떤 포즈에 적용하는가 |
| 렌더링 | 모션 벡터, 그림자, LOD, 머티리얼, bounds가 정상인가 |
| 제작 | 시퀀스 수정·본 구조 변경·추가 클립이 패키지까지 안정적으로 반영되는가 |
| 플랫폼 | 목표 PC RHI와 각 콘솔에서 셰이더·쿠킹·메모리·기능이 통과하는가 |

GPU 포즈를 매 프레임 CPU로 동기적으로 가져와 공격·소켓 계산에 사용하는 구성은 지연과 동기화 비용을 검토해야 한다. 권장 방향은 필요한 전투 시간·일부 뼈 계산을 CPU에서 유지하거나 정확한 상호작용 개체를 CPU 포즈 경로로 승격하는 것이다. 어느 쪽이든 해당 비용을 GPU 재생 성능표에서 제외해서는 안 된다.

인스턴스 컴포넌트에는 본 부착과 이전 프레임 변환 관련 API도 있다. 따라서 부착·모션 벡터가 전혀 지원되지 않는다고 단정해서는 안 된다. 다만 인스턴스 간 GPU 부착과 임의의 Actor가 CPU에서 조회하는 월드 소켓 좌표는 요구가 다르다. 실제 무기·이펙트 부착 방식으로 검증해야 한다.[^31]

`AnimRuntimeTransformProvider`의 track별 갱신 API는 선택적 CPU 포즈 공급의 가능성을 보여준다. 그러나 기본 UAF instanced pose writer를 사용하면 500개 독립 IK 포즈가 자동 생성된다고 설명하면 틀리다. 확인한 writer는 LOD0 입력을 요구하고 같은 입력 포즈를 여러 인스턴스에 기록한다. 독립적인 교정 결과를 공급하려면 별도 C++ 연결을 검증해야 한다.[^24]

**6.4 UAF·MetaHuman Crowd는 참고 구현과 채택 결정을 구분한다.**

UE 5.8 릴리스는 MetaHuman Crowd의 고품질 액터와 Instanced Skeletal Mesh 간 전환 및 UAF·시퀀스 사용을 소개한다. 이는 혼합 표현 방식이 실제 엔진 개발 방향과 맞는다는 참고 근거다. MetaHuman용 제작 파이프라인을 비인간형 몬스터에 그대로 적용할 수 있다는 증거는 아니다.[^1]

UAF 관련 로컬 플러그인의 실험 상태, 현재 프로젝트의 C++ 로직 정책, 에셋 전환 비용을 별도로 고려한다. 이 연구는 UAF 전면 도입을 선행조건으로 두지 않는다. 기존 C++ 게임플레이와 core GPU 재생 연결만으로 필요한 이익이 나오는지 먼저 확인한다.[^25]

`UAF.uplugin`에는 명시적인 Experimental 표시가 있지만 확인한 core sequence provider의 UCLASS에는 같은 표시가 없다. 이를 근거로 core 경로를 UAF와 함께 실험 기능이라고 일괄 분류하지 않는다. 반대로 표시가 없다는 이유만으로 출시 성숙도와 목표 플랫폼 지원을 보장하지도 않는다. `Animation Bank` 역시 별도 에셋·쿠킹·provider 구조이므로, 이 문서의 우선 실험은 이름이 유사한 모든 경로가 아니라 **DataInstance의 track별 시퀀스 재생**으로 특정한다.[^21][^25][^32]

### 설명 7. VAT·본 텍스처·기타 방법의 위치

**정점 VAT와 본 애니메이션 텍스처는 생산성과 메모리 성격이 다르다.** 정점 VAT는 프레임별 정점 변형을 저장하는 방식이며, 본 텍스처 방식은 프레임별 본 변환과 메시의 스킨 가중치를 사용한다. 후자의 저장량은 정점 수보다는 본 수와 총 프레임 수에 영향을 받지만, GPU 스키닝과 데이터 포맷 설계가 필요하다. 실제 메모리는 정밀도·압축·패딩·LOD·클립 구성에 따라 달라진다.[^8]

VAT 계열도 개체별 시간, 프레임 보간, 클립 전환을 구현할 수 있다. ‘VAT이므로 블렌딩이 불가능하다’는 일반화는 하지 않는다. 이 프로젝트의 문제는 가능한가보다 **수정·재생성·연결·검증까지 포함한 작업량이 기존 시퀀스 제작보다 얼마나 늘어나는가**다.

AnimToTexture 같은 엔진 도구는 변환 공정을 줄일 후보다. 그러나 원본 변경 감지, 파생 데이터 재생성, 머티리얼 설정, 노말·모션 벡터, 에셋 누락 검사까지 자동화해야 생산성 개선이라고 평가할 수 있다. 현재 조사만으로 그 공정이 TDGame 콘텐츠에서 충분히 자동화되었다고 볼 수 없다.[^26]

로컬 AnimToTexture는 정점 위치·노말 또는 본 위치·회전을 텍스처로 변환하는 API를 제공하며 플러그인은 Experimental로 표시된다. 공개 API가 있다는 것은 배치 제작 도구의 기반이 있다는 뜻이지, 현재 프로젝트에 자동 제작 공정이 준비되어 있다는 뜻은 아니다.[^26]

| 방법 | 성능상 기대 | 품질·생산성 절충 | 이번 연구의 위치 |
|---|---|---|---|
| 공유 + 위상·속도군 | CPU 기본 포즈 평가 감소 | 자유도 증가에 따라 대표 수 증가 | 기준 후보 |
| 공유 + 일부 Copy Pose·보정 | 공통 계산을 재사용하며 접지 개선 | 복사·보정·동기화 비용 | 중요한 비교 후보 |
| 독립 평가 + URO/ABA | 기존 기능 보존, 계산 빈도 조절 | 시간 해상도·이벤트 처리 확인 필요 | 기준선·고품질 경로 |
| GPU 시퀀스 + 스켈레탈 인스턴싱 | CPU 평가·제출 감소 가능 | 기능 연결·GPU 비용·플랫폼 검증 | 최우선 대안 실험 |
| 자동화한 본 텍스처·VAT | 대규모 반복 재생에 유리할 가능성 | 변환·파생 에셋·품질 검증 비용 | 앞선 후보 실패 시 또는 낮은 중요도 표현 |
| Mass | 개체 시뮬레이션·데이터 처리 구조 개선 | 기존 Actor·GAS·전투 연결 비용 | AI·이동 병목 확인 후 |
| Motion Matching | 상황에 맞는 동작 선택 품질 | 검색·콘텐츠·상태 연계 비용 | 고품질 소수 개체부터 |
| ACL·본/메시 LOD | 압축·샘플링·스키닝 비용 개선 가능 | 오차·소켓·보정 체인 유지 필요 | 각 후보에 공통 적용할 보조 수단 |

ACL은 데이터 압축과 품질 설정의 도구이며 공유의 대체재가 아니다. 본 LOD 역시 GPU 버텍스 LOD와 같은 것이 아니다. 스킨에 거의 기여하지 않는 본도 무기 소켓·공격 궤적·IK의 상위 체인에 필요할 수 있으므로 실제 required-bone 결과를 확인해야 한다.[^9]

### 설명 8. TDGame에 맞는 최소 혼합 구조

설계의 중심은 새로운 범용 군중 프레임워크가 아니라 **기존 몬스터의 게임 상태와 표현 상태를 연결하는 작은 C++ 정책**이다. 상태 선택·시간·승격/강등을 한 곳에서 결정하고, 포즈 계산과 렌더링을 선택한 경로로 보낸다. 아직 사용처가 없는 인터페이스 계층은 만들지 않는다.

| 표현 단계 | 적용 대상 | 제안 경로 | 유지해야 할 정보 |
|---|---|---|---|
| 독립 평가 | 보스, 조준 대상, 정확한 근접 공격·강한 반응, 큰 화면 점유 개체 | 독립 C++ 애니메이션 + 필요한 IK·전환 | 정확한 재생 시간·공격 상태·접촉 이력 |
| 제한된 개별 보정 | 접지가 보이지만 전체 독립 동작은 불필요한 개체 | 공유 포즈 복사 후 보정 또는 단순 독립 평가 | 위상·속도·보정 상태 |
| 대량 반복 표현 | 작은 화면 점유, 비상호작용 이동·대기 | 대표 포즈 공유, 또는 검증을 통과한 GPU 재생 | 개별 게임 상태·이동·논리 시간 |

모든 단계의 게임 규칙과 피해 정확도는 유지한다. 표현 단계가 내려갔다고 공격 자체를 취소하거나 AI 상태를 바꾸지 않는다. 경직·사망·공격 취소처럼 실루엣과 전투 의미를 바꾸는 사건은 낮은 단계에서도 식별 가능하게 보여야 한다.

**중요도는 거리 하나로 결정하지 않는다.** 탑다운 카메라에서는 같은 거리의 몬스터가 대부분 같은 크기로 보일 수 있다. 화면상 높이·점유, 플레이어와의 상호작용, 조준 여부, 공격 위험, 큰 실루엣 변화, 최근 피격·등장 등을 함께 사용한다. 화면 가림 정도는 보조 신호로 검토하되, 판정 비용이 절감 이익을 넘지 않게 한다.

처음부터 임의 가중치를 가진 복잡한 점수식을 만들 필요는 없다. 필수 고품질 대상 우선 → 화면 크기와 근접 상호작용으로 정렬 → 남은 예산 배정 순서로 시작한다. 동점일 때 순서를 안정적으로 유지하고 승격·강등 기준을 다르게 두어 깜빡임을 줄인다. 엔진 Significance Manager는 평가·정렬을 위한 기반이지만, 게임에서 어떤 대상이 중요한지는 프로젝트 정책이다.[^10]

**‘근처 32마리만 고품질’ 같은 고정 숫자를 최종 설계로 채택하지 않는다.** `16·32·64·128`개 독립 평가 예산을 비교하는 것은 유효한 실험이다. 그러나 같은 화면에서 500마리 모두 큰 크기로 보이거나 동시에 중요한 공격을 하면 그 숫자를 넘을 수 있다. 이 경우 GPU 대안·전체 비용 축소·플랫폼별 예산을 재검토해야 하며, 중요 개체가 적다는 가정으로 요구를 충족했다고 결론 내리면 안 된다.

### 설명 9. 전환과 동시 반응 폭증 처리

**승격 시에는 현재 상태를 이어받는다.** 원본 클립 또는 상태 ID, 루프 위상, 재생 속도, 전환 진행도, 공격 이벤트 식별자, 이미 처리한 타격 구간을 보존한다. 가능한 경우 목적지의 첫 포즈를 준비한 뒤 교체한다. 처음 한 프레임의 reference pose, 이전 프레임의 본, 틀린 bounds가 보이면 지속 프레임의 품질이 좋아도 실패다.

**강등 시에는 현재 동작에 맞는 대표를 고른다.** 가까운 보행 위상을 선택하고 짧은 전환을 사용하되, 정확한 공격·발 고정 중에는 무리하게 강등하지 않는다. 루프 위상 하나만으로 공격 구간·피격 중단·발 접촉 이력을 모두 표현할 수 없다는 점에 주의한다. 구체적인 스냅샷 형식은 실제로 사용하는 상태 수에 맞춰 최소화한다.

**전환 비용은 한동안 두 경로를 사용하는 비용이다.** 풀링·사전 준비는 생성 스파이크를 줄일 수 있지만 메모리를 쓴다. 카메라 회전으로 다수 개체가 승격될 때 평가·리소스 갱신·드로 비용이 동시에 증가할 수 있다. 일상적인 예산 재배치는 분산하되, 즉시 보여야 하는 전투 반응은 늦추지 않는 정책이 필요하다.

Stock Animation Sharing은 follower 등록 때 틱 플래그도 변경한다. 승격을 leader 포인터 해제만으로 끝내지 말고, 틱 복구·예산 시스템 재등록·대표의 수명·평가 순서를 함께 처리해야 한다. 보이지 않는 대표가 visibility 정책으로 멈추거나 낮은 LOD에서 필요한 뼈를 제공하지 못하는 상황도 검사한다. Copy Pose는 source가 준비된 뒤 실행되도록 의존성을 관리해야 한다.[^22][^23]

**500마리 동시 피격은 독립 시나리오다.** 공유나 GPU 경로가 평균 보행에서는 빨라도, 모든 개체가 다른 시간의 반응을 시작하면 대표·레이어·전환 수가 급증할 수 있다. 이때 완전 독립 몽타주·래그돌을 전부 활성화하는 것이 필수인지 표현 요구를 구분해야 한다. 실루엣이 맞는 짧은 경직을 개별 시간으로 보여주는 경로와, 소수의 강조된 물리 반응을 비교할 수 있다.

품질을 줄이는 경우에도 경직 여부·사망·공격 취소는 일치해야 한다. 풀 고갈로 다른 개체의 공격 중간 프레임에 합류하거나, 피해 이벤트가 중복 실행되는 것은 허용 가능한 시각적 간소화가 아니다.

### 설명 10. 현재 전투 코드와 애니메이션 시간의 연결

현재 소스에서 `UTDAnimNotifyState_MeleeAttack`의 Begin/Tick/End는 근접 피해 sweep을 직접 구동한다. 상태는 `MeshComp`를 기준으로 보관하며, owner에서 피해 문맥을 얻고 `DamageSubsystem`으로 전달한다. 대표 메시의 notify를 모든 follower에 그대로 전달하면 기존의 개체별 상태·시간·owner 의미가 유지되는지 확인해야 한다.[^27]

한편 `SampleBladePoints`는 화면에 그려진 현재 포즈를 그대로 읽는 대신, 원본 Montage track/Sequence에서 필요한 뼈를 평가하는 구조다. `SweepTimeRange`는 시간 구간을 나누어 샘플링한다. 이는 렌더 포즈와 독립적인 공격 궤적 계산을 검토할 수 있는 근거지만, 현재 notify 생명주기와 재생 시간 연결까지 이미 분리되어 있다는 뜻은 아니다.[^27]

현재 기본 공격 샘플 간격은 1/60초이며 누적 구간을 여러 번 검사한다. 애니메이션 틱을 낮추면 이 비용이 사라지기보다 한 프레임에 몰릴 수 있다. 기존 테스트는 큰 프레임 간격에서의 명중·중복 피해 방지 등을 다루지만, 공유/독립 전환과 GPU 표현, 보정 포즈와 원본 궤적의 일치까지 검증한 것은 아니다.[^27]

가장 작은 첫 실험에서는 공격 중인 개체를 기존 독립 애니메이션 경로에 유지하고, 이동·대기만 공유 또는 GPU 경로로 바꾸는 것이 좋다. 이 상태에서 얻는 이익을 확인한 뒤, 공격까지 대량 경로로 확장해야 할 때만 이벤트 시간 연결을 다룬다.

확장이 필요하면 C++ 공격 상태가 시작 시간·진행 구간·취소·완료를 소유하고 표현은 그 시간을 따라가도록 설계한다. 기존 애니메이션의 공격 창 메타데이터를 재사용하는 방향을 먼저 검토해 두 군데에서 타이밍을 따로 편집하지 않게 한다. 데이터 추출·일관성 검사와 현재 sweep 코드의 재사용 범위는 구현 단계에서 확인할 항목이다.

또한 원본 시퀀스에서 계산한 검 궤적과 화면상의 보정된 검 위치가 일치하는지 확인해야 한다. 상체 회전·공격 블렌드·워핑을 추가하면서 피해 샘플러는 원본 포즈만 사용하면 시각적 오차가 생길 수 있다. 타격에 영향을 주는 보정을 CPU 궤적에도 적용할지, 공격 중 해당 보정을 제한할지, 중요한 개체만 실제 포즈 기반으로 처리할지를 명시적으로 결정한다.

### 설명 11. URO·ABA와 나머지 병목

**URO·ABA는 개체별 평가의 보조 도구다.** URO는 평가·갱신 빈도를 조절하는 경로이고, Animation Budget Allocator는 중요도와 시간 예산에 따라 Skeletal Mesh의 작업을 조절하는 시스템이다. 둘 다 애니메이션 공유나 렌더 인스턴싱과 동일하지 않다.[^11][^12]

UE 5.8.2에서는 ABA에 컴포넌트를 등록하는 경로가 URO를 비활성화한다. 따라서 같은 컴포넌트에 독립적인 URO 정책과 ABA 정책을 겹쳐 제어하는 구성을 권장하지 않는다. 대표 포즈는 공유 시스템, 독립 개체는 선택한 예산 시스템처럼 각 대상의 틱 제어 주체를 분명히 한다.[^28]

`60fps 표시`와 `포즈 60회 평가`는 다른 개념이다. 일부 개체의 포즈를 낮은 주기로 평가하고 중간 프레임을 보간할 수 있지만, 빠른 공격·발 고정·이벤트·이동이 같은 방식으로 안전하게 생략되는 것은 아니다. 예시로 독립 주목 개체 60Hz, 보정 개체 30Hz, 작은 반복 개체 15·30Hz를 비교하되 보간·동기화 비용도 포함한다. 최종 주기는 실제 화면 평가로 정한다.

Root Motion과 URO의 관계를 ‘항상 충돌한다’로 일반화하지 않는다. 로컬 엔진에는 root-motion 조건에 따른 매 프레임 처리와 LookAhead 경로가 있다. 모드, 이동 소비 경로, 네트워크 역할이 존재하는 경우 그 역할까지 구분하여 검증해야 한다.[^28]

별도로, 기본 공유 leader의 Root Motion이 각 follower의 CharacterMovement에 자동 전달된다는 근거도 확인하지 못했다. 이동 delta를 개체별 충돌·이동에 적용하는 연결이 필요하다. 공유 이동은 C++ 이동과 in-place 동작으로 먼저 시험하고, Root Motion을 사용하는 중요한 돌진·공격은 독립 경로에서 기존 의미를 보존하는 것을 권장한다.[^33]

**GPU 스키닝과 그림자는 공유 뒤에도 남는다.** 일반 Skin Cache는 스키닝 결과를 버퍼에 저장하는 렌더 경로다. 이를 여러 캐릭터가 동일한 결과를 무조건 공유하는 군중 캐시라고 해석하면 안 된다. 캐시 메모리, 메시 LOD, 버텍스·본 영향 수, 모프·탠전트 처리, 레이트레이싱의 추가 비용을 함께 관찰한다.[^13]

VSM은 움직이거나 변형되는 그림자 투사체 때문에 캐시 페이지를 다시 그릴 수 있다. 500마리 애니메이션의 CPU 비용을 줄여도 그림자·픽셀·겹침 비용이 지배적이면 60fps를 달성하지 못한다. 그림자 품질 실험은 최종 요구를 유지한 비교와, 병목 분리를 위한 일시적 비활성 비교를 구분해서 보고한다.[^14]

AI·이동·충돌·GAS·피해 처리도 별도 축이다. 애니메이션 실험에서 전부 껐다면 그것은 표현 비용의 분리 측정이다. 실제 500마리 전투의 성능 증거는 그 시스템들을 켠 통합 장면에서 확보해야 한다. Mass는 이 축의 대안이며, 애니메이션 품질 문제만으로 현재 Actor 기반 전투를 재작성할 근거는 부족하다.

### 설명 12. 공정한 성능·품질 검증 계획

**12.1 같은 시뮬레이션을 여러 표현 경로로 재생한다.**

동일 카메라, 개체 위치·속도·회전, 공격·피격 이벤트, 랜덤 시드, 메시·머티리얼·LOD·그림자·해상도를 고정한다. 가능하면 C++ 시뮬레이션의 입력·사건 기록을 재생하여, 경로마다 AI가 달라진 탓에 비교가 오염되지 않게 한다. 이 시험에서만 게임플레이를 고정하고, 마지막에는 실제 AI·이동이 있는 통합 실행으로 검증한다.

비교는 두 단계로 보고한다. 첫째는 가능한 한 같은 기능·품질 설정으로 경로의 순수 비용을 비교한다. 둘째는 각 경로의 설정을 조절해 **같은 허용 시각 품질**을 달성한 최종 구성을 비교한다. 낮은 평가 주기·그림자·LOD로 품질을 줄인 경로와 고품질 기준선을 직접 비교하여 알고리즘의 성능 우위라고 발표하지 않는다.

| 시험 경로 | 목적 | 비교에서 지켜야 할 조건 |
|---|---|---|
| A. 개별 평가 | 품질·기능 기준선 | 실제 사용할 전환·공격·접지 포함 |
| A2. 개별 평가 + URO 또는 ABA | 기존 경로의 빈도 최적화와 비교 | 같은 컴포넌트에 두 정책 중첩 금지, 실제 품질·이벤트 오차 공개 |
| B. 기본 공유 | 공유만 적용한 성능·품질 차이 | 대표 수·전환·On Demand 사용량 공개 |
| C. 공유 다양성 개선 | 위상·속도군의 효율 | 각 단계에서 실제 활성 대표 수 기록 |
| D. 공유 + 선택적 보정 | 접지·회전 복구 비용 | Copy Pose와 단순 독립 평가 비교 |
| E. 개별 GPU 재생 + 인스턴싱 | 새 대안의 자유도·비용 | CPU 전투 연결·소켓·전환 비용 포함 |
| F. 혼합 최종 후보 | 실사용 60fps·품질 검증 | AI·충돌·GAS·효과·그림자 활성 |
| G. VAT/본 텍스처 비교 | 필요한 경우 성능·생산성 경계 확인 | 자동 변환·기능 연결 비용도 기록 |

**12.2 쉬운 장면과 어려운 장면을 분리한다.**

| 장면 | 확인하는 실패 |
|---|---|
| 평지 500마리 이동, 같은 종 | 가장 유리한 공유 조건과 반복감 |
| 다종·다른 속도·다른 상태 | 버킷 분산, 스켈레톤·클립 조합 증가 |
| 경사·계단·급회전·정지 | 발 미끄러짐·관통·회전 불일치 |
| 500마리 화면 유지, 일부만 공격 | 일반 전투의 표현 예산 배정 |
| 500마리 동시 공격 또는 다른 시점의 피격 | On Demand·전환·레이어 포화와 시간 정확도 |
| 카메라 회전·줌·등급 경계 왕복 | 승격 폭증, 상태 손실, 포즈 팝·bounds 오류 |
| 대량 사망·스폰·재사용 | 풀 생성·해제·메모리·이벤트 잔존 |
| 미사용 클립·새로운 종의 최초 등장 | GPU 데이터 준비·점진 업로드·초기 품질·쿠킹 누락 |
| 실제 최종 조명·그림자·이펙트 | GPU 병목 이동, 통합 프레임 예산 |

인원은 `100·250·500`으로 올리고 500을 필수 합격 조건으로 둔다. `750`은 여유 용량을 보기 위한 선택적 스트레스 시험이다. 평지 500마리 대기 장면만으로 500마리 전투를 통과했다고 기록하지 않는다.

**12.3 측정 항목과 보고 단위를 고정한다.**

- 프레임: median, P95, P99, 최대 스파이크, `16.67ms 초과 프레임 비율`, 발생 원인.
- CPU: Game/Render Thread의 임계 구간, 애니메이션 update/evaluate, 완료 대기, 상태 선택·보정·전투 샘플링, 워커 누적 시간.
- GPU: 전체 프레임, 스키닝·포즈 평가, Base Pass, Shadow Depths, 필요한 경우 레이트레이싱, 리소스 업로드·동기화.
- 작업량: 활성 대표, 독립 평가 개체, 평가 주기, 전환·On Demand·레이어 수, 포즈 복사 수, 풀 고갈·승격 횟수.
- 메모리: RAM/VRAM, 포즈·클립·파생 데이터·풀·캐시, 전환 중 최고 사용량, 스폰·해제 후 잔존.
- 기능: 공격 시작·타격·취소·완료 누락/중복, owner 일치, 잘못된 소켓·bounds·LOD, 지원되지 않는 경로.

Unreal Insights로 CPU 작업과 프레임을 확인하고 Animation Insights·Rewind Debugger 계열로 상태·포즈 동작을 추적한다. GPU는 GPU Profiler와 대상 플랫폼 도구를 함께 사용한다. 디버그 시각화와 상세 추적의 오버헤드를 분리하기 위해 진단 캡처와 최종 성능 캡처를 나눈다.[^15][^16]

실험 절차의 시작안은 워밍업 30초 후 120초 캡처를 같은 조건에서 3회 반복하는 것이다. 이는 통계적으로 모든 장면을 대표한다는 보장이 아니라 재현성 확보를 위한 최소 절차다. 최종 후보는 더 긴 실제 플레이에서 스트리밍·열·메모리 누적·장시간 프레임 페이싱도 확인한다.

PC에서는 최소·권장 사양을 명시하고 전원 상태·빌드 구성·드라이버·RHI를 기록한다. 콘솔은 선택한 각 기기의 실기기에서 확인한다. 에디터 PIE 결과는 동작 진단에 사용하며 패키지의 성능으로 대체하지 않는다. 60fps 상한을 끈 진단 실행과 실제 60fps 프레임 페이싱 실행을 모두 확보한다.

**12.4 품질은 정지 스크린샷보다 움직이는 비교 영상으로 평가한다.**

| 품질 지표 | 측정 방법 | 제안한 초기 합격 기준 |
|---|---|---|
| 반복감 | 실제 카메라 영상의 블라인드 A/B + 인접 개체 위상 분포 | 기본 공유보다 개선, 개별 기준선 대비 심한 군무 인지 없음 |
| 발 미끄러짐 | 지지 구간 발의 지면 상대 이동 + 영상상 픽셀 변위 | 근접 개체의 새로운 지속 미끄러짐이 기준선보다 증가하지 않음 |
| 지형 접촉 | 발 관통·뜸의 최대치·지속시간, 골반 떨림 | 실제 크기에서 식별 가능한 지속 오류 없음 |
| 회전 | 급회전 영상, 발 접촉 단절, 방향 전환 지연 | 기준선에 없는 튐·역회전·과한 비틀림 없음 |
| 전투 반응 | 이벤트 로그와 프레임 영상 대조 | 누락·중복·주체 오류 0건, 허용 지연은 게임 규칙과 함께 확정 |
| 등급 전환 | 경계 왕복·카메라 컷의 연속 영상 | reference pose·위상 초기화·공격 재시작 0건 |
| 표현·판정 일치 | 화면 무기 경로와 실제 sweep 시각화 | 오차를 측정하고 사전에 정한 판정 허용 범위 이내 |

고정된 `몇 cm`를 모든 몬스터에 적용하지 않는다. 신체 크기·카메라·내부 해상도에 따라 체감이 다르기 때문이다. cm 단위는 엔진 진단, 픽셀·영상 평가는 실제 인지 품질에 사용한다. 루트 이동이 있는 지형과 움직이는 바닥에서는 월드 공간의 발 정지 여부만으로 접지 품질을 판단하지 않는다.

60fps 목표의 초기 판정은 P99 프레임 시간이 16.67ms 이내인지와 초과 프레임 비율·스파이크를 함께 보는 것이다. P99만 통과하고 전환 때 반복적으로 큰 정지가 발생하면 합격으로 보지 않는다. 실제 제작에는 나머지 게임 시스템과 콘텐츠 증가를 위한 여유 예산이 필요하며, 애니메이션에 몇 ms를 배정할지는 기준 장면 실측 뒤 확정한다.

### 설명 13. 생산성도 실제 작업으로 비교한다

단순히 ‘VAT은 번거롭다’, ‘GPU 재생은 편하다’로 평가하지 않는다. 같은 콘텐츠 변경을 각 경로에서 수행하고 **사람의 작업시간, 자동 처리시간, 수동 단계 수, 파생 에셋 수, 오류 복구시간**을 기록한다.

| 제작 시험 | 확인할 비용 |
|---|---|
| 이동 클립 하나 수정 | 재임포트부터 실행·패키지 반영까지 걸린 시간 |
| 공격 클립 추가·타격 구간 수정 | 재생·전환·피해 시간의 중복 편집 여부 |
| 동일 스켈레톤의 다른 메시 추가 | 메시별 재변환, 머티리얼·LOD·bounds 설정 |
| 다른 스켈레톤 종 추가 | 대표군·provider·소켓·보정 체인 추가 비용 |
| 본 구조·스킨 가중치 수정 | 파생 데이터 무효화와 오류 검출 범위 |
| 에셋 이동·이름 변경 | 참조·쿠킹 누락의 검출과 수정 비용 |
| 이전 버전으로 회귀 | 원본과 파생 데이터의 재현성·버전 관리 부담 |

첫 연결 비용과 클립 20개를 반복 수정하는 유지 비용을 구분한다. 대안이 초기에는 빠르게 설치되지만 매번 수동 변환을 요구하면 누적 생산성이 낮다. 반대로 GPU 경로도 초기 C++ 연결·디버깅 비용이 크면 작은 콘텐츠 집합에서는 공유 개선이 더 경제적일 수 있다.

첫 프로토타입은 실제 사용할 대표 스켈레톤 한 종, 이동·공격·피격 클립으로 제한한다. 이것이 기능·품질을 통과하면 다른 스켈레톤 종을 추가해 범용성을 검증한다. 처음부터 모든 몬스터를 변환하지 않는다.

### 설명 14. 구현 순서와 채택 기준

| 순서 | 작업 | 다음 단계로 넘어갈 근거 |
|---|---|---|
| 1 | 고정 장면·이벤트 재생과 개별 평가 기준선 확보 | 500 가시 개체, 프레임·품질·판정 로그 재현 |
| 2 | 기본 공유와 위상·속도군 비교 | 대표 수 대비 반복감·발 오차의 개선 곡선 |
| 3 | 선택적 Copy Pose 보정과 단순 독립 평가 비교 | 접지·회전 품질 복구에 필요한 실제 CPU 비용 |
| 4 | core GPU 시퀀스 + 인스턴싱 최소 프로토타입 | 독립 시간·전환·패키지·CPU 연결 기능 통과 |
| 5 | 같은 전투·카메라·그림자로 혼합 후보 비교 | 500마리 통합 프레임·품질·생산성 기준 충족 |
| 6 | 남은 병목에만 추가 대응 | AI이면 시뮬레이션 구조, GPU이면 렌더·LOD, 변환 공정이면 제작 자동화 |

단계 2·3의 공유 개선과 단계 4의 GPU 프로토타입은 기준 장면이 준비되면 독립적으로 진행할 수 있다. 공유 개선을 완성한 뒤에야 GPU 대안을 조사할 필요는 없다.

**공유 혼합 방식을 채택할 조건:** 500마리 통합 장면이 목표를 만족하고, 독립 평가가 필요한 개체 수가 현실적인 예산 안에 있으며, 전환·대표 관리가 제작에 과도한 부담을 주지 않는다.

**GPU 재생을 대량 표현의 기본으로 채택할 조건:** 동일 품질에서 전체 프레임과 CPU/GPU 여유가 더 좋고, 공격·소켓·전환·패키징이 통과하며, 원본 클립 수정의 생산성이 유지된다. GPU가 이미 병목인데 CPU만 줄었다면 채택 근거가 약하다.

**VAT·본 텍스처를 확대할 조건:** 위 후보가 목표에 도달하지 못했거나 극단적으로 반복적인 표현에서 추가 이득이 분명하고, 변환 공정을 자동화했을 때 누적 제작 비용이 수용 가능하다.

**UAF·Mass를 확대할 조건:** 새 실행·시뮬레이션 구조가 필요한 구체적인 병목이 측정되고, 현재 C++ 게임플레이와의 통합·업그레이드 비용을 포함해 이익이 있다. 이름이나 데모의 개체 수만으로 선택하지 않는다.

### 설명 15. 최종 판단과 남은 불확실성

애니메이션 공유는 성능·생산성 면에서 유효한 출발점이다. 다만 최종 포즈까지 동일하게 유지하는 한, 서로 다른 지형·이동 이력·전투 사건을 완전히 자연스럽게 표현하는 데 한계가 있다. 대표 수 확대는 반복감에 대한 해법이며, 개별 접지와 전투 시간의 완전한 대체재는 아니다.

TDGame에서는 **중요한 움직임에 필요한 자유도만 복구하는 공유 혼합 방식**이 가장 보수적인 기준 후보다. 동시에 **UE 5.8.2의 개체별 GPU 시퀀스 재생**은 기존 시퀀스 제작 방식을 유지하면서 자유도를 넓힐 수 있어 가장 가치 있는 비교 대상이다. 새 경로가 모든 품질·플랫폼 검증을 통과하면 중·저중요도 개체의 기본 표현을 공유 follower에서 GPU 개별 재생으로 바꾸는 결정을 할 수 있다.

현재 확정할 수 없는 것은 후보별 실제 ms, 최대 고품질 개체 수, GPU 데이터 메모리, 최소 PC·각 콘솔의 지원 결과, 실제 몬스터 에셋에서의 품질이다. 이것을 얻기 위한 실험과 채택 조건까지가 이 문서의 결론이며, 500마리 60fps 달성 보고서로 사용해서는 안 된다.

### 출처와 확인 근거

웹 문서는 별도 날짜가 없으면 2026년 9월 9일 열람 기준이다. API 페이지의 버전은 UE 5.8이며 Python API 페이지 제목의 `Experimental` 표시만으로 해당 C++ 기능 전체의 출시 성숙도를 판단하지 않았다. 로컬 소스 근거는 UE 5.8.2 CL 56702186 기준이며, 라인 번호는 엔진·프로젝트 변경 시 달라질 수 있다.

[^1]: Epic Games. [Unreal Engine 5.8 Release Notes](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes). MetaHuman Crowd의 실험 상태, 고품질 액터와 인스턴스 표현 전환 설명. 특정 콘솔의 TDGame 성능 보장으로 사용하지 않음.
[^2]: Epic Games. [AnimSequenceTransformProviderDataInstance — Unreal Python 5.8 API](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/AnimSequenceTransformProviderDataInstance). C++ `Engine` 모듈의 track별 재생·속도·위치·Blend Space API를 대조하는 공개 근거.
[^3]: Epic Games. [Animation Sharing Plugin](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-sharing-plugin-in-unreal-engine). 상태 버킷·Leader Pose, randomized instances, On Demand·Additive·전환 개요. 구체적 동작은 로컬 구현과 대조.
[^4]: Epic Games. [Working with Modular Characters](https://dev.epicgames.com/documentation/en-us/unreal-engine/working-with-modular-characters-in-unreal-engine). Leader Pose와 Copy Pose의 포즈·독립 동작·렌더 비용 차이. 모듈형 캐릭터 비교표를 몬스터 500마리의 성능표로 전용하지 않음.
[^5]: Alejandro Beacco. [Simulation, Animation and Rendering of Crowds in Real-Time](https://diglib.eg.org/items/c42ea6e2-fa4c-4b23-9bc5-c6781a06a6b8). Eurographics Dissertation, 2014-12-11. 공개 초록의 이동·애니메이션·렌더링 LOD와 foot-sliding 문제 구분을 참고. 수치·알고리즘 재현을 주장하지 않음.
[^6]: Epic Games. [Pose Warping](https://dev.epicgames.com/documentation/en-us/unreal-engine/pose-warping-in-unreal-engine). Orientation·Stride·Slope Warping의 역할과 입력 조건.
[^7]: Epic Games. [Motion Matching](https://dev.epicgames.com/documentation/en-us/unreal-engine/motion-matching-in-unreal-engine). 포즈 검색과 데이터베이스·검색 비용·전환의 관계.
[^8]: Bryan Dudash. [GPU Gems 3, Chapter 2: Animated Crowd Rendering](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-2-animated-crowd-rendering). NVIDIA, 2007. 본 행렬 텍스처, 개별 프레임·인스턴싱·LOD의 원리. 과거 GPU 성능 수치는 현대 PC·콘솔 예측에 사용하지 않음.
[^9]: Epic Games. [Animation Compression Library](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-compression-library-in-unreal-engine). 압축·메모리·변형 오차 설정의 역할.
[^10]: Epic Games. [Significance Manager](https://dev.epicgames.com/documentation/en-us/unreal-engine/significance-manager-in-unreal-engine). 중요도 평가·관리의 기반 기능.
[^11]: Epic Games. [Animation Budget Allocator](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-budget-allocator-in-unreal-engine). 예산 기반 애니메이션 작업 조절.
[^12]: Epic Games. [Animation Optimization](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-optimization-in-unreal-engine). 애니메이션 평가·URO·프로파일링 개요.
[^13]: Epic Games. [Skeletal Mesh Rendering Paths](https://dev.epicgames.com/documentation/en-us/unreal-engine/skeletal-mesh-rendering-paths-in-unreal-engine). GPU 스키닝·Skin Cache·메모리·레이 트레이싱의 관계.
[^14]: Epic Games. [Virtual Shadow Maps](https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine). 변형·이동에 따른 캐시 무효화, 그림자 렌더링 비용과 프로파일링.
[^15]: Epic Games. [Unreal Insights](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-insights-in-unreal-engine). CPU·프레임 추적의 기반 도구.
[^16]: Epic Games. [Animation Insights](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-insights-in-unreal-engine). 애니메이션 런타임 추적 도구. 구체적 캡처 구성은 설치 버전에서 확인 필요.
[^20]: 프로젝트 [TDGame.uproject](C:/Project/TDGame/TDGame.uproject:3), 설치 엔진 [Build.version](<C:/Program Files/Epic Games/UE_5.8/Engine/Build/Build.version:2>). 엔진 연결과 패치·CL 확인.
[^21]: UE 5.8.2 로컬 엔진. [AnimSequenceTransformProviderData.h](<C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Classes/Animation/AnimSequenceTransformProviderData.h:1037>)의 DataInstance·track API(1037~1109행), 레이어 설정(74~100행). [DataInstance 포즈 공급](<C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Private/Animation/AnimSequenceTransformProviderData.cpp:1341>)의 track 수·offset과 기본 Data 경로(392~431행). [InstancedSkinnedMeshSceneProxyDesc.cpp](<C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Private/InstancedSkinnedMeshSceneProxyDesc.cpp:10>)의 Nanite·Static·GPUSkin 분기 및 CPU skin/scene extension 조건. 소스는 읽기 전용으로 확인.
[^22]: UE 5.8.2 로컬 플러그인. [AnimationSharingManager.cpp](<C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Developer/AnimationSharing/Source/AnimationSharing/Private/AnimationSharingManager.cpp:1135>): 대표 오프셋(1135~1142·1165~1169), 개체 배정(2183~2192), On Demand(2299~2416), Additive(1759~1764·2419~2441), 등록 시 tick 변경(583~596), leader 연결(2122~2127). [확장 함수](<C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Developer/AnimationSharing/Source/AnimationSharing/Public/AnimationSharingManager.h:401>)와 [manager factory](<C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Developer/AnimationSharing/Source/AnimationSharing/Public/AnimationSharingModule.h:51>). 개별 정책 확장은 제안이며 구현 결과가 아님.
[^23]: UE 5.8.2 로컬 엔진. [AnimNode_CopyPoseFromMesh.cpp](<C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/AnimGraphRuntime/Private/AnimNodes/AnimNode_CopyPoseFromMesh.cpp:30>)의 커브·속성 복사 기본값. [AnimNode_StrideWarping.h](<C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Animation/AnimationWarping/Source/Runtime/Public/BoneControllers/AnimNode_StrideWarping.h:114>)의 root-motion 입력 조건. [AnimNode_FootPlacement.h](<C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Animation/AnimationWarping/Source/Runtime/Public/BoneControllers/AnimNode_FootPlacement.h:522>)의 Experimental·manual plant 설정. Copy Pose 평가 순서는 출처 4의 관련 절과 교차검증.
[^24]: UE 5.8.2 로컬 엔진. [AnimRuntimeTransformProviderData.h](<C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Classes/Animation/AnimRuntimeTransformProviderData.h:193>)의 track별 갱신. [RigUnit_UAFWriteInstancedSkinnedMeshPose.cpp](<C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Experimental/UAF/UAF/Source/UAF/Private/Graph/RigUnit_UAFWriteInstancedSkinnedMeshPose.cpp:51>)의 LOD0 검사와 동일 입력 포즈를 각 instance track에 쓰는 반복문(73~102행).
[^25]: UE 5.8.2 로컬 플러그인. [UAF.uplugin](<C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Experimental/UAF/UAF/UAF.uplugin:16>)의 `IsExperimentalVersion=true`. core provider의 성숙도와 별개로 해석.
[^26]: UE 5.8.2 로컬 플러그인. [AnimToTextureBPLibrary.h](<C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Experimental/AnimToTexture/Source/AnimToTextureEditor/Public/AnimToTextureBPLibrary.h:16>)의 변환 대상 설명과 `AnimationToTexture` API(31행). [AnimToTexture.uplugin](<C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Experimental/AnimToTexture/AnimToTexture.uplugin:15>)의 Experimental 표시.
[^27]: 프로젝트 [TDAnimNotifyState_MeleeAttack.cpp](C:/Project/TDGame/Source/TDGame/Combat/AnimNotify/TDAnimNotifyState_MeleeAttack.cpp:59). Begin/Tick/End(59·76·103), `SampleBladePoints`(243), `SweepTimeRange`(300), 피해 적용(377). [Notify 설정·상태](<C:/Project/TDGame/Source/TDGame/Combat/AnimNotify/TDAnimNotifyState_MeleeAttack.h>)와 [기존 테스트](<C:/Project/TDGame/Source/TDGame/Combat/Tests/TDMeleeAttackNotifyTests.cpp:146>)는 읽기 확인이며 이번에 실행하지 않음.
[^28]: UE 5.8.2 로컬 엔진. [AnimationBudgetAllocator.cpp](<C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Runtime/AnimationBudgetAllocator/Source/AnimationBudgetAllocator/Private/AnimationBudgetAllocator.cpp:1195>)의 URO 비활성화. [SkinnedMeshComponent.cpp](<C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Private/Components/SkinnedMeshComponent.cpp:290>)의 root-motion별 URO 분기(290·302·360~363·394행). 매개변수 처방이 아닌 동작 경계의 근거로 사용.
[^29]: 프로젝트 [TDCombatCharacter.h](C:/Project/TDGame/Source/TDGame/Characters/TDCombatCharacter.h:15), [TDGame.Build.cs](C:/Project/TDGame/Source/TDGame/TDGame.Build.cs:11), `Source`·`Config`·uproject의 관련 키워드 검색. 기존 [애니메이션·물리·이동 가이드](C:/Project/TDGame/Docs/UE_Engine_Animation_Physics_Movement_Optimization_Guide.md:139), [UKGame 애니메이션 참고](C:/Project/TDGame/Docs/UKGame/11-animation-movement-optimization.md:196)는 현재 500개체의 성능 근거로 사용하지 않음.
[^30]: UE 5.8.2 로컬 renderer. [AnimSequenceTransformProvider.cpp](<C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Renderer/Private/Skinning/AnimSequenceTransformProvider.cpp:64>)의 업로드 예산·샘플 단계·최대 샘플링·정밀도 기본값(64~112행), compute shader 등록(241행), CPU 시퀀스 압축 해제(1063~1109행). 프로젝트별 CVar override와 실제 실행 결과는 미확인.
[^31]: UE 5.8.2 로컬 엔진. [InstancedSkinnedMeshComponent.h](<C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Classes/Components/InstancedSkinnedMeshComponent.h:255>)의 이전 프레임 변환 API(255~265행), animation index/provider 설정(271~297행), 본 부착 API(311행). 지원 범위 존재를 확인한 것이며 실제 무기 부착 검증은 미실시.
[^32]: UE 5.8.2 로컬 엔진. [AnimBank.h](<C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Classes/Animation/AnimBank.h:176>)의 별도 애니메이션 에셋·시퀀스·플랫폼 파생 데이터 경로와 [AnimBankTransformProvider.cpp](<C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Renderer/Private/Skinning/AnimBankTransformProvider.cpp>). DataInstance의 track API와 동일한 것으로 취급하지 않음.
[^33]: UE 5.8.2 로컬 엔진. [SkeletalMeshComponent.cpp](<C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Private/Components/SkeletalMeshComponent.cpp:4527>)의 `ConsumeRootMotion_Internal`과 [CharacterMovementComponent.cpp](<C:/Program Files/Epic Games/UE_5.8/Engine/Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp:1682>)의 자기 mesh 포즈 갱신·root-motion 소비. 기본 AnimationSharing에서 follower별 이동 delta 전파는 확인되지 않음.
