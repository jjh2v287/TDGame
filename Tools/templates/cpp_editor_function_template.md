# (템플릿) 에디터 Python이 못 하는 일을 C++ 에디터 함수로 여는 절차

에디터 Python(`unreal` 모듈)에 없는 기능(예: 랜드스케이프 생성, 컴포넌트 추가, 엔진 내부 API)은 `Source/TDGameEditor`에
`UBlueprintFunctionLibrary` 정적 함수로 감싸 노출한다. 그러면 Python에서 `unreal.TD<이름>Library.<snake_case>()`로 호출된다.

## 1. 헤더 `Source/TDGameEditor/<영역>/TD<이름>Library.h`
```cpp
#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TD<이름>Library.generated.h"

UCLASS()
class TDGAMEEDITOR_API UTD<이름>Library : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "TD|<영역>", meta = (WorldContext = "WorldContextObject"))
	static bool DoSomething(UObject* WorldContextObject, const FString& Argument);
};
```
- 구조체 인자는 `USTRUCT(BlueprintType)` + `UPROPERTY(EditAnywhere, BlueprintReadWrite)`로 만들면 Python에서 `unreal.TD<구조체>()`와 `set_editor_property`로 채울 수 있다(예: `FTDLandscapeCreateRequest`).
- 출력 인자(`float& Out`)는 Python에서 반환값과 튜플로 오거나 단독 값으로 온다. 둘 다 처리한다.

## 2. 소스 `.cpp`
- `#include "TD<이름>Library.h"` (모듈 루트가 인클루드 경로에 있으므로 서브폴더 없이 파일명으로 인클루드).
- `UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);`
- 실패는 `UE_LOG(LogTemp, Error, ...)` 후 조기 반환. 새 코드에 주석을 달지 않는다(AGENTS.md 5절).

## 3. 의존 모듈
필요한 엔진 모듈을 `Source/TDGameEditor/TDGameEditor.Build.cs`의 `PrivateDependencyModuleNames`에 추가한다
(예: `"Landscape"`, `"LandscapeEditor"`, `"PCG"`, `"NavigationSystem"`, `"LevelEditor"`).

## 4. 빌드·재시작 (라이브 코딩 불가)
```bash
python Tools/ue_editor.py restart        # stop(저장 안 된 패키지 있으면 중단) → Build.bat → start → MCP 포트 대기
python Tools/run_in_editor.py -c "import unreal; print(hasattr(unreal, 'TD<이름>Library'))"
```
- `protected` 엔진 멤버(예: `ALandscapeProxy::bEnableNanite`)는 C++에서 못 건드린다 → Python `set_editor_property("enable_nanite", True)`처럼 UPROPERTY 이름으로 설정한다.
- 런타임(게임)에서도 필요한 액터/컴포넌트 클래스는 `Source/TDGame/<영역>/`에 만든다(예: `ATDInstancedMeshActor`).

## 5. 검증
- `python Tools/ue_editor.py status`로 에디터·포트 확인 → 함수 호출 스크립트 → 결과 캡처(`Tools/WorldGen/capture_views_mcp.py`) 또는 `Tools/editor_inspect_level.py`.
- 자동화 테스트가 필요하면 `Source/TDGame/*/Tests/` 패턴(메모리 `tdgame-build-and-test-workflow`)을 따른다.
