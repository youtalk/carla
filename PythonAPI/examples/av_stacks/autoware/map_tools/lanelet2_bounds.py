#!/usr/bin/env python3
"""Detect and repair lanelets whose left and right bounds run in opposite
directions.

commonroad-scenario-designer (0.8.5) emits the shared centre linestring of an
opposing-lane pair once and references it from BOTH lanelets un-inverted
(cr2lanelet.py::_get_potential_left_way / _get_potential_right_way, the
``not adj_*_same_direction`` branch: the candidate way is matched by comparing
it against the *reversed* vertices, so the converter has established that the
way runs backwards for this second user -- but OSM has no inverted-member
syntax in which to say so). One member of every such pair therefore ends up
with its two bounds running in opposite directions. The lanelet2 convention is
that both bounds run along the direction of travel, and that shared direction
is what states it; a map like this one therefore states the direction only
implicitly, leaving the loader's ``geometry::align`` heuristic to settle it.
lanelet2 tolerates that by design -- ``align`` exists for exactly this case --
so the cost is not a load failure but a direction of travel that depends on a
heuristic rather than on the map.

This QC pass is preventive, and no drive-level impact has been demonstrated.
On Town10HD it flags 17 of the map's 160 road lanelets (246 lanelet relations
in total); loading the un-repaired map shows ``geometry::align`` landing on
exactly the orientation the repair encodes for all 17, and reversing nothing
among the remaining 229 relations, so on this map the heuristic already
resolves every case the way the repair does. What the repair buys is that the
ordering is stated by the map rather than inferred from it, which keeps the
direction of travel independent of any one loader's heuristic.

Repair: for each misoriented lanelet, the bound whose way is used in the SAME
role (left/left or right/right) by another lanelet -- the un-inverted centre
line -- is cloned with its node order reversed and the lanelet is re-pointed at
the clone. A way shared as one lanelet's left and its neighbour's right is a
normal same-direction boundary and is never touched. Node ids are preserved, so
lanelet connectivity (shared first/last nodes) is unchanged. Lanelets whose
misorientation cannot be attributed to a single shared bound are reported, not
guessed.
"""
from __future__ import annotations

import argparse
import math
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field


@dataclass
class FixReport:
    fixed: list[tuple[str, str, str]] = field(default_factory=list)  # (lanelet, old_way, new_way)
    unresolved: list[str] = field(default_factory=list)


def _node_xy(root) -> dict[str, tuple[float, float]]:
    out = {}
    for n in root.findall("node"):
        tags = {t.get("k"): t.get("v") for t in n.findall("tag")}
        if "local_x" in tags and "local_y" in tags:
            out[n.get("id")] = (float(tags["local_x"]), float(tags["local_y"]))
        elif n.get("lon") is not None and n.get("lat") is not None:
            out[n.get("id")] = (float(n.get("lon")), float(n.get("lat")))  # direction only
    return out


def _ways(root) -> dict[str, ET.Element]:
    return {w.get("id"): w for w in root.findall("way")}


def _lanelet_bounds(root) -> list[tuple[ET.Element, str, str]]:
    out = []
    for rel in root.findall("relation"):
        tags = {t.get("k"): t.get("v") for t in rel.findall("tag")}
        if tags.get("type") != "lanelet":
            continue
        left = rel.find("member[@role='left']")
        right = rel.find("member[@role='right']")
        if left is None or right is None:
            continue
        out.append((rel, left.get("ref"), right.get("ref")))
    return out


def _next_free_id(root) -> int:
    """One past the highest id used by any node, way or relation.

    crdesigner draws all three from a single counter, so a clone id picked
    only above the way ids can land on an existing relation.
    """
    ids = (e.get("id") or "" for e in root if e.tag in ("node", "way", "relation"))
    return max((int(i) for i in ids if i.lstrip("-").isdigit()), default=0) + 1


def _end_to_end(way: ET.Element, xy) -> tuple[float, float]:
    refs = [nd.get("ref") for nd in way.findall("nd")]
    if len(refs) < 2 or refs[0] not in xy or refs[-1] not in xy:
        return (0.0, 0.0)
    (x0, y0), (x1, y1) = xy[refs[0]], xy[refs[-1]]
    return (x1 - x0, y1 - y0)


def _opposed(a, b) -> bool:
    na, nb = math.hypot(*a), math.hypot(*b)
    if na < 1e-9 or nb < 1e-9:
        return False
    return (a[0] * b[0] + a[1] * b[1]) / (na * nb) < 0.0  # angle > 90 deg


def misoriented_lanelets(root) -> list[str]:
    xy, ways = _node_xy(root), _ways(root)
    bad = []
    for rel, left, right in _lanelet_bounds(root):
        if left in ways and right in ways and _opposed(_end_to_end(ways[left], xy), _end_to_end(ways[right], xy)):
            bad.append(rel.get("id"))
    return bad


def fix_shared_bounds(root) -> FixReport:
    report = FixReport()
    bad = set(misoriented_lanelets(root))
    if not bad:
        return report
    ways = _ways(root)
    # A way legitimately shared between same-direction neighbours is the LEFT
    # of one lanelet and the RIGHT of the other. A way used in the SAME role by
    # two lanelets is the un-inverted centre line of an opposing pair.
    same_role_users: dict[tuple[str, str], int] = {}
    for _, left, right in _lanelet_bounds(root):
        same_role_users[("left", left)] = same_role_users.get(("left", left), 0) + 1
        same_role_users[("right", right)] = same_role_users.get(("right", right), 0) + 1
    next_id = _next_free_id(root)
    for rel, left, right in _lanelet_bounds(root):
        lid = rel.get("id")
        if lid not in bad:
            continue
        culprits = [(role, way) for role, way in (("left", left), ("right", right))
                    if same_role_users.get((role, way), 0) > 1]
        if len(culprits) != 1:  # none: private reversed way; two: no single culprit
            report.unresolved.append(lid)
            continue
        role, old_id = culprits[0]
        src = ways[old_id]
        clone = ET.Element("way", dict(src.attrib))
        clone.set("id", str(next_id))
        for nd in reversed(src.findall("nd")):
            ET.SubElement(clone, "nd", ref=nd.get("ref"))
        for t in src.findall("tag"):
            ET.SubElement(clone, "tag", k=t.get("k"), v=t.get("v"))
        idx = list(root).index(src)
        root.insert(idx + 1, clone)
        ways[str(next_id)] = clone
        rel.find(f"member[@role='{role}']").set("ref", str(next_id))
        report.fixed.append((lid, old_id, str(next_id)))
        next_id += 1
    return report


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("osm", help="lanelet2 .osm file")
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--check", action="store_true", help="report misoriented lanelets; exit 1 if any")
    g.add_argument("--fix", action="store_true", help="repair in place (or to --out)")
    p.add_argument("--out", help="output path for --fix (default: overwrite input)")
    a = p.parse_args(argv)
    tree = ET.parse(a.osm)
    root = tree.getroot()
    before = misoriented_lanelets(root)
    if a.check:
        print(f"road/lanelet relations checked: {len(_lanelet_bounds(root))}; misoriented: {len(before)}"
              + (f" -> {' '.join(before)}" if before else ""))
        return 1 if before else 0
    report = fix_shared_bounds(root)
    ET.indent(tree, space="  ")
    tree.write(a.out or a.osm, encoding="UTF-8", xml_declaration=True)
    print(f"misoriented before: {len(before)}; fixed: {len(report.fixed)}; unresolved: {len(report.unresolved)}"
          + (f" -> {' '.join(report.unresolved)}" if report.unresolved else ""))
    for lid, old, new in report.fixed:
        print(f"  lanelet {lid}: shared way {old} -> reversed clone {new}")
    return 1 if report.unresolved else 0


if __name__ == "__main__":
    sys.exit(main())
