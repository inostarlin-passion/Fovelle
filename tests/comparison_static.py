from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def comparison_loader():
    return (ROOT / "src/qvimageloader.cpp").read_text().split(
        "QVImageLoader::readComparisonImage", 1
    )[1].split("QVImageLoader::readFile", 1)[0]


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
        loader = comparison_loader()
        self.assertRegex(
            loader,
            r"(?s)const QByteArray version = fileVersion\(path\).*?"
            r"reader.supportsAnimation\(\).*?QvAsync::isCanceled\(\) \|\| "
            r"fileVersion\(path\) != version.*?readFile\(path, 2048, false\).*?"
            r"QvAsync::isCanceled\(\) \|\| fileVersion\(path\) != version.*?"
            r"if \(result.errorData.*?return reject\(tr\(\"Unable to display this image\.\"\)\)"
            r".*?result.fileVersionStamp = version",
        )
        controls = (ROOT / "src/qvcomparison.cpp").read_text()
        self.assertRegex(
            controls,
            r"(?s)if \(request != selection\).*?return;.*?if \(!isActive\(\)\)"
            r".*?return;.*?if \(result.errorData\).*?return;.*?commitComparisonImage"
            r".*?QvAsync::Lane::Comparison",
        )
        self.assertNotIn("readThumbnail", loader)

    def test_worker_recovers_allocation_failure(self):
        self.assertRegex(
            comparison_loader(),
            r"(?s)catch \(const std::bad_alloc &\).*?tr\(\"Not enough memory to load this image\.\"\)",
        )

    def test_no_comparison_budget(self):
        loader_header = (ROOT / "src/qvimageloader.h").read_text()
        self.assertIn("readComparisonImage(const QString &path);", loader_header)
        compare = (ROOT / "src/qvcomparison.cpp").read_text()
        self.assertIn("readComparisonImage(path)", compare)
        for filename in (
            "src/qvcomparison.cpp", "src/qvcomparison.h", "src/qvimageloader.cpp",
            "src/qvimageloader.h", "src/qvcocoafunctions.h", "src/qvcocoafunctions.mm",
            "i18n/qview_zh_Hans.ts", "i18n/qview_zh_Hant.ts",
        ):
            with self.subTest(filename=filename):
                source = (ROOT / filename).read_text()
                self.assertNotRegex(
                    source,
                    r"(?i)comparison(?:Budget|AvailableBytes|Reservation|ImageSize|Footprint|"
                    r"ImageBytes|SourceBytes|DrawableBytes)|committed(?:Bytes|Reservation)"
                    r"|comparison memory budget",
                )
        self.assertNotIn('tr("SDR preview")', compare)


if __name__ == "__main__":
    unittest.main()
