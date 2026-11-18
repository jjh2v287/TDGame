# P5 본 측정 실행 절차 (0b-3 작업자용)

작성 2026-11-12 0b-1 나 작업자. 기준: `Docs/VolumeFog3D/stage0/prereg-v2-0b.md` P5(3인칭·최대 거리 20m·격자 8픽셀·128층, 0a 의 D(A,B)=6.03, 통과 4.2~7.8, 탑다운은 정보), `spec-0b.md` 의 무늬·좌표 약속. 이 절차는 만들기만 했고 실행하지 않았다(`TD.Fog3D.ProbeWrite` 가 0b-2 에서 만들어지는 콘솔 명령이라 지금은 없다). 모든 명령은 `C:\Project\TDGame` 에서 PowerShell 로 실행한다(`/Game/...` 인수, L-repo-01).

## 0. 시작 전 확인

1. 0b-2 가 끝나 `TD.Fog3D.ProbeWrite` 가 들어간 빌드가 에디터에 올라와 있어야 한다. 새 C++ 파일이라 라이브 코딩이 안 되므로 `python Tools/ue_editor.py restart`(L-build-01). 재시작하면 메모리에만 있던 시험 물체가 전부 사라지므로 아래 1번부터 다시 한다.
2. `python Tools/ue_editor.py ensure`, `Get-Process | Where-Object { $_.ProcessName -match 'UnrealBuildTool|MSBuild|cl' }` 가 비어 있고 `Saved/AgentOps/build_hold.json` 이 없어야 한다.
3. 콘솔 명령 존재 확인(에디터 명령 창 또는 원격 실행): `TD.Fog3D.ProbeWrite` 가 모르는 명령이면 0b-2 가 안 끝난 것이다. 멈추고 감독에게 알린다.
4. `git status --short Content Config Source` 를 `b1/git_status_p5_before.txt` 로 남긴다(끝에서 새 변화 0건 확인용).

## 1. 시험 물체 만들기 (에디터 PIE 꺼짐)

```powershell
cd C:\Project\TDGame
python -B Tools/scratch/fog3d/fog3d_setup.py create            # 수식판 상자(m3/setup.json 다시 씀), 25초 대기
python -B Tools/scratch/fog3d/fog3d_tex_setup.py create        # 128x128x64 볼륨 렌더 타깃 + 텍스처판 재질 + Fog3dTexBox
python -B Tools/scratch/fog3d/fog3d_p5_small_setup.py add      # 40x40x10 볼륨 렌더 타깃 + Fog3dTexBoxSmall (같은 재질)
```

- 각 출력의 `compile_errors` 가 비어 있고 `mid_read_back.DensityVolume` 이 `/Engine/Transient.Fog3dDensityVolumeRT` 와 `...SmallRT` 로 읽히는지 본다. 크기는 `b1/tex_setup.json`, `b1/p5_small_setup.json` 에 있다.

## 2. 계획 만들기와 촬영 (해상도 두 벌, 구도 둘, 장면 N·C·A·B·A')

```powershell
$b1 = "Saved/AgentOps/20261112/fog3d/stage0/b1"
foreach ($resolution in "full","small") {
  python -B Tools/scratch/fog3d/make_b1_p5_plan.py --setup Saved/AgentOps/20261112/fog3d/stage0/m3/setup.json --resolution $resolution --output $b1/p5/$resolution/plan.json
  python -B Tools/FluidFX/pie_lookdev_hires.py --shot-size 1920x1080 --level /Game/FluidFX/Maps/LV_TDFluidFogStage --variants $b1/p5/$resolution/plan.json --output-dir $b1/p5/$resolution/run
  "$resolution exit=$LASTEXITCODE"
}
```

- 계획은 해상도마다 변형 10개(3인칭 5 + 탑다운 5, 각 8장 = 80장). 변형 이름이 `<구도>_d20g8_<장면>_e025` 라서 0a 판정 도구가 그대로 읽는다. 변형마다 콘솔 `TD.Fog3D.ProbeWrite <볼륨 렌더 타깃 경로> <무늬>` 를 적용 안에서 실행한다(촬영 첫 장은 그 10초 뒤).
- 무늬 번호가 0a 의 `Packed` 번호와 다르다: ProbeWrite 는 0=N, 1=C, 2=A, 3=B (명세). 계획 스크립트가 장면 N·C·A·B·Ap → 0·1·2·3·2 로 보낸다.
- 수식판 상자(`Fog3dProbeBox`)와 다른 해상도의 텍스처판 상자는 변형마다 게임 숨김으로 가린다. 변형의 `call` 은 숨은 수식판 상자 재질에만 가서 무해하다.
- 해상도 한 벌당 예상 시간: 변형 10개 x 약 25초 + 처음 PIE 열기 약 2분 = 약 7분. 이 동안 에디터 창을 앞창으로 두고 건드리지 않는다(L-editor-06). 촬영은 기본 `r.HighResScreenshotDelay 4` 그대로다(0a 와 같게, 건드리지 않음).
- 실패(종료 코드 0 이 아님)이면 그 폴더 `run/result.json` 의 `key_errors` 와 변형별 `call`/`console` 오류를 먼저 본다. 같은 명령을 플래그만 바꿔 다시 돌리기 전에 `Docs/Lessons` 와 `Tools/README.md` 를 `rg -n "<오류 문구>"` 로 찾는다.

## 3. 판정 (0a 와 같은 도구·같은 지표)

```powershell
foreach ($resolution in "full","small") {
  python -B -X utf8 Tools/scratch/fog3d/measure_fog3d_hole.py judge --shots $b1/p5/$resolution/run --output $b1/p5/$resolution/judge.json
}
```

- 볼 것: 조합 `third_d20g8_e025` 의 D(A,B), D(N,C), D(A,A')(잡음 바닥), 영향 영역 넓이, 판정. `topdown_d20g8_e025` 는 정보. 맨 아래 `gates`(P1·P2·격자 16픽셀)는 0a 전용이라 이번엔 "측정 없음"으로 나오는 것이 정상이다.
- 통과(prereg-v2-0b P5): 128x128x64 의 3인칭 D(A,B) 가 4.2~7.8 (0a 값 6.03 의 0.7~1.3배). 화면에 안개가 아예 없으면 실패(그 판단 근거: D(N,C) 가 0a 값 7.9 와 크게 다르게 0 근처, 또는 N 장면이 안개 없는 장면과 같음). 실패 시 대체 길은 층들을 2D 그림 한 장에 펼쳐 넣기(별도 시험).
- 40x40x10 은 정보 표(칸 15cm·층 30cm 라서 경계가 번지는 정도를 D(A,B)·눈 판정으로 봄). 통과 판정에는 쓰지 않는다.
- 눈 판정용: 각 `run/grid.png` 와 장면별 대표 한 장씩을 `Docs/VolumeFog3D/measurements/stage0b/` 에 복사한다.
- 사전 점검(결과 해석 전): ① N·Ap(두 번 찍은 A 와 같은 무늬) 사이 D 가 0a 잡음 바닥 수준인가. ② `run/result.json` 의 각 변형 `console` 에 ProbeWrite 호출이 오류 없이 실행되었는가(명령이 없으면 조용히 실패할 수 있으니 D(N,C) 가 0 근처면 의심).

## 4. 뒤처리

```powershell
python -B Tools/scratch/fog3d/fog3d_p5_small_setup.py remove
python -B Tools/scratch/fog3d/fog3d_tex_setup.py remove
python -B Tools/scratch/fog3d/fog3d_setup.py remove
python Tools/run_in_editor.py Tools/scratch/fog3d/editor_fog3d_reset.py     # PIE 종료 요청, 격자 8x128, 시간 섞기 0.9, r.TDFogScattering 1, GPU CSV 통계 0
git status --short Content Config Source                                    # b1/git_status_p5_before.txt 와 같아야 함
Test-Path Content/Repro/Fog3D                                               # False 여야 함(비어 있으면 폴더 삭제)
```

- `editor_fog3d_reset.py` 는 `r.HighResScreenshotDelay` 를 되돌리지 않는다. P5 에서는 바꾸지 않으니 상관없지만, P7 처럼 바꾼 세션이면 `r.HighResScreenshotDelay 4` 를 따로 되돌린다.
- 에디터는 닫지 않는다. 레벨은 "저장 안 된 변경" 상태이므로 저장하지 않는다(AGENTS.md 11절).
