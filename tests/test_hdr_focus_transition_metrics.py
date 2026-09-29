#!/usr/bin/env python3
"""Mutation-style unit tests for HDR focus transition evidence analysis."""

from __future__ import annotations

import unittest

from hdr_quality_system import (
    focus_endpoint_captures,
    focus_proxy_matches_source_resolution,
    focus_transition_metrics,
)


def trace(include_deactivation_animation: bool = True, reverse_sample: bool = False):
    records = []
    timestamp = 1_000
    transition_count = 1
    for opacity in (0.2, 0.7):
        timestamp += 16
        records.append({
            "active_requested": True,
            "animation_in_flight": True,
            "opacity": opacity,
            "fallback_visible": True,
            "wants_edr": True,
            "transition_count": transition_count,
            "timestamp_ms": timestamp,
        })
    timestamp += 32
    records.append({
        "active_requested": True,
        "animation_in_flight": False,
        "opacity": 1.0,
        "fallback_visible": False,
        "wants_edr": True,
        "transition_count": transition_count,
        "timestamp_ms": timestamp,
    })
    if include_deactivation_animation:
        values = [1.0 - index / 28 for index in range(1, 29)]
        if reverse_sample:
            values[9] = values[8] + 0.2
        for opacity in values:
            timestamp += 16
            records.append({
                "active_requested": False,
                "animation_in_flight": True,
                "opacity": opacity,
                "fallback_visible": True,
                "wants_edr": True,
                "transition_count": transition_count + 1,
                "timestamp_ms": timestamp,
            })
    timestamp += 32
    records.append({
        "active_requested": False,
        "animation_in_flight": False,
        "opacity": 0.0,
        "fallback_visible": True,
        "wants_edr": False,
        "transition_count": transition_count + (1 if include_deactivation_animation else 0),
        "timestamp_ms": timestamp,
    })
    timestamp += 100
    for index in range(1, 29):
        timestamp += 16
        records.append({
            "active_requested": True,
            "animation_in_flight": True,
            "opacity": index / 28,
            "fallback_visible": True,
            "wants_edr": True,
            "transition_count": 3,
            "timestamp_ms": timestamp,
        })
    timestamp += 32
    records.append({
        "active_requested": True,
        "animation_in_flight": False,
        "opacity": 1.0,
        "fallback_visible": False,
        "wants_edr": True,
        "transition_count": 3,
        "timestamp_ms": timestamp,
    })
    return records


class HDRFocusTransitionEvidenceTests(unittest.TestCase):
    def test_screen_samples_are_correlated_to_stable_focus_endpoints(self):
        records = [
            {"phase": "final-frame-visible", "timestamp_ms": 1000},
            {"phase": "window-deactivated", "timestamp_ms": 1100},
            {"phase": "inactive-sdr-visible", "timestamp_ms": 1600},
            {"phase": "window-activated", "timestamp_ms": 1900},
        ]
        captures = [
            {"name": "focused", "capture_timestamp_ms": 1050},
            {"name": "during-fade", "capture_timestamp_ms": 1400},
            {"name": "inactive", "capture_timestamp_ms": 1700},
            {"name": "reactivating", "capture_timestamp_ms": 1950},
        ]
        focused, inactive = focus_endpoint_captures(records, captures)
        self.assertEqual(focused["name"], "focused")
        self.assertEqual(inactive["name"], "inactive")

    def test_missing_stable_inactive_capture_is_not_mistaken_for_endpoint(self):
        records = [
            {"phase": "final-frame-visible", "timestamp_ms": 1000},
            {"phase": "window-deactivated", "timestamp_ms": 1100},
            {"phase": "inactive-sdr-visible", "timestamp_ms": 1600},
            {"phase": "window-activated", "timestamp_ms": 1900},
        ]
        captures = [{"name": "during-fade", "capture_timestamp_ms": 1400}]
        focused, inactive = focus_endpoint_captures(records, captures)
        self.assertIsNone(focused)
        self.assertIsNone(inactive)

    def test_accepts_smooth_bidirectional_450ms_transition(self):
        metrics = focus_transition_metrics(trace())
        self.assertTrue(metrics["deactivation_has_multiple_intermediate_frames"])
        self.assertTrue(metrics["deactivation_opacity_decreases_monotonically"])
        self.assertTrue(metrics["deactivation_matches_opening_duration"])
        self.assertTrue(metrics["inactive_endpoint_is_crisp_sdr_proxy"])
        self.assertTrue(metrics["activation_opacity_increases_monotonically"])
        self.assertTrue(metrics["activation_matches_opening_duration"])
        self.assertTrue(metrics["focused_hdr_endpoint_is_restored"])

    def test_rejects_legacy_instant_deactivation_bug(self):
        metrics = focus_transition_metrics(trace(include_deactivation_animation=False))
        self.assertFalse(metrics["deactivation_has_multiple_intermediate_frames"])
        self.assertFalse(metrics["deactivation_opacity_decreases_monotonically"])
        self.assertFalse(metrics["deactivation_matches_opening_duration"])

    def test_rejects_non_monotonic_deactivation_brightness(self):
        metrics = focus_transition_metrics(trace(reverse_sample=True))
        self.assertFalse(metrics["deactivation_opacity_decreases_monotonically"])

    def test_accepts_source_resolution_in_inactive_sdr_endpoint(self):
        self.assertTrue(focus_proxy_matches_source_resolution([{
            "phase": "inactive-sdr-visible",
            "fallback_visible": True,
            "pixel_width": 6048,
            "pixel_height": 8064,
            "fallback_pixmap_width": 6048,
            "fallback_pixmap_height": 8064,
        }]))

    def test_rejects_regression_to_bounded_2048px_inactive_proxy(self):
        self.assertFalse(focus_proxy_matches_source_resolution([{
            "phase": "inactive-sdr-visible",
            "fallback_visible": True,
            "pixel_width": 6048,
            "pixel_height": 8064,
            "fallback_pixmap_width": 1536,
            "fallback_pixmap_height": 2048,
        }]))

    def test_missing_resolution_telemetry_fails_closed(self):
        self.assertFalse(focus_proxy_matches_source_resolution([{
            "phase": "inactive-sdr-visible",
            "fallback_visible": True,
        }]))


if __name__ == "__main__":
    unittest.main(verbosity=2)
