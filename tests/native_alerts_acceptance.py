#!/usr/bin/env python3
"""Static acceptance checks for delegating every QMessageBox to NSAlert."""

from pathlib import Path
import re
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
TESTS = ROOT / "tests"

CASES = {
    "ST-ALERT-CENTRAL": {
        "purpose": "Find every production QMessageBox construction and ensure it uses one adapter.",
        "preconditions": "The repository source tree is available.",
        "input": "All production .cpp and .mm files.",
        "steps": "Strip comments and C++ string literals, enumerate QMessageBox constructors and static convenience calls.",
        "expected": "Only src/nativedialogs.cpp constructs QMessageBox; production callers use NativeDialogs; no static convenience call bypasses the adapter.",
        "postcondition": "No files or application state are changed.",
    },
    "ST-ALERT-SEMANTICS": {
        "purpose": "Ensure the common factory supplies only message semantics to QMessageBox.",
        "preconditions": "The shared alert factory is present.",
        "input": "The factory's message, informative, severity, and standard-button properties.",
        "steps": "Inspect the factory body after removing comments and literals.",
        "expected": "The factory sets severity, plain messageText, optional informativeText, standardButtons and plain-text format; it adds no title, custom icon, or custom button.",
        "postcondition": "The source remains unchanged.",
    },
    "ST-ALERT-NATIVE-ROUTE": {
        "purpose": "Reject source configurations that can switch a QMessageBox to Qt's QWidget implementation.",
        "preconditions": "Qt 6.6 or later Cocoa sources are built.",
        "input": "Native-dialog option, modality, rich text/detail properties, and show method.",
        "steps": "Check the factory and all message-box callsites for native fallback triggers.",
        "expected": "DontUseNativeDialog is false before content; alerts are application-modal; text is normalized to plain text; detailedText and open() are not used.",
        "postcondition": "The test only reads source files.",
    },
    "ST-ALERT-NATIVE-STYLING": {
        "purpose": "Prevent application-owned geometry and visuals from competing with AppKit.",
        "preconditions": "Production C++ sources are present.",
        "input": "Methods invoked on QMessageBox instances and the shared factory.",
        "steps": "Scan alert objects for style sheets, fixed size, geometry, margins, fonts, custom layouts, and custom button APIs.",
        "expected": "No message box receives application-authored geometry or visual layout; standard-button behavior methods remain permitted.",
        "postcondition": "Unrelated dialogs and controls are not changed by this static check.",
    },
    "ST-ALERT-LOCALIZATION": {
        "purpose": "Ensure business copy remains translatable and standard button labels come from the Cocoa platform theme.",
        "preconditions": "Qt's Cocoa platform implementation is the configured dialog backend.",
        "input": "Production NativeDialogs callsites and native alert factory.",
        "steps": "Inspect content calls for translation wrappers and confirm no app-authored standard button labels are installed.",
        "expected": "Production message and informative copy is supplied through translated strings; buttons are QMessageBox standard identifiers only.",
        "postcondition": "Translation catalogs and source files remain unchanged.",
    },
    "UT-ALERT-NSALERT": {
        "purpose": "Verify the running Cocoa platform helper actually creates an NSAlert for every severity and appearance.",
        "preconditions": "A macOS Cocoa QtTest build and the native alert log category are available.",
        "input": "Information, Warning, Critical, Question; Light/Dark; long Japanese/Spanish informative text containing markup.",
        "steps": "Run testNativeMessageBoxesUseCocoaAlertsAcrossAppearanceAndSeverity and capture qt.qpa.dialogs output.",
        "expected": "Each case shows an NSAlert, keeps the requested severity, preserves plain content, and inherits Aqua/DarkAqua from the active AppKit modal window.",
        "postcondition": "All alerts close and saved appearance settings are restored.",
    },
    "UT-ALERT-RESPONSES": {
        "purpose": "Verify standard AppKit alert responses map back to the requested QMessageBox button identifiers.",
        "preconditions": "A macOS Cocoa QtTest build is available.",
        "input": "Save, Discard, and Cancel.",
        "steps": "Run testNativeMessageBoxStandardButtonResponses; click each standard response and observe the Qt result.",
        "expected": "Only three standard buttons are present; the native NSAlert trace is observed; clicked/returned identifiers match.",
        "postcondition": "Every test alert is closed and deleted.",
    },
    "MT-ALERT-REVERSE": {
        "purpose": "Demonstrate that the static acceptance checks reject representative native-path and custom-layout regressions.",
        "preconditions": "Static source-contract checks pass against the production tree.",
        "input": "Mutants that switch to WindowModal, set a fixed size, add a custom button, or enable DontUseNativeDialog.",
        "steps": "Feed each in-memory source mutant to its corresponding acceptance check without writing it to disk.",
        "expected": "Every mutant is rejected by the native-route or no-custom-styling acceptance check.",
        "postcondition": "The repository source remains unchanged.",
    },
}


def source_without_comments_and_strings(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    text = re.sub(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', '""', text)
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    return text


def production_sources():
    return [p for p in SRC.rglob("*") if p.suffix in {".cpp", ".mm"}]


def call_arguments(source: str, function_name: str):
    marker = function_name + "("
    offset = 0
    while True:
        start = source.find(marker, offset)
        if start < 0:
            return
        open_paren = start + len(function_name)
        depth = 0
        for index in range(open_paren, len(source)):
            if source[index] == "(":
                depth += 1
            elif source[index] == ")":
                depth -= 1
                if depth == 0:
                    yield source[open_paren + 1:index]
                    offset = index + 1
                    break
        else:
            raise AssertionError(f"Unclosed call to {function_name}")


class NativeAlertsAcceptance(unittest.TestCase):
    def test_ST_CASE_METADATA_COMPLETE(self):
        required_fields = {"purpose", "preconditions", "input", "steps", "expected", "postcondition"}
        self.assertTrue(CASES)
        for case_id, fields in CASES.items():
            self.assertEqual(set(fields), required_fields, case_id)

    def test_ST_ALERT_CENTRAL(self):
        constructions = []
        construction = re.compile(r"\bnew\s+QMessageBox\s*\(")
        stack_construction = re.compile(r"\bQMessageBox\s+[A-Za-z_]\w*\s*(?:\(|\{)")
        subclass = re.compile(r"\b(?:class|struct)\s+[A-Za-z_]\w*\s*:\s*public\s+QMessageBox\b")
        static_call = re.compile(r"\bQMessageBox::(?:information|warning|critical|question)\s*\(")
        for path in production_sources():
            source = source_without_comments_and_strings(path)
            for match in construction.finditer(source):
                constructions.append(path.relative_to(ROOT).as_posix())
            for match in stack_construction.finditer(source):
                constructions.append(path.relative_to(ROOT).as_posix())
            self.assertIsNone(subclass.search(source), path.relative_to(ROOT).as_posix())
            self.assertIsNone(static_call.search(source), path.relative_to(ROOT).as_posix())
        self.assertEqual(constructions, ["src/nativedialogs.cpp"])
        for path in production_sources():
            source = source_without_comments_and_strings(path)
            if "QMessageBox" in source and path.name not in {"nativedialogs.cpp"}:
                self.assertIn("NativeDialogs::", source, path.relative_to(ROOT).as_posix())

    def test_ST_ALERT_SEMANTICS(self):
        source = source_without_comments_and_strings(SRC / "nativedialogs.cpp")
        factory_start = source.index("QMessageBox *createMessageBox(")
        factory_end = source.index("void showMessage(", factory_start)
        factory = source[factory_start:factory_end]
        for required in (
            "messageBox->setIcon(severity)",
            "messageBox->setText(plainAlertText(messageText))",
            "messageBox->setInformativeText(plainAlertText(informativeText))",
            "messageBox->setTextFormat(Qt::PlainText)",
            "messageBox->setStandardButtons(buttons)",
        ):
            self.assertIn(required, factory)
        for forbidden in (
            "setWindowTitle(", "setIconPixmap(", "addButton(", "setButtonText(",
            "setDetailedText(", "setCheckBox(",
        ):
            self.assertNotIn(forbidden, factory)
        self.assertIn("QString plainAlertText(", source)
        self.assertIn("Qt::mightBeRichText(text)", source)
        self.assertIn("document.setHtml(text)", source)

    def test_ST_ALERT_NATIVE_ROUTE(self):
        source = source_without_comments_and_strings(SRC / "nativedialogs.cpp")
        self.assertIn("setOption(QMessageBox::Option::DontUseNativeDialog, false)", source)
        constructor = source.index("new QMessageBox(parent)")
        native_flag = source.index("setOption(QMessageBox::Option::DontUseNativeDialog, false)")
        text_assignment = source.index("setText(plainAlertText(messageText))")
        self.assertLess(constructor, native_flag)
        self.assertLess(native_flag, text_assignment)
        self.assertIn("setWindowModality(Qt::ApplicationModal)", source)
        self.assertIn("messageBox->show()", source)
        self.assertNotIn("messageBox->open()", source)
        for path in production_sources():
            source = source_without_comments_and_strings(path)
            self.assertNotRegex(source, r"\b(?:msgBox|messageBox|alert)\s*->\s*setDetailedText\s*\(")
            self.assertNotRegex(source, r"\b(?:msgBox|messageBox|alert)\s*->\s*open\s*\(")
            self.assertNotRegex(source, r"DontUseNativeDialog\s*,\s*true")

    def test_ST_ALERT_NATIVE_STYLING(self):
        forbidden = re.compile(
            r"\b(?:msgBox|messageBox|alert)\s*->\s*"
            r"(?:setStyleSheet|setFixedSize|setFixedWidth|setFixedHeight|setGeometry|"
            r"setContentsMargins|setMargin|setFont|setLayout|setIconPixmap|setWindowTitle|"
            r"setCheckBox|addButton|removeButton|setButtonText)\s*\("
        )
        for path in production_sources():
            source = source_without_comments_and_strings(path)
            self.assertIsNone(forbidden.search(source), path.relative_to(ROOT).as_posix())
        factory = source_without_comments_and_strings(SRC / "nativedialogs.cpp")
        self.assertNotIn("applyTheme(messageBox)", factory)
        self.assertNotRegex(factory, r"\b(?:setFixedSize|setGeometry|setContentsMargins|setStyleSheet)\s*\(")
        for path in production_sources():
            source = source_without_comments_and_strings(path)
            self.assertNotRegex(source, r"\b(?:msgBox|messageBox|alert)\s*->\s*setDetailedText\s*\(")
            self.assertNotRegex(source, r"\bNativeDialogs::applyTheme\s*\(\s*(?:msgBox|messageBox|alert)\s*\)")

    def test_ST_ALERT_LOCALIZATION(self):
        production_call_count = 0
        for file_name in (
            "qvapplication.cpp", "qvoptionsdialog.cpp", "qvrenamedialog.cpp",
            "qvshortcutdialog.cpp", "mainwindow.cpp", "updatechecker.cpp",
        ):
            source = source_without_comments_and_strings(SRC / file_name)
            self.assertIn("NativeDialogs::", source, file_name)
            for function in ("NativeDialogs::showMessage", "NativeDialogs::createMessageBox"):
                for arguments in call_arguments(source, function):
                    production_call_count += 1
                    localized_argument = "tr(" in arguments or (
                        "messageText" in arguments and "messageText = tr(" in source
                    )
                    self.assertTrue(localized_argument,
                                    f"untranslated alert in {file_name}: {function}")
        self.assertGreater(production_call_count, 0)
        source = source_without_comments_and_strings(SRC / "nativedialogs.cpp")
        self.assertIn("setStandardButtons(buttons)", source)
        self.assertNotRegex(source, r"addButton\s*\(\s*(?:tr|QStringLiteral|QString)\s*\(")
        test_source = source_without_comments_and_strings(TESTS / "tst_qviewtests.cpp")
        self.assertIn("longLocalizedText", test_source)
        self.assertIn("QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel", test_source)

    def test_UT_ALERT_DYNAMIC_TESTS_REGISTERED(self):
        cmake = (TESTS / "CMakeLists.txt").read_text(encoding="utf-8")
        test_source = source_without_comments_and_strings(TESTS / "tst_qviewtests.cpp")
        self.assertIn("add_test(NAME FovelleNativeAlertsDynamic", cmake)
        for method in (
            "testNativeMessageBoxesUseCocoaAlertsAcrossAppearanceAndSeverity",
            "testNativeMessageBoxStandardButtonResponses",
        ):
            self.assertIn("void WindowBehaviorTests::" + method + "()", test_source)
            self.assertIn(method, cmake)
        raw_test_source = (TESTS / "tst_qviewtests.cpp").read_text(encoding="utf-8")
        for field in ("Purpose:", "Preconditions:", "Input:", "Steps:", "Expected:", "Postcondition:"):
            self.assertIn(field, raw_test_source)

    def test_MT_ALERT_REVERSE_FALSIFICATION_GUARDS(self):
        original_reader = source_without_comments_and_strings
        source_path = SRC / "nativedialogs.cpp"
        base = original_reader(source_path)
        cases = (
            ("setWindowModality(Qt::ApplicationModal)",
             "setWindowModality(Qt::WindowModal)", self.test_ST_ALERT_NATIVE_ROUTE),
            ("messageBox->setStandardButtons(buttons);",
             "messageBox->setStandardButtons(buttons); messageBox->setFixedSize(320, 180);",
             self.test_ST_ALERT_NATIVE_STYLING),
            ("messageBox->setStandardButtons(buttons);",
             'messageBox->setStandardButtons(buttons); messageBox->addButton(QStringLiteral("Custom"), QMessageBox::ActionRole);',
             self.test_ST_ALERT_NATIVE_STYLING),
            ("DontUseNativeDialog, false", "DontUseNativeDialog, true",
             self.test_ST_ALERT_NATIVE_ROUTE),
        )
        for old, new, acceptance_check in cases:
            self.assertIn(old, base)
            mutant = base.replace(old, new, 1)

            def read_mutant(path):
                return mutant if path == source_path else original_reader(path)

            with mock.patch(__name__ + ".source_without_comments_and_strings", side_effect=read_mutant):
                with self.assertRaises(AssertionError, msg=f"mutation escaped detection: {new}"):
                    acceptance_check()


if __name__ == "__main__":
    unittest.main(verbosity=2)
