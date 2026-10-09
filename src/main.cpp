#include "decoders.h"
#include "formats.h"
#include "image.h"
#include "openwith.h"
#include "settings.h"
#include "viewer.h"

#include <QApplication>
#include <QByteArrayView>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QPainter>
#include <QScopeGuard>

#include <cstring>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>

#if defined(Q_OS_WIN)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef IMAGEVIEWER_VULKAN
#include <QVersionNumber>
#include <QVulkanInstance>
#include <rhi/qrhi.h>
#endif

namespace {

// macOS delivers files opened from Finder as QFileOpenEvent, not argv.
class FileOpenFilter : public QObject {
public:
    explicit FileOpenFilter(ViewerWindow *window) : m_window(window) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::FileOpen) {
            m_window->openFile(static_cast<QFileOpenEvent *>(event)->file());
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    ViewerWindow *m_window;
};

// `imageviewer --info <file>`: decode headlessly and report what the colour
// pipeline sees. Used by CI smoke tests and for diagnosing files.
int printInfo(const QString &path)
{
    QTextStream out(stdout);
    const Image image = decodeImage(path, 0);
    if (!image.isValid()) {
        out << "error: " << image.error << Qt::endl;
        return 1;
    }
    out << "file:        " << path << '\n'
        << "codec:       " << image.codec << '\n'
        << "size:        " << image.width << 'x' << image.height << '\n'
        << "source:      " << image.sourceChannels << " ch, " << image.sourceBits << " bits"
        << (image.sourceFloat ? " float" : "") << (image.hasAlpha ? ", alpha" : "") << '\n'
        << "orientation: " << image.orientation << '\n'
        << "colour:      " << image.colour.description << (image.colour.isHdr() ? " [HDR]" : "") << '\n'
        << "max:         " << image.maxComponent << "x SDR white ("
        << image.maxComponent * color::kSdrReferenceWhiteNits << " nits)\n"
        << "decode:      " << QString::number(image.decodeMs, 'f', 1) << " ms\n";
    if (const CameraInfo &c = image.camera; !c.isEmpty()) {
        QStringList parts;
        for (const QString &text : {c.make, c.model, c.lens})
            if (!text.isEmpty())
                parts << text;
        if (c.exposureTime > 0)
            parts << exposureTimeText(c.exposureTime, QLocale::c()) + QStringLiteral(" s");
        if (c.fNumber > 0)
            parts << QStringLiteral("f/") + QString::number(c.fNumber, 'f', 1);
        if (c.iso > 0)
            parts << QStringLiteral("ISO %1").arg(c.iso);
        if (c.focalLength > 0)
            parts << QString::number(c.focalLength, 'f', 0) + QStringLiteral(" mm");
        if (c.taken.isValid())
            parts << c.taken.toString(Qt::ISODate);
        out << "camera:      " << parts.join(QStringLiteral(" | ")) << '\n';
    }
    if (const std::shared_ptr<Animation> &animation = image.animation) {
        // Every frame once, as the viewer plays them (at most 10 000).
        QStringList durations;
        Animation::Frame frame;
        QString error;
        int frames = 0;
        for (int i = 1; i <= 10000; ++i) {
            if (!animation->frame(i, &frame, &error)) {
                out << "error: frame " << i << ": " << error << Qt::endl;
                return 1;
            }
            if (frame.index == 0)
                break;
            frames = i;
        }
        QStringList firstPixels; // straight colour of pixel (0,0) of each frame
        for (int i = 0; i <= frames && i < 8; ++i) {
            animation->frame(i, &frame, &error);
            durations << QString::number(frame.durationMs);
            const qfloat16 *p = frame.pixels->data();
            const float a = p[3];
            QStringList rgb;
            for (int c = 0; c < 3; ++c)
                rgb << QString::number(a > 0 ? float(p[c]) / a : 0.0f, 'f', 3);
            firstPixels << rgb.join(QLatin1Char(' '));
        }
        const QString more = frames >= 8 ? QStringLiteral(" ...") : QString();
        out << "frames:      " << frames + 1 << ", loops " << animation->loopCount() << ", ms "
            << durations.join(QLatin1Char(' ')) << more << '\n'
            << "frame px:    " << firstPixels.join(QStringLiteral(" | ")) << more << '\n';
    }
    // First pixel in working units, useful for colour checks on synthetic files.
    const qfloat16 *pixel = image.pixels->data();
    const float a = pixel[3];
    out << "pixel[0,0]:  " << (a > 0 ? float(pixel[0]) / a : 0.f) << ' ' << (a > 0 ? float(pixel[1]) / a : 0.f) << ' '
        << (a > 0 ? float(pixel[2]) / a : 0.f) << " a=" << a << Qt::endl;
    return 0;
}

struct RenderOptions {
    QString output = QStringLiteral("sdr"); // sdr | edr | scrgb | pq
    float white = 0.0f;                     // nits; 0 = default for the output
    float peak = 0.0f;                      // nits (scrgb, pq) or headroom (edr); 0 = default
    float exposureEv = 0.0f;
    bool toneMap = true;
    float tolerance = 1e-3f; // largest accepted GPU-vs-reference error
    QString pfm;             // optional dump of the GPU result
};

// Little-endian float RGB, rows bottom to top (Portable Float Map).
bool writePfm(const QString &path, const std::vector<float> &rgba, int width, int height)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QStringLiteral("PF\n%1 %2\n-1.0\n").arg(width).arg(height).toLatin1());
    std::vector<float> row(std::size_t(width) * 3);
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x)
            for (int c = 0; c < 3; ++c)
                row[std::size_t(x) * 3 + c] = rgba[(std::size_t(y) * width + x) * 4 + c];
        file.write(reinterpret_cast<const char *>(row.data()), qint64(row.size() * sizeof(float)));
    }
    return true;
}

// The output the harness renders for (--output, --white, --peak).
bool harnessOutput(const RenderOptions &opt, Renderer::Output *output)
{
    if (opt.output == QLatin1String("sdr"))
        *output = Renderer::sdrOutput();
    else if (opt.output == QLatin1String("edr"))
        *output = Renderer::edrOutput(opt.peak > 0 ? opt.peak : 4.0f);
    else if (opt.output == QLatin1String("scrgb"))
        *output = Renderer::scRgbOutput(opt.white > 0 ? opt.white : 80.0f, opt.peak > 0 ? opt.peak : 1000.0f);
    else if (opt.output == QLatin1String("pq"))
        *output = Renderer::pqOutput(opt.white > 0 ? opt.white : color::kSdrReferenceWhiteNits,
                                     opt.peak > 0 ? opt.peak : 1000.0f);
    else
        return false;
    return true;
}

// `imageviewer --panel-check`: the legibility of the information panels (D-49). Draws the
// panels' default style over a uniform image, at SDR white and at the output's peak, through
// the real shader and blending, reads the target back and measures, as luminance relative to
// SDR white, the panel's background and its label, value and outline pixels.
int panelCheck(const RenderOptions &opt, QVulkanInstance *vulkan)
{
    QTextStream out(stdout);
    Renderer::Output output;
    if (!harnessOutput(opt, &output)) {
        out << "error: unknown output " << opt.output << Qt::endl;
        return 2;
    }
    Renderer renderer(nullptr);
    renderer.setVulkanInstance(vulkan);
    QString error;
    if (!renderer.initialize(&error)) {
        out << "error: " << error << Qt::endl;
        return 4;
    }

    // Four 16-pixel cells: the background alone, then a label, a value and an outline pixel on it.
    constexpr int cell = 16, cells = 4;
    const QSize size(cell * cells, cell);
    const Settings style; // the defaults
    QImage overlay(size, QImage::Format_RGBA8888_Premultiplied);
    overlay.fill(Qt::transparent);
    {
        QPainter painter(&overlay);
        painter.fillRect(overlay.rect(), style.panelBackground());
        painter.fillRect(QRect(cell, 0, cell, cell), style.panelText(Settings::kPanelLabelGrey));
        painter.fillRect(QRect(2 * cell, 0, cell, cell), style.panelText(Settings::kPanelValueGrey));
        painter.fillRect(QRect(3 * cell, 0, cell, cell), QColor(0, 0, 0, style.panelText(0).alpha()));
    }
    renderer.setOverlay(Renderer::InfoLayer, overlay);

    Renderer::Frame frame;
    frame.imageRect = QRectF(QPointF(0, 0), QSizeF(size));
    frame.nearest = true;
    frame.overlayRects[Renderer::InfoLayer] = QRectF(QPointF(0, 0), QSizeF(size));
    const color::OutputStage stage = Renderer::stageFor(output, frame);
    const auto relativeLuminance = [&](const float *rgba) {
        if (output.mode == Renderer::OutputMode::Sdr)
            return double(color::luminance(color::srgbToLinear(rgba[0]), color::srgbToLinear(rgba[1]),
                                           color::srgbToLinear(rgba[2])));
        if (output.mode == Renderer::OutputMode::Pq) // BT.2020 primaries, nits
            return (0.2627 * color::pqToNits(rgba[0]) + 0.6780 * color::pqToNits(rgba[1])
                    + 0.0593 * color::pqToNits(rgba[2])) / stage.scale;
        return double(color::luminance(rgba[0], rgba[1], rgba[2])) / stage.scale;
    };
    const auto contrast = [](double a, double b) { return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05); };

    out << "output:    " << output.description << '\n';
    int failures = 0;
    // Over SDR white the labels must read against the panel itself (WCAG AA, 4.5:1); over the
    // brightest content the output can show, against their outline.
    for (const float level : {1.0f, stage.peak / stage.scale}) {
        auto pixels = std::make_shared<std::vector<qfloat16>>(std::size_t(size.width()) * size.height() * 4);
        for (std::size_t i = 0; i < pixels->size(); i += 4) {
            (*pixels)[i] = (*pixels)[i + 1] = (*pixels)[i + 2] = qfloat16(level);
            (*pixels)[i + 3] = qfloat16(1.0f);
        }
        renderer.setImage(pixels, size);
        frame.contentPeak = frame.contentLuminancePeak = level;
        std::vector<float> rgba;
        if (!renderer.renderToBuffer(frame, output, size, &rgba, &error, true)) {
            out << "error: " << error << Qt::endl;
            return 4;
        }
        double y[cells];
        for (int c = 0; c < cells; ++c)
            y[c] = relativeLuminance(&rgba[(std::size_t(cell / 2) * size.width() + c * cell + cell / 2) * 4]);
        const double labelContrast = contrast(y[1], level <= 1.0f ? y[0] : y[3]);
        out << "under " << level << ": background " << y[0] << ", label " << y[1] << ", value " << y[2]
            << ", outline " << y[3] << ", label contrast " << labelContrast << " against the "
            << (level <= 1.0f ? "background" : "outline") << '\n';
        if (!(labelContrast >= 4.5)) {
            out << "FAIL: label contrast below 4.5:1" << '\n';
            ++failures;
        }
        if (level <= 1.0f) {
            // As in SDR, where the target blends sRGB-encoded values: black at opacity a over white.
            const double expected = color::srgbToLinear(1.0f - style.overlayBackgroundOpacity / 100.0f);
            if (std::abs(y[0] - expected) > 0.005) {
                out << "FAIL: background " << y[0] << " over SDR white, " << expected << " in SDR" << '\n';
                ++failures;
            }
        }
        if (level >= stage.peak / stage.scale)
            break; // an SDR output: its peak is SDR white
    }
    out.flush();
    return failures == 0 ? 0 : 3;
}

// `imageviewer --render <file>`: the fidelity harness (docs/PLAN.md §10). Draws the
// image 1:1 offscreen through the real shader for a chosen output, reads the target
// back and compares every pixel with color::applyOutputStage().
int renderHarness(const QString &path, const RenderOptions &opt, QVulkanInstance *vulkan)
{
    QTextStream out(stdout);
    Image image = decodeImage(path, 0);
    if (!image.isValid()) {
        out << "error: " << image.error << Qt::endl;
        return 1;
    }

    Renderer::Output output;
    if (!harnessOutput(opt, &output)) {
        out << "error: unknown output " << opt.output << Qt::endl;
        return 2;
    }

    Renderer::Frame frame;
    frame.imageRect = QRectF(0, 0, image.width, image.height);
    frame.nearest = true;
    frame.background[0] = frame.background[1] = frame.background[2] = 0.0f; // translucency over black
    frame.exposure = std::exp2(opt.exposureEv);
    frame.toneMap = opt.toneMap;
    frame.contentPeak = image.maxComponent;
    frame.contentLuminancePeak = image.maxLuminance;
    frame.absoluteLuminance = image.colour.isAbsolute();
    const color::OutputStage stage = Renderer::stageFor(output, frame);

    Renderer renderer(nullptr);
    renderer.setVulkanInstance(vulkan);
    QString error;
    if (!renderer.initialize(&error)) {
        out << "error: " << error << Qt::endl;
        return 4;
    }
    const std::vector<qfloat16> &source = *image.pixels; // shared with the renderer, never modified
    const QSize size(image.width, image.height);
    renderer.setImage(image.pixels, size);
    std::vector<float> gpu;
    if (!renderer.renderToBuffer(frame, output, size, &gpu, &error)) {
        out << "error: " << error << Qt::endl;
        return 4;
    }

    double worst = 0.0;
    std::size_t worstAt = 0;
    float expected[4];
    for (std::size_t i = 0; i < gpu.size(); i += 4) {
        for (int c = 0; c < 4; ++c)
            expected[c] = float(source[i + c]);
        color::applyOutputStage(stage, expected);
        for (int c = 0; c < 4; ++c) {
            // A NaN or infinity on either side is the worst possible error, never a silent pass.
            const double e = std::isfinite(gpu[i + c]) && std::isfinite(expected[c])
                                 ? std::abs(double(gpu[i + c]) - expected[c]) / std::max(1.0, std::abs(double(expected[c])))
                                 : std::numeric_limits<double>::infinity();
            if (!(e <= worst)) {
                worst = e;
                worstAt = i + c;
            }
        }
    }

    const double toNits = output.nitsPerUnit;
    out << "file:      " << path << '\n'
        << "backend:   " << renderer.backendName() << " (" << renderer.deviceName() << ")\n"
        << "output:    " << output.description << '\n'
        << "stage:     scale " << stage.scale << ", peak " << stage.peak << " (" << stage.peak * toNits
        << " nits), exposure " << stage.exposure << '\n';
    if (stage.sourcePeak > stage.peak)
        out << "tone map:  BT.2390 " << stage.sourcePeak * toNits << " -> " << stage.peak * toNits << " nits, knee "
            << color::eetfKneeNits(float(stage.sourcePeak * toNits), float(stage.peak * toNits)) << " nits\n";
    else
        out << "tone map:  none (clip at " << stage.peak * toNits << " nits)\n";
    const std::size_t px = worstAt / 4;
    out << "max error: " << worst << " at (" << px % std::size_t(size.width()) << ',' << px / std::size_t(size.width())
        << ") channel " << worstAt % 4 << '\n'
        << "pixel[0,0]: " << gpu[0] << ' ' << gpu[1] << ' ' << gpu[2] << " a=" << gpu[3] << Qt::endl;

    if (!opt.pfm.isEmpty() && !writePfm(opt.pfm, gpu, size.width(), size.height())) {
        out << "error: cannot write " << opt.pfm << Qt::endl;
        return 5;
    }
    if (worst > opt.tolerance) {
        out << "FAIL: GPU output differs from the reference by more than " << opt.tolerance << Qt::endl;
        return 3;
    }
    return 0;
}

// Modes that never open a window run on a QCoreApplication: no platform plugin is
// loaded, so they work headless (CI checks the packaged binaries this way).
bool isConsoleMode(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QByteArrayView arg(argv[i]);
        if (arg == "--") // everything after it is a file name
            return false;
        if (arg == "--info" || arg == "--formats" || arg == "--open-with" || arg == "-h" || arg == "--help" || arg == "--help-all" || arg == "-v"
            || arg == "--version")
            return true;
#if defined(Q_OS_WIN)
        if (arg == "-?") // QCommandLineParser's help option on Windows
            return true;
#endif
    }
    return false;
}

#if defined(Q_OS_WIN)
// imageViewer.exe is a GUI-subsystem program: from a terminal its output would vanish.
// Console modes attach to the parent console when stdout is not already redirected.
void attachParentConsole()
{
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((out == nullptr || out == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
    }
}
#endif

// Parses an optional numeric option; a malformed value is an error, not a silent 0.
bool readNumber(const QCommandLineParser &parser, const QCommandLineOption &option, float *out)
{
    if (!parser.isSet(option))
        return true;
    bool ok = false;
    const float v = parser.value(option).toFloat(&ok);
    if (!ok || !std::isfinite(v)) {
        QTextStream(stderr) << "error: invalid number for --" << option.names().constFirst() << ": "
                            << parser.value(option) << Qt::endl;
        return false;
    }
    *out = v;
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    // The decode worker (D-38): no Qt application, no settings, nothing but the decode.
    if (argc >= 2 && std::strcmp(argv[1], "--decode-worker") == 0)
        return runDecodeWorker(argc, argv);
    const bool console = isConsoleMode(argc, argv);
#if defined(Q_OS_WIN)
    if (console)
        attachParentConsole();
#endif
    const std::unique_ptr<QCoreApplication> app = console ? std::make_unique<QCoreApplication>(argc, argv)
                                                          : std::make_unique<QApplication>(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("imageViewer"));
    QCoreApplication::setOrganizationName(QStringLiteral("Cristallumnis"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("cristallumnis.com"));
    QCoreApplication::setApplicationVersion(QStringLiteral(IMAGEVIEWER_VERSION));
    // Runs after the viewer window (declared later, destroyed first) has waited for its last
    // decode, and before the application object goes.
    const auto decoders = qScopeGuard(&shutdownDecoders);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Image viewer with verifiable SDR/HDR colour fidelity."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption infoOption(QStringLiteral("info"),
                                        QStringLiteral("Decode the file, print what the colour pipeline sees, exit."));
    parser.addOption(infoOption);
    const QCommandLineOption formatsOption(QStringLiteral("formats"),
                                           QStringLiteral("Print the file formats each decoder of this build reads, exit."));
    parser.addOption(formatsOption);
    const QCommandLineOption openWithOption(
        QStringLiteral("open-with"),
        QStringLiteral("Print the applications the system offers for the file (default first) and, on Linux, "
                       "the command each would run, exit."));
    parser.addOption(openWithOption);
    const QCommandLineOption renderOption(
        QStringLiteral("render"),
        QStringLiteral("Fidelity harness: render the file offscreen 1:1, compare with the CPU reference, exit."));
    const QCommandLineOption outputOption(QStringLiteral("output"), QStringLiteral("--render output: sdr, edr, scrgb or pq."),
                                          QStringLiteral("mode"), QStringLiteral("sdr"));
    const QCommandLineOption whiteOption(QStringLiteral("white"), QStringLiteral("--render SDR white in nits (scrgb, pq)."),
                                         QStringLiteral("nits"));
    const QCommandLineOption peakOption(QStringLiteral("peak"),
                                        QStringLiteral("--render display peak in nits (scrgb, pq) or headroom (edr)."),
                                        QStringLiteral("value"));
    const QCommandLineOption exposureOption(QStringLiteral("exposure"), QStringLiteral("--render exposure in stops."),
                                            QStringLiteral("ev"));
    const QCommandLineOption noToneMapOption(QStringLiteral("no-tonemap"),
                                             QStringLiteral("--render: clip instead of BT.2390 tone mapping."));
    const QCommandLineOption toleranceOption(QStringLiteral("tolerance"),
                                             QStringLiteral("--render largest accepted error (default 0.001)."),
                                             QStringLiteral("value"));
    const QCommandLineOption pfmOption(QStringLiteral("pfm"), QStringLiteral("--render: write the GPU result as PFM."),
                                       QStringLiteral("file"));
    const QCommandLineOption panelCheckOption(
        QStringLiteral("panel-check"),
        QStringLiteral("Measure the information panels' contrast offscreen for --output, --white, --peak; exit."));
    parser.addOptions({renderOption, outputOption, whiteOption, peakOption, exposureOption, noToneMapOption,
                       toleranceOption, pfmOption, panelCheckOption});
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Image or folder to open."));
    parser.process(*app);
    const QStringList files = parser.positionalArguments();
    if (parser.isSet(formatsOption)) {
        QTextStream out(stdout);
        for (const QString &line : formatReport())
            out << line << '\n';
        return 0;
    }

    if (parser.isSet(openWithOption)) {
        if (files.isEmpty()) {
            QTextStream(stderr) << "error: --open-with needs a file\n";
            return 2;
        }
        QTextStream out(stdout);
        const QString file = QFileInfo(files.first()).absoluteFilePath();
        for (const OpenWithApp &app : openWithApps(file))
            out << app.name << '\t' << app.id << '\t' << openWithCommand(app, file).join(QLatin1Char('|')) << '\n';
        return 0;
    }

    const bool harness = parser.isSet(renderOption);
    const bool panels = parser.isSet(panelCheckOption);
    if ((parser.isSet(infoOption) || harness) && files.isEmpty()) {
        QTextStream(stderr) << "error: --info and --render need a file\n\n" << parser.helpText();
        return 2;
    }
    if (parser.isSet(infoOption))
        return printInfo(files.first());

    QVulkanInstance *vulkan = nullptr;
#ifdef IMAGEVIEWER_VULKAN
    QVulkanInstance instance;
    instance.setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
    instance.setApiVersion(QVersionNumber(1, 1));
    if (qEnvironmentVariable("IMAGEVIEWER_RHI") != QLatin1String("opengl") && instance.create())
        vulkan = &instance;
#endif

    if (harness || panels) {
        RenderOptions opt;
        opt.output = parser.value(outputOption).toLower();
        if (!readNumber(parser, whiteOption, &opt.white) || !readNumber(parser, peakOption, &opt.peak)
            || !readNumber(parser, exposureOption, &opt.exposureEv) || !readNumber(parser, toleranceOption, &opt.tolerance))
            return 2;
        opt.toneMap = !parser.isSet(noToneMapOption);
        opt.pfm = parser.value(pfmOption);
        return panels ? panelCheck(opt, vulkan) : renderHarness(files.first(), opt, vulkan);
    }

    // The interface only: --info and --render output stays English (tests parse it).
    const Settings settings = Settings::load();
    if (Settings::storedIsOutdated())
        settings.save();
    applyLanguage(settings.language);

    ViewerWindow window(vulkan);
    FileOpenFilter fileOpenFilter(&window);
    app->installEventFilter(&fileOpenFilter);
    const SessionState session = SessionState::load();
    window.showRestored(session);
    if (!files.isEmpty())
        window.openFile(files.first());
    else if (settings.reopenLastImage && !session.lastFile.isEmpty() && QFileInfo::exists(session.lastFile))
        window.openFile(session.lastFile);
    return app->exec();
}
