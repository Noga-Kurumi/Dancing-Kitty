"""Assemble a portable folder, native resource payload, and release assets."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import zipfile

root = Path(__file__).resolve().parents[1]
version = (root / 'VERSION').read_text().strip()
parser = argparse.ArgumentParser()
parser.add_argument('--build-root', type=Path, required=True)
parser.add_argument('--ort-root', type=Path)
parser.add_argument('--crt-root', type=Path)
parser.add_argument('--finalize', action='store_true')
args = parser.parse_args()
build = args.build_root.resolve()
dist = root / 'dist'
package = dist / 'portable'
generated = build / 'bundle-generated'
dist.mkdir(exist_ok=True)

def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

if args.finalize:
    single = dist / f'Dancing-Kitty-{version}-windows-x64.exe'
    shutil.copy2(build / 'launcher/Release/Dancing Kitty.exe', single)
    archive = dist / f'Dancing-Kitty-{version}-windows-x64.zip'
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path in sorted(package.rglob('*')):
            if path.is_file():
                z.write(path, 'Dancing Kitty/' + path.relative_to(package).as_posix())
    sums = ''.join(f'{sha256(path)}  {path.name}\n' for path in (single, archive))
    (dist / 'SHA256SUMS.txt').write_text(sums, encoding='ascii')
    print(sums, end='')
    raise SystemExit(0)

if not args.ort_root or not args.crt_root:
    parser.error('--ort-root and --crt-root are required when staging the package')
if package.exists():
    # This script owns only its generated portable subdirectory.
    if package.resolve().parent != dist.resolve():
        raise RuntimeError('Unexpected package output path')
    shutil.rmtree(package)
package.mkdir()
shutil.copy2(build / 'src/app/Release/Dancing Kitty.exe', package / 'Dancing Kitty.exe')
shutil.copytree(root / 'assets/frames', package / 'frames')
(package / 'runtime').mkdir()
model_manifest = json.loads((root / 'models/manifest.json').read_text())
for model in model_manifest['models']:
    source = root / 'models' / model['file']
    if sha256(source) != model['sha256']:
        raise RuntimeError(f'Model checksum mismatch: {source.name}')
    shutil.copy2(source, package / 'runtime' / source.name)
shutil.copytree(root / 'licenses', package / 'licenses')
shutil.copy2(root / 'THIRD_PARTY_NOTICES.md', package / 'licenses/THIRD_PARTY_NOTICES.md')
for name in ('onnxruntime.dll', 'onnxruntime_providers_shared.dll'):
    shutil.copy2(args.ort_root / 'runtimes/win-x64/native' / name, package / name)
for name in ('msvcp140.dll','msvcp140_1.dll','vcruntime140.dll','vcruntime140_1.dll'):
    shutil.copy2(args.crt_root / name, package / name)
readme = (root / 'README.md').read_text(encoding='utf-8').split('## Build from source')[0]
readme = readme.replace('(assets/frames/', '(frames/')
readme += '\nSource: https://github.com/Noga-Kurumi/Dancing-Kitty\n\nThird-party licenses and notices are in `licenses/`.\n'
(package / 'README.md').write_text(readme, encoding='utf-8')
# Never seed a public release with a developer's personal screen coordinates.
(package / 'position.ini').write_text('[Position]\nsize=260\n', encoding='ascii')
files = [(p,p.relative_to(package).as_posix()) for p in sorted(package.rglob('*')) if p.is_file() and p.name != 'position.ini']
files.append((package / 'position.ini','initial-position.ini'))
manifest = []
digest = hashlib.sha256()
for identifier,(path,relative) in enumerate(files,201):
    file_hash = sha256(path)
    digest.update(relative.encode('utf-8'))
    digest.update(bytes.fromhex(file_hash))
    manifest.append(dict(id=identifier, relative=relative, size=path.stat().st_size, sha256=file_hash))
tag = version + '-' + digest.hexdigest()[:20]
generated.mkdir(parents=True, exist_ok=True)
resource = (root / 'src/app/cat_icon.rc').read_text(encoding='utf-8')
resource = resource.replace('"cat_icon.ico"', f'"{(root / "src/app/cat_icon.ico").as_posix()}"')
resource += '\n' + '\n'.join(f'{item["id"]} RCDATA "{path.as_posix()}"' for (path,_),item in zip(files,manifest)) + '\n'
(generated / 'bundle.rc').write_text(resource, encoding='utf-8')
header = '#pragma once\n#include <cstdint>\nstruct BundleFile {int id;const wchar_t* relative;uint32_t size;};\n'
header += f'inline constexpr wchar_t kBundleId[]=L"{tag}";\ninline constexpr BundleFile kBundleFiles[]={{\n'
for item in manifest:
    relative = item['relative'].replace('/', '\\\\')
    header += f'    {{{item["id"]},L"{relative}",{item["size"]}}},\n'
header += '};\n'
(generated / 'bundle_manifest.hpp').write_text(header, encoding='utf-8')
(generated / 'manifest.json').write_text(json.dumps(dict(version=version,tag=tag,files=manifest),indent=2), encoding='utf-8')
print(f'Staged {len(files)} files for Dancing Kitty {version}')
