"""Compact binding contracts; native loop cases each run in their own process."""
import argparse
import gc
import importlib
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest

parser = argparse.ArgumentParser()
parser.add_argument("--fixtures", type=Path, required=True)
parser.add_argument("--module-dir", type=Path)
parser.add_argument("--native", choices=("all", "normal", "callback", "event_callback", "thread"))
parser.add_argument("--expect-graphics", choices=("on", "off"), help="Check the installed wheel's compiled renderer configuration.")
args, remaining = parser.parse_known_args()
if args.module_dir:
    sys.path.insert(0, str(args.module_dir))
    sys.modules["minvex._native"] = importlib.import_module("_native")
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import minvex as mvx


class BindingTests(unittest.TestCase):
    def error(self, code, function):
        with self.assertRaises(mvx.Error) as raised:
            function()
        self.assertEqual(raised.exception.code, code)
        self.assertTrue(raised.exception.message)
        self.assertIsInstance(raised.exception.hint, str)
        return raised.exception

    def test_scene_material_and_unicode(self):
        with tempfile.TemporaryDirectory(prefix="minvex 한글 ") as folder:
            path = Path(folder) / "모델.glb"
            shutil.copyfile(args.fixtures / "SharedBox.glb", path)
            with mvx.Viewer() as viewer:
                model = viewer.add_model(path)
                parent = viewer.scene.create_object("부모")
                model.set_parent(parent.id)
                transform = mvx.Transform(position=mvx.Vec3(2, 3, 4))
                model.transform = transform
                transform.position.x = 100
                self.assertEqual(model.transform.position.x, 2)
                model.name = "모델"
                material = model.get_material()
                material.shading = mvx.ShadingModel.Pbr
                material.metallic_factor = .7
                model.set_material(0, material)
                material.metallic_factor = 0
                self.assertAlmostEqual(model.get_material().metallic_factor, .7)
                snapshot = viewer.scene.objects
                viewer.scene.save(Path(folder) / "Scene.mvxscene.json")
                self.assertEqual(viewer.scene.size, 2)
                parent.remove()
                self.error(mvx.ErrorCode.InvalidHandle, lambda: model.name)
                self.error(mvx.ErrorCode.InvalidHandle, lambda: snapshot[0].transform)
                viewer.scene.load(Path(folder) / "Scene.mvxscene.json")
                self.assertEqual(viewer.scene.size, 2)
                self.assertIn("모델", [obj.name for obj in viewer.scene.objects])
            self.error(mvx.ErrorCode.InvalidState, lambda: viewer.scene)
            viewer.close()  # Idempotent, owner thread only.

    def test_scene_with_directory_symlink(self):
        with tempfile.TemporaryDirectory(prefix="minvex links ") as folder:
            root = Path(folder).resolve()
            project = root / "physical" / "nested" / "project"
            (project / "assets").mkdir(parents=True)
            (project / "textures").mkdir()
            shutil.copyfile(args.fixtures / "SharedBox.glb", project / "assets" / "model.glb")
            alias = root / "alias"
            if sys.platform == "win32":
                # A junction exercises directory aliases without symlink privilege.
                import _winapi
                _winapi.CreateJunction(str(project), str(alias))
            else:
                alias.symlink_to(project, target_is_directory=True)
            file = alias / "Scene.mvxscene.json"
            settings = mvx.ImportSettings()
            settings.texture_directory = alias / "textures"
            with mvx.Viewer() as viewer:
                viewer.add_model(alias / "assets" / "model.glb", import_settings=settings)
                viewer.scene.save(file)
            saved = json.loads(file.read_text(encoding="utf-8"))["assets"][0]
            self.assertEqual(saved["path"], "assets/model.glb")
            self.assertEqual(saved["import"]["textureDirectory"], "textures")
            # Use fresh registries, so a cached import cannot hide a broken path.
            for scene in (file, project / file.name):
                with self.subTest(scene=scene), mvx.Viewer() as viewer:
                    viewer.scene.load(scene)
                    self.assertEqual(viewer.scene.size, 1)
            # Relative references must also survive moving the physical project.
            moved = root / "moved"
            project.rename(moved)
            with mvx.Viewer() as viewer:
                viewer.scene.load(moved / file.name)
                self.assertEqual(viewer.scene.size, 1)

    def test_animation_morph_and_tree(self):
        with mvx.Viewer() as viewer:
            model = viewer.add_model(args.fixtures / "MorphRibbon.gltf")
            self.error(mvx.ErrorCode.InvalidArgument, lambda: model.play_animation(-1))
            with self.assertRaises(TypeError):
                model.play_animation(None)
            model.play_animation("Bend")
            model.animation_loop = False
            model.seek_animation(.5)
            model.pause_animation()
            self.assertEqual(model.playback_state, mvx.PlaybackState.Paused)
            model.animation_speed = -2
            model.resume_animation()
            viewer.scene.update_animations(.1)
            self.assertAlmostEqual(model.animation_time, .3)
            nodes = model.morph_nodes
            self.assertEqual(len(nodes), 2)
            a, b = (node["node"] for node in nodes)
            model.set_morph_weights(a, [.8, .2])
            self.assertAlmostEqual(model.get_morph_weights(a)[0], .8)
            self.assertNotEqual(model.get_morph_weights(a), model.get_morph_weights(b))
            self.error(mvx.ErrorCode.InvalidArgument, lambda: model.set_morph_weights(a, [math.nan, 0]))
            desc = mvx.BlendTreeDesc()
            desc.samples = [mvx.BlendSample(0, 0), mvx.BlendSample(1, 1)]
            tree = model.create_blend_tree(desc)
            model.play_blend_tree(tree)
            model.set_blend_parameter("Speed", .25)
            model.seek_blend_tree(.5)
            self.assertEqual(model.blend_weights, [.75, .25])
            self.error(mvx.ErrorCode.InvalidArgument, lambda: model.set_blend_parameter("Missing", 1))
            rig = viewer.add_model(args.fixtures / "AnimatedRibbon.gltf")
            rig.play_animation(0)
            reference = rig.reference_pose
            skeleton = rig.skeleton_nodes
            self.assertEqual([n["node"] for n in skeleton], list(range(len(skeleton))))
            self.assertIsNone(skeleton[0]["parent"])
            self.assertIn("name", skeleton[2])
            joints = reference.joints
            joints[2].local.scale.x = 1.25
            reference.joints = joints
            rig.set_additive_animation(2, .5, reference=reference)
            rig.seek_additive(1)
            rig.pause_animation()
            self.assertAlmostEqual(rig.capture_pose().joints[2].local.scale.x, 1.1, places=5)
            snapshot = rig.additive
            snapshot.weight = .1
            self.assertEqual(rig.additive.weight, .5)  # Value snapshots never mutate native state.
            self.assertAlmostEqual(rig.additive.reference.joints[2].local.scale.x, 1.25)
            self.error(mvx.ErrorCode.InvalidArgument, lambda: rig.set_additive_mask([1]))
            self.error(mvx.ErrorCode.InvalidArgument, lambda: rig.set_additive_weight(2))
            self.assertEqual(rig.additive.weight, .5)
            rig.set_additive_mask([0] * len(skeleton))
            self.assertAlmostEqual(rig.capture_pose().joints[2].local.scale.x, 1)
            rig.set_additive_mask([])
            state = rig.additive
            state.time, state.speed, state.loop, state.state = 1.9, 2, False, mvx.PlaybackState.Playing
            rig.restore_additive_animation(state)
            viewer.scene.update_animations(.2)
            self.assertEqual(rig.additive.time, 2)
            self.assertEqual(rig.additive.state, mvx.PlaybackState.Stopped)
            invalid = rig.additive
            invalid.time = 99
            self.error(mvx.ErrorCode.InvalidArgument, lambda: rig.restore_additive_animation(invalid))
            self.assertEqual(rig.additive.time, 2)
            rig.apply_pose(reference)
            self.assertAlmostEqual(rig.capture_pose().joints[2].local.scale.x, 1.25)
            rig.reset_pose()
            self.assertIsNone(rig.additive)
            rig.set_additive_animation(2)
            rig.set_additive_weight(.4)
            rig.clear_additive_animation()
            self.assertIsNone(rig.additive)

    def test_events_and_python_exception(self):
        with mvx.Viewer() as viewer:
            model = viewer.add_model(args.fixtures / "AnimatedRibbon.gltf")
            model.set_animation_events(0, [mvx.AnimationEvent(.2, "step", "왼발")])
            model.play_animation(0)
            model.seek_animation(.4, collect_events=True)
            events = viewer.poll_animation_events()
            self.assertEqual([(e.object, e.occurrence.event.payload) for e in events], [(model.id, "왼발")])
            model.seek_animation(0)
            model.seek_animation(.4, collect_events=True)
            saved = []
            self.assertEqual(viewer.dispatch_animation_events(saved.append), 1)
            model.set_animation_events(0, [])
            self.assertEqual(saved[0].occurrence.event.name, "step")  # Owned callback snapshot.
            model.set_animation_events(0, [mvx.AnimationEvent(.2, "step")])
            model.seek_animation(0)
            model.seek_animation(.4, collect_events=True)
            def broken(event):
                self.error(mvx.ErrorCode.InvalidState, viewer.close)
                raise ValueError("callback detail")
            failure = self.error(mvx.ErrorCode.CallbackFailure, lambda: viewer.dispatch_animation_events(broken))
            self.assertIsInstance(failure.__cause__, ValueError)
            self.assertIn("callback detail", str(failure.__cause__))
            self.assertEqual(viewer.poll_animation_events(), [])

    def test_lifetime_thread_and_errors(self):
        viewer = mvx.Viewer()
        obj = viewer.add_model(args.fixtures / "SharedBox.glb")
        scene = viewer.scene
        failures = []
        def worker():
            failures.append(self.error(mvx.ErrorCode.WrongThread, lambda: obj.transform).code)
            failures.append(self.error(mvx.ErrorCode.WrongThread, viewer.close).code)
            viewer.request_close()
            self.assertFalse(viewer.is_running)
        thread = threading.Thread(target=worker)
        thread.start()
        thread.join(timeout=5)
        self.assertFalse(thread.is_alive())
        self.assertEqual(len(failures), 2)
        error = self.error(mvx.ErrorCode.AssetLoad, lambda: viewer.add_model(args.fixtures / "missing.glb"))
        self.assertIn("missing.glb", error.source_path)
        del viewer
        gc.collect()
        self.error(mvx.ErrorCode.InvalidState, lambda: obj.transform)
        self.error(mvx.ErrorCode.InvalidState, lambda: scene.size)

    def test_quality_and_cpu_runtime(self):
        if args.expect_graphics is not None:
            self.assertEqual(bool(mvx.graphics_available), args.expect_graphics == "on",
                             "Installed wheel renderer configuration differs from the requested build")
        with mvx.Viewer() as viewer, tempfile.TemporaryDirectory() as folder:
            quality = mvx.make_quality_profile(mvx.QualityPreset.Medium)
            quality.overrides.shadow = mvx.ShadowMode.Pcss
            quality.overrides.shadow_source_angle = 2
            viewer.set_quality(quality)
            self.assertEqual(viewer.quality.requested_settings.shadow, mvx.ShadowMode.Pcss)
            quality.overrides.shadow_blocker_samples = 0
            self.error(mvx.ErrorCode.InvalidArgument, lambda: viewer.set_quality(quality))
            self.assertEqual(viewer.quality.requested_settings.shadow_blocker_samples, 16)
            file = Path(folder) / "Quality.mvxquality.json"
            mvx.save_quality(viewer.quality.requested, file)
            self.assertEqual(mvx.expand_quality(mvx.load_quality(file)).shadow_source_angle, 2)
            if not mvx.graphics_available:
                self.error(mvx.ErrorCode.UnsupportedFeature, lambda: viewer.run(frame_limit=1))


def native_case(mode):
    options = mvx.ViewerOptions()
    options.quality = mvx.make_quality_profile(mvx.QualityPreset.Low)
    options.quality.overrides.target_fps = 0
    with mvx.Viewer(options) as viewer:
        model = viewer.add_model(args.fixtures / "MorphRibbon.gltf")
        model.play_animation(0)
        model.set_animation_events(0, [mvx.AnimationEvent(.2, "step")])
        frames, events = [], []
        first_frame = threading.Event()
        stopped = threading.Event()
        thread = None
        if mode == "thread":
            def close_window():
                if first_frame.wait(timeout=3) and viewer.is_running:
                    viewer.request_close()
                    stopped.set()
            thread = threading.Thread(target=close_window)
            thread.start()
        def on_frame(current, info):
            frames.append(info)
            assert current is viewer
            if mode == "callback":
                raise LookupError("frame failure")
            if info.frame_number == 0:
                quality = mvx.make_quality_profile(mvx.QualityPreset.Medium)
                quality.overrides.target_fps = 0
                quality.overrides.pbr = True
                quality.overrides.shadow = mvx.ShadowMode.Pcss
                viewer.set_quality(quality)
                assert viewer.quality.effective.pbr
                material = model.get_material()
                material.shading = mvx.ShadingModel.Pbr
                material.roughness_factor = .3
                model.set_material(0, material)
                model.seek_animation(.4, collect_events=True)
                model.set_morph_weights(3, [.8, .2])
                model.set_additive_animation(1, .5)
                model.seek_additive(.5)
                assert model.additive.weight == .5
                first_frame.set()
        def on_event(event):
            if mode == "event_callback":
                raise LookupError("event failure")
            events.append(event)
        try:
            viewer.run(frame_limit=0 if mode == "thread" else 3, on_frame=on_frame, on_event=on_event, advance_animations=False)
        except mvx.Error as error:
            assert mode in ("callback", "event_callback") and error.code == mvx.ErrorCode.CallbackFailure
            assert isinstance(error.__cause__, LookupError)
        else:
            assert mode not in ("callback", "event_callback")
            assert events[0].occurrence.event.name == "step"
            assert events[0].object == model.id
        assert frames[0].frame_number == 0  # Still valid after native Run stack cleanup.
        assert not viewer.is_running
        if thread:
            thread.join(timeout=3)
            assert stopped.is_set() and not thread.is_alive(), "Worker did not run while native Run released the GIL"
    print("native", mode, "passed")


if __name__ == "__main__":
    if args.native == "all":
        for mode in ("normal", "callback", "event_callback", "thread"):
            command = [sys.executable, __file__, "--fixtures", str(args.fixtures.resolve()), "--native", mode]
            if args.module_dir:
                command += ["--module-dir", str(args.module_dir.resolve())]
            subprocess.run(command, check=True, timeout=20)
    elif args.native:
        native_case(args.native)
    else:
        unittest.main(argv=[sys.argv[0], *remaining])
