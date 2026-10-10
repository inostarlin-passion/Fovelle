// Detailed six-part cases and criteria are recorded in zoom_color_cases.json.
// All fixtures/settings are scoped and restored even on an assertion return.
void GraphicsViewTests::testKeepZoomAcrossNavigation_data()
{
    QTest::addColumn<QString>("format");
    QTest::addColumn<double>("level");
    for (const auto &format :
         { QStringLiteral("png"), QStringLiteral("xpm"), QStringLiteral("svg") })
        for (double level : { 0.01, 1.2, 64.0 })
            QTest::newRow(qPrintable(format + QString::number(level))) << format << level;
}

void GraphicsViewTests::testKeepZoomAcrossNavigation()
{
    QFETCH(QString, format);
    QFETCH(double, level);
    ScopedOptionValues options({ { "navresetszoom", false }, { "smallimageoneone", false } });
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto make = [&](const QString &name, QSize size) {
        if (format == "svg") {
            QString path = dir.filePath(name + ".svg");
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly))
                return QString();
            f.write(QString("<svg xmlns='http://www.w3.org/2000/svg' width='%1' height='%2'><rect "
                            "width='100%%' height='100%%' fill='red'/></svg>")
                            .arg(size.width())
                            .arg(size.height())
                            .replace("%%", "%")
                            .toUtf8());
            return path;
        }
        return format == "png" ? createTestImage(dir, name, Qt::red, size)
                               : createQtRasterTestImage(dir, name, Qt::red, size);
    };
    const auto a = make("a", QSize(640, 400));
    const auto b = make("b", QSize(220, 700));
    QVERIFY(!a.isEmpty() && !b.isEmpty());
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    const bool quitPolicy = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    const auto closeWindow = qScopeGuard([&]() {
        window.close();
        qvApp->setQuitOnLastWindowClosed(quitPolicy);
    });
    window.resize(800, 600);
    window.show();
    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    window.openFile(a);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    view->zoomAbsolute(level);
    QCOMPARE(view->getZoomLevel(), level);
    window.openFile(b);
    QTRY_COMPARE_WITH_TIMEOUT(window.getCurrentFileDetails().fileInfo.absoluteFilePath(), b, 5000);
    QVERIFY(window.getIsPixmapLoaded());
    QCOMPARE(view->getZoomLevel(), level);
    QVERIFY(!view->getCalculatedZoomMode().has_value());
    window.resize(700, 550);
    QTest::qWait(120);
    QCOMPARE(view->getZoomLevel(), level);
    QVERIFY(!view->getCalculatedZoomMode().has_value());
    window.openFile(a); // Cached reverse navigation uses the same contract.
    QTRY_COMPARE_WITH_TIMEOUT(window.getCurrentFileDetails().fileInfo.absoluteFilePath(), a, 5000);
    QCOMPARE(view->getZoomLevel(), level);
}

void GraphicsViewTests::testKeepCalculatedZoomAndFailedNavigation()
{
    ScopedOptionValues options({ { "navresetszoom", false }, { "smallimageoneone", false } });
    QTemporaryDir dir;
    const auto a = createTestImage(dir, "a", Qt::red, QSize(1600, 1000));
    const auto b = createTestImage(dir, "b", Qt::blue, QSize(240, 700));
    const auto c = createTestImage(dir, "c", Qt::green, QSize(900, 300));
    const auto bad = dir.filePath("broken.png");
    {
        QFile f(bad);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("broken");
    }
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    const bool quitPolicy = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    const auto closeWindow = qScopeGuard([&]() {
        window.close();
        qvApp->setQuitOnLastWindowClosed(quitPolicy);
    });
    window.resize(800, 600);
    window.show();
    auto *view = window.findChild<QVGraphicsView *>();
    window.openFile(a);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    view->setCalculatedZoomMode(Qv::CalculatedZoomMode::ZoomToFit);
    const double fit = view->getZoomLevel();
    window.openFile(b);
    QTRY_COMPARE_WITH_TIMEOUT(window.getCurrentFileDetails().fileInfo.absoluteFilePath(), b, 5000);
    QCOMPARE(view->getZoomLevel(), fit);
    QVERIFY(!view->getCalculatedZoomMode().has_value());
    window.resize(720, 540);
    QTest::qWait(100);
    QCOMPARE(view->getZoomLevel(), fit);
    view->zoomAbsolute(1.2);
    window.openFile(bad);
    QTRY_VERIFY_WITH_TIMEOUT(window.getCurrentFileDetails().errorData.has_value(), 5000);
    window.openFile(c);
    QTRY_COMPARE_WITH_TIMEOUT(window.getCurrentFileDetails().fileInfo.absoluteFilePath(), c, 5000);
    QCOMPARE(view->getZoomLevel(), 1.2);
    window.openFile(a);
    window.openFile(b);
    view->zoomAbsolute(1.5);
    QTRY_COMPARE_WITH_TIMEOUT(window.getCurrentFileDetails().fileInfo.absoluteFilePath(), b, 5000);
    QCOMPARE(view->getZoomLevel(), 1.5);
    QTest::qWait(150);
    QCOMPARE(window.getCurrentFileDetails().fileInfo.absoluteFilePath(), b);
    QCOMPARE(view->getZoomLevel(), 1.5);
    view->closeImage();
    window.openFile(a);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    QVERIFY(view->getCalculatedZoomMode().has_value()); // Fresh opening uses default.
}

void GraphicsViewTests::testKeepZoomWindowIsolationAndSession()
{
    ScopedOptionValues options({ { "navresetszoom", false }, { "smallimageoneone", false } });
    QTemporaryDir dir;
    const auto a = createTestImage(dir, "a", Qt::red, QSize(1600, 1000));
    const auto b = createTestImage(dir, "b", Qt::blue, QSize(100, 200));
    MainWindow first, second;
    first.setAttribute(Qt::WA_DeleteOnClose, false);
    second.setAttribute(Qt::WA_DeleteOnClose, false);
    const bool quitPolicy = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    const auto closeWindows = qScopeGuard([&]() {
        first.close();
        second.close();
        qvApp->setQuitOnLastWindowClosed(quitPolicy);
    });
    first.show();
    second.show();
    auto *v1 = first.findChild<QVGraphicsView *>();
    auto *v2 = second.findChild<QVGraphicsView *>();
    first.openFile(a);
    second.openFile(a);
    QTRY_VERIFY_WITH_TIMEOUT(first.getIsPixmapLoaded() && second.getIsPixmapLoaded(), 5000);
    v1->zoomAbsolute(1.2);
    v2->zoomAbsolute(0.7);
    first.openFile(b);
    second.openFile(b);
    QTRY_COMPARE_WITH_TIMEOUT(first.getCurrentFileDetails().fileInfo.absoluteFilePath(), b, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(second.getCurrentFileDetails().fileInfo.absoluteFilePath(), b, 5000);
    QCOMPARE(v1->getZoomLevel(), 1.2);
    QCOMPARE(v2->getZoomLevel(), 0.7);
    const auto session = v1->getSessionState();
    v2->closeImage();
    v2->loadSessionState(session);
    v2->setLoadIsFromSessionRestore(true);
    second.openFile(a);
    QTRY_VERIFY_WITH_TIMEOUT(second.getIsPixmapLoaded(), 5000);
    QCOMPARE(v2->getZoomLevel(), 1.2);
    QVERIFY(!v2->getCalculatedZoomMode().has_value());
    // Disabling preservation continues to use the historical default policy.
    v2->setNavigationResetsZoom(true);
    second.openFile(b);
    QTRY_COMPARE_WITH_TIMEOUT(second.getCurrentFileDetails().fileInfo.absoluteFilePath(), b, 5000);
    QVERIFY(v2->getCalculatedZoomMode().has_value());
    QCOMPARE(v1->getZoomLevel(), 1.2);
}

void FeatureTests::testKeepZoomPreferenceAndColorTranslations()
{
    ScopedOptionValues options({ { "navresetszoom", true } });
    QVOptionsDialog dialog;
    auto *keep = dialog.findChild<QCheckBox *>("keepZoomCheckbox");
    QVERIFY(keep);
    QVERIFY(!keep->isChecked());
    keep->click();
    QVERIFY(!QSettings().value("options/navresetszoom").toBool());
    QVERIFY(!qvApp->getSettingsManager().getBoolean("navresetszoom"));
    QVOptionsDialog reopened;
    QVERIFY(reopened.findChild<QCheckBox *>("keepZoomCheckbox")->isChecked());
#ifdef FOVELLE_TRANSLATIONS_DIR
    const QStringList keys = {
        "Source color space:", "Embedded ICC profile:", "Current output color space:", "Unknown",
        "Not embedded",        "Invalid profile",       "Unsupported profile",         "Preview: %1"
    };
    SourceLanguageTranslator sourceTranslator;
    QVERIFY(QCoreApplication::installTranslator(&sourceTranslator));
    const auto restoreSourceLanguage =
            qScopeGuard([&]() { QCoreApplication::removeTranslator(&sourceTranslator); });
    QVInfoDialog info;
    QTemporaryDir fixtures;
    QImage preview(32, 24, QImage::Format_RGB32);
    preview.fill(Qt::red);
    preview.setColorSpace(QColorSpace::DisplayP3);
    const auto previewPath = fixtures.filePath("profile.png");
    QVERIFY(preview.save(previewPath));
    QvColor::Information information;
    information.source = QvColor::readSource(previewPath);
    information.hasOutput = true;
    information.outputSpace = QStringLiteral("Extended Linear Display P3");
    info.setColorInfoProvider([information]() { return information; });
    info.setInfo(QFileInfo(previewPath), preview.size(), 0);
    info.show();
    dialog.setAttribute(Qt::WA_DeleteOnClose, false);
    const auto closeSettings = qScopeGuard([&]() { dialog.close(); });
    dialog.prepareForDisplay();
    dialog.show();
    for (const auto &language : { "zh_Hans", "zh_Hant", "es", "ja" }) {
        QTranslator translator;
        QVERIFY(translator.load(QStringLiteral(FOVELLE_TRANSLATIONS_DIR) + "/qview_" + language
                                + ".qm"));
        for (const auto &key : keys)
            QVERIFY2(!translator.translate("QVInfoDialog", key.toUtf8().constData()).isEmpty(),
                     qPrintable(key));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        QCoreApplication::processEvents();
        QCOMPARE(info.findChild<QLabel *>("sourceColorSpaceCaption")->text(),
                 translator.translate("QVInfoDialog", "Source color space:"));
        QCOMPARE(dialog.findChild<QCheckBox *>("keepZoomCheckbox")->text(),
                 translator.translate("QVOptionsDialog", "Keep zoom level when switching images"));
        QTRY_VERIFY_WITH_TIMEOUT(!dialog.property("settingsCategoryTransitionActive").toBool()
                                         && keep->width() >= keep->sizeHint().width(),
                                 2000);
        const auto *area = dialog.findChild<QScrollArea *>("generalScrollArea");
        QVERIFY(area);
        QVERIFY(area->viewport()->rect().contains(
                QRect(keep->mapTo(area->viewport(), QPoint()), keep->size())));
        auto *profileLabel = info.findChild<QLabel *>("iccProfileLabel");
        QCOMPARE(profileLabel->textFormat(), Qt::PlainText);
        QVERIFY(profileLabel->text().contains(information.source.profileDescription));
        info.layout()->activate();
        for (const auto &name :
             { "sourceColorSpaceLabel", "iccProfileLabel", "outputColorSpaceLabel" }) {
            const auto *value = info.findChild<QLabel *>(name);
            QVERIFY2(value->height() >= value->heightForWidth(value->width()), name);
        }
        const QString evidence = qEnvironmentVariable("FOVELLE_TEST_EVIDENCE_DIR");
        if (!evidence.isEmpty()) {
            QVERIFY(QDir().mkpath(evidence));
            QVERIFY(info.grab().save(evidence + "/file-info-" + language + ".png"));
            QVERIFY(dialog.grab().save(evidence + "/settings-" + language + ".png"));
        }
        QCoreApplication::removeTranslator(&translator);
        QCoreApplication::processEvents();
    }
    QCOMPARE(info.findChild<QLabel *>("sourceColorSpaceCaption")->text(),
             QString("Source color space:"));
    QTRY_VERIFY_WITH_TIMEOUT(!dialog.property("settingsCategoryTransitionActive").toBool()
                                     && keep->width() >= keep->sizeHint().width(),
                             2000);
    const QString evidence = qEnvironmentVariable("FOVELLE_TEST_EVIDENCE_DIR");
    if (!evidence.isEmpty()) {
        QVERIFY(info.grab().save(evidence + "/file-info-en.png"));
        QVERIFY(dialog.grab().save(evidence + "/settings-en.png"));
    }
#endif
}

void ImageCoreAndMovieTests::testSourceColorMetadata()
{
    QTemporaryDir dir;
    QImage image(32, 24, QImage::Format_RGB32);
    image.fill(Qt::red);
    image.setColorSpace(QColorSpace::DisplayP3);
    const QString tagged = dir.filePath("tagged.png");
    QVERIFY(image.save(tagged));
    const auto source = QvColor::readSource(tagged);
    QCOMPARE(source.iccState, QvColor::ICCState::Embedded);
    QCOMPARE(QColorSpace::fromIccProfile(source.iccData), QColorSpace(QColorSpace::DisplayP3));
    QVERIFY(!source.spaceName.isEmpty());
    QVERIFY(source.iccData.capacity() <= source.iccData.size() * 2);
    const auto loaded = loadImage(tagged);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->sourceColor.iccData, source.iccData);
    const QString untagged = createTestImage(dir, "untagged", Qt::blue);
    const auto absent = QvColor::readSource(untagged);
    QCOMPARE(absent.iccState, QvColor::ICCState::Absent);
    QVERIFY(absent.iccData.isEmpty());
    // A named Qt space generates ICC bytes, but the source still has none.
    QVERIFY(!QColorSpace(QColorSpace::SRgb).iccProfile().isEmpty());
    QCOMPARE(QvColor::readSource(untagged).iccState, QvColor::ICCState::Absent);
    const QString unknown = dir.filePath("unknown.bin");
    {
        QFile f(unknown);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not an image");
    }
    QCOMPARE(QvColor::readSource(unknown).iccState, QvColor::ICCState::Unknown);
    // Corrupt the actual compressed profile (not the PNG image pixels).
    QFile f(tagged);
    QVERIFY(f.open(QIODevice::ReadOnly));
    auto bytes = f.readAll();
    f.close();
    const int pos = bytes.indexOf("iCCP");
    QVERIFY(pos > 0);
    bytes[pos + 4] = char(0); // Invalid empty profile name / CRC.
    const auto corrupt = dir.filePath("corrupt.png");
    {
        QFile out(corrupt);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(bytes);
    }
    QCOMPARE(QvColor::readSource(corrupt).iccState, QvColor::ICCState::Invalid);
    // A truncated recognized container is unknown, never verified absent.
    {
        QFile out(corrupt);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(bytes.left(19));
    }
    QCOMPARE(QvColor::readSource(corrupt).iccState, QvColor::ICCState::Unknown);
}

void ImageCoreAndMovieTests::testColorContainerBoundaries()
{
    QTemporaryDir dir;
    const auto icc = QColorSpace(QColorSpace::DisplayP3).iccProfile();
    const auto be = [](quint32 value) {
        QByteArray b(4, 0);
        qToBigEndian(value, reinterpret_cast<uchar *>(b.data()));
        return b;
    };
    const auto le = [](quint32 value) {
        QByteArray b(4, 0);
        qToLittleEndian(value, reinterpret_cast<uchar *>(b.data()));
        return b;
    };
    auto check = [&](const QString &name, const QByteArray &bytes, QvColor::ICCState state) {
        const auto path = dir.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(bytes);
        f.close();
        return QvColor::readSource(path).iccState == state;
    };
    const auto pngChunk = [&](const QByteArray &type, const QByteArray &data) {
        const QByteArray body = type + data;
        return be(quint32(data.size())) + body
                + be(quint32(crc32(0, reinterpret_cast<const Bytef *>(body.constData()),
                                   uInt(body.size()))));
    };
    // Read a genuine PNG to keep the image payload independent of the parser.
    QFile pngFile(createTestImage(dir, "plain", Qt::red));
    QVERIFY(pngFile.open(QIODevice::ReadOnly));
    const auto plain = pngFile.readAll();
    auto pngWithProfile = [&](const QByteArray &profileBytes) {
        const auto compressed = qCompress(profileBytes).mid(4); // Strip Qt length prefix.
        return plain.left(33) + pngChunk("iCCP", QByteArray("p3\0\0", 4) + compressed)
                + plain.mid(33);
    };
    auto unsupported = icc;
    unsupported[8] = char(5); // Unsupported ICC major version, bounded structure.
    QVERIFY(check("unsupported.png", pngWithProfile(unsupported), QvColor::ICCState::Unsupported));
    auto malformed = icc;
    malformed.replace(36, 4, "xxxx");
    QVERIFY(check("bad-profile.png", pngWithProfile(malformed), QvColor::ICCState::Invalid));
    QVERIFY(check("oversized.png", pngWithProfile(QByteArray(16 * 1024 * 1024 + 1, 'x')),
                  QvColor::ICCState::Unknown));
    const auto srgb = plain.left(33) + pngChunk("sRGB", QByteArray(1, 0)) + plain.mid(33);
    QVERIFY(check("srgb.png", srgb, QvColor::ICCState::Absent));
    QCOMPARE(QvColor::readSource(dir.filePath("srgb.png")).spaceName, QString("sRGB"));
    const auto cicp =
            plain.left(33) + pngChunk("cICP", QByteArray::fromHex("0c0d0001")) + plain.mid(33);
    QVERIFY(check("cicp.png", cicp, QvColor::ICCState::Absent));
    QCOMPARE(QvColor::readSource(dir.filePath("cicp.png")).spaceName, QString("Display P3"));
    // JPEG APP2 is multipart and can arrive out of sequence.
    const auto segment = [](int index, int total, const QByteArray &part) {
        const auto payload = QByteArray("ICC_PROFILE\0", 12) + char(index) + char(total) + part;
        QByteArray length(2, 0);
        qToBigEndian(quint16(payload.size() + 2), reinterpret_cast<uchar *>(length.data()));
        return QByteArray::fromHex("ffe2") + length + payload;
    };
    const auto jpegTail = QByteArray::fromHex("ffc0000b080001000101011100ffda0008010100003f00ffd9");
    const auto first = segment(1, 2, icc.left(icc.size() / 2));
    const auto second = segment(2, 2, icc.mid(icc.size() / 2));
    QVERIFY(check("p3.jpeg", QByteArray::fromHex("ffd8") + second + first + jpegTail,
                  QvColor::ICCState::Embedded));
    QVERIFY(check("missing.jpeg", QByteArray::fromHex("ffd8") + first + jpegTail,
                  QvColor::ICCState::Invalid));
    QVERIFY(check("duplicate.jpeg", QByteArray::fromHex("ffd8") + first + first + jpegTail,
                  QvColor::ICCState::Invalid));
    // TIFF tag 34675 with a bounded offset; nonzero next IFD is unknown.
    auto tiff = QByteArray::fromHex("49492a0008000000010073870700") + le(quint32(icc.size()))
            + le(26) + le(0) + icc;
    QVERIFY(check("p3.tiff", tiff, QvColor::ICCState::Embedded));
    tiff.replace(18, 4, le(0xfffffff0));
    QVERIFY(check("bounds.tiff", tiff, QvColor::ICCState::Unknown));
    auto webpBody = QByteArray("WEBP") + QByteArray("ICCP") + le(quint32(icc.size())) + icc;
    if (icc.size() & 1)
        webpBody += char(0);
    webpBody += QByteArray("VP8L") + le(0);
    const auto webp = QByteArray("RIFF") + le(quint32(webpBody.size())) + webpBody;
    QVERIFY(check("p3.webp", webp, QvColor::ICCState::Embedded));
    QVERIFY(check("truncated.webp", webp.left(webp.size() - 1), QvColor::ICCState::Unknown));
}

void ImageCoreAndMovieTests::testColorInformationFollowsPresentation()
{
    ScopedOptionValues options(
            { { "colorspaceconversion", static_cast<int>(Qv::ColorSpaceConversion::SRgb) } });
    QTemporaryDir dir;
    QImage image(80, 60, QImage::Format_RGB32);
    image.fill(Qt::red);
    image.setColorSpace(QColorSpace::DisplayP3);
    const auto path = dir.filePath("p3.png");
    QVERIFY(image.save(path));
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    const bool quitPolicy = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    const auto closeWindow = qScopeGuard([&]() {
        window.close();
        qvApp->setQuitOnLastWindowClosed(quitPolicy);
    });
    window.show();
    auto *view = window.findChild<QVGraphicsView *>();
    window.openFile(path);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    const auto source = view->colorInformation().source;
    QCOMPARE(source.iccState, QvColor::ICCState::Embedded);
    QCOMPARE(QColorSpace::fromIccProfile(source.iccData), QColorSpace(QColorSpace::DisplayP3));
    QTRY_VERIFY_WITH_TIMEOUT(view->nativeMetalRendererDiagnostics().firstFramePresented, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(view->colorInformation().nativeOutput, 5000);
    QCOMPARE(view->colorInformation().outputSpace, QString("Extended Linear Display P3"));
    QVInfoDialog info;
    info.setColorInfoProvider([view]() { return view->colorInformation(); });
    info.setInfo(QFileInfo(path), QSize(80, 60), 0);
    info.show();
    QTRY_VERIFY_WITH_TIMEOUT(info.findChild<QLabel *>("iccProfileLabel")
                                     ->text()
                                     .contains(source.profileDescription.isEmpty()
                                                       ? source.spaceName
                                                       : source.profileDescription),
                             2000);
    QCOMPARE(info.findChild<QLabel *>("outputColorSpaceLabel")->text(),
             view->colorInformation().outputSpace);
    window.openFile(createQtRasterTestImage(dir, "qt", Qt::blue, QSize(20, 20)));
    QTRY_VERIFY_WITH_TIMEOUT(!view->colorInformation().nativeOutput, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
            info.findChild<QLabel *>("outputColorSpaceLabel")->text().contains("sRGB"), 2000);
    QVERIFY(view->colorInformation().source.iccData.isEmpty());
    view->closeImage();
    QTRY_COMPARE_WITH_TIMEOUT(info.findChild<QLabel *>("outputColorSpaceLabel")->text(),
                              QString("No output"), 2000);
    QCOMPARE(view->colorInformation().source.iccState, QvColor::ICCState::Unknown);
}

// KZ-P2: seed true/false/missing, run real startup migration twice, and
// inspect disk + fresh SettingsManager + UI. Scoped stores/options restored.
void FeatureTests::testKeepZoomSurvivesStartupMigration_data()
{
    QTest::addColumn<int>("stored");
    QTest::newRow("preserve-enabled") << 0;
    QTest::newRow("preserve-disabled") << 1;
    QTest::newRow("new-profile-default") << -1;
}
void FeatureTests::testKeepZoomSurvivesStartupMigration()
{
    QFETCH(int, stored);
    ScopedSettingPreserver marker(QStringLiteral("firstlaunch"));
    ScopedOptionValues options({ { "navresetszoom", true }, { "navigationregionsenabled", true } });
    QSettings disk;
    disk.setValue("firstlaunch", true);
    if (stored < 0)
        disk.remove("options/navresetszoom");
    else
        disk.setValue("options/navresetszoom", bool(stored));
    disk.sync();
    QCOMPARE(disk.status(), QSettings::NoError);
    for (int startup = 0; startup < 2; ++startup) {
        SettingsManager::migrateOldSettings();
        disk.sync();
        QCOMPARE(disk.status(), QSettings::NoError);
        QCOMPARE(disk.contains("options/navresetszoom"), stored >= 0);
        QCOMPARE(disk.value("options/navresetszoom", true).toBool(), stored != 0);
        SettingsManager fresh;
        QCOMPARE(fresh.getBoolean("navresetszoom"), stored != 0);
        QVOptionsDialog dialog;
        auto *keep = dialog.findChild<QCheckBox *>("keepZoomCheckbox");
        QVERIFY(keep);
        QCOMPARE(keep->isChecked(), stored == 0);
        // Retired policies must still normalize; removing all migration would
        // preserve zoom but fail this independent compatibility assertion.
        QCOMPARE(disk.value("options/navigationregionsenabled").toBool(), false);
    }
}

// KZ-P3: real independent processes share a test-owned persisted store.
// Seed -> GUI enable -> restart read twice -> GUI disable -> restart read.
// Both INI and macOS native backends run; no desktop Fovelle domain is used.
void FeatureTests::testKeepZoomPersistsAcrossProcesses_data()
{
    QTest::addColumn<QString>("backend");
    QTest::newRow("ini-restart") << QStringLiteral("ini");
    QTest::newRow("macos-native-restart") << QStringLiteral("native");
}
void FeatureTests::testKeepZoomPersistsAcrossProcesses()
{
    QFETCH(QString, backend);
    QTemporaryDir store;
    QVERIFY(store.isValid());
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove("FOVELLE_TEST_SUITE");
    environment.remove("FOVELLE_TEST_EVIDENCE_DIR");
    environment.insert("FOVELLE_KEEP_ZOOM_PROBE_BACKEND", backend);
    environment.insert("FOVELLE_KEEP_ZOOM_PROBE_ROOT", store.path());
    environment.insert("FOVELLE_KEEP_ZOOM_PROBE_DOMAIN",
                       QStringLiteral("org.fovelle.keepzoomtest.")
                               + QUuid::createUuid().toString(QUuid::Id128));
    environment.insert("FOVELLE_DISABLE_AUTO_UPDATE_CHECK", "1");
    auto run = [&](const QString &phase, QByteArray &output) {
        QProcess process;
        auto childEnvironment = environment;
        childEnvironment.insert("FOVELLE_KEEP_ZOOM_PROBE_PHASE", phase);
        process.setProcessEnvironment(childEnvironment);
        process.start(QCoreApplication::applicationFilePath(), QStringList{ });
        if (!process.waitForFinished(10000)) {
            process.kill();
            process.waitForFinished(1000);
            output = process.errorString().toUtf8();
            return false;
        }
        output = process.readAllStandardOutput();
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            output += process.readAllStandardError();
            return false;
        }
        return true;
    };
    const auto cleanup = qScopeGuard([&]() {
        QByteArray ignored;
        run("cleanup", ignored);
    });
    QByteArray output;
    for (const auto &step : QList<QPair<QString, bool>>{ { "seed", false },
                                                         { "enable", true },
                                                         { "read", true },
                                                         { "read", true },
                                                         { "disable", false },
                                                         { "read", false } }) {
        QVERIFY2(run(step.first, output), output.constData());
        const auto object = QJsonDocument::fromJson(output).object();
        QVERIFY2(object.contains("checked"), output.constData());
        qInfo().noquote() << "KEEP_ZOOM_RESTART" << backend << step.first
                          << "checked=" << object.value("checked").toBool()
                          << "storedReset=" << object.value("storedReset").toBool();
        QCOMPARE(object.value("checked").toBool(), step.second);
        QCOMPARE(object.value("storedReset").toBool(), !step.second);
        QCOMPARE(object.value("managerReset").toBool(), !step.second);
        QCOMPARE(object.value("status").toInt(), int(QSettings::NoError));
    }
}

static int runKeepZoomPersistenceProbe(const QString &phase)
{
    QVOptionsDialog dialog;
    auto *keep = dialog.findChild<QCheckBox *>("keepZoomCheckbox");
    if (!keep)
        return 4;
    if (phase == QStringLiteral("enable") && !keep->isChecked())
        keep->click();
    else if (phase == QStringLiteral("disable") && keep->isChecked())
        keep->click();
    else if (phase != QStringLiteral("enable") && phase != QStringLiteral("disable")
             && phase != QStringLiteral("seed") && phase != QStringLiteral("read"))
        return 2;
    QSettings disk;
    disk.sync();
    const QJsonObject result{ { "checked", keep->isChecked() },
                              { "storedReset", disk.value("options/navresetszoom", true).toBool() },
                              { "managerReset",
                                qvApp->getSettingsManager().getBoolean("navresetszoom") },
                              { "status", int(disk.status()) } };
    QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << Qt::endl;
    return disk.status() == QSettings::NoError ? 0 : 3;
}
