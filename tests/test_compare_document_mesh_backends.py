"""Il confronto diagnostico deve rilevare differenze senza confondere bit e geometria."""
import copy
import struct
import unittest

import numpy as np

from tools.compare_document_mesh_backends import SnapshotReader, compare_snapshots


def fixture():
    return dict(vertices=np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype="<f4"),
                normals=np.array([[0, 0, 1]] * 3, dtype="<f4"), triangle_faces=np.array([7], dtype="<i4"),
                edges=[np.array([[0, 0, 0], [1, 0, 0]], dtype="<f4")],
                edge_ids=np.array([4], dtype="<i4"), face_ids=np.array([7], dtype="<i4"),
                labels=np.array([[.3, .3, 0]], dtype="<f4"), face_edges=[np.array([0], dtype="<i4")],
                instances=np.empty((0, 16), dtype="<f4"))


def encode(mesh):
    def array(value):
        return struct.pack("<Q", len(value)) + value.tobytes()

    def nested(value):
        return struct.pack("<Q", len(value)) + b"".join(array(child) for child in value)

    return (b"FCMSH001" + array(mesh["vertices"]) + array(mesh["normals"]) + array(mesh["triangle_faces"])
            + nested(mesh["edges"]) + array(mesh["edge_ids"]) + array(mesh["face_ids"])
            + array(mesh["labels"]) + nested(mesh["face_edges"]) + array(mesh["instances"]))


class NumericMeshTests(unittest.TestCase):
    def test_roundtrip_all_fields_and_identity(self):
        source = fixture()
        source["instances"] = np.eye(4, dtype="<f4").reshape((1, 16))
        restored = SnapshotReader(encode(source)).read()
        result = compare_snapshots(source, restored)
        self.assertTrue(result["passed"])
        self.assertEqual(result["per_face"][0]["face"], 7)
        self.assertEqual(result["vertices"]["bitwise_different_rows"], 0)

    def test_signed_zero_is_only_a_bit_difference(self):
        source = fixture()
        changed = copy.deepcopy(source)
        changed["vertices"][0, 0] = -0.0
        result = compare_snapshots(source, changed)
        self.assertTrue(result["passed"])
        self.assertEqual(result["vertices"]["bitwise_different_rows"], 1)
        self.assertEqual(result["vertices"]["numeric_different_rows"], 0)
        self.assertEqual(result["vertices"]["max_distance"], 0)

    def test_small_coordinate_change_measured_and_large_change_rejected(self):
        source = fixture()
        changed = copy.deepcopy(source)
        changed["vertices"][1, 0] += 1e-6
        result = compare_snapshots(source, changed)
        self.assertTrue(result["passed"])
        self.assertGreater(result["vertices"]["max_distance"], 0)
        self.assertEqual(result["vertices"]["worst_row"], 1)
        changed["vertices"][1, 0] += 1e-3
        self.assertFalse(compare_snapshots(source, changed)["passed"])

    def test_normal_angle_zero_mask_and_nonfinite(self):
        source = fixture()
        changed = copy.deepcopy(source)
        changed["normals"][0, 0] = 1e-6
        result = compare_snapshots(source, changed)
        self.assertTrue(result["passed"])
        self.assertAlmostEqual(result["normals"]["max_angle_degrees"], 0.0000572957795, places=10)
        changed["normals"][0] = 0
        self.assertFalse(compare_snapshots(source, changed)["passed"])
        changed["normals"][0, 0] = np.nan
        with self.assertRaises(ValueError):
            compare_snapshots(source, changed)

    def test_topology_difference_not_accepted_as_geometric_identity(self):
        source = fixture()
        for key in ["triangle_faces", "edge_ids", "face_ids"]:
            changed = copy.deepcopy(source)
            changed[key][0] += 1
            self.assertFalse(compare_snapshots(source, changed)["passed"], key)
        changed = copy.deepcopy(source)
        changed["face_edges"][0][0] = 1
        self.assertFalse(compare_snapshots(source, changed)["passed"])

    def test_edges_labels_and_instance_changes_are_checked(self):
        source = fixture()
        for key in ["edges", "labels"]:
            changed = copy.deepcopy(source)
            if key == "edges":
                changed[key][0][0, 0] += .1
            else:
                changed[key][0, 0] += .1
            self.assertFalse(compare_snapshots(source, changed)["passed"], key)
        changed = copy.deepcopy(source)
        changed["instances"] = np.eye(4, dtype="<f4").reshape((1, 16))
        self.assertFalse(compare_snapshots(source, changed)["passed"])

    def test_flipped_and_degenerate_triangles(self):
        source = fixture()
        changed = copy.deepcopy(source)
        changed["vertices"][[1, 2]] = changed["vertices"][[2, 1]]
        result = compare_snapshots(source, changed)
        self.assertFalse(result["passed"])
        self.assertEqual(result["triangles"]["winding_flips"], 1)
        changed["vertices"][2] = changed["vertices"][1]
        self.assertFalse(compare_snapshots(source, changed)["triangles"]["degenerate_mask_equal"])

    def test_truncations_and_absurd_count_rejected_before_allocation(self):
        data = encode(fixture())
        for cut in [0, 7, 8, 15, 16, 40, len(data) - 1]:
            with self.assertRaises(ValueError):
                SnapshotReader(data[:cut]).read()
        broken = data[:8] + struct.pack("<Q", 2**63) + data[16:]
        with self.assertRaises(ValueError):
            SnapshotReader(broken).read()
        broken = b"FCMSH001" + struct.pack("<Q", 0) * 3 + struct.pack("<Q", 2**63)
        with self.assertRaises(ValueError):
            SnapshotReader(broken).read()
        with self.assertRaises(ValueError):
            SnapshotReader(data + b"residuo").read()

    def test_incomplete_face_mapping_rejected(self):
        source = fixture()
        source["triangle_faces"] = np.empty(0, dtype="<i4")
        with self.assertRaises(ValueError):
            SnapshotReader(encode(source)).read()


if __name__ == "__main__":
    unittest.main()
