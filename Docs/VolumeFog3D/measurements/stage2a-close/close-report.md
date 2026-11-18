# 2a 닫기 전 최종 재대조 (2026-11-16, 소스·에셋 변경 없음)
| 단계 | 명령 | 끝 코드 | 수치 | 기대와 |
|---|---|---|---|---|
| 에디터 | ue_editor.py status | - | 닫혀 있음 | 같음 |
| D3D12 시험 | build_and_test.py --rhi offscreen --filter TDGame.FluidFXRhi.Fog3D --label s2a-close-d3d12 | 0 | 29/29 통과, 실패 0(GroundOffMatchesStage1 포함), RHI D3D12 SM6 | 같음 |
| R5 대조 D3D12 | fog3d_compare_s2a.py --numbers ...r5.json --gpu-dir gpu_s2a_r5 --ref-dir ref_s2a/R5 | 0 | 검사 506, 실패 0, 틀 없음 0, 가장 가까운 비 0.00832 (s2a_box_on_slope__end wind) | 같음 |
| fog3d_compare.py | --extras | 0 | 86검사 통과 | 같음 |
| fog3d_compare_v2.py e1/e2/e3 | --out 작업 폴더 | 0/0/0 | 558/78/10검사 실패 0 | 같음 |
| fog3d_compare_add1.py | | 1 | 12검사 중 실패 1: ADD-a add1_connect4_wall45 (넷째 단계 칸 1+g 평균, nan, 칸 0개) | 같음 |
| D3D11 시험 | --rhi d3d11 --label s2a-close-d3d11 | 0 | 29/29 통과, RHI D3D11 SM5 | 같음 |
| R5 대조 D3D11 | 같은 명령 | 0 | 검사 506, 실패 0, 틀 없음 0, 가장 가까운 비 0.00732 (s2a_box_on_slope__end wind) | 같음 |
지문: close/fingerprints.txt (HEAD 6830ced2, git status 84줄). 소스·도구·에셋 git status 는 시작 전과 같음.
D3D12 R5 덤프 보존본: close/gpu_s2a_r5_d3d12/ (Saved/FluidFX/Fog3D/gpu_s2a_r5 는 D3D11 값으로 덮임). 1단계 도구는 D3D12 덤프(12:52 생성)로 돌림.
로그·판정 JSON: 이 폴더. 시험 로그: Saved/AgentOps/20261116/s2a-close-d3d1{1,2}-*.
