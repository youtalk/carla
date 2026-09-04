"""Unit tests for lanelet2_bounds: detection and repair of lanelets whose
left/right bounds run in opposite directions because crdesigner shares the
centre linestring of an opposing-lane pair un-inverted."""
import os
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import lanelet2_bounds as lb  # noqa: E402

# Three lanes on a straight road. Way 100 is the centre line, emitted once in
# +x order. Lanelet A (travel +x) uses it correctly as left; lanelet B (travel
# -x) also references it as LEFT, un-inverted -> misoriented. Lanelet C is A's
# same-direction neighbour and legitimately shares way 101 (A's right, C's
# left) -- a shared way that must NOT be treated as a culprit.
SYNTHETIC = """<?xml version='1.0' encoding='UTF-8'?>
<osm version="0.6" generator="test">
  <MetaInfo format_version="1.0.0" map_version="1"/>
  <node id="1"><tag k="local_x" v="0"/><tag k="local_y" v="0"/></node>
  <node id="2"><tag k="local_x" v="10"/><tag k="local_y" v="0"/></node>
  <node id="3"><tag k="local_x" v="0"/><tag k="local_y" v="3.5"/></node>
  <node id="4"><tag k="local_x" v="10"/><tag k="local_y" v="3.5"/></node>
  <node id="5"><tag k="local_x" v="10"/><tag k="local_y" v="7"/></node>
  <node id="6"><tag k="local_x" v="0"/><tag k="local_y" v="7"/></node>
  <way id="100"><nd ref="3"/><nd ref="4"/><tag k="type" v="line_thin"/><tag k="subtype" v="dashed"/></way>
  <way id="101"><nd ref="1"/><nd ref="2"/></way>
  <way id="102"><nd ref="5"/><nd ref="6"/></way>
  <relation id="200"><member type="way" role="left" ref="100"/><member type="way" role="right" ref="101"/>
    <tag k="type" v="lanelet"/><tag k="subtype" v="road"/></relation>
  <relation id="201"><member type="way" role="left" ref="100"/><member type="way" role="right" ref="102"/>
    <tag k="type" v="lanelet"/><tag k="subtype" v="road"/></relation>
  <node id="7"><tag k="local_x" v="0"/><tag k="local_y" v="-3.5"/></node>
  <node id="8"><tag k="local_x" v="10"/><tag k="local_y" v="-3.5"/></node>
  <way id="103"><nd ref="7"/><nd ref="8"/></way>
  <relation id="202"><member type="way" role="left" ref="101"/><member type="way" role="right" ref="103"/>
    <tag k="type" v="lanelet"/><tag k="subtype" v="road"/></relation>
</osm>
"""


# Both lanelets sharing way 300 in the LEFT role come out misoriented, because
# the chord test cannot tell a hairpin lanelet from a reversed bound. Neither
# is a trustworthy reference for way 300's true direction, so reversing it for
# one would only move the defect onto the other.
AMBIGUOUS_SHARED_BOUND = """<?xml version='1.0' encoding='UTF-8'?>
<osm version="0.6" generator="test">
  <node id="1"><tag k="local_x" v="0"/><tag k="local_y" v="0"/></node>
  <node id="2"><tag k="local_x" v="10"/><tag k="local_y" v="0"/></node>
  <node id="3"><tag k="local_x" v="10"/><tag k="local_y" v="3.5"/></node>
  <node id="4"><tag k="local_x" v="0"/><tag k="local_y" v="3.5"/></node>
  <node id="5"><tag k="local_x" v="10"/><tag k="local_y" v="-3.5"/></node>
  <node id="6"><tag k="local_x" v="0"/><tag k="local_y" v="-3.5"/></node>
  <way id="300"><nd ref="1"/><nd ref="2"/></way>
  <way id="301"><nd ref="3"/><nd ref="4"/></way>
  <way id="302"><nd ref="5"/><nd ref="6"/></way>
  <relation id="400"><member type="way" role="left" ref="300"/><member type="way" role="right" ref="301"/>
    <tag k="type" v="lanelet"/><tag k="subtype" v="road"/></relation>
  <relation id="401"><member type="way" role="left" ref="300"/><member type="way" role="right" ref="302"/>
    <tag k="type" v="lanelet"/><tag k="subtype" v="road"/></relation>
</osm>
"""


def _root():
    return ET.ElementTree(ET.fromstring(SYNTHETIC)).getroot()


def _root_with_cross_role_neighbour():
    """``_root()`` plus lanelet D, the misoriented lanelet's same-direction
    neighbour on its other side: D takes way 102 as its LEFT while lanelet B
    takes it as its RIGHT.

    This is the shape 14 of the 17 real Town10HD culprits have -- the bound
    that is *not* the un-inverted centre line is nonetheless shared, just
    across roles. Counting shares without regard to role would see two shared
    bounds on lanelet B and refuse to repair it.
    """
    root = _root()
    for node_id, x, y in (("9", "10", "10.5"), ("10", "0", "10.5")):
        node = ET.SubElement(root, "node", id=node_id)
        ET.SubElement(node, "tag", k="local_x", v=x)
        ET.SubElement(node, "tag", k="local_y", v=y)
    way = ET.SubElement(root, "way", id="104")
    ET.SubElement(way, "nd", ref="9")
    ET.SubElement(way, "nd", ref="10")
    rel = ET.SubElement(root, "relation", id="203")
    ET.SubElement(rel, "member", type="way", role="left", ref="102")
    ET.SubElement(rel, "member", type="way", role="right", ref="104")
    ET.SubElement(rel, "tag", k="type", v="lanelet")
    ET.SubElement(rel, "tag", k="subtype", v="road")
    return root


def test_detects_only_the_lanelet_whose_bounds_oppose():
    assert lb.misoriented_lanelets(_root()) == ["201"]


def test_fix_clones_the_shared_way_reversed_for_the_misoriented_member():
    root = _root()
    report = lb.fix_shared_bounds(root)
    assert report.unresolved == []
    assert len(report.fixed) == 1
    lanelet_id, old_way, new_way = report.fixed[0]
    assert (lanelet_id, old_way) == ("201", "100")
    ways = {w.get("id"): w for w in root.findall("way")}
    assert new_way in ways and new_way != "100"
    assert len(ways) == 5  # 100, 101, 102, 103 + one clone
    assert [nd.get("ref") for nd in ways[new_way].findall("nd")] == ["4", "3"]
    # tags travel with the clone; the original way is untouched
    assert {t.get("k"): t.get("v") for t in ways[new_way].findall("tag")} == {"type": "line_thin", "subtype": "dashed"}
    assert [nd.get("ref") for nd in ways["100"].findall("nd")] == ["3", "4"]
    # lanelet A keeps the original, lanelet B now points at the clone
    rel = {r.get("id"): r for r in root.findall("relation")}
    assert rel["200"].find("member[@role='left']").get("ref") == "100"
    assert rel["201"].find("member[@role='left']").get("ref") == new_way
    assert lb.misoriented_lanelets(root) == []


def test_fix_is_idempotent():
    root = _root()
    lb.fix_shared_bounds(root)
    second = lb.fix_shared_bounds(root)
    assert second.fixed == [] and second.unresolved == []


def test_unshared_misorientation_is_reported_not_guessed():
    root = _root()
    # give lanelet B its own private (still reversed) left way, so no
    # way is shared in the same role by two lanelets
    priv = ET.SubElement(root, "way", id="104")
    ET.SubElement(priv, "nd", ref="3")
    ET.SubElement(priv, "nd", ref="4")
    root.find("relation[@id='201']/member[@role='left']").set("ref", "104")
    report = lb.fix_shared_bounds(root)
    assert report.fixed == [] and report.unresolved == ["201"]


def test_new_way_id_does_not_collide():
    root = _root()
    report = lb.fix_shared_bounds(root)
    ids = [int(w.get("id")) for w in root.findall("way")]
    assert len(ids) == len(set(ids)) and int(report.fixed[0][2]) == max(ids)


def test_new_way_id_avoids_ids_taken_by_nodes_and_relations():
    root = _root()
    # crdesigner draws node, way and relation ids from a single counter, so the
    # id just above the highest way id can already belong to a relation.
    squatter = ET.SubElement(root, "relation", id="104")
    ET.SubElement(squatter, "tag", k="type", v="regulatory_element")
    report = lb.fix_shared_bounds(root)
    taken = {e.get("id") for e in root.findall("node")} | {e.get("id") for e in root.findall("relation")}
    assert report.fixed[0][2] not in taken


def test_bound_shared_across_roles_is_not_a_culprit():
    root = _root_with_cross_role_neighbour()
    # D is aligned with B, so only B is misoriented and only the same-role
    # share (way 100) may be repaired -- way 102, shared across roles with D,
    # is a normal boundary between same-direction lanes.
    assert lb.misoriented_lanelets(root) == ["201"]
    report = lb.fix_shared_bounds(root)
    assert report.unresolved == []
    assert [(lanelet, old) for lanelet, old, _ in report.fixed] == [("201", "100")]
    assert root.find("relation[@id='201']/member[@role='right']").get("ref") == "102"
    assert lb.misoriented_lanelets(root) == []


def test_shared_bound_with_no_correctly_oriented_user_is_reported():
    root = ET.fromstring(AMBIGUOUS_SHARED_BOUND)
    assert lb.misoriented_lanelets(root) == ["400", "401"]
    report = lb.fix_shared_bounds(root)
    assert report.fixed == []
    assert report.unresolved == ["400", "401"]
    # no clone emitted, and the shared way is not left orphaned
    assert [w.get("id") for w in root.findall("way")] == ["300", "301", "302"]
    assert root.find("relation[@id='400']/member[@role='left']").get("ref") == "300"
    assert root.find("relation[@id='401']/member[@role='left']").get("ref") == "300"
