#!/usr/bin/env python3
"""
Converts an Open Animation Replacer (OAR) animation mod into a Swap Moveset pack.

What it does
  1. Copies the mod's OAR folder (meshes/.../OpenAnimationReplacer/<Name>/) to "<Name> [SwapMoveset]".
  2. Adds the SwapMoveset_PackActive condition to every submod config.json, so the animations only play
     while the Swap Moveset mode is on with this pack.
  3. Removes the conditions that tie the moveset to a particular weapon or hand equipment
     (IsEquippedType, IsEquipped, IsEquippedHasKeyword, ...), so the moveset works with any weapon, or with
     none. This is the default because a swapped moveset has to work for everything the player carries.
     --keep-equipment-conditions keeps them, --strip-condition NAME removes more kinds of condition.
     A removed condition is deleted; a group (AND / OR) left empty by that is deleted with it.
     --variant NAME marks a submod as a variant picked by the item in the OFF hand (for example "magic in
     the left hand"): its conditions on the right hand go, the ones on the left hand stay. Without it such
     a submod would win over the plain one in every situation; the converter warns when that happens.
  4. Removes conditions that depend on other plugins you do not want to require (--drop-plugin, nothing
     is removed unless you ask). A removed condition is treated as "satisfied".
  5. Writes the pack manifest  SKSE/Plugins/SwapMoveset/Packs/<id>.json .
  6. Copies the .hkx files byte for byte and checks them with SHA-256.
  7. Re-reads everything it wrote and checks it (valid JSON, our condition first, hashes equal, no
     equipment condition left in the AND/OR tree).

OAR replaces an animation by the name of the original clip. Dropping the equipment conditions makes the
pack apply whenever the game plays one of the clips it contains; it cannot add clips it does not contain.

The input is never modified. Output files sit directly at the root of the output folder / zip
(meshes/..., SKSE/...), with no container folder, so a mod manager installs them as they are.

Usage
  python3 -I swapmoveset_convert.py <mod folder> -o <output folder> [--zip pack.zip]
          [--id my_pack] [--name "My Pack"] [--author someone] [--order 100]
          [--drop-plugin "Some Mod.esp"] [--keep-equipment-conditions] [--strip-condition Name]
          [--variant submod_folder] [--notes "text"]

Only the Python standard library is used.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
import zipfile
from pathlib import Path

CONDITION_NAME = "SwapMoveset_PackActive"
PLUGIN_NAME = "SwapMoveset"
PLUGIN_VERSION = "1.0.0.0"
SUFFIX = " [SwapMoveset]"
ID_RE = re.compile(r"^[a-z0-9_-]{1,48}$")

# OAR conditions that tie an animation set to the weapon / item in the player's hands. Compared in lower case.
EQUIPMENT_CONDITIONS = frozenset(n.lower() for n in (
    "IsEquipped",
    "IsEquippedType",
    "IsEquippedHasKeyword",
    "IsEquippedHasEnchantment",
    "IsEquippedHasEnchantmentWithKeyword",
    "HasBoundWeaponEquipped",
    "EquippedObjectWeight",
))


class ConvertError(Exception):
    pass


# ----------------------------------------------------------------------------------------------------------
# helpers
# ----------------------------------------------------------------------------------------------------------
def slugify(text: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")
    return slug[:48] or "pack"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_json(path: Path):
    # utf-8-sig: some mod authors save with a BOM
    with path.open("r", encoding="utf-8-sig") as f:
        return json.load(f)


def write_json(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=4, ensure_ascii=False)
        f.write("\n")


def find_oar_mods(root: Path) -> list[Path]:
    """Every folder that sits directly inside a directory called OpenAnimationReplacer."""
    found = []
    for oar in sorted(root.rglob("*")):
        if oar.is_dir() and not oar.is_symlink() and oar.name.lower() == "openanimationreplacer":
            for child in sorted(oar.iterdir()):
                if child.is_dir() and not child.is_symlink():
                    found.append(child)
    return found


# ----------------------------------------------------------------------------------------------------------
# condition rewriting
# ----------------------------------------------------------------------------------------------------------
TRUE, FALSE, COND, GONE = "true", "false", "cond", "gone"


def references_plugin(cond: dict, plugins: set[str]) -> bool:
    """True if this condition itself (not its children) names one of the plugins."""
    wanted = {p.lower() for p in plugins}

    def walk(node) -> bool:
        if isinstance(node, dict):
            name = node.get("pluginName")
            if isinstance(name, str) and name.lower() in wanted:
                return True
            return any(walk(v) for k, v in node.items() if k != "Conditions")
        if isinstance(node, list):
            return any(walk(v) for v in node)
        return False

    return walk(cond)


def simplify(cond, plugins: set[str], report: list[str], strip: frozenset[str] = frozenset(),
             stripped: list[str] | None = None, mixed: list[str] | None = None, keep_left: bool = False):
    """
    Returns (kind, condition). kind is TRUE / FALSE (the condition became a constant), GONE (the condition
    was removed on purpose, it is simply not there any more) or COND.

    A condition whose name is in `strip` (lower case) is GONE, whatever its "negated" flag says, and is
    appended to `stripped`. With `keep_left` a condition that checks the left hand ("Left hand": true) is
    not stripped. A group whose members are all GONE is GONE too. When only some members of a
    group are GONE, the others stay and a line is appended to `mixed`.
    A condition that needs one of `plugins` counts as satisfied (TRUE); NOT of it is FALSE.
    """
    if not isinstance(cond, dict):
        return COND, cond

    negated = bool(cond.get("negated", False))
    name = cond.get("condition")

    if name in ("AND", "OR") and isinstance(cond.get("Conditions"), list):
        kinds = [simplify(c, plugins, report, strip, stripped, mixed, keep_left) for c in cond["Conditions"]]
        live = [(k, c) for k, c in kinds if k != GONE]
        if kinds and not live:
            return GONE, None
        if len(live) != len(kinds) and mixed is not None:
            mixed.append(f"{name} group{' (negated)' if negated else ''}: "
                         f"{len(kinds) - len(live)} equipment condition(s) removed, {len(live)} other(s) kept")
        kinds = live
        if name == "AND":
            if any(k == FALSE for k, _ in kinds):
                result = FALSE
            else:
                rest = [c for k, c in kinds if k == COND]
                result = TRUE if not rest else COND
        else:
            if any(k == TRUE for k, _ in kinds):
                result = TRUE
            else:
                rest = [c for k, c in kinds if k == COND]
                result = FALSE if not rest else COND
        if result == COND:
            out = dict(cond)
            out["Conditions"] = rest
            return COND, out
        if negated:
            result = FALSE if result == TRUE else TRUE
        return result, None

    if isinstance(name, str) and name.lower() in strip and not (keep_left and cond.get("Left hand") is True):
        if stripped is not None:
            stripped.append(name)
        return GONE, None

    if references_plugin(cond, plugins):
        report.append(f"{name}" + (" (negated)" if negated else ""))
        return (FALSE if negated else TRUE), None

    return COND, cond


def find_gating(node, strip: frozenset[str], keep_left: bool = False) -> list[str]:
    """Names of `strip` conditions reachable through AND / OR groups (what simplify() must have removed)."""
    found: list[str] = []
    if isinstance(node, list):
        for c in node:
            found += find_gating(c, strip, keep_left)
    elif isinstance(node, dict):
        name = node.get("condition")
        if name in ("AND", "OR") and isinstance(node.get("Conditions"), list):
            found += find_gating(node["Conditions"], strip, keep_left)
        elif isinstance(name, str) and name.lower() in strip and not (keep_left and node.get("Left hand") is True):
            found.append(name)
    return found


def find_shadowing(subs: list[dict]) -> list[str]:
    """
    Submods whose clips can never be used any more. OAR picks the matching submod with the highest priority
    for each clip. If submod A has the same priority or a higher one, shares clips with B, and asks for
    nothing that B does not also ask for (A's conditions are a subset of B's), then A matches whenever B
    does and B loses the shared clips. Before the equipment conditions were removed something told them
    apart. `subs` items: name, priority, conds (set of canonical condition texts), clips (lower case set).
    """
    lines = []
    for i, a in enumerate(subs):
        for j, b in enumerate(subs):
            if i == j or a["priority"] < b["priority"] or not a["conds"] <= b["conds"]:
                continue
            if a["priority"] == b["priority"] and a["conds"] == b["conds"] and i > j:
                continue  # identical: report the pair once
            shared = sorted(a["clips"] & b["clips"])
            if not shared:
                continue
            lines.append(f"'{a['name']}' (priority {a['priority']}) now applies whenever '{b['name']}' "
                         f"(priority {b['priority']}) does and they share {', '.join(shared)}: "
                         f"'{b['name']}' loses those clips. If '{a['name']}' was a variant for something in "
                         f"the off hand, convert with --variant {a['name']}")
    return lines


def count_named(node, strip: frozenset[str]) -> int:
    """How many `strip` conditions are left anywhere inside `node` (for example inside a TARGET condition)."""
    if isinstance(node, list):
        return sum(count_named(c, strip) for c in node)
    if isinstance(node, dict):
        own = 1 if isinstance(node.get("condition"), str) and node["condition"].lower() in strip else 0
        return own + sum(count_named(v, strip) for v in node.values())
    return 0


def collect_plugins(node, out: set[str]) -> None:
    if isinstance(node, dict):
        name = node.get("pluginName")
        if isinstance(name, str):
            out.add(name)
        for v in node.values():
            collect_plugins(v, out)
    elif isinstance(node, list):
        for v in node:
            collect_plugins(v, out)


def our_condition(pack_id: str) -> dict:
    return {
        "condition": CONDITION_NAME,
        "requiredPlugin": PLUGIN_NAME,
        "requiredVersion": PLUGIN_VERSION,
        "Pack Id": pack_id,
    }


# ----------------------------------------------------------------------------------------------------------
# conversion
# ----------------------------------------------------------------------------------------------------------
def convert_mod(src_mod: Path, out_root: Path, rel_parent: Path, pack_id: str, display_name: str,
                author: str | None, drop_plugins: set[str], log: list[str],
                strip: frozenset[str] = frozenset(), variants: frozenset[str] = frozenset()) -> dict:
    """Converts one OAR mod folder. Returns statistics."""
    dest_mod = out_root / rel_parent / (src_mod.name + SUFFIX)
    stats = {"submods": 0, "disabled": 0, "hkx": 0, "other": 0, "dropped": [], "plugins": set(), "hashes": {},
             "stripped": [], "nested_equipment": 0, "strip": strip, "variant_dirs": set(), "subinfo": [],
             "shadowing": []}
    matched_variants: set[str] = set()

    legacy = [p for p in src_mod.rglob("_conditions.txt")]
    if legacy:
        raise ConvertError(
            f"{src_mod.name}: found legacy DAR-style folders ({len(legacy)} x _conditions.txt). They have no "
            "place for a Swap Moveset condition and would stay active all the time. Convert them to OAR "
            "config.json first (OAR can do that from its menu)."
        )

    for path in sorted(src_mod.rglob("*")):
        if path.is_symlink():
            log.append(f"skipped symlink: {path.relative_to(src_mod)}")
            continue
        if path.is_dir():
            continue
        rel = path.relative_to(src_mod)
        target = dest_mod / rel

        if path.name.lower() == "user.json":
            log.append(f"skipped user.json (your personal OAR overrides): {rel}")
            continue

        if path.name.lower() == "config.json":
            data = read_json(path)
            if not isinstance(data, dict):
                raise ConvertError(f"{rel}: config.json is not a JSON object")

            if rel.parent == Path("."):
                # the mod's own header (name / author / description): not a submod
                data["name"] = display_name + " (Swap Moveset)"
                if author:
                    data["author"] = author
                data["description"] = (
                    f"{display_name}, converted for the Swap Moveset plugin. "
                    "These animations only play while the Swap Moveset mode is on with this pack."
                )
                write_json(target, data)
                continue

            stats["submods"] += 1
            conditions = data.get("conditions", [])
            if not isinstance(conditions, list):
                raise ConvertError(f"{rel}: 'conditions' is not a list")

            is_variant = rel.parent.name.lower() in variants or rel.parent.as_posix().lower() in variants
            if is_variant:
                matched_variants.add(rel.parent.name.lower() if rel.parent.name.lower() in variants
                                     else rel.parent.as_posix().lower())
                stats["variant_dirs"].add(rel.parent.as_posix())
            kept = []
            impossible = False
            mixed: list[str] = []
            for c in conditions:
                kind, new = simplify(c, drop_plugins, stats["dropped"], strip, stats["stripped"], mixed,
                                     keep_left=is_variant)
                if kind == COND:
                    kept.append(new)
                elif kind == FALSE:
                    impossible = True
                # TRUE and GONE: nothing to keep
            for line in mixed:
                log.append(f"{rel.parent}: {line}")
            collect_plugins(kept, stats["plugins"])
            if strip:  # equipment conditions that sit inside other conditions (TARGET ...), not in an AND / OR chain
                stats["nested_equipment"] += count_named(kept, strip) - len(find_gating(kept, strip))
            if impossible:
                data["disabled"] = True
                stats["disabled"] += 1
                log.append(f"{rel.parent}: can never be satisfied without the dropped plugin -> submod disabled")

            data["conditions"] = [our_condition(pack_id)] + kept
            write_json(target, data)
            if strip and not impossible:
                prio = data.get("priority", 0)
                stats["subinfo"].append({
                    "name": rel.parent.name,
                    "priority": prio if isinstance(prio, int) else 0,
                    "conds": {json.dumps(k, sort_keys=True) for k in kept},
                    "clips": {f.name.lower() for f in path.parent.iterdir() if f.suffix.lower() == ".hkx"},
                })
            continue

        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        if path.suffix.lower() == ".hkx":
            stats["hkx"] += 1
            stats["hashes"][str(rel)] = sha256(path)
        else:
            stats["other"] += 1

    unknown = sorted(variants - matched_variants)
    if unknown:
        raise ConvertError("--variant: no submod folder called " + ", ".join(unknown))
    if strip:
        stats["shadowing"] = find_shadowing(stats["subinfo"])
    stats["dest"] = dest_mod
    return stats


def verify(stats: dict, pack_id: str, drop_plugins: set[str], src_mod: Path) -> list[str]:
    """Re-reads what was written. Returns a list of problems (empty = fine)."""
    problems = []
    dest: Path = stats["dest"]

    for rel, digest in stats["hashes"].items():
        out = dest / rel
        if not out.is_file():
            problems.append(f"missing animation: {rel}")
        elif sha256(out) != digest:
            problems.append(f"animation differs from the original: {rel}")

    src_hkx = {str(p.relative_to(src_mod)) for p in src_mod.rglob("*.hkx") if not p.is_symlink()}
    out_hkx = {str(p.relative_to(dest)) for p in dest.rglob("*.hkx")}
    if src_hkx != out_hkx:
        problems.append(f"animation file list differs ({len(src_hkx)} in, {len(out_hkx)} out)")

    submods = 0
    for cfg in dest.rglob("config.json"):
        rel = cfg.relative_to(dest)
        try:
            data = read_json(cfg)
        except Exception as e:  # noqa: BLE001
            problems.append(f"{rel}: not valid JSON ({e})")
            continue
        if rel.parent == Path("."):
            continue
        submods += 1
        conds = data.get("conditions")
        if not conds or conds[0] != our_condition(pack_id):
            problems.append(f"{rel}: our condition is not the first one")
        if drop_plugins and references_any(data, drop_plugins):
            problems.append(f"{rel}: still references a dropped plugin")
        left = find_gating(conds, stats["strip"], rel.parent.as_posix() in stats["variant_dirs"]) \
            if stats["strip"] else []
        if left:
            problems.append(f"{rel}: equipment condition(s) still there: {', '.join(sorted(set(left)))}")
    if submods != stats["submods"]:
        problems.append(f"submod count differs ({stats['submods']} converted, {submods} found)")
    return problems


def references_any(node, plugins: set[str]) -> bool:
    found: set[str] = set()
    collect_plugins(node, found)
    wanted = {p.lower() for p in plugins}
    return any(p.lower() in wanted for p in found)


def write_zip(src_dir: Path, zip_path: Path) -> int:
    """Files at the archive root (no container folder)."""
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    if zip_path.exists():
        zip_path.unlink()
    count = 0
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for path in sorted(src_dir.rglob("*")):
            if path.is_file() and not path.is_symlink():
                z.write(path, path.relative_to(src_dir).as_posix())
                count += 1
    return count


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Convert an OAR animation mod into a Swap Moveset pack.")
    ap.add_argument("mod", type=Path, help="mod folder (the one containing meshes/..., or the OAR folder itself)")
    ap.add_argument("-o", "--out", type=Path, required=True, help="output folder (created; must not exist or be empty)")
    ap.add_argument("--zip", type=Path, help="also write a zip with the files at its root")
    ap.add_argument("--id", help="pack id: a-z 0-9 _ - (default: from the name)")
    ap.add_argument("--name", help="name shown in the menu (default: the OAR folder name)")
    ap.add_argument("--author", help="author shown in the menu (default: the mod's own config.json)")
    ap.add_argument("--order", type=int, default=100, help="position in the pack list, lower first")
    ap.add_argument("--notes", default="", help="free text shown under the pack in the menu")
    ap.add_argument("--description", default="", help="short description shown in the menu")
    ap.add_argument("--drop-plugin", action="append", default=[], metavar="FILE.esp",
                    help="remove conditions that need this plugin (treated as satisfied); repeatable")
    ap.add_argument("--keep-equipment-conditions", action="store_true",
                    help="keep the conditions that tie the moveset to a weapon / hand equipment "
                         "(by default they are removed so the pack works with any weapon or none)")
    ap.add_argument("--variant", action="append", default=[], metavar="SUBMOD",
                    help="submod folder that is a variant picked by the item in the off hand: only its "
                         "right-hand equipment conditions are removed; repeatable")
    ap.add_argument("--strip-condition", action="append", default=[], metavar="NAME",
                    help="also remove every condition with this OAR name (for example IsWorn); repeatable")
    args = ap.parse_args(argv)

    src = args.mod.resolve()
    if not src.is_dir():
        print(f"error: {src} is not a folder", file=sys.stderr)
        return 2
    out = args.out.resolve()
    if out.exists() and any(out.iterdir()):
        print(f"error: output folder {out} is not empty", file=sys.stderr)
        return 2
    if out == src or src in out.parents or out in src.parents:
        print("error: input and output folders must be separate and not nested", file=sys.stderr)
        return 2

    default_parent = Path("meshes/actors/character/animations/OpenAnimationReplacer")
    if src.name.lower() == "openanimationreplacer":
        mods = [p for p in sorted(src.iterdir()) if p.is_dir() and not p.is_symlink()]
    else:
        mods = find_oar_mods(src)
    if not mods:
        print("error: no OpenAnimationReplacer/<mod> folder found inside the input", file=sys.stderr)
        return 2
    if len(mods) > 1:
        print("error: more than one OAR mod folder found; convert them one at a time:", file=sys.stderr)
        for m in mods:
            print(f"   {m}", file=sys.stderr)
        return 2
    mod = mods[0]

    # path of the OpenAnimationReplacer folder relative to the input root, e.g. meshes/actors/.../OpenAnimationReplacer
    rel_parent = default_parent if src.name.lower() == "openanimationreplacer" else mod.parent.relative_to(src)

    header = {}
    if (mod / "config.json").is_file():
        try:
            header = read_json(mod / "config.json")
        except Exception as e:  # noqa: BLE001
            print(f"warning: mod config.json unreadable ({e})", file=sys.stderr)
    display_name = args.name or (header.get("name") if isinstance(header.get("name"), str) and header.get("name") else mod.name)
    author = args.author or (header.get("author") if isinstance(header.get("author"), str) else "")
    pack_id = args.id or slugify(display_name)
    if not ID_RE.match(pack_id):
        print(f"error: pack id '{pack_id}' is not valid (a-z 0-9 _ - , up to 48 characters)", file=sys.stderr)
        return 2

    drop = set(args.drop_plugin)
    strip = {n.lower() for n in args.strip_condition}
    if not args.keep_equipment_conditions:
        strip |= EQUIPMENT_CONDITIONS
    strip = frozenset(strip)
    variants = frozenset(v.lower().strip("/") for v in args.variant)
    log: list[str] = []
    try:
        stats = convert_mod(mod, out, rel_parent, pack_id, display_name, author, drop, log, strip, variants)
    except ConvertError as e:
        print(f"error: {e}", file=sys.stderr)
        shutil.rmtree(out, ignore_errors=True)
        return 1

    manifest = {
        "id": pack_id,
        "name": display_name,
        "author": author,
        "description": args.description or f"{display_name} animation set.",
        "notes": args.notes,
        "order": args.order,
    }
    write_json(out / "SKSE" / "Plugins" / "SwapMoveset" / "Packs" / f"{pack_id}.json", manifest)

    problems = verify(stats, pack_id, drop, mod)
    manifest_check = read_json(out / "SKSE" / "Plugins" / "SwapMoveset" / "Packs" / f"{pack_id}.json")
    if manifest_check.get("id") != pack_id:
        problems.append("manifest id mismatch")

    # ---- report -------------------------------------------------------------------------------------------------
    print(f"pack        : {display_name}  (id '{pack_id}')")
    print(f"source      : {mod}")
    print(f"output      : {out}")
    print(f"submods     : {stats['submods']} (disabled: {stats['disabled']})")
    print(f"animations  : {stats['hkx']} .hkx copied, SHA-256 verified" if not problems else
          f"animations  : {stats['hkx']} .hkx copied")
    if stats["other"]:
        print(f"other files : {stats['other']} copied unchanged")
    plugins = sorted(p for p in stats["plugins"] if p.lower() not in {d.lower() for d in drop})
    if plugins:
        print("plugins named by the conditions (must be in your load order): " + ", ".join(plugins))
    if stats["stripped"]:
        sc: dict[str, int] = {}
        for name in stats["stripped"]:
            sc[name] = sc.get(name, 0) + 1
        print("equipment conditions removed (works with any weapon): " +
              ", ".join(f"{n} x{c}" for n, c in sorted(sc.items())))
    elif args.keep_equipment_conditions:
        print("equipment conditions KEPT: the pack only applies to the equipment it was made for")
    if stats["variant_dirs"]:
        print("variant submods (left-hand conditions kept): " + ", ".join(sorted(stats["variant_dirs"])))
    for line in stats["shadowing"]:
        print("WARNING     : " + line)
    if stats["nested_equipment"]:
        print(f"warning     : {stats['nested_equipment']} equipment condition(s) sit inside other conditions "
              "(for example TARGET) and were left in place")
    if stats["dropped"]:
        counts: dict[str, int] = {}
        for name in stats["dropped"]:
            counts[name] = counts.get(name, 0) + 1
        print("conditions removed (treated as satisfied): " +
              ", ".join(f"{n} x{c}" for n, c in sorted(counts.items())))
    for line in log:
        print("note        : " + line)

    if problems:
        print("\nPROBLEMS:", file=sys.stderr)
        for p in problems:
            print("  - " + p, file=sys.stderr)
        return 1

    if args.zip:
        n = write_zip(out, args.zip.resolve())
        print(f"zip         : {args.zip.resolve()}  ({n} files, at the archive root)")
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
