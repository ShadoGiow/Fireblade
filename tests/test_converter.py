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


def equip(kind, left=False, negated=False, name="IsEquippedType"):
    c = {"condition": name, "requiredVersion": "1.0.0.0", "Type": {"value": float(kind)}, "Left hand": left}
    if negated:
        c["negated"] = True
    return c


def group(name, *members, negated=False):
    g = {"condition": name, "Conditions": list(members)}
    if negated:
        g["negated"] = True
    return g


def weapon_cond():
    """What a typical sword pack looks like: sword on the right, nothing / magic / shield on the left."""
    return [base_cond(), equip(1),
            group("OR", equip(0, left=True), equip(12, left=True), equip(16, left=True))]


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


class StripTests(unittest.TestCase):
    STRIP = conv.EQUIPMENT_CONDITIONS

    def s(self, cond, mixed=None):
        stripped = []
        kind, c = conv.simplify(cond, set(), [], self.STRIP, stripped, mixed)
        return kind, c, stripped

    def test_equipment_leaf_is_gone(self):
        kind, c, stripped = self.s(equip(1))
        self.assertEqual(kind, conv.GONE)
        self.assertEqual(stripped, ["IsEquippedType"])

    def test_negated_equipment_leaf_is_gone_not_false(self):
        # "not wearing a shield" must not disable the submod: the gate is simply opened
        self.assertEqual(self.s(equip(11, left=True, negated=True))[0], conv.GONE)

    def test_all_equipment_group_is_gone(self):
        g = group("OR", equip(0, left=True), equip(12, left=True), equip(16, left=True))
        kind, c, stripped = self.s(g)
        self.assertEqual(kind, conv.GONE)
        self.assertEqual(len(stripped), 3)

    def test_negated_all_equipment_group_is_gone_not_false(self):
        g = group("OR", equip(5), equip(6), negated=True)
        self.assertEqual(self.s(g)[0], conv.GONE)

    def test_nested_groups_all_equipment(self):
        g = group("AND", group("OR", equip(1), equip(2)), group("AND", equip(3, left=True)))
        self.assertEqual(self.s(g)[0], conv.GONE)

    def test_mixed_group_keeps_the_other_members(self):
        mixed = []
        g = group("AND", equip(1), base_cond())
        kind, c, _ = self.s(g, mixed)
        self.assertEqual(kind, conv.COND)
        self.assertEqual(c["Conditions"], [base_cond()])
        self.assertEqual(len(mixed), 1)

    def test_mixed_or_group_keeps_the_other_alternative(self):
        g = group("OR", equip(1), perk("Keep.esp"))
        kind, c, _ = self.s(g)
        self.assertEqual(kind, conv.COND)
        self.assertEqual(c["Conditions"], [perk("Keep.esp")])

    def test_negated_mixed_group_stays_negated(self):
        g = group("OR", equip(1), base_cond(), negated=True)
        kind, c, _ = self.s(g)
        self.assertEqual(kind, conv.COND)
        self.assertTrue(c["negated"])
        self.assertEqual(c["Conditions"], [base_cond()])

    def test_other_conditions_untouched(self):
        for cond in (base_cond(), {"condition": "IsWeaponDrawn"}, {"condition": "IsCombatState", "negated": True}):
            kind, c, stripped = self.s(cond)
            self.assertEqual((kind, c, stripped), (conv.COND, cond, []))

    def test_every_equipment_name_is_stripped_case_insensitively(self):
        for name in ("IsEquipped", "IsEquippedType", "IsEquippedHasKeyword", "IsEquippedHasEnchantment",
                     "IsEquippedHasEnchantmentWithKeyword", "HasBoundWeaponEquipped", "EquippedObjectWeight"):
            self.assertEqual(self.s({"condition": name})[0], conv.GONE, name)
            self.assertEqual(self.s({"condition": name.upper()})[0], conv.GONE, name)

    def test_armor_and_shouts_are_not_weapon_conditions(self):
        for name in ("IsWorn", "IsWornHasKeyword", "IsEquippedShout", "IsEquippedPower"):
            self.assertEqual(self.s({"condition": name})[0], conv.COND, name)

    def test_no_strip_set_changes_nothing(self):
        kind, c = conv.simplify(equip(1), set(), [])
        self.assertEqual((kind, c), (conv.COND, equip(1)))

    def test_plugin_drop_still_works_together_with_strip(self):
        g = group("AND", equip(1), perk(OTHER_PLUGIN), base_cond())
        kind, c = conv.simplify(g, {OTHER_PLUGIN}, [], self.STRIP, [], [])
        self.assertEqual(kind, conv.COND)
        self.assertEqual(c["Conditions"], [base_cond()])

    def test_find_gating_and_count_named(self):
        tree = [base_cond(), group("OR", equip(1), group("AND", equip(2))),
                {"condition": "TARGET", "Conditions": [equip(3)]}]
        self.assertEqual(conv.find_gating(tree, self.STRIP), ["IsEquippedType", "IsEquippedType"])
        self.assertEqual(conv.count_named(tree, self.STRIP), 3)

    def test_keep_left_keeps_only_left_hand_conditions(self):
        g = group("AND", equip(1), group("OR", equip(12, left=True), equip(13, left=True)))
        kind, c = conv.simplify(g, set(), [], self.STRIP, [], [], keep_left=True)
        self.assertEqual(kind, conv.COND)
        self.assertEqual(c["Conditions"], [group("OR", equip(12, left=True), equip(13, left=True))])

    def test_keep_left_off_strips_everything(self):
        g = group("AND", equip(1), group("OR", equip(12, left=True), equip(13, left=True)))
        self.assertEqual(conv.simplify(g, set(), [], self.STRIP, [], [])[0], conv.GONE)

    def test_find_shadowing(self):
        a = {"name": "spell", "priority": 22, "conds": {"base"}, "clips": {"run.hkx", "x.hkx"}}
        b = {"name": "plain", "priority": 2, "conds": {"base"}, "clips": {"run.hkx"}}
        c = {"name": "stricter", "priority": 2, "conds": {"base", "drawn"}, "clips": {"x.hkx"}}
        d = {"name": "apart", "priority": 2, "conds": {"base"}, "clips": {"y.hkx"}}
        e = {"name": "higher", "priority": 99, "conds": {"base", "drawn"}, "clips": {"run.hkx"}}
        lines = conv.find_shadowing([a, b, c, d, e])
        self.assertEqual(len(lines), 2, lines)
        self.assertIn("'plain' loses those clips", lines[0])
        self.assertIn("--variant spell", lines[0])
        self.assertIn("'stricter' loses those clips", lines[1])   # a asks for less than c
        # "higher" needs more conditions and has more priority: it is the intended override, not a shadow

    def test_find_shadowing_identical_pair_reported_once(self):
        a = {"name": "a", "priority": 5, "conds": {"c"}, "clips": {"r.hkx"}}
        b = {"name": "b", "priority": 5, "conds": {"c"}, "clips": {"r.hkx"}}
        self.assertEqual(len(conv.find_shadowing([a, b])), 1)

    def test_input_is_not_mutated(self):
        g = group("AND", equip(1), group("OR", equip(2), base_cond()))
        before = json.dumps(g)
        self.s(g)
        self.assertEqual(json.dumps(g), before)


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

    def read_cfg(self, out, sub, name="Test Mod"):
        return json.loads((out / OAR / f"{name} [SwapMoveset]" / sub / "config.json").read_text())

    def test_weapon_conditions_removed_by_default(self):
        src = self.root / "src"
        make_mod(src, {"a": {"name": "a", "conditions": weapon_cond()}})
        rc, so, se, out = self.convert(src)
        self.assertEqual(rc, 0, se)
        cfg = self.read_cfg(out, "a")
        self.assertEqual(cfg["conditions"], [conv.our_condition("test_mod"), base_cond()])
        self.assertNotIn("disabled", cfg)
        self.assertIn("IsEquippedType x4", so)
        self.assertIn("any weapon", so)

    def test_submod_with_only_weapon_conditions_keeps_only_ours(self):
        src = self.root / "src"
        make_mod(src, {"a": {"name": "a", "conditions": [equip(1), group("OR", equip(0, left=True), equip(11, left=True))]}})
        rc, so, se, out = self.convert(src)
        self.assertEqual(rc, 0, se)
        cfg = self.read_cfg(out, "a")
        self.assertEqual(cfg["conditions"], [conv.our_condition("test_mod")])
        self.assertNotIn("disabled", cfg)

    def test_negated_weapon_condition_does_not_disable_the_submod(self):
        src = self.root / "src"
        make_mod(src, {"a": {"name": "a", "conditions": [base_cond(), equip(11, left=True, negated=True)]}})
        rc, so, se, out = self.convert(src)
        self.assertEqual(rc, 0, se)
        cfg = self.read_cfg(out, "a")
        self.assertNotIn("disabled", cfg)
        self.assertEqual(len(cfg["conditions"]), 2)

    def test_other_conditions_survive_stripping(self):
        src = self.root / "src"
        keep = [base_cond(), {"condition": "IsWeaponDrawn", "requiredVersion": "1.0.0.0"},
                {"condition": "IsCombatState", "negated": True, "Combat state": {"value": 1.0}}]
        make_mod(src, {"a": {"name": "a", "conditions": keep[:1] + [equip(1)] + keep[1:]}})
        rc, so, se, out = self.convert(src)
        self.assertEqual(rc, 0, se)
        self.assertEqual(self.read_cfg(out, "a")["conditions"][1:], keep)

    def test_keep_equipment_conditions_opt_out(self):
        src = self.root / "src"
        make_mod(src, {"a": {"name": "a", "conditions": weapon_cond()}})
        rc, so, se, out = self.convert(src, "--keep-equipment-conditions")
        self.assertEqual(rc, 0, se)
        self.assertEqual(self.read_cfg(out, "a")["conditions"][1:], weapon_cond())
        self.assertIn("KEPT", so)

    def test_extra_strip_condition(self):
        src = self.root / "src"
        worn = {"condition": "IsWorn", "requiredVersion": "1.0.0.0"}
        make_mod(src, {"a": {"name": "a", "conditions": [base_cond(), worn]}})
        rc, so, se, out = self.convert(src, "--strip-condition", "isworn")
        self.assertEqual(rc, 0, se)
        self.assertEqual(self.read_cfg(out, "a")["conditions"][1:], [base_cond()])

    def test_equipment_inside_target_is_kept_and_reported(self):
        src = self.root / "src"
        target = {"condition": "TARGET", "requiredVersion": "1.0.0.0", "Conditions": [equip(1)]}
        make_mod(src, {"a": {"name": "a", "conditions": [equip(1), target]}})
        rc, so, se, out = self.convert(src)
        self.assertEqual(rc, 0, se)
        self.assertEqual(self.read_cfg(out, "a")["conditions"][1:], [target])
        self.assertIn("1 equipment condition(s) sit inside other conditions", so)

    def test_stripped_equipment_does_not_count_as_a_required_plugin(self):
        src = self.root / "src"
        ring = {"condition": "IsEquipped", "requiredVersion": "1.0.0.0",
                "Form": {"pluginName": OTHER_PLUGIN, "formID": "801"}}
        make_mod(src, {"a": {"name": "a", "conditions": [base_cond(), ring]}})
        rc, so, se, out = self.convert(src)
        self.assertEqual(rc, 0, se)
        self.assertNotIn(OTHER_PLUGIN, so)

    def test_weapon_stripping_with_drop_plugin(self):
        src = self.root / "src"
        make_mod(src, {"a": {"name": "a", "conditions": weapon_cond() + [perk(OTHER_PLUGIN)]}})
        rc, so, se, out = self.convert(src, "--drop-plugin", OTHER_PLUGIN)
        self.assertEqual(rc, 0, se)
        self.assertEqual(self.read_cfg(out, "a")["conditions"], [conv.our_condition("test_mod"), base_cond()])

    def test_verify_catches_a_leftover_equipment_condition(self):
        src = self.root / "src"
        mod = make_mod(src, {"a": {"name": "a", "conditions": [equip(1)]}})
        out = self.root / "out"
        stats = conv.convert_mod(mod, out, Path(OAR), "test_mod", "Test Mod", "x", set(), [], conv.EQUIPMENT_CONDITIONS)
        self.assertEqual(conv.verify(stats, "test_mod", set(), mod), [])
        cfg_path = stats["dest"] / "a" / "config.json"
        cfg = json.loads(cfg_path.read_text())
        cfg["conditions"].append(group("OR", equip(2)))
        cfg_path.write_text(json.dumps(cfg))
        problems = conv.verify(stats, "test_mod", set(), mod)
        self.assertTrue(any("equipment condition" in p for p in problems), problems)

    def two_variant_mod(self):
        src = self.root / "src"
        mod = make_mod(src, {
            "plain": {"name": "plain", "priority": 2, "conditions": weapon_cond()},
            "spell": {"name": "spell", "priority": 22,
                      "conditions": [base_cond(), equip(1), group("OR", equip(12, left=True), equip(13, left=True))]},
        })
        for sub in ("plain", "spell"):
            (mod / sub / "run.hkx").write_bytes(sub.encode() * 7)   # a clip both of them replace
        return src

    def test_shadowing_is_reported_without_variant(self):
        src = self.two_variant_mod()
        rc, so, se, out = self.convert(src)
        self.assertEqual(rc, 0, se)
        self.assertIn("WARNING", so)
        self.assertIn("'plain' loses those clips", so)
        self.assertIn("--variant spell", so)

    def test_variant_keeps_left_hand_condition_only(self):
        src = self.two_variant_mod()
        rc, so, se, out = self.convert(src, "--variant", "Spell")
        self.assertEqual(rc, 0, se)
        spell = self.read_cfg(out, "spell")["conditions"]
        self.assertEqual(spell, [conv.our_condition("test_mod"), base_cond(),
                                 group("OR", equip(12, left=True), equip(13, left=True))])
        self.assertEqual(self.read_cfg(out, "plain")["conditions"], [conv.our_condition("test_mod"), base_cond()])
        self.assertNotIn("WARNING", so)
        self.assertIn("variant submods", so)

    def test_unknown_variant_is_an_error(self):
        src = self.two_variant_mod()
        rc, so, se, out = self.convert(src, "--variant", "nope")
        self.assertEqual(rc, 1)
        self.assertIn("nope", se)
        self.assertFalse(out.exists())

    def test_variant_left_hand_conditions_are_not_reported_as_nested(self):
        src = self.two_variant_mod()
        rc, so, se, out = self.convert(src, "--variant", "spell")
        self.assertEqual(rc, 0, se)
        self.assertNotIn("sit inside other conditions", so)

    def test_verify_accepts_left_hand_conditions_in_a_variant(self):
        src = self.two_variant_mod()
        mod = src / OAR / "Test Mod"
        out = self.root / "out"
        stats = conv.convert_mod(mod, out, Path(OAR), "test_mod", "Test Mod", "x", set(), [],
                                 conv.EQUIPMENT_CONDITIONS, frozenset({"spell"}))
        self.assertEqual(conv.verify(stats, "test_mod", set(), mod), [])

    def test_slugify(self):
        self.assertEqual(conv.slugify("Cool Blade"), "cool_blade")
        self.assertEqual(conv.slugify("  !! "), "pack")
        self.assertTrue(conv.ID_RE.match(conv.slugify("x" * 100)))


if __name__ == "__main__":
    unittest.main(verbosity=1)
