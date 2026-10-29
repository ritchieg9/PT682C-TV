"""Self-contained macOS packaging, strict dependency audit and optional universal merge."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import sys
import tempfile

import vlc_runtime as vr


def architectures(app):
    main = app / "Contents/MacOS/PT682CTV"
    result = vr.run("lipo", main, "-archs").split()
    if not result or any(a not in {"arm64", "x86_64"} for a in result):
        raise vr.RuntimeError_("Arquitectura de aplicacion no compatible.")
    return sorted(result)


def remove_external_rpaths(image, arch):
    for value in set(vr.rpaths(image, arch)):
        if not value.startswith(("@loader_path", "@executable_path", "/usr/lib", "/System/")):
            vr.run("install_name_tool", "-delete_rpath", value, image)


def bundle_dependencies(app, arch):
    """Copy the receiver's non-system dylib closure, including libusb."""
    vr.require_macos()
    app = app.resolve()
    folder = app / "Contents/Frameworks/PT682CDeps"
    folder.mkdir(parents=True, exist_ok=True)
    binaries = app / "Contents/MacOS"
    queue = [(path, path) for path in sorted(binaries.iterdir()) if vr.is_macho(path)]
    copied = {}
    names = {}
    processed = set()
    while queue:
        image, original = queue.pop(0)
        if image in processed:
            continue
        processed.add(image)
        vr.run("lipo", image, "-verify_arch", arch)
        identity = vr.dylib_id(original)
        changes = []
        for reference in vr.deps(original, arch):
            if reference == identity or vr.is_system(reference):
                continue
            resolved = vr.resolve_original(reference, original, binaries,
                vr.rpaths(original, arch), [Path("/usr/lib/swift")])
            if isinstance(resolved, str) and vr.is_system(resolved):
                # Pin Swift's system libraries to the dyld cache, not a local toolchain.
                changes.extend(["-change", reference, resolved])
                continue
            resolved = Path(resolved).resolve()
            if vr.within(resolved, app):
                target = resolved
            else:
                if resolved not in copied:
                    name = resolved.name
                    if name in names and names[name] != resolved:
                        raise vr.RuntimeError_(f"Colision de nombres dylib: {name}")
                    names[name] = resolved
                    target = folder / name
                    shutil.copy2(resolved, target)
                    copied[resolved] = target
                    queue.append((target, resolved))
                target = copied[resolved]
            relative = os.path.relpath(target, image.parent)
            changes.extend(["-change", reference, "@loader_path/" + relative])
        vr.run("codesign", "--remove-signature", image, check=False)
        if identity:
            changes.extend(["-id", "@rpath/" + image.name])
        if changes:
            vr.run("install_name_tool", *changes, image)
        remove_external_rpaths(image, arch)
    if not any(p.name.startswith("libusb") for p in folder.iterdir()):
        raise vr.RuntimeError_("No se incluyo libusb. Revisa el enlace del receptor.")
    print(f"Dependencias del receptor incluidas: {len(copied)} bibliotecas.")


def audit(app, archs=None):
    vr.require_macos()
    app = app.resolve()
    archs = archs or architectures(app)
    for path in app.rglob("*"):
        if path.is_symlink() and not vr.within(path, app):
            raise vr.RuntimeError_(f"Enlace fuera del bundle: {path}")
    paths = vr.images(app)
    if not paths:
        raise vr.RuntimeError_("La aplicacion no contiene codigo.")
    for image in paths:
        for arch in archs:
            vr.run("lipo", image, "-verify_arch", arch)
            identity = vr.dylib_id(image)
            for reference in vr.deps(image, arch):
                if reference == identity or vr.is_system(reference):
                    continue
                target = vr.resolve_original(reference, image, app / "Contents/MacOS",
                    vr.rpaths(image, arch), [Path("/usr/lib/swift")])
                if isinstance(target, str) and vr.is_system(target):
                    continue
                if not vr.within(target, app):
                    raise vr.RuntimeError_(f"Dependencia externa: {image}: {reference}")
            for value in vr.rpaths(image, arch):
                if value.startswith("/") and not vr.is_system(value + "/"):
                    raise vr.RuntimeError_(f"LC_RPATH externo: {image}: {value}")
    for item in ["Contents/Resources/Firmware/dvb-demod-mxl692.fw",
                 "Contents/Resources/Defaults/last-scan.tsv",
                 "Contents/Resources/PT682CTV.icns"]:
        if not (app / item).is_file():
            raise vr.RuntimeError_(f"Recurso necesario ausente: {item}")
    firmware = (app / "Contents/Resources/Firmware/dvb-demod-mxl692.fw").read_bytes()
    if hashlib.sha256(firmware).hexdigest() != \
            "8f77d5cb0de9111ed63c113cb2fd54a63e07a30040ae0b562cb025ab9c4e600d":
        raise vr.RuntimeError_("Firmware no verificado.")
    print(f"APP AUTOCONTENIDA: {len(paths)} imagenes; arquitecturas {', '.join(archs)}.")


def sign(app, identity="-"):
    vr.sign_tree(app, identity=identity)
    command = ["codesign", "--force", "--sign", identity]
    command += ["--timestamp=none"] if identity == "-" else ["--timestamp", "--options", "runtime"]
    vr.run(*command, app)
    vr.run("codesign", "--verify", "--deep", "--strict", app)


IGNORED = {"Contents/Resources/VLCRuntime/runtime-manifest.json",
           "Contents/Resources/VLCRuntime/bundle-layout.json"}


def inventory(app):
    return {str(p.relative_to(app)): p for p in app.rglob("*")
            if (p.is_symlink() or p.is_file()) and "_CodeSignature" not in p.parts
            and p.name not in {"CodeResources", ".DS_Store", "distribution.json"}}


def validate_pair(arm, intel):
    """Fail closed if independent builds have incompatible plugins or data."""
    a, b = inventory(arm), inventory(intel)
    if a.keys() != b.keys():
        raise vr.RuntimeError_("Inventarios diferentes; usa la misma version y plugins de VLC.")
    for relative, first in a.items():
        second = b[relative]
        if first.is_symlink() or second.is_symlink():
            if not (first.is_symlink() and second.is_symlink()
                    and os.readlink(first) == os.readlink(second)):
                raise vr.RuntimeError_(f"Enlace incompatible: {relative}")
        elif vr.is_macho(first) != vr.is_macho(second):
            raise vr.RuntimeError_(f"Tipo de archivo incompatible: {relative}")
        elif not vr.is_macho(first) and relative not in IGNORED:
            if first.read_bytes() != second.read_bytes():
                raise vr.RuntimeError_(f"Recurso diferente entre compilaciones: {relative}")
    return a, b


def merge_universal(arm, intel, output, identity="-"):
    vr.require_macos()
    arm, intel, output = arm.resolve(), intel.resolve(), output.absolute()
    if output.suffix != ".app":
        raise vr.RuntimeError_("La salida debe tener extension .app.")
    if output.exists():
        raise vr.RuntimeError_("La salida ya existe; elige una carpeta nueva.")
    audit(arm, ["arm64"])
    audit(intel, ["x86_64"])
    a, b = validate_pair(arm, intel)
    output.parent.mkdir(parents=True, exist_ok=True)
    # Stage all edits; never modify the two native apps or expose a partial result.
    with tempfile.TemporaryDirectory(prefix=".pt682c-universal-", dir=output.parent) as temp:
        stage = Path(temp) / output.name
        shutil.copytree(arm, stage, symlinks=True)
        for path in list(stage.rglob("_CodeSignature")):
            if path.is_dir():
                shutil.rmtree(path)
        for path in list(stage.rglob("CodeResources")):
            path.unlink()
        for relative, first in a.items():
            if not first.is_symlink() and vr.is_macho(first):
                target = stage / relative
                mode = target.stat().st_mode
                with tempfile.TemporaryDirectory(dir=temp) as slices:
                    first_slice = Path(slices) / "arm64"
                    second_slice = Path(slices) / "x86_64"
                    extract_slice(first, "arm64", first_slice)
                    extract_slice(b[relative], "x86_64", second_slice)
                    vr.run("lipo", "-create", first_slice, second_slice, "-output", target)
                target.chmod(mode)
                vr.run("codesign", "--remove-signature", target, check=False)
        for relative in IGNORED:
            path = stage / relative
            data = json.loads(path.read_text())
            data["architecture"] = "universal2"
            data["architectures"] = ["arm64", "x86_64"]
            if "sha256" in data:
                # Source-import hashes no longer describe the combined binaries.
                data.pop("sha256")
                data["note"] = "Universal combinado; comprobar arquitecturas y firma final."
            path.write_text(json.dumps(data, indent=2) + "\n")
        write_manifest(stage, ["arm64", "x86_64"])
        audit(stage, ["arm64", "x86_64"])
        sign(stage, identity)
        stage.rename(output)
    print(f"APP UNIVERSAL CREADA: {output}")


def extract_slice(source, architecture, output):
    archs = vr.run("lipo", source, "-archs").split()
    if architecture not in archs:
        raise vr.RuntimeError_(f"Falta {architecture}: {source}")
    if archs == [architecture]:
        shutil.copy2(source, output)
    else:
        vr.run("lipo", source, "-thin", architecture, "-output", output)


def write_manifest(app, archs):
    data = {"version": "0.6", "architectures": archs, "selfContained": True}
    (app / "Contents/Resources/distribution.json").write_text(
        json.dumps(data, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    for name in ("bundle-deps", "audit", "sign", "manifest"):
        command = commands.add_parser(name)
        command.add_argument("app", type=Path)
        command.add_argument("--arch")
        command.add_argument("--identity", default="-")
    command = commands.add_parser("universal")
    command.add_argument("arm_app", type=Path)
    command.add_argument("intel_app", type=Path)
    command.add_argument("output", type=Path)
    command.add_argument("--identity", default="-")
    args = parser.parse_args()
    try:
        if args.action == "bundle-deps":
            bundle_dependencies(args.app, args.arch or architectures(args.app)[0])
        elif args.action == "audit":
            audit(args.app, [args.arch] if args.arch else None)
        elif args.action == "sign":
            sign(args.app, args.identity)
        elif args.action == "manifest":
            write_manifest(args.app, [args.arch] if args.arch else architectures(args.app))
        else:
            merge_universal(args.arm_app, args.intel_app, args.output, args.identity)
    except (vr.RuntimeError_, OSError, ValueError, KeyError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
