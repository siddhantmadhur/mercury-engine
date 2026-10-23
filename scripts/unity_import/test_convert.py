"""Tests for the Unity -> Mercury conversions.

    python3 -m unittest discover -s scripts/unity_import
"""
import math
import random
import unittest

import convert as cv


def rotate(q, v):
    x, y, z, w = q
    p = (v[0], v[1], v[2], 0.0)
    conj = (-x, -y, -z, w)
    r = cv.quat_multiply(cv.quat_multiply(q, p), conj)
    return r[:3]


def glm_quat_from_euler(e):
    """glm::quat(vec3 radians), which Mercury's Transform builds from euler."""
    cx, cy, cz = (math.cos(math.radians(a) / 2) for a in e)
    sx, sy, sz = (math.sin(math.radians(a) / 2) for a in e)
    return (sx * cy * cz - cx * sy * sz,
            cx * sy * cz + sx * cy * sz,
            cx * cy * sz - sx * sy * cz,
            cx * cy * cz + sx * sy * sz)


def mirror(v):
    return (v[0], v[1], -v[2])


class ConvertTest(unittest.TestCase):
    def assertVecAlmostEqual(self, a, b, places=5):
        for x, y in zip(a, b):
            self.assertAlmostEqual(x, y, places=places)

    def test_position_mirrors_z(self):
        self.assertEqual(cv.unity_position({"x": 1, "y": 2, "z": 3}), (1.0, 2.0, -3.0))

    def test_quaternion_matches_mirrored_rotation(self):
        rng = random.Random(3)
        for _ in range(50):
            q = cv.unity_euler_to_quat([rng.uniform(-180, 180) for _ in range(3)])
            v = (rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1))
            mq = cv.unity_rotation({"x": q[0], "y": q[1], "z": q[2], "w": q[3]})
            self.assertVecAlmostEqual(mirror(rotate(q, v)), rotate(mq, mirror(v)))

    def test_euler_round_trips_through_mercury_transform(self):
        rng = random.Random(5)
        for _ in range(50):
            e = [rng.uniform(-180, 180) for _ in range(3)]
            v = (rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1))
            unity = rotate(cv.unity_euler_to_quat(e), v)
            mercury = rotate(glm_quat_from_euler(cv.unity_euler_to_mercury(e)), mirror(v))
            self.assertVecAlmostEqual(mirror(unity), mercury)

    def test_unity_yaw_turns_forward_right(self):
        # Quaternion.Euler(0, 90, 0) turns +Z towards +X.
        q = cv.unity_euler_to_quat((0, 90, 0))
        self.assertVecAlmostEqual(rotate(q, (0, 0, 1)), (1, 0, 0))

    def test_srgb_to_linear(self):
        self.assertAlmostEqual(cv.srgb_to_linear(1.0), 1.0)
        self.assertAlmostEqual(cv.srgb_to_linear(0.5), 0.21404, places=4)


if __name__ == "__main__":
    unittest.main()
