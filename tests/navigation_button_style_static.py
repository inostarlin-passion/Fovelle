#!/usr/bin/env python3
"""Source contracts for the single floating navigation button style."""

from pathlib import Path
import unittest

SRC = Path(__file__).resolve().parents[1] / "src"
WINDOW = (SRC / "mainwindow.cpp").read_text(encoding="utf-8")
WIDGET = WINDOW.split("class ImageNavigationButton", 1)[1].split("class TitlebarBubble", 1)[0]
COCOA = (SRC / "qvcocoafunctions.mm").read_text(encoding="utf-8")
OVERLAY = COCOA.split("    void setNavigationOverlay(", 1)[1].split(
    "    void clearNavigationOverlays()", 1
)[0]
VIEW = (SRC / "qvgraphicsview.cpp").read_text(encoding="utf-8")
HEADERS = "\n".join(
    (SRC / name).read_text(encoding="utf-8")
    for name in ("mainwindow.h", "qvgraphicsview.h", "qvcocoafunctions.h")
)


class NavigationButtonStyleTests(unittest.TestCase):
    def test_widget_draws_dark_artwork(self):
        self.assertIn("QColor background(128, 128, 128, isHovered ? 235 : 220)", WIDGET)
        self.assertIn("background.setAlpha(100)", WIDGET)
        self.assertIn("drawRoundedRect(rect().adjusted(1, 1, -1, -1), 10, 10)", WIDGET)
        self.assertIn("QColor foreground(48, 48, 48)", WIDGET)
        self.assertIn("foreground.setAlpha(90)", WIDGET)
        for obsolete in (
            "updateNavigationButtonAppearance",
            "setDarkBackground",
            "sampleDisplayedImageBrightness",
            "sampledContentBrightness",
            "navigationSamplingImage",
            "light-transparent",
        ):
            with self.subTest(obsolete=obsolete):
                self.assertNotIn(obsolete, WINDOW + VIEW + HEADERS)

    def test_native_draws_dark_artwork(self):
        self.assertIn("QColor background(128, 128, 128, highlighted ? 235 : 220)", OVERLAY)
        self.assertIn("background.setAlpha(100)", OVERLAY)
        self.assertIn("QColor foreground(48, 48, 48)", OVERLAY)
        self.assertIn("foreground.setAlpha(90)", OVERLAY)
        self.assertIn("backgroundLayer.fillColor = backgroundCGColor", OVERLAY)
        self.assertIn("backgroundLayer.hidden = !artworkVisible;", OVERLAY)
        self.assertIn("chevronLayer.strokeColor = foregroundCGColor", OVERLAY)
        self.assertNotIn("darkBackground", WINDOW + VIEW + COCOA + HEADERS)

    def test_opacity_applies_to_composite(self):
        self.assertIn("artwork.fill(Qt::transparent)", WIDGET)
        self.assertLess(WIDGET.index("artworkPainter.end()"), WIDGET.index("painter.setOpacity"))
        self.assertIn("painter.setOpacity(paintOpacity)", WIDGET)
        self.assertIn("painter.drawImage(QPointF(0.0, 0.0), artwork)", WIDGET)
        self.assertIn("navigationButtonLayers[index].allowsGroupOpacity = YES", COCOA)
        self.assertIn("buttonLayer.opacity = boundedOpacity", OVERLAY)
        self.assertIn("backgroundLayer.opacity = 1.0F", OVERLAY)
        self.assertIn("chevronLayer.opacity = 1.0F", OVERLAY)


if __name__ == "__main__":
    unittest.main(verbosity=2)
