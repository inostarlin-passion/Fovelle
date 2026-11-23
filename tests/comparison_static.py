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
