// Fixed-pixel production QPainter measurements. CPU paint time is not a claim
// about physical presentation FPS or the operating system's event scheduling.
static Qv::VectorImageData performanceVectorFixture(QTemporaryDir &directory, bool eps)
{
    const QString path = directory.filePath(eps ? "motion.eps" : "motion.svg");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return { };
    QByteArray data = eps
            ? QByteArray("%!PS-Adobe-3.0 EPSF-3.0\n%%BoundingBox: 0 0 2400 1600\n")
            : QByteArray("<svg xmlns='http://www.w3.org/2000/svg' width='2400' height='1600'>");
    for (int i = 0; i < 160; ++i) {
        const int x = i * 137 % 2300, y = i * 89 % 1500;
        if (eps)
            data += QByteArray::number((i % 7 + 1) / 8.0) + " 0.2 0.7 setrgbcolor "
                    + QByteArray::number(x) + ' ' + QByteArray::number(y) + " 100 100 rectfill\n";
        else
            data += "<rect x='" + QByteArray::number(x) + "' y='" + QByteArray::number(y)
                    + "' width='100' height='100' fill='#ce408a' fill-opacity='0.8'/>";
    }
    data += eps ? "showpage\n%%EOF\n" : "</svg>";
    file.write(data);
    file.close();
    const auto result = loadImage(path);
    return result && !result->errorData ? result->vectorImage : Qv::VectorImageData{ };
}
static double vectorPercentile(QVector<double> samples, double percentile)
{
    std::sort(samples.begin(), samples.end());
    return samples[qBound<int>(0, int(std::ceil(percentile * samples.size())) - 1,
                               int(samples.size()) - 1)];
}
struct VectorPaintTiming
{
    double cpuMs;
    double wallMs;
};
template <typename Operation>
static std::optional<VectorPaintTiming> measureVectorPaint(Operation operation)
{
    QElapsedTimer wall;
    wall.start();
    const auto cpuStart = currentThreadCpuTimeNanoseconds();
    if (!cpuStart)
        return std::nullopt;
    operation();
    const auto cpuEnd = currentThreadCpuTimeNanoseconds();
    const double wallMs = wall.nsecsElapsed() / 1e6;
    if (!cpuEnd || *cpuEnd < *cpuStart)
        return std::nullopt;
    return VectorPaintTiming{ (*cpuEnd - *cpuStart) / 1e6, wallMs };
}
static double vectorPaintBudget(const QVector<double> &reference)
{
    return qMax(16.67, 4 * vectorPercentile(reference, .95));
}
void ImageLoaderTests::testVectorPaintTimingClockIsolation()
{
    QVERIFY(currentThreadCpuTimeNanoseconds().has_value());
    QVector<double> sleepCpu, sleepWall, busyCpu;
    for (int i = 0; i < 5; ++i) {
        const auto sleeping = measureVectorPaint([] { QThread::msleep(40); });
        QVERIFY(sleeping.has_value());
        sleepCpu.append(sleeping->cpuMs);
        sleepWall.append(sleeping->wallMs);
        bool validClock = true;
        const auto busy = measureVectorPaint([&] {
            const auto start = currentThreadCpuTimeNanoseconds();
            if (!start) {
                validClock = false;
                return;
            }
            while (true) {
                const auto now = currentThreadCpuTimeNanoseconds();
                if (!now) {
                    validClock = false;
                    return;
                }
                if (*now - *start >= 30000000)
                    return;
            }
        });
        QVERIFY(validClock);
        QVERIFY(busy.has_value());
        busyCpu.append(busy->cpuMs);
    }
    const double budget = vectorPaintBudget({ 4.0, 4.0, 4.0, 4.0, 4.0 });
    qInfo() << "VECTOR_CLOCK_ISOLATION sleep_cpu_p95_ms=" << vectorPercentile(sleepCpu, .95)
            << "sleep_wall_p95_ms=" << vectorPercentile(sleepWall, .95)
            << "busy_cpu_p95_ms=" << vectorPercentile(busyCpu, .95) << "budget_ms=" << budget;
    QVERIFY(vectorPercentile(sleepWall, .95) > budget);
    QVERIFY2(vectorPercentile(sleepCpu, .95) <= budget, "Suspended time counted as paint CPU");
    QVERIFY(vectorPercentile(busyCpu, .95) >= 30.0);
    QVERIFY2(vectorPercentile(busyCpu, .95) > budget, "Real CPU regression was not detected");
}
void ImageLoaderTests::testVectorPresentationFrameBudget_data()
{
    QTest::addColumn<bool>("eps");
    QTest::addColumn<int>("pixelScale");
    QTest::newRow("svg-1x") << false << 1;
    QTest::newRow("eps-1x") << true << 1;
    QTest::newRow("svg-2x") << false << 2;
    QTest::newRow("eps-2x") << true << 2;
}
void ImageLoaderTests::testVectorPresentationFrameBudget()
{
    QFETCH(bool, eps);
    QFETCH(int, pixelScale);
    QTemporaryDir directory;
    const auto vector = performanceVectorFixture(directory, eps);
    QVERIFY(vector.isValid());
    const bool originalQuit = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    const auto restoreQuit = qScopeGuard([&] { qvApp->setQuitOnLastWindowClosed(originalQuit); });
    QWidget host;
    host.resize(320, 240);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    QVGraphicsImageItem item;
    QVERIFY(item.setVectorImage(vector));
    item.setVectorBackgroundBrush(Qt::black);
    item.setVectorInteractionActive(true);
    QImage canvas(1920 * pixelScale, 1200 * pixelScale, QImage::Format_ARGB32_Premultiplied);
    QStyleOptionGraphicsItem option;
    QVector<double> cold, warm, reference, wallCold, wallWarm, wallReference;
    bool warmQueriesStable = true;
    bool timingValid = true;
    const auto paint = [&](double scale, double x, double y, QVector<double> &wallSamples) {
        option.exposedRect = QRectF(x, y, canvas.width() / scale, canvas.height() / scale);
        QPainter painter(&canvas);
        painter.scale(scale, scale);
        painter.translate(-x, -y);
        const auto sample = measureVectorPaint([&] { item.paint(&painter, &option, &host); });
        if (!sample) {
            timingValid = false;
            return 0.0;
        }
        wallSamples.append(sample->wallMs);
        return sample->cpuMs;
    };
    for (int step = 0; step < 12; ++step) {
        const double scale = (1.0 + .125 * (step % 6)) * pixelScale;
        const double x = 120 + (step % 3) * 30, y = 130;
        const quint64 before = item.vectorTileGenerationCount();
        cold.append(paint(scale, x, y, wallCold));
        QElapsedTimer deadline;
        deadline.start();
        while (item.vectorTileGenerationCount() == before && deadline.elapsed() < 5000) {
            QTest::qWait(1);
            if (!item.vectorRequestDepth() && item.vectorTileGenerationCount() == before)
                cold.append(paint(scale, x, y, wallCold));
        }
        QVERIFY(item.vectorTileGenerationCount() > before);
        cold.append(paint(scale, x, y, wallCold));
        const auto queries = item.vectorProfileQueries();
        for (int frame = 0; frame < 8; ++frame)
            warm.append(paint(scale, x + frame * 3.0 / scale, y, wallWarm));
        // Baseline deliberately records this failure after the JSON metrics.
        warmQueriesStable = warmQueriesStable && queries == item.vectorProfileQueries();
    }
    QImage fast(canvas.size(), QImage::Format_ARGB32_Premultiplied);
    fast.fill(QColor(30, 80, 120, 200));
    for (int frame = 0; frame < 40; ++frame) {
        QPainter painter(&canvas);
        const auto sample = measureVectorPaint([&] { painter.drawImage(QPoint(), fast); });
        QVERIFY2(sample.has_value(), "Thread CPU clock unavailable for reference paint");
        reference.append(sample->cpuMs);
        wallReference.append(sample->wallMs);
    }
    QVERIFY2(timingValid, "Thread CPU clock unavailable for vector paint");
    QVERIFY(!cold.isEmpty() && !warm.isEmpty() && !reference.isEmpty());
    const QVector<double> all = cold + warm;
    const QVector<double> wallAll = wallCold + wallWarm;
    const double p95 = vectorPercentile(all, .95);
    const QJsonObject metrics{
        { "clock", "thread_cpu" },
        { "budget_ms", vectorPaintBudget(reference) },
        { "cold_samples", cold.size() },
        { "warm_samples", warm.size() },
        { "wall_cold_p95_ms", vectorPercentile(wallCold, .95) },
        { "wall_warm_p50_ms", vectorPercentile(wallWarm, .5) },
        { "wall_paint_p95_ms", vectorPercentile(wallAll, .95) },
        { "wall_max_ms", *std::max_element(wallAll.begin(), wallAll.end()) },
        { "wall_reference_p95_ms", vectorPercentile(wallReference, .95) },
        { "format", eps ? "eps" : "svg" },
        { "pixel_scale", pixelScale },
        { "pixels", canvas.width() * canvas.height() },
        { "cold_p50_ms", vectorPercentile(cold, .5) },
        { "cold_p95_ms", vectorPercentile(cold, .95) },
        { "warm_p50_ms", vectorPercentile(warm, .5) },
        { "paint_p95_ms", p95 },
        { "max_ms", *std::max_element(all.begin(), all.end()) },
        { "reference_p95_ms", vectorPercentile(reference, .95) },
        { "gui_conversions", double(item.vectorGuiColorConversions()) },
        { "worker_conversions", double(item.vectorWorkerColorConversions()) },
        { "retained_raster_bytes", double(item.vectorRetainedRasterBytes()) },
        { "profile_queries", double(item.vectorProfileQueries()) },
        { "display_format", int(item.vectorDisplayFormat()) }
    };
    qInfo().noquote() << "VECTOR_FRAME_BUDGET"
                      << QJsonDocument(metrics).toJson(QJsonDocument::Compact);
    const QString evidence = qEnvironmentVariable("FOVELLE_VECTOR_PERF_EVIDENCE");
    if (!evidence.isEmpty()) {
        QDir().mkpath(evidence);
        QFile file(evidence + QString("/%1-%2x.json").arg(eps ? "eps" : "svg").arg(pixelScale));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(metrics).toJson());
    }
    QCOMPARE(item.vectorGuiColorConversions(), quint64(0));
    QVERIFY(warmQueriesStable);
    QVERIFY(item.vectorWorkerColorConversions() > 0);
    QCOMPARE(item.vectorDisplayFormat(), QImage::Format_ARGB32_Premultiplied);
    const double budget = vectorPaintBudget(reference);
    QVERIFY2(p95 <= budget, "GUI paint CPU exceeds frame budget");
    QVERIFY2(vectorPercentile(cold, .95) <= budget, "First tile paint CPU exceeds frame budget");
}
