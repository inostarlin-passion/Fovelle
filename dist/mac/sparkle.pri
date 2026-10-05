# Provision the pinned official distribution with CMake once, or set FOVELLE_SPARKLE_ROOT.
FOVELLE_REPO_ROOT = $$clean_path($$PWD/../..)
SPARKLE_ROOT = $$(FOVELLE_SPARKLE_ROOT)
isEmpty(SPARKLE_ROOT): SPARKLE_ROOT = $$FOVELLE_REPO_ROOT/build/_deps/sparkle
!exists($$SPARKLE_ROOT/Sparkle.framework): error("Provision Sparkle with CMake first or set FOVELLE_SPARKLE_ROOT")
LIBS += -F$$shell_quote($$SPARKLE_ROOT) -framework Sparkle
QMAKE_LFLAGS += -Wl,-rpath,@executable_path/../Frameworks
!contains(CONFIG, app_bundle): QMAKE_LFLAGS += -Wl,-rpath,$$shell_quote($$SPARKLE_ROOT)
SPARKLE_SOURCE = $$FOVELLE_REPO_ROOT/src/updatechecker_sparkle.mm
sparkle_compile.input = SPARKLE_SOURCE
sparkle_compile.output = $$OBJECTS_DIR/updatechecker_sparkle.o
sparkle_compile.commands = $$QMAKE_CXX -c -x objective-c++ -fobjc-arc $(CXXFLAGS) $(INCPATH) -F$$shell_quote($$SPARKLE_ROOT) ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT}
sparkle_compile.variable_out = OBJECTS
sparkle_compile.CONFIG += target_predeps
QMAKE_EXTRA_COMPILERS += sparkle_compile
contains(CONFIG, app_bundle) {
    SPARKLE_PLIST = $$OUT_PWD/Fovelle-Sparkle-Info.plist
    PLIST_COMMAND = python3 $$shell_quote($$FOVELLE_REPO_ROOT/dist/scripts/configure-update-plist.py) $$shell_quote($$FOVELLE_REPO_ROOT/dist/mac/Info.plist.in) $$shell_quote($$SPARKLE_PLIST) $$shell_quote($$VERSION)
    !system($$PLIST_COMMAND): error("Cannot generate Sparkle Info.plist")
    QMAKE_INFO_PLIST = $$SPARKLE_PLIST
    sparkle_license.files = $$FOVELLE_REPO_ROOT/third_party/sparkle/LICENSE
    sparkle_license.path = Contents/Resources/licenses/Sparkle
    QMAKE_BUNDLE_DATA += sparkle_license
    QMAKE_POST_LINK += /usr/bin/ditto $$shell_quote($$SPARKLE_ROOT/Sparkle.framework) $$shell_quote($$DESTDIR/Fovelle.app/Contents/Frameworks/Sparkle.framework)
}
