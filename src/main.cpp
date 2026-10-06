#include "image.h"
#include "viewer.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileOpenEvent>
#include <QTextStream>

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

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("imageViewer"));
    QApplication::setOrganizationName(QStringLiteral("Cristallumnis"));
    QApplication::setOrganizationDomain(QStringLiteral("cristallumnis.com"));
    QApplication::setApplicationVersion(QStringLiteral(IMAGEVIEWER_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Image viewer with verifiable SDR/HDR colour fidelity."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption infoOption(QStringLiteral("info"),
                                        QStringLiteral("Decode the file, print what the colour pipeline sees, exit."));
    parser.addOption(infoOption);
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Image or folder to open."));
    parser.process(app);
    const QStringList files = parser.positionalArguments();

    if (parser.isSet(infoOption))
        return files.isEmpty() ? 2 : printInfo(files.first());

    QVulkanInstance *vulkan = nullptr;
#ifdef IMAGEVIEWER_VULKAN
    QVulkanInstance instance;
    instance.setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
    instance.setApiVersion(QVersionNumber(1, 1));
    if (qEnvironmentVariable("IMAGEVIEWER_RHI") != QLatin1String("opengl") && instance.create())
        vulkan = &instance;
#endif

    ViewerWindow window(vulkan);
    FileOpenFilter fileOpenFilter(&window);
    app.installEventFilter(&fileOpenFilter);
    window.resize(1280, 800);
    window.show();
    if (!files.isEmpty())
        window.openFile(files.first());
    return app.exec();
}
