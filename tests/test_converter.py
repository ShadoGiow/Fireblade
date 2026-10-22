"""Tests for tools/swapmoveset_convert.py  (python3 -I tests/test_converter.py)"""
import hashlib
import io
import json
import sys
import tempfile
import unittest
import zipfile
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import swapmoveset_convert as conv  # noqa: E402

OAR = "meshes/actors/character/animations/OpenAnimationReplacer"
OTHER_PLUGIN = "Some Other Mod.esp"


def perk(plugin, negated=False):
    c = {"condition": "HasPerk", "requiredVersion": "1.0.0.0", "Perk": {"pluginName": plugin, "formID": "42518"}}
    if negated:
        c["negated"] = True
    return c


def base_cond():
    return {"condition": "IsActorBase", "requiredVersion": "1.0.0.0",
            "Actor base": {"pluginName": "Skyrim.esm", "formID": "7"}}


def make_mod(root: Path, submods: dict, name="Test Mod"):
    mod = root / OAR / name
    mod.mkdir(parents=True)
    (mod / "config.json").write_text(json.dumps({"name": name, "author": "someone", "description": "d"}))
    (root / "meta.ini").write_text("[General]\n")
    for sub, cfg in submods.items():
        d = mod / sub
        d.mkdir(parents=True, exist_ok=True)
        (d / "config.json").write_text(json.dumps(cfg))
        (d / "anim.hkx").write_bytes(sub.encode() * 50)
    return mod


def run(argv):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        rc = conv.main(argv)
    return rc, out.getvalue(), err.getvalue()


class SimplifyTests(unittest.TestCase):
    def s(self, cond, plugins=(OTHER_PLUGIN,)):
        return conv.simplify(cond, set(plugins), [])

    def test_plain_condition_untouched(self):
        kind, c = self.s(base_cond())
        self.assertEqual(kind, conv.COND)
        self.assertEqual(c, base_cond())

    def test_dropped_leaf_is_true(self):
        self.assertEqual(self.s(perk(OTHER_PLUGIN))[0], conv.TRUE)

    def test_negated_dropped_leaf_is_false(self):
        self.assertEqual(self.s(perk(OTHER_PLUGIN, negated=True))[0], conv.FALSE)

    def test_case_insensitive_plugin_name(self):
        self.assertEqual(self.s(perk(OTHER_PLUGIN.upper()))[0], conv.TRUE)

    def test_or_with_dropped_member_is_true(self):
        or_ = {"condition": "OR", "Conditions": [base_cond(), perk(OTHER_PLUGIN)]}
        self.assertEqual(self.s(or_)[0], conv.TRUE)

    def test_and_loses_only_the_dropped_member(self):
        and_ = {"condition": "AND", "Conditions": [base_cond(), perk(OTHER_PLUGIN)]}
        kind, c = self.s(and_)
        self.assertEqual(kind, conv.COND)
        self.assertEqual(c["Conditions"], [base_cond()])

    def test_and_with_false_member_is_false(self):
        and_ = {"condition": "AND", "Conditions": [base_cond(), perk(OTHER_PLUGIN, negated=True)]}
        self.assertEqual(self.s(and_)[0], conv.FALSE)

    def test_negated_group_that_became_true_is_false(self):
        or_ = {"condition": "OR", "negated": True, "Conditions": [base_cond(), perk(OTHER_PLUGIN)]}
        self.assertEqual(self.s(or_)[0], conv.FALSE)

    def test_nested(self):
        inner = {"condition": "OR", "Conditions": [perk(OTHER_PLUGIN), base_cond()]}
        outer = {"condition": "AND", "Conditions": [inner, base_cond()]}
        kind, c = self.s(outer)
        self.assertEqual(kind, conv.COND)
        self.assertEqual(c["Conditions"], [base_cond()])  # the OR became true and vanished

    def test_input_is_not_mutated(self):
        and_ = {"condition": "AND", "Conditions": [base_cond(), perk(OTHER_PLUGIN)]}
        before = json.dumps(and_)
        self.s(and_)
        self.assertEqual(json.dumps(and_), before)

    def test_no_plugins_means_nothing_dropped(self):
        self.assertEqual(self.s(perk(OTHER_PLUGIN), plugins=())[0], conv.COND)


class ConvertTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def convert(self, mod_root, *extra):
        out = self.root / "out"
        rc, so, se = run([str(mod_root), "-o", str(out), *extra])
        return rc, so, se, out

    def test_basic_conversion(self):
        src = self.root / "src"
        make_mod(src, {"idle": {"name": "idle", "priority": 5, "conditions": [base_cond()]},
                       "run": {"name": "run", "priority": 6}}, name="Cool Blade")
        rc, so, se, out = self.convert(src, "--zip", str(self.root / "p.zip"))
        self.assertEqual(rc, 0, se)
        dest = out / OAR / "Cool Blade [SwapMoveset]"
        for sub in ("idle", "run"):
            cfg = json.loads((dest / sub / "config.json").read_text())
            self.assertEqual(cfg["conditions"][0], conv.our_condition("cool_blade"))
        self.assertEqual(json.loads((dest / "idle" / "config.json").read_text())["conditions"][1], base_cond())
        self.assertEqual(len(json.loads((dest / "run" / "config.json").read_text())["conditions"]), 1)
        manifest = json.loads((out / "SKSE/Plugins/SwapMoveset/Packs/cool_blade.json").read_text())
        self.assertEqual(manifest["id"], "cool_blade")
        self.assertEqual(manifest["name"], "Cool Blade")
        self.assertFalse((out / "meta.ini").exists())
        self.assertEqual(hashlib.sha256((dest / "idle/anim.hkx").read_bytes()).digest(),
                         hashlib.sha256((src / OAR / "Cool Blade/idle/anim.hkx").read_bytes()).digest())
        names = zipfile.ZipFile(self.root / "p.zip").namelist()
        self.assertTrue(all(n.startswith(("meshes/", "SKSE/")) for n in names), names)

    def test_input_untouched(self):
        src = self.root / "src"
        make_mod(src, {"idle": {"name": "idle", "conditions": [base_cond()]}})
        before = {p.relative_to(src): p.read_bytes() for p in src.rglob("*") if p.is_file()}
        self.assertEqual(self.convert(src)[0], 0)
        after = {p.relative_to(src): p.read_bytes() for p in src.rglob("*") if p.is_file()}
        self.assertEqual(before, after)

    def test_drop_plugin(self):
        src = self.root / "src"
        make_mod(src, {"a": {"name": "a", "conditions": [base_cond(), perk(OTHER_PLUGIN)]}})
        rc, so, se, out = self.convert(src, "--drop-plugin", OTHER_PLUGIN)
        self.assertEqual(rc, 0, se)
        cfg = json.loads((out / OAR / "Test Mod [SwapMoveset]/a/config.json").read_text())
        self.assertEqual([c["condition"] for c in cfg["conditions"]], [conv.CONDITION_NAME, "IsActorBase"])
        self.assertIn("HasPerk x1", so)

    def test_without_drop_plugin_the_condition_stays(self):
        src = self.root / "src"
        make_mod(src, {"a": {"name": "a", "conditions": [perk(OTHER_PLUGIN)]}})
        rc, so, se, out = self.convert(src)
        cfg = json.loads((out / OAR / "Test Mod [SwapMoveset]/a/config.json").read_text())
        self.assertEqual(len(cfg["conditions"]), 2)
        self.assertIn(OTHER_PLUGIN, so)  # reported as a requirement

    def test_impossible_submod_is_disabled(self):
        src = self.root / "src"
        make_mod(src, {"a": {"name": "a", "conditions": [perk(OTHER_PLUGIN, negated=True)]}})
        rc, so, se, out = self.convert(src, "--drop-plugin", OTHER_PLUGIN)
        self.assertEqual(rc, 0, se)
        cfg = json.loads((out / OAR / "Test Mod [SwapMoveset]/a/config.json").read_text())
        self.assertTrue(cfg["disabled"])

    def test_legacy_dar_folder_is_refused(self):
        src = self.root / "src"
        mod = make_mod(src, {"a": {"name": "a"}})
        (mod / "old").mkdir()
        (mod / "old" / "_conditions.txt").write_text("IsFemale")
        rc, so, se, out = self.convert(src)
        self.assertEqual(rc, 1)
        self.assertIn("legacy", se)
        self.assertFalse(out.exists())

    def test_user_json_is_skipped(self):
        src = self.root / "src"
        mod = make_mod(src, {"a": {"name": "a"}})
        (mod / "a" / "user.json").write_text("{}")
        rc, so, se, out = self.convert(src)
        self.assertEqual(rc, 0, se)
        self.assertFalse((out / OAR / "Test Mod [SwapMoveset]/a/user.json").exists())

    def test_bad_pack_id_rejected(self):
        src = self.root / "src"
        make_mod(src, {"a": {"name": "a"}})
        rc, so, se, out = self.convert(src, "--id", "Bad Id!")
        self.assertEqual(rc, 2)

    def test_output_must_be_empty(self):
        src = self.root / "src"
        make_mod(src, {"a": {"name": "a"}})
        out = self.root / "out"
        out.mkdir()
        (out / "x").write_text("x")
        rc, so, se = run([str(src), "-o", str(out)])
        self.assertEqual(rc, 2)

    def test_two_oar_mods_refused(self):
        src = self.root / "src"
        make_mod(src, {"a": {"name": "a"}}, name="One")
        (src / OAR / "Two").mkdir()
        rc, so, se, out = self.convert(src)
        self.assertEqual(rc, 2)

    def test_bom_and_unicode(self):
        src = self.root / "src"
        mod = make_mod(src, {"a": {"name": "a"}})
        (mod / "a" / "config.json").write_bytes(b"\xef\xbb\xbf" + json.dumps({"name": "café", "priority": 1}).encode())
        rc, so, se, out = self.convert(src)
        self.assertEqual(rc, 0, se)
        cfg = json.loads((out / OAR / "Test Mod [SwapMoveset]/a/config.json").read_text(encoding="utf-8"))
        self.assertEqual(cfg["name"], "café")

    def test_slugify(self):
        self.assertEqual(conv.slugify("Demara Blade"), "demara_blade")
        self.assertEqual(conv.slugify("  !! "), "pack")
        self.assertTrue(conv.ID_RE.match(conv.slugify("x" * 100)))


if __name__ == "__main__":
    unittest.main(verbosity=1)
