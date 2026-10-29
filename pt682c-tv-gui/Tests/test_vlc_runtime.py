"""Unit tests of path resolution and audit policy; no native tools are emulated."""
import importlib.util
import tempfile
import unittest
import os
import shutil
import subprocess
import sys
import plistlib
import json
from pathlib import Path
from unittest.mock import patch

MODULE = Path(__file__).resolve().parents[1] / "support/vlc_runtime.py"
spec = importlib.util.spec_from_file_location("vlc_runtime", MODULE)
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


class RuntimeTests(unittest.TestCase):
    def test_dependency_parser_handles_spaces(self):
        text = "/X Y/file:\n\t@rpath/A.dylib (compatibility version 1.0.0, current version 2.0.0)\n\t/X Y/B (compatibility version 1.0.0, current version 1.0.0)\n"
        self.assertEqual(runtime.parse_deps(text), ["@rpath/A.dylib", "/X Y/B"])

    def test_rpath_parser_handles_spaces(self):
        self.assertEqual(runtime.parse_rpaths(
            "Load command 4\n cmd LC_RPATH\n cmdsize 40\n path /Path With Spaces/lib (offset 12)\n"),
            ["/Path With Spaces/lib"])

    def test_loader_path_resolution(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "plugins").mkdir()
            (root / "lib").mkdir()
            (root / "lib/core.dylib").touch()
            actual = runtime.resolve_original("@loader_path/../lib/core.dylib",
                root / "plugins/p.dylib", root, [], [])
            self.assertEqual(actual, (root / "lib/core.dylib").resolve())

    def test_executable_path_resolution(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "MacOS").mkdir()
            (root / "Frameworks").mkdir()
            (root / "Frameworks/F").touch()
            actual = runtime.resolve_original("@executable_path/../Frameworks/F",
                root / "MacOS/plugins/p", root / "MacOS", [], [])
            self.assertEqual(actual, (root / "Frameworks/F").resolve())

    def test_rpath_with_loader_token(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "a").mkdir()
            (root / "lib").mkdir()
            (root / "lib/B").touch()
            actual = runtime.resolve_original("@rpath/B", root / "a/A", root,
                                              ["@loader_path/../lib"], [])
            self.assertEqual(actual, (root / "lib/B").resolve())

    def test_system_shared_cache_path_allowed_without_file(self):
        self.assertEqual(runtime.resolve_original(
            "@rpath/libswiftCore.dylib", Path("/a/A"), Path("/a"),
            ["/usr/lib/swift"], []), "/usr/lib/swift/libswiftCore.dylib")

    def test_missing_dependency_fails_closed(self):
        with self.assertRaises(runtime.RuntimeError_):
            runtime.resolve_original("@rpath/MISSING-pt682c.dylib",
                                     Path("/a/A"), Path("/a"), [], [])

    def test_system_prefix_not_confused_with_neighbor(self):
        self.assertTrue(runtime.is_system("/usr/lib/libSystem.B.dylib"))
        self.assertFalse(runtime.is_system("/usr/library-mine/lib.dylib"))
        self.assertFalse(runtime.is_system("/opt/homebrew/lib/lib.dylib"))

    def test_lipo_input_precedes_variadic_verify_arch(self):
        with tempfile.TemporaryDirectory(prefix="vlc runtime ") as temp:
            root = Path(temp)
            image = root / "Frameworks/Breakpad.framework/Versions/A/Breakpad"
            image.parent.mkdir(parents=True)
            image.write_bytes(b"\xcf\xfa\xed\xfe" + b"fixture-not-real-macho")
            with patch.object(runtime, "require_macos"), \
                 patch.object(runtime, "run", return_value="") as command, \
                 patch.object(runtime, "dylib_id", return_value=None), \
                 patch.object(runtime, "deps", return_value=[]), \
                 patch.object(runtime, "rpaths", return_value=[]):
                runtime.audit(root, "arm64")
                command.assert_called_once_with("lipo", image, "-verify_arch", "arm64")

    def test_external_symlink_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "runtime"
            root.mkdir()
            (root / "leak").symlink_to("/Applications/VLC.app")
            with patch.object(runtime, "require_macos"):
                with self.assertRaises(runtime.RuntimeError_):
                    runtime.audit(root, "arm64")

    def test_nonportable_reference_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            image = root / "lib.dylib"
            image.write_bytes(b"\xcf\xfa\xed\xfe" + b"fixture-not-real-macho")
            with patch.object(runtime, "require_macos"), \
                 patch.object(runtime, "run", return_value=""), \
                 patch.object(runtime, "dylib_id", return_value=None), \
                 patch.object(runtime, "deps", return_value=["/Applications/VLC.app/lib/core"]), \
                 patch.object(runtime, "rpaths", return_value=[]):
                with self.assertRaises(runtime.RuntimeError_):
                    runtime.audit(root, "arm64")

    def test_relative_dependency_cannot_escape_runtime(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "runtime"
            root.mkdir()
            image = root / "lib.dylib"
            image.write_bytes(b"\xcf\xfa\xed\xfe" + b"fixture-not-real-macho")
            (Path(temp) / "outside").touch()
            with patch.object(runtime, "require_macos"), \
                 patch.object(runtime, "run", return_value=""), \
                 patch.object(runtime, "dylib_id", return_value=None), \
                 patch.object(runtime, "deps", return_value=["@loader_path/../outside"]), \
                 patch.object(runtime, "rpaths", return_value=[]):
                with self.assertRaises(runtime.RuntimeError_):
                    runtime.audit(root, "arm64")

    def test_no_absolute_source_paths_in_loader(self):
        source = (MODULE.parents[1] / "Sources/VLCShim/vlcshim.c").read_text()
        self.assertNotIn("/Applications/", source)
        self.assertNotIn("/opt/homebrew/", source)
        self.assertNotIn("/usr/local/lib", source)
        self.assertNotIn('getenv("PT682C_LIBVLC")', source)

    def test_jar_and_manifest_are_resources_not_code(self):
        self.assertEqual(runtime.runtime_destination(Path("plugins/libbluray.jar"), False),
                         Path("Contents/Resources/VLCRuntime/plugins/libbluray.jar"))
        self.assertEqual(runtime.runtime_destination(Path("runtime-manifest.json"), False),
                         Path("Contents/Resources/VLCRuntime/runtime-manifest.json"))
        self.assertEqual(runtime.runtime_destination(Path("plugins/libts_plugin.dylib"), True),
                         Path("Contents/PlugIns/VLCPlugins/libts_plugin.dylib"))
        self.assertEqual(runtime.runtime_destination(Path("lib/libvlc.5.dylib"), True),
                         Path("Contents/Frameworks/VLCLibraries/libvlc.5.dylib"))

    def test_signing_never_seals_outer_app_as_plain_executable(self):
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp) / "TV.app"
            helper = app / "Contents/Frameworks/F.framework/Resources/Updater.app"
            main = app / "Contents/MacOS/TV"
            helper_main = helper / "Contents/MacOS/Updater"
            plugin = app / "Contents/PlugIns/P.dylib"
            for bundle, name in ((app, "TV"), (helper, "Updater")):
                info = bundle / "Contents/Info.plist"
                info.parent.mkdir(parents=True, exist_ok=True)
                info.write_bytes(plistlib.dumps({"CFBundleExecutable": name}))
            for path in (main, helper_main, plugin):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"\xcf\xfa\xed\xfe" + b"fixture-not-real-macho")
            with patch.object(runtime, "require_macos"), \
                 patch.object(runtime, "run", return_value="") as command:
                runtime.sign_tree(app)
                signed = [call.args[-1] for call in command.call_args_list]
                self.assertNotIn(main, signed)
                self.assertNotIn(helper_main, signed)
                self.assertNotIn(app, signed)
                self.assertEqual(signed, [plugin, helper])

    def test_framework_owner_is_signed_after_its_helper(self):
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp) / "TV.app"
            framework = app / "Contents/Frameworks/F.framework"
            helper = framework / "Resources/Updater.app"
            app_main = app / "Contents/MacOS/TV"
            framework_main = framework / "F"
            helper_main = helper / "Contents/MacOS/Updater"
            for info, name in ((app / "Contents/Info.plist", "TV"),
                               (framework / "Resources/Info.plist", "F"),
                               (helper / "Contents/Info.plist", "Updater")):
                info.parent.mkdir(parents=True, exist_ok=True)
                info.write_bytes(plistlib.dumps({"CFBundleExecutable": name}))
            for path in (app_main, framework_main, helper_main):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"\xcf\xfa\xed\xfe" + b"fixture-not-real-macho")
            with patch.object(runtime, "require_macos"), \
                 patch.object(runtime, "run", return_value="") as command:
                runtime.sign_tree(app)
                signed = [call.args[-1] for call in command.call_args_list]
                self.assertEqual(signed, [helper, framework])

    def test_repackaging_keeps_vendor_and_moves_jar_to_resources(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            vendor = root / "vendor"
            app = root / "TV.app"
            (vendor / "lib").mkdir(parents=True)
            (vendor / "plugins").mkdir()
            (app / "Contents").mkdir(parents=True)
            core = vendor / "lib/core.dylib"
            plugin = vendor / "plugins/plugin.dylib"
            for path in (core, plugin):
                path.write_bytes(b"\xcf\xfa\xed\xfe" + b"fixture-not-real-macho")
            (vendor / "lib/libvlc.dylib").symlink_to("core.dylib")
            jar = vendor / "plugins/libbluray.jar"
            jar.write_bytes(b"PK-test-resource")
            (vendor / "Resources/en.lproj").mkdir(parents=True)
            (vendor / "Resources/en.lproj/strings.txt").write_text("test locale")
            (vendor / "Resources/English.lproj").symlink_to("en.lproj")
            (vendor / "runtime-manifest.json").write_text(json.dumps({
                "vlc_version": "3.0.24", "architecture": "arm64", "sha256": {}
            }))
            refs = {plugin: ["@loader_path/../lib/core.dylib"]}

            def fake_command(*args, **kwargs):
                if args[0] == "install_name_tool":
                    target = args[-1]
                    values = list(args[1:-1])
                    changed = []
                    while values:
                        option = values.pop(0)
                        if option == "-change":
                            values.pop(0)
                            changed.append(values.pop(0))
                        else:
                            values.pop(0)
                    refs[target] = changed
                return ""

            with patch.object(runtime, "require_macos"), \
                 patch.object(runtime, "audit"), \
                 patch.object(runtime, "run", side_effect=fake_command), \
                 patch.object(runtime, "dylib_id", return_value=None), \
                 patch.object(runtime, "deps", side_effect=lambda path, arch: refs.get(path, [])):
                runtime.bundle_runtime(vendor, app, "arm64")
            self.assertEqual(jar.read_bytes(), b"PK-test-resource")
            self.assertTrue((app / "Contents/Resources/VLCRuntime/plugins/libbluray.jar").is_file())
            self.assertFalse((app / "Contents/PlugIns/VLCPlugins/libbluray.jar").exists())
            self.assertTrue((app / "Contents/Frameworks/VLCLibraries/libvlc.dylib").is_symlink())
            self.assertEqual((app / "Contents/Resources/VLCRuntime/Resources/English.lproj/strings.txt")
                             .read_text(), "test locale")
            copied_plugin = app / "Contents/PlugIns/VLCPlugins/plugin.dylib"
            self.assertEqual(refs[copied_plugin],
                             ["@loader_path/../../Frameworks/VLCLibraries/core.dylib"])


@unittest.skipUnless(shutil.which("cc"), "C compiler not available")
class LoaderFixtureTests(unittest.TestCase):
    """Exercises the real C shim, but with a tiny fake API, not native VLC."""
    def build_and_run(self, missing_symbol=False, missing_runtime=False, split=False):
        root = MODULE.parents[1]
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            local = work / "VLCRuntime"
            (local / "plugins").mkdir(parents=True)
            (local / "lib").mkdir()
            dynamic = ["-dynamiclib"] if sys.platform == "darwin" else ["-shared", "-fPIC"]
            if missing_symbol:
                dynamic.append("-DOMIT_NSOBJECT")
            subprocess.run(["cc", *dynamic, str(root / "Tests/fixtures/mock_vlc.c"),
                            "-o", str(local / "lib/libvlc.dylib")], check=True)
            link = [] if sys.platform == "darwin" else ["-ldl", "-lpthread"]
            executable = work / "check-vlc"
            subprocess.run(["cc", "-I", str(root / "Sources/VLCShim/include"),
                            str(root / "Sources/VLCShim/vlcshim.c"),
                            str(root / "support/check_vlc.c"),
                            *link, "-o", str(executable)], check=True)
            environment = os.environ.copy()
            # Even a valid override must not replace the configured private root.
            environment["PT682C_LIBVLC"] = str(local / "lib/libvlc.dylib")
            configured = work / "missing" if missing_runtime else local
            arguments = ([str(local / "lib"), str(local / "plugins"), str(local / "share")]
                         if split else [str(configured)])
            return subprocess.run([str(executable), *arguments], env=environment,
                                  text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def test_real_loader_opens_only_private_fixture(self):
        result = self.build_and_run()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("VLC_RUNTIME_CHECK_OK", result.stdout)

    def test_missing_symbol_has_specific_diagnostic(self):
        result = self.build_and_run(missing_symbol=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("libvlc_media_player_set_nsobject", result.stderr)

    def test_missing_private_runtime_does_not_use_override(self):
        result = self.build_and_run(missing_runtime=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Faltan plugins del runtime local", result.stderr)

    def test_real_loader_accepts_split_code_and_data_paths(self):
        result = self.build_and_run(split=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("VLC_RUNTIME_CHECK_OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
