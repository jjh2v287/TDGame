"""
실행 환경: Python 3.12(추가 설치 없음), 저장소 루트에서 실행
실행 명령: python -B Tools/FluidFX/fog3d_dump_snapshot.py snapshot <사본 폴더> [--folders a,b] | compare <기준 사본> <비교 사본>
출력: snapshot 은 사본 폴더에 Saved/FluidFX/Fog3D 의 GPU 덤프 폴더 사본과 sha256 목록(manifest.json), compare 는 요약 json 한 줄과 종료 코드(0 모두 같음 / 1 다름·빠짐 / 2 오류)
상태: 3D 안개 '스위치를 끄면 지금과 바이트까지 같음' 확인용(섞기 B4 BG-1·2b B2-1). RHI 마다 따로 사본을 뜬다(L-build-29). Tools/scratch 판에서 2026-11-17 올림
"""
import hashlib
import json
import os
import shutil
import sys

SOURCE_ROOT = os.path.join("Saved", "FluidFX", "Fog3D")
DEFAULT_FOLDERS = ("gpu", "gpu_add1", "gpu_1d", "gpu_s2a_r5", "gpu_blend")


def hash_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def list_files(root):
    found = []
    for directory, _, names in os.walk(root):
        for name in names:
            full = os.path.join(directory, name)
            found.append(os.path.relpath(full, root).replace("\\", "/"))
    return sorted(found)


def snapshot(destination, folders):
    if os.path.exists(destination):
        raise SystemExit(f"사본 폴더가 이미 있다: {destination}")
    manifest = dict(source_root=SOURCE_ROOT, folders={})
    for folder in folders:
        source = os.path.join(SOURCE_ROOT, folder)
        if not os.path.isdir(source):
            manifest["folders"][folder] = None
            continue
        target = os.path.join(destination, folder)
        shutil.copytree(source, target)
        manifest["folders"][folder] = {name: hash_file(os.path.join(target, name)) for name in list_files(target)}
    with open(os.path.join(destination, "manifest.json"), "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, ensure_ascii=False, indent=1, sort_keys=True)
    counts = {k: (len(v) if v is not None else None) for k, v in manifest["folders"].items()}
    print(json.dumps(dict(snapshot=destination, files=counts), ensure_ascii=False))


def load_manifest(root):
    with open(os.path.join(root, "manifest.json"), encoding="utf-8") as handle:
        return json.load(handle)["folders"]


def compare(baseline_root, candidate_root):
    baseline = load_manifest(baseline_root)
    candidate = load_manifest(candidate_root)
    summary = {}
    all_same = True
    for folder, base_files in baseline.items():
        cand_files = candidate.get(folder)
        if base_files is None or cand_files is None:
            summary[folder] = dict(error="폴더 없음", baseline=base_files is not None, candidate=cand_files is not None)
            all_same = False
            continue
        missing = sorted(set(base_files) - set(cand_files))
        extra = sorted(set(cand_files) - set(base_files))
        different = sorted(n for n in set(base_files) & set(cand_files) if base_files[n] != cand_files[n])
        same = len(set(base_files) & set(cand_files)) - len(different)
        summary[folder] = dict(same=same, different=len(different), missing=len(missing), extra=len(extra),
                               first_different=different[:5], first_missing=missing[:5], first_extra=extra[:5])
        if different or missing or extra:
            all_same = False
    print(json.dumps(dict(baseline=baseline_root, candidate=candidate_root, all_same=all_same, folders=summary), ensure_ascii=False))
    return 0 if all_same else 1


def main(argv):
    if len(argv) >= 2 and argv[0] == "snapshot":
        folders = DEFAULT_FOLDERS
        if "--folders" in argv:
            folders = tuple(argv[argv.index("--folders") + 1].split(","))
        snapshot(argv[1], folders)
        return 0
    if len(argv) == 3 and argv[0] == "compare":
        return compare(argv[1], argv[2])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
