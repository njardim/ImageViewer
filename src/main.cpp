#include "image.h"
#include "settings.h"
#include "viewer.h"

#include <QApplication>
#include <QByteArrayView>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QFileOpenEvent>
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
    // First pixel in working units, useful for colour checks on synthetic files.
    const float a = image.pixels[3];
    out << "pixel[0,0]:  " << (a > 0 ? float(image.pixels[0]) / a : 0.f) << ' '
        << (a > 0 ? float(image.pixels[1]) / a : 0.f) << ' ' << (a > 0 ? float(image.pixels[2]) / a : 0.f)
        << " a=" << a << Qt::endl;
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
    if (opt.output == QLatin1String("sdr"))
        output = Renderer::sdrOutput();
    else if (opt.output == QLatin1String("edr"))
        output = Renderer::edrOutput(opt.peak > 0 ? opt.peak : 4.0f);
    else if (opt.output == QLatin1String("scrgb"))
        output = Renderer::scRgbOutput(opt.white > 0 ? opt.white : 80.0f, opt.peak > 0 ? opt.peak : 1000.0f);
    else if (opt.output == QLatin1String("pq"))
        output = Renderer::pqOutput(opt.white > 0 ? opt.white : color::kSdrReferenceWhiteNits,
                                    opt.peak > 0 ? opt.peak : 1000.0f);
    else {
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
    const std::vector<qfloat16> source = image.pixels;
    const QSize size(image.width, image.height);
    renderer.setImage(std::move(image.pixels), size);
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
        if (arg == "--info" || arg == "-h" || arg == "--help" || arg == "--help-all" || arg == "-v"
            || arg == "--version")
            return true;
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

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Image viewer with verifiable SDR/HDR colour fidelity."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption infoOption(QStringLiteral("info"),
                                        QStringLiteral("Decode the file, print what the colour pipeline sees, exit."));
    parser.addOption(infoOption);
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
    parser.addOptions({renderOption, outputOption, whiteOption, peakOption, exposureOption, noToneMapOption,
                       toleranceOption, pfmOption});
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Image or folder to open."));
    parser.process(*app);
    const QStringList files = parser.positionalArguments();

    const bool harness = parser.isSet(renderOption);
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

    if (harness) {
        RenderOptions opt;
        opt.output = parser.value(outputOption).toLower();
        if (!readNumber(parser, whiteOption, &opt.white) || !readNumber(parser, peakOption, &opt.peak)
            || !readNumber(parser, exposureOption, &opt.exposureEv) || !readNumber(parser, toleranceOption, &opt.tolerance))
            return 2;
        opt.toneMap = !parser.isSet(noToneMapOption);
        opt.pfm = parser.value(pfmOption);
        return renderHarness(files.first(), opt, vulkan);
    }

    // The interface only: --info and --render output stays English (tests parse it).
    const Settings settings = Settings::load();
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
