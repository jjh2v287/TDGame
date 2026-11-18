# 예전 판정선 다시 돌리기 — 기록과 견줌 (fog3d_blend_regress.py compare)

- (가)·(나)·(라): 기록과 같음(json 에 저장된 자릿수 그대로, 해시 같음). (다): 문턱에 대한 값. 시간·코드 지문 칸은 견주지 않음
- 도구 지문: {"fog3d_blend_patch.py": "edcb43dc6a8eec973f8684cdc67ab57561567234da33203d4c6facc66316a610", "fog3d_blend_gates.py": "91ab8df3ceb6689381ba572910cd7c233c637a40828b6be89a702e79c75054e2", "fog3d_blend_regress.py": "ac23e90b54a316e9d19411dfb0fc5dee55d16334bf37a0716c2e374ded39c2c0", "fog3d_blend_probe.py": "ebe8b6e7604f14b93c9da2783694ba87df56d9af14df595e726ef0819e21b178"}

## 1단계 조각

| 조각 | 견준 것 | 같음 | 해시 같음 | 다른 곳(앞 3) | 도구의 통과 | 갈아 끼운 식·호출 |
|---|---|---|---|---|---|---|
| walk_px | 기록 | False | False | ["/abs_left: 14.158800416082387 ≠ 14.19866501806492", "/abs_right: 14.15806738621196 ≠ 14.197865873478122", "/face/face_path_hist[0]: 58181652 ≠ 58180884"] | null | {"formula": "B4", "installed": true, "pid": 11656, "calls": {"B4|core.Sim._apply_inputs": 148}} |
| walk_mx | 기록 | False | False | ["/abs_left: 14.15879324734747 ≠ 14.198656814693303", "/abs_right: 14.158058307140644 ≠ 14.197857903283387", "/face/face_path_hist[0]: 58181652 ≠ 58180884"] | null | {"formula": "B4", "installed": true, "pid": 35256, "calls": {"B4|core.Sim._apply_inputs": 148}} |
| walk_py | 기록 | False | False | ["/abs_left: 14.158067150489977 ≠ 14.197864912447871", "/abs_right: 14.158799984005121 ≠ 14.198665815904212", "/face/face_path_hist[0]: 58181652 ≠ 58180884"] | null | {"formula": "B4", "installed": true, "pid": 26820, "calls": {"B4|core.Sim._apply_inputs": 148}} |
| walk_my | 기록 | False | False | ["/abs_left: 14.158058558543544 ≠ 14.197857833829895", "/abs_right: 14.158793609325137 ≠ 14.198657225619778", "/face/face_path_hist[0]: 58181652 ≠ 58180884"] | null | {"formula": "B4", "installed": true, "pid": 16596, "calls": {"B4|core.Sim._apply_inputs": 148}} |
| walk_pxpy | 기록 | False | False | ["/abs_left: 9.14397885333366 ≠ 9.17136144851833", "/abs_right: 9.143979156680413 ≠ 9.17136163281873", "/max_wind: 101.7085952758789 ≠ 101.84587860107422"] | null | {"formula": "B4", "installed": true, "pid": 8644, "calls": {"B4|core.Sim._apply_inputs": 148}} |
| v2_N2 | 기록 | True | True | [] | {"p1": true, "p2": true, "all": true} | {"formula": "B4", "installed": true, "pid": 35404, "calls": {"B4|core.Sim._apply_inputs": 36}} |
| g12_N2 | 기록 | True | True | [] | {"a": true, "c": true, "b": true, "d": true, "d_undecidable": false, "all": true… | {"formula": "B4", "installed": true, "pid": 28704, "calls": {"B4|core.Sim._apply_inputs": 150}} |
| t_N2 | 기록 | True | True | [] | true | {"formula": "B4", "installed": true, "pid": 34212, "calls": {"B4|core.Sim._apply_inputs": 6}} |
| v3_N2 | 기록 | True | True | [] | {"p1": true, "p2": true, "p3_info": false, "undecidable": false, "p12": true} | {"formula": "B4", "installed": true, "pid": 25848, "calls": {"B4|core.Sim._apply_inputs": 540}} |
| k4hash_N2 | 기록 | True | True | [] | null | {"formula": "B4", "installed": true, "pid": 33448, "calls": {"B4|core.Sim._apply_inputs": 396}} |
| k4hash_N2_run2 | 기록 | True | True | [] | null | {"formula": "B4", "installed": true, "pid": 25944, "calls": {"B4|core.Sim._apply_inputs": 396}} |
| g14_N2 | 기록 | True | True | [] | {"ga": true, "gc": true, "gd": true, "ge": true, "all": true} | {"formula": "B4", "installed": true, "pid": 23408, "calls": {"B4|core.Sim._apply_inputs": 102}} |
| g15_N2 | 기록 | True | True | [] | {"ga": true, "gb": true, "gc": true, "gd": true, "all": true} | {"formula": "B4", "installed": true, "pid": 34832, "calls": {"B4|core.Sim._apply_inputs": 76}} |
| leak_N2 | 기록 | True | True | [] | true | {"formula": "B4", "installed": true, "pid": 4656, "calls": {"B4|core.Sim._apply_inputs": 150}} |
| g16_ga_N2 | 기록 | True | True | [] | {"ga_a": true, "ga_b": true, "all": true} | {"formula": "B4", "installed": true, "pid": 32868, "calls": {"B4|core.Sim._apply_inputs": 768}} |
| g16_gb26_N2 | 기록 | True | True | [] | {"gb26_a": true, "gb26_b": true, "all": true} | {"formula": "B4", "installed": true, "pid": 9512, "calls": {"B4|core.Sim._apply_inputs": 768}} |
| g16_gb45_N2 | 기록 | True | True | [] | {"gb45_a": true, "gb45_b": true, "all": true} | {"formula": "B4", "installed": true, "pid": 10188, "calls": {"B4|core.Sim._apply_inputs": 768}} |
| g16_gc_N2 | 기록 | True | True | [] | {"gc_a": true, "gc_b": true, "all": true} | {"formula": "B4", "installed": true, "pid": 3332, "calls": {"B4|core.Sim._apply_inputs": 768}} |
| g16_gd_N2 | 기록 | True | True | [] | {"gd_a": true, "gd_b": true, "all": true} | {"formula": "B4", "installed": true, "pid": 2028, "calls": {"B4|core.Sim._apply_inputs": 768}} |
| g2_N2 | B0 기준 실행 | True | True | [] | true | {"formula": "B4", "installed": true, "pid": 34832, "calls": {"B4|core.Sim._apply_inputs": 600}} |
| g3_N2 | B0 기준 실행 | True | True | [] | true | {"formula": "B4", "installed": true, "pid": 35700, "calls": {"B4|core.Sim._apply_inputs": 30}} |
| g4_N2 | B0 기준 실행 | True | True | [] | true | {"formula": "B4", "installed": true, "pid": 21700, "calls": {"B4|core.Sim._apply_inputs": 615}} |
| g5_N2 | B0 기준 실행 | True | True | [] | {"D_G5a": true, "D_G5b": true} | {"formula": "B4", "installed": true, "pid": 35128, "calls": {"B4|core.Sim._apply_inputs": 720}} |
| g6_N2 | B0 기준 실행 | True | True | [] | true | {"formula": "B4", "installed": true, "pid": 19772, "calls": {"B4|core.Sim._apply_inputs": 542}} |
| g7_N2 | B0 기준 실행 | True | True | [] | true | {"formula": "B4", "installed": true, "pid": 34576, "calls": {"B4|core.Sim._apply_inputs": 126}} |
| g8_N2 | B0 기준 실행 | True | True | [] | true | {"formula": "B4", "installed": true, "pid": 35340, "calls": {"B4|core.Sim._apply_inputs": 410}} |
| g9_N2 | B0 기준 실행 | True | True | [] | {"p1": true, "p2": true, "p3": true, "all": true} | {"formula": "B4", "installed": true, "pid": 5052, "calls": {"B4|core.Sim._apply_inputs": 600}} |
| g10_N2 | B0 기준 실행 | True | True | [] | true | {"formula": "B4", "installed": true, "pid": 20292, "calls": {"B4|core.Sim._apply_inputs": 35}} |
| g11_N2 | B0 기준 실행 | True | True | [] | true | {"formula": "B4", "installed": true, "pid": 33128, "calls": {"B4|core.Sim._apply_inputs": 54}} |
| g13_N2 | B0 기준 실행 | True | True | [] | true | {"formula": "B4", "installed": true, "pid": 35532, "calls": {}} |
| v1_N2 | B0 기준 실행 | True | True | [] | true | {"formula": "B4", "installed": true, "pid": 35832, "calls": {"B4|core.Sim._apply_inputs": 30}} |
| g4_dt30_N2 | B0 기준 실행 | False | False | ["/state_hash/run: 'd305afac9f7272b5' ≠ '4d1217a50f95e724'", "/state_hash_raw/run: '31b440fcd3d2fd9e' ≠ 'd06c1bd10023aa18'", "/values/energy_end_over_peak: 7.189910522245234e-08 ≠ 7.65010254259194e-08… | true | {"formula": "B4", "installed": true, "pid": 22620, "calls": {"B4|core.Sim._apply_inputs": 308}} |
| g4_dt144_N2 | B0 기준 실행 | False | False | ["/state_hash/run: '79f15a07e5623b6f' ≠ 'e094e332d1a9f9ed'", "/state_hash_raw/run: 'b9e28ce1b2795085' ≠ '04a852f88d053fe5'", "/values/energy_end_over_peak: 8.472181150972596e-08 ≠ 7.602905413434116e-0… | true | {"formula": "B4", "installed": true, "pid": 33472, "calls": {"B4|core.Sim._apply_inputs": 1476}} |

## 1단계 판정선 묶음(등록 6절)

| 묶음 | 판정선 | 조각 | 같음/견줌 | 해시 같음 | 문턱 대어 본 값 |
|---|---|---|---|---|---|
| 가 | D-G3·D-V3 | v3_N2 | 1/1 | 1 | [{"p1": true, "p2": true, "p3_info": false, "undecidable": false, "p12": true}] |
| 가 | D-V2 | v2_N2 | 1/1 | 1 | [{"p1": true, "p2": true, "all": true}] |
| 가 | D-G12b | g12_N2 | 1/1 | 1 | [{"a": true, "c": true, "b": true, "d": true, "d_undecidable": false, "all": true}] |
| 가 | R2-P(a) | t_N2 | 1/1 | 1 | [true] |
| 가 | 결정론 해시 | k4hash_N2, k4hash_N2_run2 | 2/2 | 2 | [null, null] |
| 가 | D-G4(1/60 판, 조각 안 dt60) | g4_N2 | 1/1 | 1 | [true] |
| 가 | D-G5a·b | g5_N2 | 1/1 | 1 | [{"D_G5a": true, "D_G5b": true}] |
| 가 | D-G7 | g7_N2 | 1/1 | 1 | [true] |
| 가 | D-G9 | g9_N2 | 1/1 | 1 | [{"p1": true, "p2": true, "p3": true, "all": true}] |
| 가 | D-G10 | g10_N2 | 1/1 | 1 | [true] |
| 가 | D-G11 | g11_N2 | 1/1 | 1 | [true] |
| 가 | D-G13 | g13_N2 | 1/1 | 1 | [true] |
| 가 | D-V1 | v1_N2 | 1/1 | 1 | [true] |
| 나 | D-G2 | g2_N2 | 1/1 | 1 | [true] |
| 나 | D-G6 | g6_N2 | 1/1 | 1 | [true] |
| 나 | D-G8 | g8_N2 | 1/1 | 1 | [true] |
| 나 | D-G12a·c·d | g12_N2 | 1/1 | 1 | [{"a": true, "c": true, "b": true, "d": true, "d_undecidable": false, "all": true}] |
| 나 | R2-S 누수 | leak_N2 | 1/1 | 1 | [true] |
| 나 | D-G14 | g14_N2 | 1/1 | 1 | [{"ga": true, "gc": true, "gd": true, "ge": true, "all": true}] |
| 나 | D-G15 | g15_N2 | 1/1 | 1 | [{"ga": true, "gb": true, "gc": true, "gd": true, "all": true}] |
| 나 | D-G16 | g16_ga_N2, g16_gb26_N2, g16_gb45_N2, g16_gc_N2, g16_gd_N2 | 5/5 | 5 | [{"ga_a": true, "ga_b": true, "all": true}, {"gb26_a": true, "gb26_b": true, "all": true}, {"gb45_a"… |
| 다 | D-G4(1/30·1/144 판) | g4_dt30_N2, g4_dt144_N2 | 0/2 | 0 | [true, true] |
| 다 | D-S1 | walk_px, walk_mx, walk_py, walk_my | -/- | - | {"px": 2.5886686181048338e-05, "mx": 2.5954163915841667e-05, "py": 2.5879752575152242e-05, "my": 2.595806826391981e-05} ≤ 0.03 → True |
| 다 | D-S2 | walk_px, walk_mx, walk_py, walk_my | -/- | - | {"sizes": {"px": 14.158433901147172, "mx": 14.158425777244057, "py": 14.158433567247549, "my": 14.158426083934339}, "max_relative_spread": 2.8737324266841627e-0… ≤ 0.05 → True |

## 2a (stage2a)

| 묶음 | 조각 수 | 기록과 같음 | 해시 같음 |
|---|---|---|---|
| 가 | 8 | 8 | 8 |
| 나 | 13 | 13 | 13 |
| 다·라·정보 | 109 | 11 | 39 |

기록과 다른 조각:

- R3_G13_S30CURB100_a_x-20: ["/values/max_wind: 192.67428588867188 ≠ 192.55088806152344"]
- R3_G13_S30CURB130_a_x-20: ["/values/max_wind: 192.67428588867188 ≠ 192.55088806152344"]
- R3_G13_S30CURB45_a_x-20: ["/values/max_wind: 192.67428588867188 ≠ 192.55088806152344"]
- R3_G13_S30CURB60_a_x-20: ["/values/max_wind: 192.67428588867188 ≠ 192.55088806152344"]
- R3_G13_S30CURB80_a_x-20: ["/values/max_wind: 192.67428588867188 ≠ 192.55088806152344"]
- R3_G13_S30DROP100_a_x-20: ["/values/max_wind: 182.09695434570312 ≠ 183.16307067871094", "/values/opposite_max: 457.1493835449219 ≠ 457.69451904296875"]
- R3_G13_S30DROP60_a_x-20: ["/values/max_wind: 182.09695434570312 ≠ 183.16307067871094", "/values/opposite_max: 452.2951354980469 ≠ 452.3650817871094"]
- R3_G14_FOOT30_diagonal: ["/values/S: 12.07475133126735 ≠ 12.17784018048911", "/values/abs_left: 12.9104717947014 ≠ 13.110154801521588", "/values/abs_right: 11.239030867833304 ≠ 11.245525559456633"]
- R3_G14_FOOT45_along_fold: ["/values/S: 16.064442071989287 ≠ 16.134292934189652", "/values/abs_left: 17.43028540754516 ≠ 17.610364562303044", "/values/abs_right: 14.69859873643341 ≠ 14.658221306076262"]
- R3_G14_FOOT45_cross_x: ["/values/S: 21.49675769027186 ≠ 21.88039150120919", "/values/abs_left: 21.497431063822912 ≠ 21.879508587003198", "/values/abs_right: 21.4960843167208 ≠ 21.88127441541518"]
- R3_G14_S30CURB100_along_cliff: ["/values/S: 12.299740802367422 ≠ 12.421535530828425", "/values/abs_left: 5.820866893032412 ≠ 5.90131373604403", "/values/abs_right: 17.853061296083144 ≠ 18.01029706921505"]
- R3_G14_S30DROP100_along_cliff: ["/values/S: 10.823449870239081 ≠ 10.933944862026312", "/values/abs_left: 4.457741168478713 ≠ 4.466395288874056", "/values/abs_right: 16.279771614605107 ≠ 16.477558781871103"]
- R3_G14_TOP45_cross_x: ["/values/S: 12.815326547912695 ≠ 12.49118553570707", "/values/abs_left: 12.815215347854826 ≠ 12.491131565515667", "/values/abs_right: 12.815437747970568 ≠ 12.491239505898472"]
- R4_G13_S30CURB100_a_x-20: ["/values/max_wind: 188.10647583007812 ≠ 186.37509155273438"]
- R4_G13_S30CURB130_a_x-20: ["/values/max_wind: 188.10647583007812 ≠ 186.37509155273438"]
- R4_G13_S30CURB45_a_x-20: ["/values/max_wind: 188.10647583007812 ≠ 186.37509155273438"]
- R4_G13_S30CURB60_a_x-20: ["/values/max_wind: 188.10647583007812 ≠ 186.37509155273438"]
- R4_G13_S30CURB80_a_x-20: ["/values/max_wind: 188.10647583007812 ≠ 186.37509155273438"]
- R4_G13_S30DROP100_a_x-20: ["/values/max_wind: 174.4737091064453 ≠ 174.21109008789062", "/values/opposite_max: 457.47723388671875 ≠ 457.5469665527344"]
- R4_G13_S30DROP60_a_x-20: ["/values/max_wind: 174.4737091064453 ≠ 174.21109008789062", "/values/opposite_max: 459.0520935058594 ≠ 461.153564453125"]
- R4_G14_FOOT30_diagonal: ["/values/S: 11.153543220112214 ≠ 11.174003128852572", "/values/abs_left: 11.656260449084359 ≠ 11.67624404755836", "/values/abs_right: 10.650825991140067 ≠ 10.671762210146786"]
- R4_G14_FOOT45_along_fold: ["/values/S: 16.140095266048625 ≠ 16.1874432446871", "/values/abs_left: 16.139111288010593 ≠ 16.186600004375624", "/values/abs_right: 16.141079244086658 ≠ 16.188286484998578"]
- R4_G14_FOOT45_cross_x: ["/values/S: 16.027679586114058 ≠ 16.179047141610827", "/values/abs_left: 16.02908501468676 ≠ 16.18127783593872", "/values/abs_right: 16.026274157541355 ≠ 16.176816447282928"]
- R4_G14_S30CURB100_along_cliff: ["/values/S: 12.846079143775597 ≠ 12.983168060957478", "/values/abs_left: 7.113479485815081 ≠ 7.197442507356148", "/values/abs_right: 17.75973599345604 ≠ 17.94236139261576"]
- R4_G14_S30DROP100_along_cliff: ["/values/S: 11.27055970681248 ≠ 11.378767444050073", "/values/abs_left: 4.627316424476999 ≠ 4.653489595612406", "/values/abs_right: 16.964768234528606 ≠ 17.1432913141395"]
- R4_G14_TOP45_cross_x: ["/values/S: 17.84338157080431 ≠ 17.976354186454937", "/values/abs_left: 17.84458228693496 ≠ 17.978288312036927", "/values/abs_right: 17.842180854673664 ≠ 17.974420060872944"]
- R5_G11_F0_down: ["/values/S: 16.872076472848494 ≠ 17.00410406765813", "/values/abs_left: 16.873427604594426 ≠ 17.006103548216057", "/values/abs_right: 16.870725341102567 ≠ 17.002104587100202"]
- R5_G11_F0_hill: ["/values/S: 16.87307054240968 ≠ 17.00509287767846", "/values/abs_left: 16.87442148343576 ≠ 17.00709288406932", "/values/abs_right: 16.871719601383592 ≠ 17.003092871287603"]
- R5_G11_F0_up: ["/values/S: 16.87208588816721 ≠ 17.004114367953512", "/values/abs_left: 16.873435535566074 ≠ 17.00611452498911", "/values/abs_right: 16.87073624076834 ≠ 17.002114210917917"]
- R5_G11_FOOT30_down: ["/values/S: 16.3548459930963 ≠ 16.5042179431049", "/values/abs_left: 16.356225874481954 ≠ 16.506257598621442", "/values/abs_right: 16.35346611171065 ≠ 16.50217828758836"]
- R5_G11_FOOT30_up: ["/values/S: 16.354854886491278 ≠ 16.50422801038685", "/values/abs_left: 16.356234022091076 ≠ 16.50626706853474", "/values/abs_right: 16.35347575089148 ≠ 16.502188952238964"]
- R5_G11_HILL_hill: ["/values/S: 17.96963335462027 ≠ 18.066530044735117", "/values/abs_left: 17.97109825020493 ≠ 18.06829147202992", "/values/abs_right: 17.968168459035613 ≠ 18.06476861744031"]
- R5_G11_TOP30_down: ["/values/S: 17.459564927399757 ≠ 17.586256510514925", "/values/abs_left: 17.460840311760485 ≠ 17.5884362362891", "/values/abs_right: 17.458289543039026 ≠ 17.584076784740752"]
- R5_G11_TOP30_up: ["/values/S: 17.45957405902733 ≠ 17.586266062138453", "/values/abs_left: 17.46084870276073 ≠ 17.58844617123446", "/values/abs_right: 17.458299415293926 ≠ 17.584085953042447"]
- R5_G13_S30CURB100_a_x-20: ["/values/max_wind: 188.10647583007812 ≠ 186.37509155273438"]
- R5_G13_S30CURB130_a_x-20: ["/values/max_wind: 188.10647583007812 ≠ 186.37509155273438"]
- R5_G13_S30CURB45_a_x-20: ["/values/max_wind: 188.10647583007812 ≠ 186.37509155273438"]
- R5_G13_S30CURB60_a_x-20: ["/values/max_wind: 188.10647583007812 ≠ 186.37509155273438"]
- R5_G13_S30CURB80_a_x-20: ["/values/max_wind: 188.10647583007812 ≠ 186.37509155273438"]
- R5_G13_S30DROP100_a_x-20: ["/values/max_wind: 174.4737091064453 ≠ 174.21109008789062", "/values/opposite_max: 457.1493835449219 ≠ 457.69451904296875"]
- R5_G13_S30DROP60_a_x-20: ["/values/max_wind: 174.4737091064453 ≠ 174.21109008789062", "/values/opposite_max: 469.13665771484375 ≠ 470.8173828125"]
- R5_G14_FOOT30_diagonal: ["/values/S: 11.153543220112214 ≠ 11.174003128852572", "/values/abs_left: 11.656260449084359 ≠ 11.67624404755836", "/values/abs_right: 10.650825991140067 ≠ 10.671762210146786"]
- R5_G14_FOOT45_along_fold: ["/values/S: 16.140095266048625 ≠ 16.1874432446871", "/values/abs_left: 16.139111288010593 ≠ 16.186600004375624", "/values/abs_right: 16.141079244086658 ≠ 16.188286484998578"]
- R5_G14_FOOT45_cross_x: ["/values/S: 16.028286222734597 ≠ 16.179648409659368", "/values/abs_left: 16.029690699315648 ≠ 16.181877751198094", "/values/abs_right: 16.026881746153542 ≠ 16.177419068120646"]
- R5_G14_S30CURB100_along_cliff: ["/values/S: 12.353389047026475 ≠ 12.490477964208354", "/values/abs_left: 6.04598427619198 ≠ 6.129947297733049", "/values/abs_right: 17.75973599345604 ≠ 17.94236139261576"]
- R5_G14_S30DROP100_along_cliff: ["/values/S: 11.19229420404404 ≠ 11.29241622555545", "/values/abs_left: 4.457741168478713 ≠ 4.466395288874056", "/values/abs_right: 16.964768234528606 ≠ 17.1432913141395"]
- R5_G14_TOP45_cross_x: ["/values/S: 17.842863549177018 ≠ 17.97585289785172", "/values/abs_left: 17.84406428301663 ≠ 17.977786985709297", "/values/abs_right: 17.841662815337408 ≠ 17.973918809994142"]
- R5_G1_walk_px_F0: ["/values/locked_hash: 'beb1e4a913bf5b19' ≠ '586d8998e9b18f11'", "/values/locked_max_wind: 95.35391235351562 ≠ 95.41167449951172"]
- R5_G1_walk_px_F1: ["/values/locked_hash: 'beb1e4a913bf5b19' ≠ '586d8998e9b18f11'", "/values/locked_max_wind: 95.35391235351562 ≠ 95.41167449951172"]
- R5_G4_CLIFF_a: ["/values/max_wind: 189.2244110107422 ≠ 187.4755096435547"]
- R5_G4_CLIFF_a_x-20: ["/values/max_wind: 188.10647583007812 ≠ 186.37509155273438"]
- R5_G4_CLIFF_b: ["/values/max_wind: 106.5745849609375 ≠ 758.6461181640625"]
- R5_G4_DIAG_a: ["/values/max_wind: 142.8418731689453 ≠ 143.44178771972656"]
- R5_G4_DIAG_b: ["/values/max_wind: 107.21688842773438 ≠ 749.8521728515625"]
- R5_G4_MESA_a: ["/values/max_wind: 180.0511474609375 ≠ 178.7229461669922"]
- R5_G4_MESA_b: ["/values/max_wind: 87.99535369873047 ≠ 692.1962280273438"]
- R5_G6_ROUGH_k2_60: ["/values/M: 2350.598064796698 ≠ 2867.22216796875", "/values/energy_end_over_peak: 0.0002805502272488145 ≠ 0.000251613857490363", "/values/flat_control/energy_end_over_peak: 0.00048335886264303164 ≠ 0.000484058493040035"]
- R5_G6_ROUGH_k3_60: ["/values/M: 2350.598064796698 ≠ 2859.177490234375", "/values/energy_end_over_peak: 0.0004855770297607993 ≠ 0.0004749599398288292", "/values/flat_control/energy_end_over_peak: 0.0002061009445562427 ≠ 0.00016133095900915621"]
- R5_G6_S30_k2_60: ["/values/M: 2309.401076758503 ≠ 2856.425537109375", "/values/energy_end_over_peak: 0.0004926123840397474 ≠ 0.0004932957769695545", "/values/flat_control/energy_end_over_peak: 0.0004926123840397474 ≠ 0.0004932957769695545"]
- R5_G6_S30_k3_60: ["/values/M: 2309.401076758503 ≠ 2849.4501953125", "/values/energy_end_over_peak: 0.0002423493964665282 ≠ 0.00020464469436222023", "/values/flat_control/energy_end_over_peak: 0.0002423493964665282 ≠ 0.00020464469436222023"]
- R5_G6_S45_k2_60: ["/values/M: 2828.4271247461897 ≠ 2995.986572265625", "/values/energy_end_over_peak: 0.00039339371199116873 ≠ 0.00040103482415835645", "/values/flat_control/energy_end_over_peak: 0.00039339371199116873 ≠ 0.00040103482415835645"]
- R5_G6_S45_k3_60: ["/values/M: 2828.4271247461897 ≠ 2984.4892578125", "/values/energy_end_over_peak: 3.778383902563589e-05 ≠ 2.718433138902987e-05", "/values/flat_control/energy_end_over_peak: 3.778383902563589e-05 ≠ 2.718433138902987e-05"]
- R5_G6_S60_k2_144: ["/values/M: 4216.508508232849 ≠ 4721.0009765625", "/values/energy_end_over_peak: 0.0003385056461826449 ≠ 0.00039236837527335854", "/values/flat_control/energy_end_over_peak: 0.0003385056461826449 ≠ 0.00039236837527335854"]
- R5_G6_S60_k2_30: ["/values/M: 4216.508508232849 ≠ 5340.81591796875", "/values/energy_end_over_peak: 0.00029507605958257685 ≠ 0.00022846067009818524", "/values/flat_control/energy_end_over_peak: 0.00029507605958257685 ≠ 0.00022846067009818524"]
- R5_G6_S60_k2_60: ["/values/M: 4216.508508232849 ≠ 5523.2763671875", "/values/energy_end_over_peak: 0.00031690204475278225 ≠ 0.00031539269001294425", "/values/flat_control/energy_end_over_peak: 0.00031690204475278225 ≠ 0.00031539269001294425"]
- R5_G6_S60_k3_60: ["/values/M: 4216.508508232849 ≠ 5537.6728515625", "/values/energy_end_over_peak: 5.41009770870258e-06 ≠ 3.953927706604404e-06", "/values/flat_control/energy_end_over_peak: 5.41009770870258e-06 ≠ 3.953927706604404e-06"]
- R5_WALK_CURB30_down: ["/values/S: 17.800769424764795 ≠ 18.191783303860557", "/values/abs_left: 17.801746075522676 ≠ 18.19127614130058", "/values/abs_right: 17.799792774006914 ≠ 18.192290466420534"]
- R5_WALK_CURB30_up: ["/values/S: 16.530209210911753 ≠ 16.587465356740363", "/values/abs_left: 16.529712602981874 ≠ 16.587882691271705", "/values/abs_right: 16.53070581884163 ≠ 16.587048022209014"]
- R5_WALK_CURB45_down: ["/values/S: 16.07136394174697 ≠ 16.068211858581147", "/values/abs_left: 16.162827221922907 ≠ 16.149110477826852", "/values/abs_right: 15.979900661571032 ≠ 15.987313239335435"]
- R5_WALK_CURB45_up: ["/values/S: 13.392819690327036 ≠ 13.486622225717033", "/values/abs_left: 13.392514784671116 ≠ 13.482365039852587", "/values/abs_right: 13.393124595982956 ≠ 13.49087941158148"]
- R5_WALK_CURB60_down: ["/values/S: 19.55502327360636 ≠ 20.452830519013162", "/values/abs_left: 19.588151549001193 ≠ 20.49293285445583", "/values/abs_right: 19.521894998211526 ≠ 20.41272818357049"]
- R5_WALK_CURB60_up: ["/values/S: 13.120790401126044 ≠ 13.155084548012821", "/values/abs_left: 13.122177299795274 ≠ 13.15466477501485", "/values/abs_right: 13.119403502456814 ≠ 13.15550432101079"]
- R5_WALK_S10_contour: ["/values/S: 16.87208596424235 ≠ 17.004113836165256", "/values/abs_left: 16.870735311910135 ≠ 17.00211317558735", "/values/abs_right: 16.87343661657457 ≠ 17.006114496743162"]
- R5_WALK_S10_down: ["/values/S: 16.872076472848494 ≠ 17.00410406765813", "/values/abs_left: 16.873427604594426 ≠ 17.006103548216057", "/values/abs_right: 16.870725341102567 ≠ 17.002104587100202"]
- R5_WALK_S10_up: ["/values/S: 16.87208588816721 ≠ 17.004114367953512", "/values/abs_left: 16.873435535566074 ≠ 17.00611452498911", "/values/abs_right: 16.87073624076834 ≠ 17.002114210917917"]
- R5_WALK_S20_contour: ["/values/S: 16.87208596424235 ≠ 17.004113836165256", "/values/abs_left: 16.870735311910135 ≠ 17.00211317558735", "/values/abs_right: 16.87343661657457 ≠ 17.006114496743162"]
- R5_WALK_S20_down: ["/values/S: 16.872076472848494 ≠ 17.00410406765813", "/values/abs_left: 16.873427604594426 ≠ 17.006103548216057", "/values/abs_right: 16.870725341102567 ≠ 17.002104587100202"]
- R5_WALK_S20_up: ["/values/S: 16.87208588816721 ≠ 17.004114367953512", "/values/abs_left: 16.873435535566074 ≠ 17.00611452498911", "/values/abs_right: 16.87073624076834 ≠ 17.002114210917917"]
- R5_WALK_S30Y_contour: ["/values/S: 16.87208596424235 ≠ 17.004113836165256", "/values/abs_left: 16.870735311910135 ≠ 17.00211317558735", "/values/abs_right: 16.87343661657457 ≠ 17.006114496743162"]
- R5_WALK_S30Y_ydown: ["/values/S: 16.87207691660944 ≠ 17.004103776950917", "/values/abs_left: 16.870726086505357 ≠ 17.00210340905961", "/values/abs_right: 16.873427746713524 ≠ 17.006104144842222"]
- R5_WALK_S30_contour: ["/values/S: 16.87208596424235 ≠ 17.004113836165256", "/values/abs_left: 16.870735311910135 ≠ 17.00211317558735", "/values/abs_right: 16.87343661657457 ≠ 17.006114496743162"]
- R5_WALK_S30_down: ["/values/S: 16.872076472848494 ≠ 17.00410406765813", "/values/abs_left: 16.873427604594426 ≠ 17.006103548216057", "/values/abs_right: 16.870725341102567 ≠ 17.002104587100202"]
- R5_WALK_S30_up: ["/values/S: 16.87208588816721 ≠ 17.004114367953512", "/values/abs_left: 16.873435535566074 ≠ 17.00611452498911", "/values/abs_right: 16.87073624076834 ≠ 17.002114210917917"]
- R5_WALK_S45_contour: ["/values/S: 16.87208596424235 ≠ 17.004113836165256", "/values/abs_left: 16.870735311910135 ≠ 17.00211317558735", "/values/abs_right: 16.87343661657457 ≠ 17.006114496743162"]
- R5_WALK_S45_down: ["/values/S: 16.872076472848494 ≠ 17.00410406765813", "/values/abs_left: 16.873427604594426 ≠ 17.006103548216057", "/values/abs_right: 16.870725341102567 ≠ 17.002104587100202"]
- R5_WALK_S45_up: ["/values/S: 16.87208588816721 ≠ 17.004114367953512", "/values/abs_left: 16.873435535566074 ≠ 17.00611452498911", "/values/abs_right: 16.87073624076834 ≠ 17.002114210917917"]
- REF_G14_F0_along_cliff: ["/values/S: 16.519006433812642 ≠ 16.690048492505095", "/values/abs_left: 16.74202159135795 ≠ 16.961507158769248", "/values/abs_right: 16.327850584488083 ≠ 16.45736963570725"]
- REF_G14_F0_along_fold: ["/values/S: 16.87208596424235 ≠ 17.004113836165256", "/values/abs_left: 16.870735311910135 ≠ 17.00211317558735", "/values/abs_right: 16.87343661657457 ≠ 17.006114496743162"]
- REF_G14_F0_cross_x: ["/values/S: 16.87208588816721 ≠ 17.004114367953512", "/values/abs_left: 16.873435535566074 ≠ 17.00611452498911", "/values/abs_right: 16.87073624076834 ≠ 17.002114210917917"]
- REF_G14_F0_diagonal: ["/values/S: 11.287938823242106 ≠ 11.286629829871533", "/values/abs_left: 11.287938683707301 ≠ 11.286629943056726", "/values/abs_right: 11.287938962776908 ≠ 11.286629716686344"]
- REF_G6_F0_k2_144: ["/values/M: 2060.298583984375 ≠ 2793.40673828125", "/values/energy_end_over_peak: 0.0005423327841242997 ≠ 0.0005544062733640324", "/values/flat_control/energy_end_over_peak: 0.0005423327841242997 ≠ 0.0005544062733640324"]
- REF_G6_F0_k2_30: ["/values/M: 2000.0 ≠ 3153.139892578125", "/values/energy_end_over_peak: 0.0004886421053775596 ≠ 0.00043194856932089536", "/values/flat_control/energy_end_over_peak: 0.0004886421053775596 ≠ 0.00043194856932089536"]
- REF_G6_F0_k2_60: ["/values/M: 2000.0 ≠ 2793.40673828125", "/values/energy_end_over_peak: 0.000524098995217062 ≠ 0.0005105719212756793", "/values/flat_control/energy_end_over_peak: 0.000524098995217062 ≠ 0.0005105719212756793"]
- REF_G6_F0_k3_60: ["/values/M: 2000.0 ≠ 2793.40673828125", "/values/energy_end_over_peak: 0.0005241109832792902 ≠ 0.0005105927461364347", "/values/flat_control/energy_end_over_peak: 0.0005241109832792902 ≠ 0.0005105927461364347"]
- REF_WALK_F0_contour: ["/values/S: 16.87208596424235 ≠ 17.004113836165256", "/values/abs_left: 16.870735311910135 ≠ 17.00211317558735", "/values/abs_right: 16.87343661657457 ≠ 17.006114496743162"]
- REF_WALK_F0_down: ["/values/S: 16.872076472848494 ≠ 17.00410406765813", "/values/abs_left: 16.873427604594426 ≠ 17.006103548216057", "/values/abs_right: 16.870725341102567 ≠ 17.002104587100202"]
- REF_WALK_F0_up: ["/values/S: 16.87208588816721 ≠ 17.004114367953512", "/values/abs_left: 16.873435535566074 ≠ 17.00611452498911", "/values/abs_right: 16.87073624076834 ≠ 17.002114210917917"]
- REF_WALK_F0_ydown: ["/values/S: 16.87207691660944 ≠ 17.004103776950917", "/values/abs_left: 16.870726086505357 ≠ 17.00210340905961", "/values/abs_right: 16.873427746713524 ≠ 17.006104144842222"]

| 조각 | 같음 | 해시 같음 | 갈아 끼운 식·호출 |
|---|---|---|---|
| R3_G13_S30CURB100_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 34424, "calls": {"B4|TerrainSim._apply_inputs": 190}} |
| R3_G13_S30CURB130_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 13276, "calls": {"B4|TerrainSim._apply_inputs": 190}} |
| R3_G13_S30CURB45_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 35188, "calls": {"B4|TerrainSim._apply_inputs": 190}} |
| R3_G13_S30CURB60_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 34260, "calls": {"B4|TerrainSim._apply_inputs": 190}} |
| R3_G13_S30CURB80_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 21912, "calls": {"B4|TerrainSim._apply_inputs": 190}} |
| R3_G13_S30DROP100_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 20192, "calls": {"B4|TerrainSim._apply_inputs": 190}} |
| R3_G13_S30DROP60_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 35464, "calls": {"B4|TerrainSim._apply_inputs": 190}} |
| R3_G14_FOOT30_diagonal | False | False | {"formula": "B4", "installed": true, "pid": 13276, "calls": {"B4|TerrainSim._apply_inputs": 148}} |
| R3_G14_FOOT45_along_fold | False | False | {"formula": "B4", "installed": true, "pid": 35188, "calls": {"B4|TerrainSim._apply_inputs": 148}} |
| R3_G14_FOOT45_cross_x | False | False | {"formula": "B4", "installed": true, "pid": 20192, "calls": {"B4|TerrainSim._apply_inputs": 148}} |
| R3_G14_S30CURB100_along_cliff | False | False | {"formula": "B4", "installed": true, "pid": 34424, "calls": {"B4|TerrainSim._apply_inputs": 148}} |
| R3_G14_S30DROP100_along_cliff | False | False | {"formula": "B4", "installed": true, "pid": 35464, "calls": {"B4|TerrainSim._apply_inputs": 148}} |
| R3_G14_TOP45_cross_x | False | False | {"formula": "B4", "installed": true, "pid": 21912, "calls": {"B4|TerrainSim._apply_inputs": 148}} |
| R4_G13_S30CURB100_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 13276, "calls": {"B4|TerrainSim._apply_inputs": 190}} |
| R4_G13_S30CURB130_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 20192, "calls": {"B4|TerrainSim._apply_inputs": 190}} |
| R4_G13_S30CURB45_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 34424, "calls": {"B4|TerrainSim._apply_inputs": 190}} |
| R4_G13_S30CURB60_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 20192, "calls": {"B4|TerrainSim._apply_inputs": 190}} |
| R4_G13_S30CURB80_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 35188, "calls": {"B4|TerrainSim._apply_inputs": 190}} |
| R4_G13_S30DROP100_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 35464, "calls": {"B4|TerrainSim._apply_inputs": 190}} |
| R4_G13_S30DROP60_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 21912, "calls": {"B4|TerrainSim._apply_inputs": 190}} |
| R4_G14_FOOT30_diagonal | False | False | {"formula": "B4", "installed": true, "pid": 20192, "calls": {"B4|TerrainSim._apply_inputs": 148}} |
| R4_G14_FOOT45_along_fold | False | False | {"formula": "B4", "installed": true, "pid": 21912, "calls": {"B4|TerrainSim._apply_inputs": 148}} |
| R4_G14_FOOT45_cross_x | False | False | {"formula": "B4", "installed": true, "pid": 34424, "calls": {"B4|TerrainSim._apply_inputs": 148}} |
| R4_G14_S30CURB100_along_cliff | False | False | {"formula": "B4", "installed": true, "pid": 35188, "calls": {"B4|TerrainSim._apply_inputs": 148}} |
| R4_G14_S30DROP100_along_cliff | False | False | {"formula": "B4", "installed": true, "pid": 21912, "calls": {"B4|TerrainSim._apply_inputs": 148}} |
| R4_G14_TOP45_cross_x | False | False | {"formula": "B4", "installed": true, "pid": 34260, "calls": {"B4|TerrainSim._apply_inputs": 148}} |
| R5_G10_S30 | True | True | {"formula": "B4", "installed": true, "pid": 35464, "calls": {"B4|TerrainSimR5._apply_inputs": 70}} |
| R5_G11_F0_down | False | False | {"formula": "B4", "installed": true, "pid": 13276, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_G11_F0_hill | False | False | {"formula": "B4", "installed": true, "pid": 20192, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_G11_F0_up | False | False | {"formula": "B4", "installed": true, "pid": 34424, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_G11_FOOT30_down | False | False | {"formula": "B4", "installed": true, "pid": 35188, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_G11_FOOT30_up | False | False | {"formula": "B4", "installed": true, "pid": 34260, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_G11_HILL_hill | False | False | {"formula": "B4", "installed": true, "pid": 20192, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_G11_TOP30_down | False | False | {"formula": "B4", "installed": true, "pid": 21912, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_G11_TOP30_up | False | False | {"formula": "B4", "installed": true, "pid": 35464, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_G13_S30CURB100_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 35188, "calls": {"B4|TerrainSimR5._apply_inputs": 190}} |
| R5_G13_S30CURB130_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 34260, "calls": {"B4|TerrainSimR5._apply_inputs": 190}} |
| R5_G13_S30CURB45_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 13276, "calls": {"B4|TerrainSimR5._apply_inputs": 190}} |
| R5_G13_S30CURB60_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 35464, "calls": {"B4|TerrainSimR5._apply_inputs": 190}} |
| R5_G13_S30CURB80_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 34424, "calls": {"B4|TerrainSimR5._apply_inputs": 190}} |
| R5_G13_S30DROP100_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 34260, "calls": {"B4|TerrainSimR5._apply_inputs": 190}} |
| R5_G13_S30DROP60_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 35188, "calls": {"B4|TerrainSimR5._apply_inputs": 190}} |
| R5_G14_FOOT30_diagonal | False | False | {"formula": "B4", "installed": true, "pid": 35464, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_G14_FOOT45_along_fold | False | False | {"formula": "B4", "installed": true, "pid": 34424, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_G14_FOOT45_cross_x | False | False | {"formula": "B4", "installed": true, "pid": 13276, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_G14_S30CURB100_along_cliff | False | False | {"formula": "B4", "installed": true, "pid": 20192, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_G14_S30DROP100_along_cliff | False | False | {"formula": "B4", "installed": true, "pid": 34260, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_G14_TOP45_cross_x | False | False | {"formula": "B4", "installed": true, "pid": 35188, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_G1_v2_high_F0 | True | True | {"formula": "B4", "installed": true, "pid": 35188, "calls": {"B4|core.Sim._apply_inputs": 18, "B4|TerrainSimR5._apply_inputs": 18}} |
| R5_G1_v2_high_F1 | True | True | {"formula": "B4", "installed": true, "pid": 20192, "calls": {"B4|core.Sim._apply_inputs": 18, "B4|TerrainSimR5._apply_inputs": 18}} |
| R5_G1_v2_low_F0 | True | True | {"formula": "B4", "installed": true, "pid": 35464, "calls": {"B4|core.Sim._apply_inputs": 18, "B4|TerrainSimR5._apply_inputs": 18}} |
| R5_G1_v2_low_F1 | True | True | {"formula": "B4", "installed": true, "pid": 13276, "calls": {"B4|core.Sim._apply_inputs": 18, "B4|TerrainSimR5._apply_inputs": 18}} |
| R5_G1_v3_free_F0 | True | True | {"formula": "B4", "installed": true, "pid": 35464, "calls": {"B4|core.Sim._apply_inputs": 180, "B4|TerrainSimR5._apply_inputs": 180}} |
| R5_G1_v3_free_F1 | True | True | {"formula": "B4", "installed": true, "pid": 13276, "calls": {"B4|core.Sim._apply_inputs": 180, "B4|TerrainSimR5._apply_inputs": 180}} |
| R5_G1_v3_high_F0 | True | True | {"formula": "B4", "installed": true, "pid": 20192, "calls": {"B4|core.Sim._apply_inputs": 180, "B4|TerrainSimR5._apply_inputs": 180}} |
| R5_G1_v3_high_F1 | True | True | {"formula": "B4", "installed": true, "pid": 34424, "calls": {"B4|core.Sim._apply_inputs": 180, "B4|TerrainSimR5._apply_inputs": 180}} |
| R5_G1_v3_low_F0 | True | True | {"formula": "B4", "installed": true, "pid": 35188, "calls": {"B4|core.Sim._apply_inputs": 180, "B4|TerrainSimR5._apply_inputs": 180}} |
| R5_G1_v3_low_F1 | True | True | {"formula": "B4", "installed": true, "pid": 34260, "calls": {"B4|core.Sim._apply_inputs": 180, "B4|TerrainSimR5._apply_inputs": 180}} |
| R5_G1_walk_px_F0 | False | False | {"formula": "B4", "installed": true, "pid": 34424, "calls": {"B4|core.Sim._apply_inputs": 148, "B4|TerrainSimR5._apply_inputs": 148}} |
| R5_G1_walk_px_F1 | False | False | {"formula": "B4", "installed": true, "pid": 21912, "calls": {"B4|core.Sim._apply_inputs": 148, "B4|TerrainSimR5._apply_inputs": 148}} |
| R5_G2_CLIFF | True | True | {"formula": "B4", "installed": true, "pid": 20192, "calls": {"B4|TerrainSimR5._apply_inputs": 300}} |
| R5_G2_HILL | True | True | {"formula": "B4", "installed": true, "pid": 13276, "calls": {"B4|TerrainSimR5._apply_inputs": 300}} |
| R5_G2_ROUGH | True | True | {"formula": "B4", "installed": true, "pid": 34260, "calls": {"B4|TerrainSimR5._apply_inputs": 300}} |
| R5_G2_S30 | True | True | {"formula": "B4", "installed": true, "pid": 35464, "calls": {"B4|TerrainSimR5._apply_inputs": 300}} |
| R5_G3_CLIFF | True | True | {"formula": "B4", "installed": true, "pid": 13276, "calls": {}} |
| R5_G3_DIAG | True | True | {"formula": "B4", "installed": true, "pid": 13276, "calls": {}} |
| R5_G3_HILL | True | True | {"formula": "B4", "installed": true, "pid": 13276, "calls": {}} |
| R5_G3_MESA | True | True | {"formula": "B4", "installed": true, "pid": 13276, "calls": {}} |
| R5_G3_S10 | True | True | {"formula": "B4", "installed": true, "pid": 13276, "calls": {}} |
| R5_G3_S20 | True | True | {"formula": "B4", "installed": true, "pid": 13276, "calls": {}} |
| R5_G3_S30 | True | True | {"formula": "B4", "installed": true, "pid": 13276, "calls": {}} |
| R5_G3_S45 | True | True | {"formula": "B4", "installed": true, "pid": 13276, "calls": {}} |
| R5_G4_CLIFF_a | False | True | {"formula": "B4", "installed": true, "pid": 13276, "calls": {"B4|TerrainSimR5._apply_inputs": 190}} |
| R5_G4_CLIFF_a_x-20 | False | True | {"formula": "B4", "installed": true, "pid": 21912, "calls": {"B4|TerrainSimR5._apply_inputs": 190}} |
| R5_G4_CLIFF_b | False | True | {"formula": "B4", "installed": true, "pid": 21912, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_G4_DIAG_a | False | True | {"formula": "B4", "installed": true, "pid": 34260, "calls": {"B4|TerrainSimR5._apply_inputs": 190}} |
| R5_G4_DIAG_b | False | True | {"formula": "B4", "installed": true, "pid": 35188, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_G4_MESA_a | False | True | {"formula": "B4", "installed": true, "pid": 20192, "calls": {"B4|TerrainSimR5._apply_inputs": 190}} |
| R5_G4_MESA_b | False | True | {"formula": "B4", "installed": true, "pid": 21912, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_G6_ROUGH_k2_60 | False | False | {"formula": "B4", "installed": true, "pid": 35464, "calls": {"B4|TerrainSimR5._apply_inputs": 255, "B4|core.Sim._apply_inputs": 255}} |
| R5_G6_ROUGH_k3_60 | False | False | {"formula": "B4", "installed": true, "pid": 35188, "calls": {"B4|TerrainSimR5._apply_inputs": 255, "B4|core.Sim._apply_inputs": 255}} |
| R5_G6_S30_k1_60 | True | True | {"formula": "B4", "installed": true, "pid": 35188, "calls": {"B4|TerrainSimR5._apply_inputs": 255, "B4|core.Sim._apply_inputs": 255}} |
| R5_G6_S30_k2_60 | False | False | {"formula": "B4", "installed": true, "pid": 21912, "calls": {"B4|TerrainSimR5._apply_inputs": 255, "B4|core.Sim._apply_inputs": 255}} |
| R5_G6_S30_k3_60 | False | False | {"formula": "B4", "installed": true, "pid": 34424, "calls": {"B4|TerrainSimR5._apply_inputs": 255, "B4|core.Sim._apply_inputs": 255}} |
| R5_G6_S45_k1_60 | True | True | {"formula": "B4", "installed": true, "pid": 35188, "calls": {"B4|TerrainSimR5._apply_inputs": 255, "B4|core.Sim._apply_inputs": 255}} |
| R5_G6_S45_k2_60 | False | False | {"formula": "B4", "installed": true, "pid": 34424, "calls": {"B4|TerrainSimR5._apply_inputs": 255, "B4|core.Sim._apply_inputs": 255}} |
| R5_G6_S45_k3_60 | False | False | {"formula": "B4", "installed": true, "pid": 21912, "calls": {"B4|TerrainSimR5._apply_inputs": 255, "B4|core.Sim._apply_inputs": 255}} |
| R5_G6_S60_k1_60 | True | True | {"formula": "B4", "installed": true, "pid": 35188, "calls": {"B4|TerrainSimR5._apply_inputs": 255, "B4|core.Sim._apply_inputs": 255}} |
| R5_G6_S60_k2_144 | False | False | {"formula": "B4", "installed": true, "pid": 35464, "calls": {"B4|TerrainSimR5._apply_inputs": 612, "B4|core.Sim._apply_inputs": 612}} |
| R5_G6_S60_k2_30 | False | False | {"formula": "B4", "installed": true, "pid": 21912, "calls": {"B4|TerrainSimR5._apply_inputs": 128, "B4|core.Sim._apply_inputs": 128}} |
| R5_G6_S60_k2_60 | False | False | {"formula": "B4", "installed": true, "pid": 21912, "calls": {"B4|TerrainSimR5._apply_inputs": 255, "B4|core.Sim._apply_inputs": 255}} |
| R5_G6_S60_k3_60 | False | False | {"formula": "B4", "installed": true, "pid": 34424, "calls": {"B4|TerrainSimR5._apply_inputs": 255, "B4|core.Sim._apply_inputs": 255}} |
| R5_G8_all | True | True | {"formula": "B4", "installed": true, "pid": 13276, "calls": {}} |
| R5_G9_HILL_fixed | True | True | {"formula": "B4", "installed": true, "pid": 20192, "calls": {"B4|TerrainSimR5._apply_inputs": 300}} |
| R5_G9_HILL_follow | True | True | {"formula": "B4", "installed": true, "pid": 34424, "calls": {"B4|TerrainSimR5._apply_inputs": 300}} |
| R5_G9_MESA_fixed | True | True | {"formula": "B4", "installed": true, "pid": 34260, "calls": {"B4|TerrainSimR5._apply_inputs": 300}} |
| R5_G9_MESA_follow | True | True | {"formula": "B4", "installed": true, "pid": 21912, "calls": {"B4|TerrainSimR5._apply_inputs": 300}} |
| R5_WALK_CURB30_down | False | False | {"formula": "B4", "installed": true, "pid": 13276, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_WALK_CURB30_up | False | False | {"formula": "B4", "installed": true, "pid": 34424, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_WALK_CURB45_down | False | False | {"formula": "B4", "installed": true, "pid": 35188, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_WALK_CURB45_up | False | False | {"formula": "B4", "installed": true, "pid": 34424, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_WALK_CURB60_down | False | False | {"formula": "B4", "installed": true, "pid": 34260, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_WALK_CURB60_up | False | False | {"formula": "B4", "installed": true, "pid": 21912, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_WALK_S10_contour | False | False | {"formula": "B4", "installed": true, "pid": 35188, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_WALK_S10_down | False | False | {"formula": "B4", "installed": true, "pid": 13276, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_WALK_S10_up | False | False | {"formula": "B4", "installed": true, "pid": 35464, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_WALK_S20_contour | False | False | {"formula": "B4", "installed": true, "pid": 21912, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_WALK_S20_down | False | False | {"formula": "B4", "installed": true, "pid": 21912, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_WALK_S20_up | False | False | {"formula": "B4", "installed": true, "pid": 35464, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_WALK_S30Y_contour | False | False | {"formula": "B4", "installed": true, "pid": 34260, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_WALK_S30Y_ydown | False | False | {"formula": "B4", "installed": true, "pid": 35464, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_WALK_S30_contour | False | False | {"formula": "B4", "installed": true, "pid": 35188, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_WALK_S30_down | False | False | {"formula": "B4", "installed": true, "pid": 34424, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_WALK_S30_up | False | False | {"formula": "B4", "installed": true, "pid": 13276, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_WALK_S45_contour | False | False | {"formula": "B4", "installed": true, "pid": 34260, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_WALK_S45_down | False | False | {"formula": "B4", "installed": true, "pid": 34424, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| R5_WALK_S45_up | False | False | {"formula": "B4", "installed": true, "pid": 20192, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| REF_G14_F0_along_cliff | False | False | {"formula": "B4", "installed": true, "pid": 35464, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| REF_G14_F0_along_fold | False | False | {"formula": "B4", "installed": true, "pid": 34260, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| REF_G14_F0_cross_x | False | False | {"formula": "B4", "installed": true, "pid": 13276, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| REF_G14_F0_diagonal | False | False | {"formula": "B4", "installed": true, "pid": 20192, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| REF_G6_F0_k1_60 | True | True | {"formula": "B4", "installed": true, "pid": 13276, "calls": {"B4|TerrainSimR5._apply_inputs": 255, "B4|core.Sim._apply_inputs": 255}} |
| REF_G6_F0_k2_144 | False | False | {"formula": "B4", "installed": true, "pid": 13276, "calls": {"B4|TerrainSimR5._apply_inputs": 612, "B4|core.Sim._apply_inputs": 612}} |
| REF_G6_F0_k2_30 | False | False | {"formula": "B4", "installed": true, "pid": 35464, "calls": {"B4|TerrainSimR5._apply_inputs": 128, "B4|core.Sim._apply_inputs": 128}} |
| REF_G6_F0_k2_60 | False | False | {"formula": "B4", "installed": true, "pid": 20192, "calls": {"B4|TerrainSimR5._apply_inputs": 255, "B4|core.Sim._apply_inputs": 255}} |
| REF_G6_F0_k3_60 | False | False | {"formula": "B4", "installed": true, "pid": 34260, "calls": {"B4|TerrainSimR5._apply_inputs": 255, "B4|core.Sim._apply_inputs": 255}} |
| REF_WALK_F0_contour | False | False | {"formula": "B4", "installed": true, "pid": 21912, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| REF_WALK_F0_down | False | False | {"formula": "B4", "installed": true, "pid": 34260, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| REF_WALK_F0_up | False | False | {"formula": "B4", "installed": true, "pid": 20192, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |
| REF_WALK_F0_ydown | False | False | {"formula": "B4", "installed": true, "pid": 13276, "calls": {"B4|TerrainSimR5._apply_inputs": 148}} |

| 묶음 | 판정선 | 값 | 문턱 | 통과 | 기록 값 | 기록 통과 | 값이 기록과 같음 |
|---|---|---|---|---|---|---|---|
| 다 | 2A-G4 | 0.0 | 1e-06 | True | 0.0 | True | True |
| 다 | 2A-G5 | {"up": 0.9657898211488661, "down": 1.082903221896812} | 0.5 | True | {"up": 0.9594058684886234, "down": 1.1066340136741897} | True | False |
| 다 | 2A-G6 | {"S30_k1_60": 0.5217655436197917, "S30_k2_60": 0.5400559930792416, "S30_k3_60": 0.5570246078573152, "S45_k1_60": 0.5590702718098959, "S45_k2_60": 0.454856795007… | {"wind_over_limit": 1.0, "energy": 0.01} | True | {"S30_k1_60": 0.5217655436197917, "S30_k2_60": 0.6666666666666666, "S30_k3_60": 0.6666666666666666, "S45_k1_60": 0.5590702718098959, "S45_k2_60": 0.666666666666… | True | False |
| 다 | 2A-G7 | {"S20": 2.790206677578652e-07, "S30": 2.790206677578652e-07} | 0.15 | True | {"S20": 3.028766532522033e-07, "S30": 3.028766532522033e-07} | True | False |
| 다 | 2A-G10 | 2.6716406864579767e-12 | 0.5 | True | 2.6716406864579767e-12 | True | True |
| 다 | 2A-G11 | {"HILL_hill": {"finite": true, "value_over_flat": 1.0649889307019984, "run_max_wind_over_flat": 0.9915942723214276}, "FOOT30_up": {"finite": true, "value_over_f… | {"value_over_flat": [0.5, 2.0], "run_max_wind_over_flat": 1.5} | True | {"HILL_hill": {"finite": true, "value_over_flat": 1.062418780931796, "run_max_wind_over_flat": 0.9909004135933714}, "FOOT30_up": {"finite": true, "value_over_fl… | True | False |
| 다 | 2A-G14 | {"가_S30CURB100_along_cliff": "pass", "나_S30DROP100_along_cliff": "pass", "다_FOOT45_cross_x": "pass", "라_TOP45_cross_x": "pass", "마_FOOT30_diagonal": "pass", "바_… | {"value_over_flat": [0.5, 2.0], "run_max_wind_over_M": 1.5, "rule": "R5 의 실패가 없으… | True | {"가_S30CURB100_along_cliff": "pass", "나_S30DROP100_along_cliff": "pass", "다_FOOT45_cross_x": "pass", "라_TOP45_cross_x": "pass", "마_FOOT30_diagonal": "pass", "바_… | True | True |
| 라 | 2A-G1 | {"F0": 0.0, "F1": 0.0} | {"F0": 1e-05, "F1": 0.0001} | True | {"F0": 0.0, "F1": 0.0} | True | True |
| 라 | 2A-G12 | {"walk_S20_up": 0.0, "walk_S20_down": 0.0, "walk_S30_up": 0.0, "walk_S30_down": 0.0, "walk_S30_contour": 0.0, "walk_S30Y_contour": 0.0, "walk_S30Y_ydown": 0.0, … | 0.001 | True | {"walk_S20_up": 0.0, "walk_S20_down": 0.0, "walk_S30_up": 0.0, "walk_S30_down": 0.0, "walk_S30_contour": 0.0, "walk_S30Y_contour": 0.0, "walk_S30Y_ydown": 0.0, … | True | True |
| 라 | 2A-G13 | {"S30CURB100": 1.0, "S30DROP100": 1.0} | {"ratio": [0.8, 1.25], "zero": 1e-06} | True | {"S30CURB100": 1.0, "S30DROP100": 1.0} | True | True |
| 가 | 2A-G9 | {"HILL": {"ratio_1": 0.002363205277024509, "ratio_2": 0.0037029713441274177, "ratio_3": 1.3490285899776394e-05}, "MESA": {"ratio_1": 0.002505171076445747, "rati… | [0.05, 0.05, 0.01] | True | {"HILL": {"ratio_1": 0.002363205277024509, "ratio_2": 0.0037029713441274177, "ratio_3": 1.3490285899776394e-05}, "MESA": {"ratio_1": 0.002505171076445747, "rati… | True | True |
| 나 | 2A-G2 | 0.0 | 0.0 | True | 0.0 | True | True |
| 나 | 2A-G3 | {"smooth": 0.25570711922563305, "cliff": 0.0} | 3.0 | True | {"smooth": 0.25570711922563305, "cliff": 0.0} | True | True |
| 나 | 2A-G8 | {"S10_r45": 1.0, "S10_r60": 1.0, "S20_r45": 1.0, "S20_r60": 1.0, "S30_r45": 1.0, "S30_r60": 1.0, "S45_r45": 1.0, "S45_r60": 1.0, "S60_r45": 1.0, "S60_r60": 1.0} | [0.95, 1.05] | True | {"S10_r45": 1.0, "S10_r60": 1.0, "S20_r45": 1.0, "S20_r60": 1.0, "S30_r45": 1.0, "S30_r60": 1.0, "S45_r45": 1.0, "S45_r60": 1.0, "S60_r45": 1.0, "S60_r60": 1.0} | True | True |
