#!/usr/bin/env python3
"""Import and audit a relocatable, single-architecture VLC 3 runtime on macOS.

Does not alter the source VLC.app. install_name_tool only touches staging copies.
All non-system dependency paths are rewritten to @loader_path-relative paths.
No network access, downloads, sudo, or edits to the original receiver are used.
"""
import argparse
import hashlib
import json
import os
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

MAGICS = {
    b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe",
    b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",
    b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
    b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca",
}
EXCLUDED = {"libmacosx_plugin.dylib", "libosx_notifications_plugin.dylib"}
ARCHES = {"arm64", "x86_64"}
SYSTEM_PREFIXES = ("/usr/lib/", "/System/Library/", "/Library/Apple/System/Library/")


class RuntimeError_(Exception):
    pass


def run(*args, check=True):
    result = subprocess.run([str(a) for a in args], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and result.returncode:
        raise RuntimeError_(f"Fallo: {' '.join(map(str, args))}\n{result.stderr.strip()}")
    return result.stdout


def require_macos():
    if sys.platform != "darwin":
        raise RuntimeError_("Importar/auditar binarios reales requiere macOS.")
    for name in ("otool", "lipo", "install_name_tool", "codesign"):
        if not shutil.which(name):
            raise RuntimeError_(f"Falta {name}. Instala Command Line Tools.")


def is_macho(path):
    if path.is_symlink() or not path.is_file():
        return False
    with path.open("rb") as handle:
        return handle.read(4) in MAGICS


def images(root):
    return sorted(p for p in root.rglob("*") if is_macho(p))


def parse_deps(text):
    result = []
    for line in text.splitlines():
        if line.startswith(("\t", " ")) and " (compatibility version " in line:
            result.append(line.strip().split(" (compatibility version ", 1)[0])
    return result


def parse_rpaths(text):
    return re.findall(r"\bcmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset \d+\)", text)


def deps(path, arch):
    return parse_deps(run("otool", "-arch", arch, "-L", path))


def rpaths(path, arch):
    return parse_rpaths(run("otool", "-arch", arch, "-l", path))


def dylib_id(path):
    lines = run("otool", "-D", path).splitlines()
    return lines[1].strip() if len(lines) > 1 else None


def is_system(name):
    return name.startswith(SYSTEM_PREFIXES)


def expand_token(value, loader, executable):
    if value == "@loader_path":
        return loader
    if value.startswith("@loader_path/"):
        return loader / value[len("@loader_path/"):]
    if value == "@executable_path":
        return executable
    if value.startswith("@executable_path/"):
        return executable / value[len("@executable_path/"):]
    if value.startswith("/"):
        return Path(value)
    return None


def resolve_original(name, origin, executable, search_paths, fallback):
    if is_system(name):
        return name
    candidates = []
    if name.startswith("@rpath/"):
        tail = name[len("@rpath/"):]
        for item in search_paths:
            base = expand_token(item, origin.parent, executable)
            if base is not None:
                candidate = (base / tail).resolve()
                # System dylibs can live in dyld's shared cache, not on disk.
                if is_system(str(candidate)):
                    return str(candidate)
                candidates.append(candidate)
        candidates.extend((base / tail).resolve() for base in fallback)
    else:
        direct = expand_token(name, origin.parent, executable)
        if direct is not None:
            candidates.append(direct.resolve())
        elif not name.startswith("@"):
            candidates.append((origin.parent / name).resolve())
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise RuntimeError_(f"Dependencia no resuelta:\n  archivo: {origin}\n  referencia: {name}")


def within(path, root):
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def copy_runtime(source, stage):
    mappings = []
    layouts = [
        (source / "Contents/MacOS/lib", stage / "lib"),
        (source / "Contents/MacOS/plugins", stage / "plugins"),
        (source / "Contents/Frameworks", stage / "Frameworks"),
        (source / "Contents/MacOS/share", stage / "share"),
        (source / "Contents/Resources", stage / "Resources"),
    ]
    for original, target in layouts:
        if original.is_dir():
            shutil.copytree(original, target, symlinks=True,
                            ignore=shutil.ignore_patterns(".DS_Store", "._*"))
            mappings.append((original.resolve(), target))
    if not (stage / "lib/libvlc.dylib").exists():
        if not (stage / "lib/libvlc.5.dylib").exists():
            raise RuntimeError_("VLC.app no contiene lib/libvlc[.5].dylib.")
    if not (stage / "plugins").is_dir():
        raise RuntimeError_("No se encontro Contents/MacOS/plugins. No se mezclaran versiones.")
    # Desktop UI and Growl integration are not used by the embedded player.
    for path in list(stage.rglob("*")):
        if path.name in EXCLUDED and path.is_file():
            path.unlink()
    for path in sorted(stage.rglob("_CodeSignature"), reverse=True):
        if path.is_dir():
            shutil.rmtree(path)
    for path in stage.rglob("plugins.dat"):
        path.unlink()

    def to_stage(original):
        resolved = original.resolve()
        for base, target in mappings:
            try:
                return target / resolved.relative_to(base)
            except ValueError:
                pass
        raise RuntimeError_(f"Dependencia fuera del runtime importado: {original}")

    # Reject dangling/external symlinks and remove absolute source-machine links.
    for link in sorted(stage.rglob("*")):
        if not link.is_symlink():
            continue
        original = None
        for base, target in mappings:
            try:
                original = base / link.relative_to(target)
                break
            except ValueError:
                pass
        if original is None or not original.exists():
            raise RuntimeError_(f"Enlace no portable o roto: {link}")
        target = to_stage(original)
        relative = os.path.relpath(target, link.parent)
        link.unlink()
        link.symlink_to(relative)
    return mappings, to_stage


def rewrite(stage, mappings, to_stage, source, arch):
    executable = source / "Contents/MacOS"
    fallback = [source / "Contents/MacOS/lib", source / "Contents/Frameworks",
                source / "Contents/MacOS"]
    binaries = images(stage)
    if not binaries:
        raise RuntimeError_("El runtime no contiene imagenes Mach-O.")
    for number, target in enumerate(binaries, 1):
        available = run("lipo", "-archs", target).split()
        if arch not in available:
            raise RuntimeError_(f"Arquitectura {arch} ausente en {target}: {available}")
        os.chmod(target, target.stat().st_mode | 0o200)
        run("codesign", "--remove-signature", target, check=False)
        if len(available) > 1:
            temporary = target.with_name(target.name + ".thin")
            run("lipo", target, "-thin", arch, "-output", temporary)
            os.chmod(temporary, target.stat().st_mode & 0o777)
            os.replace(temporary, target)
        original = None
        for base, copied in mappings:
            try:
                original = base / target.relative_to(copied)
                break
            except ValueError:
                pass
        if original is None:
            raise RuntimeError_(f"No se encontro origen de {target}")
        original_id = dylib_id(target)
        image_executable = executable
        if not original_id and re.search(r"\bEXECUTE\b", run("otool", "-hv", target)):
            # A nested framework helper has its own executable_path.
            image_executable = original.parent
        references = deps(target, arch)
        original_rpaths = rpaths(target, arch)
        args = []
        for reference in references:
            if reference == original_id or is_system(reference):
                continue
            origin_target = resolve_original(reference, original, image_executable,
                                             original_rpaths, fallback)
            if isinstance(origin_target, str) and is_system(origin_target):
                replacement = origin_target
            else:
                copied_target = to_stage(origin_target)
                if not copied_target.is_file():
                    raise RuntimeError_(f"Dependencia excluida o ausente: {reference}")
                replacement = "@loader_path/" + os.path.relpath(copied_target, target.parent)
            if reference != replacement:
                args.extend(["-change", reference, replacement])
        if original_id:
            args.extend(["-id", "@rpath/" + target.relative_to(stage).as_posix()])
        for value in dict.fromkeys(original_rpaths):
            args.extend(["-delete_rpath", value])
        if args:
            run("install_name_tool", *args, target)
        # Necessary for executing modified arm64 code; parent bundles are signed at build.
        run("codesign", "--force", "--sign", "-", "--timestamp=none", target)
        if number % 50 == 0:
            print(f"Reubicadas {number}/{len(binaries)} imagenes...", flush=True)


def audit(root, arch, check_hashes=False):
    require_macos()
    root = root.resolve()
    if arch not in ARCHES:
        raise RuntimeError_(f"Arquitectura no soportada: {arch}")
    for link in root.rglob("*"):
        if link.is_symlink() and (not link.exists() or not within(link, root)):
            raise RuntimeError_(f"Enlace fuera del runtime o roto: {link}")
    binaries = images(root)
    if not binaries:
        raise RuntimeError_("Runtime sin binarios.")
    for path in binaries:
        # -verify_arch consumes all following tokens as architecture names.
        # Therefore the input filename MUST precede that option.
        run("lipo", path, "-verify_arch", arch)
        identity = dylib_id(path)
        for reference in deps(path, arch):
            if reference == identity or is_system(reference):
                continue
            if not reference.startswith("@loader_path/"):
                raise RuntimeError_(f"Dependencia externa/no normalizada: {path}: {reference}")
            target = (path.parent / reference[len("@loader_path/"):]).resolve()
            if not within(target, root) or not target.is_file():
                raise RuntimeError_(f"Dependencia rota: {path}: {reference}")
        if rpaths(path, arch):
            raise RuntimeError_(f"LC_RPATH residual en runtime: {path}")
    if check_hashes:
        manifest = json.loads((root / "runtime-manifest.json").read_text())
        if manifest["architecture"] != arch:
            raise RuntimeError_("El runtime fue importado para otra arquitectura.")
        for rel, expected in manifest["sha256"].items():
            path = root / rel
            if not within(path, root) or not path.is_file():
                raise RuntimeError_(f"Archivo de manifiesto ausente: {rel}")
            if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                raise RuntimeError_(f"El runtime cambio desde su importacion: {rel}")
    print(f"Runtime auditado: {len(binaries)} imagenes, {arch}, sin rutas VLC externas.")
    return binaries


def import_runtime(source, destination, arch):
    require_macos()
    source = source.resolve()
    destination = destination.absolute()
    if destination.exists():
        raise RuntimeError_(f"Ya existe {destination}. Conserva un respaldo o eliminalo manualmente.")
    if within(destination, source) or within(source, destination):
        raise RuntimeError_("El origen y destino no pueden contenerse entre si.")
    info_path = source / "Contents/Info.plist"
    with info_path.open("rb") as handle:
        info = plistlib.load(handle)
    version = str(info.get("CFBundleShortVersionString", ""))
    if not version.startswith("3."):
        raise RuntimeError_(f"Se requiere VLC 3.x; version encontrada: {version!r}")
    if arch not in ARCHES:
        raise RuntimeError_(f"Arquitectura no soportada: {arch}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=".vlc-import-", dir=destination.parent))
    try:
        mappings, to_stage = copy_runtime(source, stage)
        rewrite(stage, mappings, to_stage, source, arch)
        audit(stage, arch)
        manifest = {
            "format": 1,
            "vlc_version": version,
            "architecture": arch,
            "origin": "VLC.app proporcionado al importador; original no modificado.",
            "upstream": "https://www.videolan.org/vlc/",
            "source_guidance": "https://www.videolan.org/legal.html",
            "excluded_desktop_plugins": sorted(EXCLUDED),
            "sha256": {p.relative_to(stage).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in stage.rglob("*") if p.is_file() and not p.is_symlink()},
        }
        (stage / "runtime-manifest.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
        os.replace(stage, destination)
        print(f"VLC {version} importado en: {destination}")
        print("No se modifico ni se requiere conservar el VLC.app original.")
    finally:
        if stage.exists():
            shutil.rmtree(stage)


def sign_tree(root, identity="-"):
    require_macos()
    # Signing Contents/MacOS/AppExecutable also signs the enclosing app.
    # Never do that while nested components are still being processed.
    candidates = [root] + [p for p in root.rglob("*")
                          if p.is_dir() and not p.is_symlink()
                          and p.suffix in {".framework", ".app", ".xpc", ".bundle"}]
    bundles = []
    owned_executables = set()
    for bundle in candidates:
        for info in (bundle / "Contents/Info.plist", bundle / "Resources/Info.plist"):
            if not info.is_file():
                continue
            with info.open("rb") as handle:
                executable = plistlib.load(handle).get("CFBundleExecutable")
            if executable:
                main = (bundle / executable if bundle.suffix == ".framework"
                        else bundle / "Contents/MacOS" / executable)
                owned_executables.add(main.resolve())
                bundles.append(bundle)
            break
    options = ["--timestamp=none"] if identity == "-" else ["--timestamp", "--options", "runtime"]
    for path in sorted(images(root), key=lambda p: len(p.parts), reverse=True):
        if path.resolve() in owned_executables:
            continue
        run("codesign", "--force", "--sign", identity, *options, path)
    for path in sorted((p for p in bundles if p != root),
                       key=lambda p: len(p.parts), reverse=True):
        run("codesign", "--force", "--sign", identity, *options, path)


def runtime_destination(relative, code):
    """Return a conventional bundle path, keeping resource files out of code dirs."""
    parts = relative.parts
    if not parts:
        raise RuntimeError_("Ruta vacia")
    if parts[0] == "lib" and code:
        return Path("Contents/Frameworks/VLCLibraries").joinpath(*parts[1:])
    if parts[0] == "plugins" and code:
        return Path("Contents/PlugIns/VLCPlugins").joinpath(*parts[1:])
    if parts[0] == "Frameworks":
        # A framework is a code bundle with its own correctly sealed Resources.
        if len(parts) < 2 or not parts[1].endswith((".framework", ".app", ".xpc", ".bundle")):
            if code:
                return Path("Contents/Frameworks/VLCLibraries/extra").joinpath(*parts[1:])
            return Path("Contents/Resources/VLCRuntime").joinpath(*parts)
        return Path("Contents/Frameworks").joinpath(*parts[1:])
    if code:
        return Path("Contents/Frameworks/VLCLibraries/extra").joinpath(*parts)
    return Path("Contents/Resources/VLCRuntime").joinpath(*parts)


def bundle_runtime(runtime, app, arch):
    """Repackage an already-imported runtime; never modify the vendor copy."""
    audit(runtime, arch, check_hashes=True)
    runtime = runtime.resolve()
    app = app.resolve()
    if app.suffix != ".app" or not (app / "Contents").is_dir():
        raise RuntimeError_("El destino debe ser el bundle .app en construccion.")
    destinations = {}
    code_origins = []
    links = []
    for source in sorted(runtime.rglob("*")):
        if source.is_symlink():
            links.append(source)
            continue
        if not source.is_file():
            continue
        if "_CodeSignature" in source.parts or source.name == "CodeResources":
            continue
        relative = source.relative_to(runtime)
        code = is_macho(source)
        target = app / runtime_destination(relative, code)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        destinations[source.resolve()] = target
        if code:
            code_origins.append((source, target))
    for source in links:
        original_target = source.resolve()
        if original_target.is_dir():
            # Conventional framework alias (Versions/Current, Resources, ...).
            # Translate the logical directory paths without following them.
            relative = source.relative_to(runtime)
            if relative.parts[0] not in {"Frameworks", "Resources", "share"}:
                raise RuntimeError_(f"Alias de directorio no soportado en zona de codigo: {source}")
            link = app / runtime_destination(relative, False)
            target = app / runtime_destination(original_target.relative_to(runtime), False)
        else:
            target = destinations.get(original_target)
            if target is None:
                raise RuntimeError_(f"Alias no resoluble al empaquetar: {source}")
            link = app / runtime_destination(source.relative_to(runtime), is_macho(original_target))
        link.parent.mkdir(parents=True, exist_ok=True)
        link.symlink_to(os.path.relpath(target, link.parent))
    for number, (source, target) in enumerate(code_origins, 1):
        os.chmod(target, target.stat().st_mode | 0o200)
        run("codesign", "--remove-signature", target, check=False)
        identity = dylib_id(source)
        changes = []
        for reference in deps(source, arch):
            if reference == identity or is_system(reference):
                continue
            if not reference.startswith("@loader_path/"):
                raise RuntimeError_(f"Referencia no normalizada al empaquetar: {source}: {reference}")
            original_target = (source.parent / reference[len("@loader_path/"):]).resolve()
            copied = destinations.get(original_target)
            if copied is None or not is_macho(copied):
                raise RuntimeError_(f"Dependencia no copiada como codigo: {original_target}")
            replacement = "@loader_path/" + os.path.relpath(copied, target.parent)
            if reference != replacement:
                changes.extend(["-change", reference, replacement])
        if identity:
            changes.extend(["-id", "@rpath/" + target.relative_to(app / "Contents").as_posix()])
        if changes:
            run("install_name_tool", *changes, target)
        if number % 50 == 0:
            print(f"Empaquetadas {number}/{len(code_origins)} imagenes desde vendor...", flush=True)
    data = app / "Contents/Resources/VLCRuntime"
    data.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((runtime / "runtime-manifest.json").read_text())
    (data / "bundle-layout.json").write_text(json.dumps({
        "format": 1, "architecture": arch, "vlc_version": manifest["vlc_version"],
        "images": [target.relative_to(app).as_posix() for _, target in code_origins],
        "libraries": "Contents/Frameworks/VLCLibraries",
        "plugins": "Contents/PlugIns/VLCPlugins",
        "data": "Contents/Resources/VLCRuntime",
    }, indent=2) + "\n")
    audit_bundle(app, arch)
    print("VLC empaquetado: bibliotecas en Frameworks, plugins en PlugIns, datos en Resources.")


def audit_bundle(app, arch):
    require_macos()
    app = app.resolve()
    layout_path = app / "Contents/Resources/VLCRuntime/bundle-layout.json"
    layout = json.loads(layout_path.read_text())
    if layout["architecture"] != arch and arch not in layout.get("architectures", []):
        raise RuntimeError_("Arquitectura del bundle incorrecta.")
    registered = {(app / item).resolve() for item in layout["images"]}
    if not registered:
        raise RuntimeError_("No hay bibliotecas de VLC en el bundle.")
    for path in registered:
        if not within(path, app) or not is_macho(path):
            raise RuntimeError_(f"Imagen ausente o fuera del bundle: {path}")
        run("lipo", path, "-verify_arch", arch)
        identity = dylib_id(path)
        for reference in deps(path, arch):
            if reference == identity or is_system(reference):
                continue
            if not reference.startswith("@loader_path/"):
                raise RuntimeError_(f"Dependencia no portable: {path}: {reference}")
            target = (path.parent / reference[len("@loader_path/"):]).resolve()
            if target not in registered:
                raise RuntimeError_(f"Dependencia fuera del conjunto VLC: {path}: {reference}")
    for folder in ("Contents/Frameworks/VLCLibraries", "Contents/PlugIns/VLCPlugins"):
        directory = app / folder
        for path in directory.rglob("*"):
            if path.is_file() and not is_macho(path.resolve()):
                raise RuntimeError_(f"Archivo de datos en directorio de codigo: {path}")
    print(f"Bundle VLC auditado: {len(registered)} imagenes y datos en Resources.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    imp = commands.add_parser("import")
    imp.add_argument("source", type=Path)
    imp.add_argument("destination", type=Path)
    imp.add_argument("--arch", default=platform.machine())
    check = commands.add_parser("audit")
    check.add_argument("root", type=Path)
    check.add_argument("--arch", default=platform.machine())
    check.add_argument("--hashes", action="store_true")
    sign = commands.add_parser("sign")
    sign.add_argument("root", type=Path)
    pack = commands.add_parser("bundle")
    pack.add_argument("runtime", type=Path)
    pack.add_argument("app", type=Path)
    pack.add_argument("--arch", default=platform.machine())
    bundle_check = commands.add_parser("audit-bundle")
    bundle_check.add_argument("app", type=Path)
    bundle_check.add_argument("--arch", default=platform.machine())
    args = parser.parse_args()
    try:
        if args.action == "import":
            import_runtime(args.source, args.destination, args.arch)
        elif args.action == "audit":
            audit(args.root, args.arch, args.hashes)
        elif args.action == "sign":
            sign_tree(args.root)
        elif args.action == "bundle":
            bundle_runtime(args.runtime, args.app, args.arch)
        else:
            audit_bundle(args.app, args.arch)
    except (RuntimeError_, OSError, ValueError, KeyError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
