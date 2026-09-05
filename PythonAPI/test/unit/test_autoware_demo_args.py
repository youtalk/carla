# Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma de
# Barcelona (UAB).
#
# This work is licensed under the terms of the MIT license.
# For a copy, see <https://opensource.org/licenses/MIT>.

"""Argument parsing of PythonAPI/examples/autoware_demo.py (no simulator needed)."""

import argparse
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "examples"))
import autoware_demo  # noqa: E402


class TestSpawnPose(unittest.TestCase):
    def test_four_fields_become_a_transform(self):
        t = autoware_demo.parse_spawn_pose("-84.114,117.603,42.3,-10.43")
        self.assertAlmostEqual(t.location.x, -84.114, places=4)
        self.assertAlmostEqual(t.location.y, 117.603, places=4)
        self.assertAlmostEqual(t.location.z, 42.3, places=4)
        self.assertAlmostEqual(t.rotation.yaw, -10.43, places=4)
        self.assertEqual(t.rotation.pitch, 0.0)
        self.assertEqual(t.rotation.roll, 0.0)

    def test_wrong_arity_is_an_argument_error(self):
        with self.assertRaises(argparse.ArgumentTypeError):
            autoware_demo.parse_spawn_pose("1,2,3")

    def test_non_numeric_is_an_argument_error(self):
        with self.assertRaises(argparse.ArgumentTypeError):
            autoware_demo.parse_spawn_pose("1,2,x,4")


class TestMgrsOffset(unittest.TestCase):
    def test_three_fields(self):
        self.assertEqual(autoware_demo.parse_mgrs_offset("81655.73,50137.43,42.49998"),
                         (81655.73, 50137.43, 42.49998))

    def test_wrong_arity_is_an_argument_error(self):
        with self.assertRaises(argparse.ArgumentTypeError):
            autoware_demo.parse_mgrs_offset("1,2")


if __name__ == "__main__":
    unittest.main()
