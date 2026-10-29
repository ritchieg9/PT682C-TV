"""Distribution policy tests with simulated Mach-O tools, not native signing."""
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "support"))
spec = importlib.util.spec_from_file_location("distribution", ROOT / "support/distribution.py")
dist = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dist)


class DistributionTests(unittest.TestCase):
    def image(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"\xcf\xfa\xed\xfe" + b"fixture-not-real-macho")
        return path

    def test_dependency_closure_copies_libusb_without_changing_original(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            app = root / "TV.app"
            main = self.image(app / "Contents/MacOS/PT682CTV")
            original = self.image(root / "Homebrew/libusb-1.0.0.dylib")
            before = original.read_bytes()
            def references(path, arch):
                return [str(original)] if path == main else ["/usr/lib/libSystem.B.dylib"]
            with patch.object(dist.vr, "require_macos"), \
                 patch.object(dist.vr, "run", return_value="") as command, \
                 patch.object(dist.vr, "dylib_id", return_value=None), \
                 patch.object(dist.vr, "rpaths", return_value=[]), \
                 patch.object(dist.vr, "deps", side_effect=references):
                dist.bundle_dependencies(app, "arm64")
            local = app / "Contents/Frameworks/PT682CDeps/libusb-1.0.0.dylib"
            self.assertEqual(local.read_bytes(), before)
            self.assertEqual(original.read_bytes(), before)
            changes = [call.args for call in command.call_args_list
                       if call.args[:1] == ("install_name_tool",)]
            self.assertTrue(any("@loader_path/../Frameworks/PT682CDeps/libusb-1.0.0.dylib"
                                in args for args in changes))

    def test_external_rpaths_are_removed_but_system_paths_preserved(self):
        with patch.object(dist.vr, "rpaths", return_value=[
            "/opt/homebrew/lib", "/usr/lib/swift", "@loader_path/../Frameworks"]), \
             patch.object(dist.vr, "run") as command:
            dist.remove_external_rpaths(Path("/TV"), "arm64")
            command.assert_called_once_with(
                "install_name_tool", "-delete_rpath", "/opt/homebrew/lib", Path("/TV"))

    def test_unresolved_external_dependency_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp) / "TV.app"
            self.image(app / "Contents/MacOS/PT682CTV")
            with patch.object(dist.vr, "require_macos"), \
                 patch.object(dist.vr, "run", return_value=""), \
                 patch.object(dist.vr, "dylib_id", return_value=None), \
                 patch.object(dist.vr, "rpaths", return_value=[]), \
                 patch.object(dist.vr, "deps", return_value=["/missing/libusb.dylib"]):
                with self.assertRaises(dist.vr.RuntimeError_):
                    dist.bundle_dependencies(app, "arm64")

    def test_audit_rejects_external_absolute_dependency(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            app = root / "TV.app"
            image = self.image(app / "Contents/MacOS/PT682CTV")
            outside = self.image(root / "external.dylib")
            with patch.object(dist.vr, "require_macos"), \
                 patch.object(dist.vr, "run", return_value=""), \
                 patch.object(dist.vr, "dylib_id", return_value=None), \
                 patch.object(dist.vr, "rpaths", return_value=[]), \
                 patch.object(dist.vr, "deps", return_value=[str(outside)]):
                with self.assertRaisesRegex(dist.vr.RuntimeError_, "externa"):
                    dist.audit(app, ["arm64"])

    def test_pair_requires_identical_plugin_inventory(self):
        with tempfile.TemporaryDirectory() as temp:
            arm, intel = Path(temp) / "arm.app", Path(temp) / "intel.app"
            self.image(arm / "Contents/PlugIns/P.dylib")
            intel.mkdir()
            with self.assertRaisesRegex(dist.vr.RuntimeError_, "Inventarios"):
                dist.validate_pair(arm, intel)

    def test_pair_rejects_conflicting_seed_channels(self):
        with tempfile.TemporaryDirectory() as temp:
            arm, intel = Path(temp) / "arm.app", Path(temp) / "intel.app"
            for app, value in ((arm, "A"), (intel, "B")):
                seed = app / "Contents/Resources/Defaults/last-scan.tsv"
                seed.parent.mkdir(parents=True)
                seed.write_text(value)
            with self.assertRaisesRegex(dist.vr.RuntimeError_, "Recurso diferente"):
                dist.validate_pair(arm, intel)

    def test_pair_allows_architecture_specific_manifests_and_binary_bytes(self):
        with tempfile.TemporaryDirectory() as temp:
            arm, intel = Path(temp) / "arm.app", Path(temp) / "intel.app"
            for app, arch in ((arm, "arm64"), (intel, "x86_64")):
                self.image(app / "Contents/MacOS/PT682CTV")
                manifest = app / "Contents/Resources/VLCRuntime/runtime-manifest.json"
                manifest.parent.mkdir(parents=True)
                manifest.write_text(json.dumps({"architecture": arch}))
            a, b = dist.validate_pair(arm, intel)
            self.assertEqual(a.keys(), b.keys())

    def test_extract_thin_slice_copies_and_does_not_call_lipo_thin(self):
        with tempfile.TemporaryDirectory() as temp:
            source = self.image(Path(temp) / "thin")
            output = Path(temp) / "slice"
            with patch.object(dist.vr, "run", return_value="arm64") as command:
                dist.extract_slice(source, "arm64", output)
                command.assert_called_once_with("lipo", source, "-archs")
            self.assertEqual(output.read_bytes(), source.read_bytes())

    def test_extract_fat_slice_requires_correct_architecture(self):
        with patch.object(dist.vr, "run", return_value="x86_64"):
            with self.assertRaisesRegex(dist.vr.RuntimeError_, "Falta arm64"):
                dist.extract_slice(Path("source"), "arm64", Path("out"))
        with patch.object(dist.vr, "run", return_value="arm64 x86_64") as command:
            dist.extract_slice(Path("source"), "x86_64", Path("out"))
            command.assert_any_call("lipo", Path("source"), "-thin", "x86_64",
                                    "-output", Path("out"))

    def test_developer_id_signature_uses_runtime_timestamp_not_adhoc(self):
        with patch.object(dist.vr, "sign_tree") as nested, \
             patch.object(dist.vr, "run") as command:
            dist.sign(Path("TV.app"), "Developer ID Application: Test (TEST)")
            nested.assert_called_once_with(Path("TV.app"),
                                            identity="Developer ID Application: Test (TEST)")
            arguments = command.call_args_list[0].args
            self.assertIn("--timestamp", arguments)
            self.assertIn("runtime", arguments)
            self.assertNotIn("--timestamp=none", arguments)
            self.assertNotIn("--deep", arguments)

    def test_packaged_app_uses_runtime_paths_not_build_machine_config(self):
        build = (ROOT / "build-app.sh").read_text()
        storage = (ROOT / "Sources/PT682CTV/AppStorage.swift").read_text()
        appstate = (ROOT / "Sources/PT682CTV/AppState.swift").read_text()
        self.assertNotIn('make_config "$APPBUNDLE/', build)
        self.assertIn('Contents/MacOS', storage)
        self.assertIn('applicationSupportDirectory', appstate)
        self.assertIn('if !fm.fileExists(atPath: channels.path)', storage)
        self.assertIn('SHA256.hash', storage)
        self.assertIn('bundle-deps "$APPBUNDLE"', build)
        self.assertIn('CFBundleIconFile', build)

    def test_dmg_creation_does_not_submit_or_install(self):
        source = (ROOT / "build-dmg.sh").read_text()
        self.assertIn('ln -s /Applications', source)
        self.assertIn('hdiutil create', source)
        self.assertNotIn('notarytool submit', source)
        self.assertNotIn('sudo ', source)

    def test_universal_pipeline_stages_all_images_and_preserves_inputs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            arm, intel = root / "arm" / "TV.app", root / "intel" / "TV.app"
            for app, arch in ((arm, "arm64"), (intel, "x86_64")):
                for name in ["Contents/MacOS/PT682CTV",
                             "Contents/PlugIns/VLCPlugins/P.dylib"]:
                    self.image(app / name)
                for name in dist.IGNORED:
                    path = app / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(json.dumps({"architecture": arch, "sha256": {}}))
            before = {str(p): p.read_bytes() for app in [arm, intel]
                      for p in app.rglob("*") if p.is_file()}
            output = root / "combined" / "TV.app"
            def command(*args, **kwargs):
                if args[0] == "lipo" and args[-1] == "-archs":
                    return "arm64" if "/arm/" in str(args[1]) else "x86_64"
                if args[:2] == ("lipo", "-create"):
                    target = Path(args[args.index("-output") + 1])
                    target.write_bytes(Path(args[2]).read_bytes() + Path(args[3]).read_bytes())
                return ""
            with patch.object(dist.vr, "require_macos"), \
                 patch.object(dist.vr, "run", side_effect=command), \
                 patch.object(dist, "audit") as audit, patch.object(dist, "sign") as sign:
                dist.merge_universal(arm, intel, output)
                self.assertEqual(audit.call_count, 3)
                sign.assert_called_once()
            self.assertTrue((output / "Contents/MacOS/PT682CTV").exists())
            self.assertTrue((output / "Contents/PlugIns/VLCPlugins/P.dylib").exists())
            manifest = json.loads((output / "Contents/Resources/distribution.json").read_text())
            self.assertEqual(manifest["architectures"], ["arm64", "x86_64"])
            for name in dist.IGNORED:
                metadata = json.loads((output / name).read_text())
                self.assertEqual(metadata["architecture"], "universal2")
                self.assertNotIn("sha256", metadata)
            for name, content in before.items():
                self.assertEqual(Path(name).read_bytes(), content)


if __name__ == "__main__":
    unittest.main()
