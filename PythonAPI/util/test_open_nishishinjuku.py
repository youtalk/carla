"""Unit tests for the pure helpers in open_nishishinjuku."""

import pytest

from open_nishishinjuku import compute_overhead_view


def test_overhead_view_centers_and_frames_points():
    points = [(0.0, 0.0), (100.0, 50.0)]
    cx, cy, height, pitch = compute_overhead_view(points)
    assert cx == pytest.approx(50.0)
    assert cy == pytest.approx(25.0)
    # span is 100 (x), height = span * 0.6 = 60, floored to min 100
    assert height == pytest.approx(100.0)
    assert pitch == pytest.approx(-90.0)


def test_overhead_view_scales_height_for_large_span():
    points = [(0.0, 0.0), (1000.0, 0.0)]
    _, _, height, _ = compute_overhead_view(points)
    assert height == pytest.approx(600.0)


def test_overhead_view_raises_on_empty():
    with pytest.raises(ValueError):
        compute_overhead_view([])
