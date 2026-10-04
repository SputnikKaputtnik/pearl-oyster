"""Assembles the Quest release package: installer, signed APK, content checklist, licenses.

    python tools/make_quest_release.py <version> <signed release apk> <output dir>

Output: <output dir>/pearl-oyster-quest-v<version>/ and a .zip of it. The content checklist
(pearl-content.tsv: path, size, sha256 of the 4474 files the app reads) is derived from
manifests/steam-476540-build1340090.tsv.
"""
import shutil
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
FOLDERS = ('common/', 'pearl_vrcam/', 'pearlpackage/', 'story/')


def main():
    version, apk, out = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
    name = f'pearl-oyster-quest-v{version}'
    dst = out / name
    if dst.exists():
        shutil.rmtree(dst)
    dst.mkdir(parents=True)
    inst = REPO / 'runtime' / 'apps' / 'quest' / 'installer'
    for f in ('install.ps1', 'Install Pearl on Quest.cmd', 'README.txt'):
        shutil.copy2(inst / f, dst / f)
    shutil.copy2(apk, dst / f'oyster-pearl-{version}.apk')
    for f in ('LICENSE', 'THIRD_PARTY.md'):
        shutil.copy2(REPO / f, dst / f)
    shutil.copytree(REPO / 'licenses', dst / 'licenses')

    rows = []
    for line in (REPO / 'manifests' / 'steam-476540-build1340090.tsv').read_text(encoding='utf-8').splitlines():
        if line.startswith('#'):
            continue
        path, size, sha = line.split('\t')[:3]
        if path.startswith(FOLDERS):
            rows.append(f'{path}\t{size}\t{sha}')
    (dst / 'pearl-content.tsv').write_text(
        '# Pearl (Steam AppID 476540, build 1340090): files the app reads - path, size, sha256\n'
        + '\n'.join(rows) + '\n', encoding='utf-8', newline='\n')
    # the installer and README are Windows files
    for f in ('install.ps1', 'Install Pearl on Quest.cmd', 'README.txt'):
        p = dst / f
        p.write_bytes(p.read_bytes().replace(b'\r\n', b'\n').replace(b'\n', b'\r\n'))

    zpath = out / f'{name}.zip'
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
        for p in sorted(dst.rglob('*')):
            if p.is_file():
                z.write(p, Path(name) / p.relative_to(dst))
    print(f'{len(rows)} content files listed; {zpath} ({zpath.stat().st_size / 1048576:.1f} MB)')


if __name__ == '__main__':
    main()
