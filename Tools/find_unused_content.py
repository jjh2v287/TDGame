"""Content 안에서 어떤 루트에서도 참조되지 않는 (도달 불가능한) 에셋을 찾는다.

.uasset/.umap 파일 안에는 참조하는 패키지 경로가 문자열(FName)로 들어있다.
그 문자열을 긁어 의존성 그래프를 만들고, 루트(기본 맵·게임모드·설정·C++/툴 스크립트가
가리키는 경로)에서 도달 가능한 집합을 구한 뒤 나머지를 '미사용'으로 보고한다.

문자열 기반이라 과검출(실제로는 안 쓰는데 쓰는 것으로 판정) 쪽으로 치우친다.
즉 '미사용'으로 나온 것은 대체로 진짜 미사용이지만, 런타임에 경로를 조립해 로드하는
소프트 참조는 잡지 못하므로 삭제 전 눈으로 확인한다.

실행:
  python Tools/find_unused_content.py                    # 폴더별 요약
  python Tools/find_unused_content.py --list <폴더>      # 해당 폴더의 미사용 파일 나열
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTENT = os.path.join(ROOT, "Content")
EXT_ROOTS = ("__ExternalActors__", "__ExternalObjects__")
PATH_RE = re.compile(rb"/Game/[A-Za-z0-9_/\-]+")
SMALL_FILE_LIMIT = 20 * 1024 * 1024  # 이보다 작은 파일만 UTF-16 문자열까지 훑는다

# 루트: 여기서부터 참조를 따라간다.
# (1) 우리 게임이 실제로 쓰는 폴더  (2) 소스·설정·툴 스크립트가 문자열로 가리키는 경로
ROOT_GLOBS = [
    "/Game/Level/",            # 기본 맵 LV-World 외
    "/Game/Combat/",           # 우리 게임모드·캐릭터·컨트롤러
    "/Game/World/",            # 월드 생성 데이터 자산과 메인 맵
    "/Game/Dungeon/",          # 던전 방 레벨 인스턴스
    "/Game/POI/",
    "/Game/Tests/",
    "/Game/Python/",
]
# 아래 폴더의 텍스트 파일에서 /Game/... 을 긁어 루트에 더한다 (C++ 하드코딩 경로, 파이썬 툴 등)
ROOT_SCAN_DIRS = ["Source", "Config", "Tools", "Content/Python"]
# 문서(Docs)는 "무엇을 지울지" 논의까지 담고 있어 루트로 치면 안 된다.
# 아래 파일은 콘텐츠 브라우저에서 마지막으로 열어둔 폴더 같은 에디터 UI 상태라 의존성이 아니다.
ROOT_SCAN_SKIP = ("DefaultEditorPerProjectUserSettings.ini",)
ROOT_SCAN_EXTS = (".cpp", ".h", ".cs", ".ini", ".py", ".json", ".md", ".uproject")


def scan_text_roots():
    """소스·설정·툴이 문자열로 가리키는 /Game 경로를 모은다 (소프트 참조 보호)."""
    found = set()
    targets = [os.path.join(ROOT, d) for d in ROOT_SCAN_DIRS] + [os.path.join(ROOT, "TDGame.uproject")]
    for base in targets:
        walk = [(os.path.dirname(base), [], [os.path.basename(base)])] if os.path.isfile(base) else os.walk(base)
        for dirpath, _dirnames, filenames in walk:
            if "__pycache__" in dirpath:
                continue
            for name in filenames:
                if not name.endswith(ROOT_SCAN_EXTS) or name in ROOT_SCAN_SKIP:
                    continue
                try:
                    data = open(os.path.join(dirpath, name), "rb").read()
                except OSError:
                    continue
                for match in PATH_RE.findall(data):
                    found.add(match.decode("ascii", "ignore"))
    return found


def package_of(rel_path):
    """Content 기준 상대 경로 -> /Game/... 패키지 경로. 외부 액터는 소유 레벨로 접는다."""
    parts = rel_path.replace("\\", "/").split("/")
    if parts[0] in EXT_ROOTS:
        # __ExternalActors__/<경로>/<맵>/<1자>/<2자>/<GUID>.uasset -> /Game/<경로>/<맵>
        return "/Game/" + "/".join(parts[1:-3])
    return "/Game/" + "/".join(parts)[: -len(os.path.splitext(rel_path)[1])]


def collect():
    files = {}
    for dirpath, _dirnames, filenames in os.walk(CONTENT):
        for name in filenames:
            if not name.endswith((".uasset", ".umap")):
                continue
            full = os.path.join(dirpath, name)
            files[full] = package_of(os.path.relpath(full, CONTENT))
    return files


def references(full):
    size = os.path.getsize(full)
    with open(full, "rb") as handle:
        data = handle.read()
    found = set(PATH_RE.findall(data))
    if size < SMALL_FILE_LIMIT:
        found |= set(PATH_RE.findall(data.replace(b"\x00", b"")))
    return {m.decode("ascii", "ignore") for m in found}


def main(argv):
    list_dir = argv[argv.index("--list") + 1] if "--list" in argv else None

    files = collect()
    packages = set(files.values())
    edges = {}
    for full, owner in files.items():
        targets = edges.setdefault(owner, set())
        for ref in references(full):
            if ref != owner:
                targets.add(ref)

    # 루트 집합
    text_roots = scan_text_roots()
    globs = list(ROOT_GLOBS) + [r + "/" for r in text_roots]
    reachable = {p for p in packages if any(p.startswith(g) for g in globs) or p in text_roots}
    stack = list(reachable)
    while stack:
        current = stack.pop()
        for target in edges.get(current, ()):  # 그래프에 없는 경로(엔진 에셋)는 무시된다
            # 참조 문자열이 폴더까지만 있는 경우도 있어 접두사로 넓게 도달 처리
            for candidate in packages:
                if candidate == target or candidate.startswith(target + "/"):
                    if candidate not in reachable:
                        reachable.add(candidate)
                        stack.append(candidate)

    unused = sorted(packages - reachable)
    if list_dir:
        prefix = "/Game/" + list_dir.strip("/") + "/"
        for p in unused:
            if p.startswith(prefix):
                print(p)
        return 0

    summary = {}
    for full, owner in files.items():
        if owner in reachable:
            continue
        top = owner.split("/")[2] if owner.count("/") >= 2 else owner
        count, size = summary.get(top, (0, 0))
        summary[top] = (count + 1, size + os.path.getsize(full))

    print(f"전체 패키지 {len(packages)} 개 중 도달 불가 {len(unused)} 개\n")
    print(f"{'폴더':<28}{'파일':>8}{'용량(MB)':>12}")
    for top in sorted(summary, key=lambda k: -summary[k][1]):
        count, size = summary[top]
        print(f"{top:<28}{count:>8}{size / 1048576:>12.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
