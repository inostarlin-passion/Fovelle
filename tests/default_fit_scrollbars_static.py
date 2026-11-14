"""Source contracts for default Fit geometry and native scrollbars."""

import re
import unittest
from pathlib import Path


SOURCE = (Path(__file__).resolve().parents[1] / "src/qvgraphicsview.cpp").read_text()


def method_body(name):
    return re.search(rf"QVGraphicsView::{name}\([^{{]*\{{(.*?)^\}}", SOURCE, re.M | re.S)[1]


class DefaultFitScrollBarsTests(unittest.TestCase):
    def test_fit_uses_one_calculation(self):
        recalculate = method_body("recalculateZoom")
        calculate = method_body("calculateZoomLevelForMode")
        toggle = method_body("toggleFitAnd100")
        self.assertIn("calculateZoomLevelForMode", recalculate)
        self.assertIn("calculateZoomLevelForMode", toggle)
        self.assertIn("getFitViewportSize", calculate)
        self.assertIn("containedFitScale", calculate)

    def test_fit_leaves_scrollbar_policies_to_qt(self):
        constructor = method_body("QVGraphicsView")
        for axis in ("Horizontal", "Vertical"):
            setter = f"set{axis}ScrollBarPolicy"
            self.assertIn(f"{setter}(Qt::ScrollBarAsNeeded)", constructor)
            for method in ("recalculateZoom", "calculateZoomLevelForMode", "toggleFitAnd100"):
                self.assertNotIn(setter, method_body(method))


if __name__ == "__main__":
    unittest.main()
