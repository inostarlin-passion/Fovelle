from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ComparisonSourceTests(unittest.TestCase):
    def test_ui_wiring(self):
        menu = (ROOT / "src/actionmanager.cpp").read_text()
        for key in ("compare", "comparezoom", "comparepan", "compareswap"):
            self.assertIn(f'addCloneOfAction(viewMenu, "{key}")', menu)
        controls = (ROOT / "src/qvcomparison.cpp").read_text()
        self.assertIn("setAccessibleName(", controls)
        self.assertIn("QFileDialog::ExistingFile", controls)
        for build in ("CMakeLists.txt", "src/src.pri"):
            self.assertIn("qvcomparison.cpp", (ROOT / build).read_text())

    def test_divider_layout_contract(self):
        controls = (ROOT / "src/qvcomparison.cpp").read_text()
        native = (ROOT / "src/qvcocoafunctions.mm").read_text()
        self.assertIn("setOpaqueResize(false)", controls)
        self.assertNotIn("setSizes({ 1, 1 })", controls)
        self.assertRegex(controls, r"(?s)referencePane->setMinimumWidth\(minimumPaneWidth\).*"
                                   r"candidatePane->setMinimumWidth\(minimumPaneWidth\)")
        self.assertIn("closestLegalPosition(", controls)
        self.assertIn("fovelle.comparison.divider", native)
        self.assertIn("ComparisonDividerGuide::show", native)

    def test_live_session_restore_contract(self):
        view = (ROOT / "src/qvgraphicsview.cpp").read_text()
        restore = view.split("void QVGraphicsView::loadSessionState", 1)[1].split(
            "void QVGraphicsView::setLoadIsFromSessionRestore", 1
        )[0]
        self.assertIn("setTransform(transform)", restore)
        self.assertRegex(
            restore,
            r"(?s)if \(getCurrentFileDetails\(\).isPixmapLoaded\).*?"
            r"commitZoomImmediately\(makeZoomPlan\(.*?else.*?zoomAbsolute\(",
        )
        compare = (ROOT / "src/qvcomparison.cpp").read_text()
        leave = compare.split("void QVComparison::leave()", 1)[1].split("void QVComparison::swap", 1)[0]
        self.assertLess(leave.index("loadSessionState("), leave.index("restoreComparisonCenter("))
        self.assertLess(leave.index("restoreComparisonCenter("), leave.index("setFocus()"))

    def test_worker_contract(self):
        worker = (ROOT / "src/asyncwork.cpp").read_text()
        self.assertRegex(worker, r"(?s)comparisonPool\(\).*?setMaxThreadCount\(1\)")
        loader = (ROOT / "src/qvimageloader.cpp").read_text().split(
            "QVImageLoader::readComparisonImage", 1
        )[1]
        before = loader.index("fileVersion(path)")
        preflight = loader.index("comparisonImageBytes(", before)
        decode = loader.index("readFile(path,", preflight)
        self.assertIn("fileVersion(path)", loader[decode:])
        controls = (ROOT / "src/qvcomparison.cpp").read_text()
        self.assertRegex(controls, r"(?s)if \(.*?request != .*?\).*?return;.*?commitComparisonImage")


if __name__ == "__main__":
    unittest.main()
