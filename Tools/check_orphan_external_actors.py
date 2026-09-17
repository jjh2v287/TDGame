"""Content/__ExternalActors__ · __ExternalObjects__ 에서 '.umap 이 없는' 잔재 폴더를 찾는다 (결정론적 점검).

월드 파티션 레벨의 액터는 레벨 패키지가 아니라
  Content/__External(Actors|Objects)__/<패키지 경로>/<맵 이름>/<1자>/<2자>/<GUID>.uasset
에 따로 저장된다. 이 파일들은 .umap 이 하드 레퍼런스로 붙잡고 있는 것이 아니라
'폴더 경로 규약'으로만 연결되므로, 맵을 에디터 밖(탐색기·git·소스 컨트롤)에서 지우면
그대로 남는다. 콘텐츠 브라우저는 이 폴더를 숨기기 때문에 눈에도 띄지 않는다.

실행:
  python Tools/check_orphan_external_actors.py                # 점검만 (읽기 전용)
  python Tools/check_orphan_external_actors.py --delete       # 잔재 폴더 삭제
  python Tools/check_orphan_external_actors.py --prune-empty  # 빈 디렉터리 정리
"""
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(ROOT, "Content")
ROOTS = ("__ExternalActors__", "__ExternalObjects__")


def scan():
    """{루트: {맵 상대경로: (파일 수, 바이트)}} 를 돌려준다."""
    found = {}
    for root in ROOTS:
        base = os.path.join(CONTENT, root)
        maps = {}
        for dirpath, _dirnames, filenames in os.walk(base):
            if not filenames:
                continue
            # <맵 폴더>/<1자>/<2자>/<파일> 구조라 3단계를 걷어내면 맵 폴더가 된다
            map_dir = os.path.dirname(os.path.dirname(dirpath))
            key = os.path.relpath(map_dir, base).replace("\\", "/")
            count, size = maps.get(key, (0, 0))
            size += sum(os.path.getsize(os.path.join(dirpath, f)) for f in filenames)
            maps[key] = (count + len(filenames), size)
        found[root] = maps
    return found


def prune_empty_dirs(base):
    removed = 0
    for dirpath, _dirnames, _filenames in os.walk(base, topdown=False):
        if dirpath == base:
            continue
        try:
            os.rmdir(dirpath)  # 비어 있을 때만 성공
            removed += 1
        except OSError:
            pass
    return removed


def main(argv):
    do_delete = "--delete" in argv
    do_prune = "--prune-empty" in argv

    orphans = []
    for root, maps in scan().items():
        for key in sorted(maps):
            count, size = maps[key]
            alive = os.path.isfile(os.path.join(CONTENT, key + ".umap"))
            mark = "OK    " if alive else "ORPHAN"
            print(f"{mark} {count:6d} files {size / 1048576:8.1f} MB  [{root}] {key}")
            if not alive:
                orphans.append(os.path.join(CONTENT, root, key.replace("/", os.sep)))

    print(f"\n잔재 맵 폴더: {len(orphans)} 개")
    for path in orphans:
        rel = os.path.relpath(path, ROOT)
        if do_delete:
            shutil.rmtree(path)
            print(f"  삭제됨 {rel}")
        else:
            print(f"  {rel}   (--delete 로 삭제)")

    if do_prune:
        total = sum(prune_empty_dirs(os.path.join(CONTENT, r)) for r in ROOTS)
        print(f"빈 디렉터리 {total} 개 정리")
    return 1 if orphans and not do_delete else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
