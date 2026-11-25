// Unit tests of the window's parts with state of their own (D-102): the gallery's thumbnails and
// layout, the slideshow's clock, the zoom's detail of an SVG and the toolbar's layout and hiding
// (D-112), each driven alone, its decoding handed in. The rules here are the ones a defect once broke: a folder that keeps changing never got
// its thumbnails (D-100), a slow image was skipped by the slideshow (D-100), a failed detail was
// asked for again without end (D-95); and the scale an SVG is drawn at for its window, by which the
// window draws an SVG again when the window outgrows it (D-106). Run by CI on the three systems; no
// argument needed (the offscreen platform unless QT_QPA_PLATFORM says otherwise).
#include "colorpicker.h"
#include "decoders.h"
#include "detail.h"
#include "gallery.h"
#include "histogram.h"
#include "slideshow.h"
#include "toolbar.h"

#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QMutex>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QWaitCondition>

#include <atomic>
#include <limits>
#include <memory>

namespace {

// A decoder whose calls can be held until the test lets them go.
struct Gate {
    QMutex mutex;
    QWaitCondition opened;
    bool open = true;
    QHash<QString, int> calls; // by path

    void close()
    {
        const QMutexLocker lock(&mutex);
        open = false;
    }
    void release()
    {
        const QMutexLocker lock(&mutex);
        open = true;
        opened.wakeAll();
    }
    void pass(const QString &path)
    {
        QMutexLocker lock(&mutex);
        ++calls[path];
        while (!open)
            opened.wait(&mutex);
    }
    int count(const QString &path)
    {
        const QMutexLocker lock(&mutex);
        return calls.value(path);
    }
};

void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(bytes), bytes.size());
}

QHash<QString, FileStamp> stampsOf(const QStringList &files)
{
    QHash<QString, FileStamp> stamps;
    for (const QString &path : files) {
        const QFileInfo info(path);
        stamps.insert(path, {info.size(), info.lastModified()});
    }
    return stamps;
}

Image pixelsOf(const VectorDetail &job)
{
    Image image;
    image.path = job.path;
    image.width = qRound(job.area.width());
    image.height = qRound(job.area.height());
    image.pixels = std::make_shared<const std::vector<qfloat16>>(std::size_t(image.width) * std::size_t(image.height) * 4);
    return image;
}

} // namespace

class SlideshowTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void stepsWhenThereIsANextImage()
    {
        Slideshow slideshow;
        slideshow.start(1000);
        QVERIFY(slideshow.running() && slideshow.counting());
        QCOMPARE(slideshow.next({}), Slideshow::Action::Step);
        Slideshow::Conditions last;
        last.hasNext = false;
        QCOMPARE(slideshow.next(last), Slideshow::Action::End);
    }

    void waitsForASlowImageAndGivesItAFullInterval()
    {
        Slideshow slideshow;
        slideshow.start(1000);
        Slideshow::Conditions awaiting;
        awaiting.awaiting = true;
        QCOMPARE(slideshow.next(awaiting), Slideshow::Action::None); // never skipped
        QTest::qWait(300);
        const int before = slideshow.remainingMs(); // about 700 ms
        slideshow.restart(); // the image arrived
        QVERIFY(slideshow.counting());
        QVERIFY(slideshow.remainingMs() > before + 150); // a full interval from its arrival, about 1000 ms
    }

    void waitsOffTheScreenAndBehindADialog()
    {
        Slideshow slideshow;
        slideshow.start(1000);
        Slideshow::Conditions hidden;
        hidden.onScreen = false;
        QCOMPARE(slideshow.next(hidden), Slideshow::Action::None);
        QVERIFY(!slideshow.counting() && slideshow.waitsForScreen());
        slideshow.resumeOnScreen();
        QVERIFY(slideshow.counting() && !slideshow.waitsForScreen());
        Slideshow::Conditions dialog;
        dialog.behindDialog = true;
        QCOMPARE(slideshow.next(dialog), Slideshow::Action::None);
        QVERIFY(slideshow.counting()); // another interval
    }

    void pausedNothingRuns()
    {
        Slideshow slideshow;
        slideshow.start(1000);
        QVERIFY(slideshow.togglePause());
        QVERIFY(!slideshow.counting());
        slideshow.restart();
        slideshow.resumeOnScreen();
        QVERIFY(!slideshow.counting());
        QCOMPARE(slideshow.next({}), Slideshow::Action::None);
        QVERIFY(!slideshow.togglePause());
        QVERIFY(slideshow.counting());
        slideshow.stop();
        QVERIFY(!slideshow.running() && !slideshow.counting() && !slideshow.paused());
    }

    void takesANewIntervalAtOnce()
    {
        Slideshow slideshow;
        slideshow.start(10000);
        slideshow.setInterval(500);
        QVERIFY(slideshow.counting() && slideshow.remainingMs() <= 600); // a coarse timer: within 5 %, not 10 s
        QSignalSpy due(&slideshow, &Slideshow::due);
        slideshow.setInterval(20);
        QVERIFY(due.wait(2000));
    }
};

class GalleryTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_files.clear();
        for (const char *name : {"a.png", "b.png", "c.png", "d.png", "e.png"}) {
            m_files << m_dir->filePath(QString::fromLatin1(name));
            writeFile(m_files.last(), QByteArray("content"));
        }
        m_settings = Settings();
        m_settings.showGallery = true;
        m_settings.loop = false;
        m_gate = std::make_shared<Gate>();
        m_gallery = std::make_unique<Gallery>(m_settings, m_files, [gate = m_gate](const QString &path, int, qint64) {
            gate->pass(path);
            QImage image(4, 4, QImage::Format_RGBA8888_Premultiplied);
            image.fill(Qt::red);
            return image;
        });
        connect(m_gallery.get(), &Gallery::changed, this, [this] { m_gallery->update(scene()); });
    }

    void cleanup()
    {
        m_gate->release();
        m_gallery.reset();
    }

    // D-100: every listing dropped every thumbnail being decoded, so a folder that kept changing
    // (a file added beside the images) never got them.
    void aChangingFolderKeepsTheThumbnailsOfUnchangedFiles()
    {
        m_gate->close();
        m_gallery->update(scene());
        QTRY_VERIFY(m_gate->count(m_files.at(0)) == 1 && m_gate->count(m_files.at(1)) == 1); // two decoding
        for (int i = 0; i < 3; ++i) // listings made while they are decoded, the images unchanged
            m_gallery->forgetChanged(stampsOf(m_files));
        m_gate->release();
        QTRY_VERIFY(m_gallery->idle());
        for (const QString &path : std::as_const(m_files)) {
            QVERIFY2(m_gallery->has(path), qPrintable(path));
            QCOMPARE(m_gate->count(path), 1); // each decoded once
        }
    }

    void aFileWrittenAgainDuringItsDecodeIsDecodedAgain()
    {
        m_gate->close();
        m_gallery->update(scene());
        QTRY_COMPARE(m_gate->count(m_files.at(0)), 1);
        writeFile(m_files.at(0), QByteArray("other, longer content")); // its stamp changes
        m_gallery->forgetChanged(stampsOf(m_files));
        m_gate->release();
        QTRY_VERIFY(m_gallery->idle() && m_gallery->has(m_files.at(0)));
        QCOMPARE(m_gate->count(m_files.at(0)), 2); // read before it was written: asked for again
    }

    void aStaleListingDoesNotHoldAThumbnailBack()
    {
        // A share that reports no change: the listing's stamp is older than the file the decoder reads.
        QHash<QString, FileStamp> stale = stampsOf(m_files);
        stale[m_files.at(0)].size = 1;
        m_gallery->forgetChanged(stale);
        m_gallery->update(scene());
        QTRY_VERIFY(m_gallery->idle() && m_gallery->has(m_files.at(0)));
        QCOMPARE(m_gate->count(m_files.at(0)), 2); // read twice from the same content: kept
    }

    void aFileNoLongerListedIsNotKept()
    {
        m_gate->close();
        m_gallery->update(scene());
        QTRY_COMPARE(m_gate->count(m_files.at(0)), 1);
        const QString gone = m_files.takeFirst();
        m_gallery->forgetChanged(stampsOf(m_files));
        m_gate->release();
        QTRY_VERIFY(m_gallery->idle());
        QVERIFY(!m_gallery->has(gone));
        QVERIFY(m_gallery->has(m_files.at(0)));
    }

    void aRenamedFileKeepsItsThumbnail()
    {
        m_gallery->update(scene());
        QTRY_VERIFY(m_gallery->idle() && m_gallery->has(m_files.at(0)));
        const QString target = m_dir->filePath(QStringLiteral("renamed.png"));
        QVERIFY(QFile::rename(m_files.at(0), target));
        m_gallery->rename(m_files.at(0), target);
        m_files[0] = target;
        QVERIFY(m_gallery->has(target));
        m_gallery->update(scene());
        QTRY_VERIFY(m_gallery->idle());
        QCOMPARE(m_gate->count(target), 0); // never decoded under its new name
    }

    void layoutCentresTheCurrentImageInAnOddRow()
    {
        for (int i = 0; i < 15; ++i) {
            m_files << m_dir->filePath(QStringLiteral("more%1.png").arg(i));
            writeFile(m_files.last(), QByteArray("x"));
        }
        // 1200 px: (1200 - 2·128 - 8) / (96 + 8) = 9 thumbnails, an odd number already.
        QCOMPARE(m_gallery->capacity(1200), 9);
        QCOMPARE(m_gallery->capacity(1250), 9);
        QSignalSpy texture(m_gallery.get(), &Gallery::textureChanged);
        Gallery::Scene centred = scene();
        centred.current = 10;
        m_gallery->update(centred);
        QVERIFY(m_gallery->visible() && !texture.isEmpty() && !texture.last().first().value<QImage>().isNull());
        QCOMPARE(m_gallery->fileAt(4), 10); // the middle of nine
        QCOMPARE(m_gallery->fileAt(-1), 5);
        // The middle slot is under the pointer at the texture's centre.
        const QRectF rect = m_gallery->rect();
        QCOMPARE(m_gallery->slotAt(rect.center()), 4);
        QVERIFY(m_gallery->contains(rect.center()));
        QVERIFY(!m_gallery->contains(QPointF(5, 5)));
        // Off in the settings: its texture goes, once.
        m_settings.showGallery = false;
        texture.clear();
        m_gallery->update(scene());
        QCOMPARE(texture.size(), 1);
        QVERIFY(texture.first().first().value<QImage>().isNull() && !m_gallery->visible() && m_gallery->reserve() == 0.0);
    }

    // A row that held every image is not centred on the current one: when the window narrows, the
    // current image stays in sight (its middle thumbnail stayed, and the first image went out).
    void aNarrowedRowKeepsTheCurrentImage()
    {
        QCOMPARE(m_gallery->capacity(600), 3);
        Gallery::Scene wide = scene(); // all five fit, the first one current
        m_gallery->update(wide);
        QCOMPARE(m_gallery->fileAt(0), 0);
        Gallery::Scene narrow = wide;
        narrow.window.setWidth(600);
        m_gallery->update(narrow);
        QCOMPARE(m_gallery->fileAt(0), 0); // the first image, current, at the row's start
        QCOMPARE(m_gallery->fileAt(2), 2);
    }

    void theRingWhenNavigationLoops()
    {
        for (int i = 0; i < 15; ++i) {
            m_files << m_dir->filePath(QStringLiteral("more%1.png").arg(i));
            writeFile(m_files.last(), QByteArray("x"));
        }
        m_settings.loop = true;
        Gallery::Scene first = scene();
        first.current = 0;
        m_gallery->update(first);
        QCOMPARE(m_gallery->fileAt(4), 0);
        QCOMPARE(m_gallery->fileAt(3), int(m_files.size()) - 1); // before the first, the last
    }

private:
    Gallery::Scene scene() const { return {QSizeF(1200, 800), 1.0, 0, 0}; }

    std::unique_ptr<QTemporaryDir> m_dir;
    QStringList m_files;
    Settings m_settings;
    std::shared_ptr<Gate> m_gate;
    std::unique_ptr<Gallery> m_gallery;
};

class DetailTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void theScaleAnSvgIsDrawnAt()
    {
        // As svg.cpp draws (D-90, D-94) and the window asks whether a window outgrew it (D-106): its
        // own size, within the GPU's largest texture, never larger than the window; a thumbnail
        // exactly at its side.
        QCOMPARE(vectorDrawScale(4001, 3001, 16384, false, QSize(1280, 800)), 800.0 / 3001);
        QCOMPARE(vectorDrawScale(4001, 3001, 16384, false, QSize(1600, 1000)), 1000.0 / 3001);
        QCOMPARE(vectorDrawScale(4001, 3001, 16384, false, QSize(4000, 4000)), 4000.0 / 4001);
        QCOMPARE(vectorDrawScale(200, 200, 16384, false, QSize(1280, 800)), 1.0);
        QCOMPARE(vectorDrawScale(40000, 20000, 16384, false, QSize()), 16384.0 / 40000);
        QCOMPARE(vectorDrawScale(100, 50, 256, true, QSize(1280, 800)), 2.56);
    }

    void theVisiblePartInEveryTurnAndMirror()
    {
        // A 100 × 100 texture at 4×, its corner at the window's: the window shows its top-left
        // 200 × 100 device pixels.
        DetailView view;
        view.path = QStringLiteral("a.svg");
        view.vector = view.valid = true;
        view.texture = view.source = QSizeF(100, 100);
        view.zoom = view.shownZoom = 4.0;
        view.imageRect = QRectF(0, 0, 400, 400);
        view.window = QSizeF(200, 100);
        view.maxTextureSize = 4096;
        const auto area = [&view](int turns, bool mirrored) {
            view.quarterTurns = turns;
            view.mirrored = mirrored;
            const std::optional<VectorDetail> detail = vectorDetailFor(view);
            return detail ? detail->area : QRectF();
        };
        QCOMPARE(area(0, false), QRectF(0, 0, 200, 100));
        QCOMPARE(area(0, true), QRectF(200, 0, 200, 100));
        QCOMPARE(area(1, false), QRectF(0, 200, 100, 200));
        QCOMPARE(area(2, false), QRectF(200, 300, 200, 100));
        QCOMPARE(area(3, false), QRectF(300, 0, 100, 200));
        const std::optional<VectorDetail> detail = vectorDetailFor(view);
        QVERIFY(detail && detail->rect == QRectF(0.75, 0, 0.25, 0.5) && detail->scale == 4.0);
    }

    void noDetailUnlessAnSvgIsEnlarged()
    {
        DetailView view;
        view.path = QStringLiteral("a.svg");
        view.vector = view.valid = true;
        view.texture = view.source = QSizeF(100, 100);
        view.zoom = view.shownZoom = 1.0;
        view.imageRect = QRectF(0, 0, 100, 100);
        view.window = QSizeF(200, 200);
        view.maxTextureSize = 4096;
        QVERIFY(!vectorDetailFor(view)); // at its own size
        view.zoom = 3.0;
        view.imageRect = QRectF(0, 0, 300, 300);
        QVERIFY(vectorDetailFor(view));
        view.maxTextureSize = 100; // the visible part past the GPU's largest texture
        QVERIFY(!vectorDetailFor(view));
        view.maxTextureSize = 4096;
        view.vector = false; // a raster image has its own pixels
        QVERIFY(!vectorDetailFor(view));
        view.vector = true;
        view.imageRect = QRectF(500, 500, 300, 300); // off the window
        QVERIFY(!vectorDetailFor(view));
    }

    void drawnOnceTheViewSettles()
    {
        std::atomic_int draws{0};
        VectorDetails details([&draws](const VectorDetail &job, const std::atomic_bool *) {
            ++draws;
            return pixelsOf(job);
        });
        details.setSettleMs(20);
        details.drop(QStringLiteral("a.svg"));
        QSignalSpy drawn(&details, &VectorDetails::drawn);
        const VectorDetail wanted{QStringLiteral("a.svg"), 4.0, QRectF(0, 0, 20, 10), QRectF(0, 0, 0.5, 0.5)};
        details.want(wanted);
        QCOMPARE(draws.load(), 0); // not while the view still changes
        QVERIFY(drawn.wait(2000));
        QCOMPARE(details.shown(), wanted);
        details.want(wanted); // the same view: nothing more
        QTest::qWait(60);
        QCOMPARE(draws.load(), 1);
        QSignalSpy cleared(&details, &VectorDetails::cleared);
        details.want(std::nullopt); // back at its own size: the GPU memory back
        QCOMPARE(cleared.size(), 1);
        QVERIFY(details.shown().path.isEmpty());
    }

    // D-95 (5): a failed detail was asked for again without end.
    void aFailedDetailIsNotAskedForAgainWhileTheViewStays()
    {
        std::atomic_int draws{0};
        VectorDetails details([&draws](const VectorDetail &job, const std::atomic_bool *) {
            ++draws;
            Image failed;
            failed.path = job.path;
            failed.error = QStringLiteral("past its memory");
            return failed;
        });
        details.setSettleMs(10);
        details.drop(QStringLiteral("a.svg"));
        QSignalSpy failed(&details, &VectorDetails::failed);
        const VectorDetail wanted{QStringLiteral("a.svg"), 4.0, QRectF(0, 0, 20, 10), QRectF()};
        details.want(wanted);
        QVERIFY(failed.wait(2000));
        details.want(std::nullopt);
        details.want(wanted); // the view back where it failed
        QTest::qWait(80);
        QCOMPARE(draws.load(), 1);
        VectorDetail other = wanted;
        other.area = QRectF(0, 0, 30, 10);
        details.want(other); // another view: tried
        QVERIFY(failed.wait(2000));
        QCOMPARE(draws.load(), 2);
    }

    void aDetailNoLongerWantedIsStoppedAndDropped()
    {
        auto gate = std::make_shared<QSemaphore>(0);
        std::atomic_bool sawCancel{false};
        VectorDetails details([gate, &sawCancel](const VectorDetail &job, const std::atomic_bool *cancel) {
            if (job.area.width() == 20) { // the first: held until the view has moved on
                gate->acquire();
                sawCancel = cancel->load();
            }
            return pixelsOf(job);
        });
        details.setSettleMs(10);
        details.drop(QStringLiteral("a.svg"));
        QSignalSpy drawn(&details, &VectorDetails::drawn);
        const VectorDetail first{QStringLiteral("a.svg"), 4.0, QRectF(0, 0, 20, 10), QRectF()};
        details.want(first);
        QTRY_VERIFY(details.busy());
        VectorDetail second = first;
        second.area = QRectF(0, 0, 30, 10);
        details.want(second);
        gate->release();
        QVERIFY(drawn.wait(2000));
        QVERIFY(sawCancel.load());
        QCOMPARE(drawn.size(), 1);
        QCOMPARE(drawn.first().first().value<VectorDetail>(), second); // the first was never shown
    }

    void anotherImageDiscardsTheRunningJob()
    {
        auto gate = std::make_shared<QSemaphore>(0);
        VectorDetails details([gate](const VectorDetail &job, const std::atomic_bool *) {
            gate->acquire();
            return pixelsOf(job);
        });
        details.setSettleMs(10);
        details.drop(QStringLiteral("a.svg"));
        QSignalSpy drawn(&details, &VectorDetails::drawn);
        details.want(VectorDetail{QStringLiteral("a.svg"), 4.0, QRectF(0, 0, 20, 10), QRectF()});
        QTRY_VERIFY(details.busy());
        details.drop(QStringLiteral("b.svg"));
        gate->release();
        QTRY_VERIFY(!details.busy());
        QTest::qWait(30);
        QCOMPARE(drawn.size(), 0);
        QVERIFY(details.shown().path.isEmpty());
    }
};

// The one limit for the decodes that run at once (D-111): DecodeMemory, with a budget of 100.
class DecodeMemoryTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void aDecodeAloneIsNeverHeld()
    {
        const DecodeMemory decode;
        DecodeMemory::reserve(90, 100);
        QCOMPARE(DecodeMemory::reserved(), qint64(90));
        DecodeMemory::reserve(60, 100); // less than it holds: nothing changes
        QCOMPARE(DecodeMemory::reserved(), qint64(90));
    }

    void whatADecodeReservedIsFreeWhenItEnds()
    {
        QCOMPARE(DecodeMemory::reserved(), qint64(0));
        DecodeMemory::reserve(50, 100); // outside a decode: nothing is reserved
        QCOMPARE(DecodeMemory::reserved(), qint64(0));
        {
            const DecodeMemory outer;
            DecodeMemory::reserve(30, 100);
            {
                const DecodeMemory inner; // a decode within a decode is the same decode
                DecodeMemory::reserve(40, 100);
            }
            QCOMPARE(DecodeMemory::reserved(), qint64(40));
        }
        QCOMPARE(DecodeMemory::reserved(), qint64(0));
    }

    void aDecodeThatDoesNotFitWaitsForTheOthers()
    {
        QSemaphore holds, mayEnd;
        std::atomic_int through{0}; // the decodes past their reservation
        QThread *first = QThread::create([&] {
            const DecodeMemory decode;
            DecodeMemory::reserve(60, 100);
            holds.release();
            mayEnd.acquire();
        });
        first->start();
        holds.acquire();
        // 60 are held: 50 more do not fit; 30, asked for later, would fit, but does not pass the 50 (in the
        // order asked). Which of the two goes on first once the 60 are free is not observable from here:
        // both then fit.
        QThread *second = QThread::create([&] {
            const DecodeMemory decode;
            DecodeMemory::reserve(50, 100);
            ++through;
            holds.release();
            mayEnd.acquire();
        });
        second->start();
        QTest::qWait(100);
        QThread *third = QThread::create([&] {
            const DecodeMemory decode;
            DecodeMemory::reserve(30, 100);
            ++through;
            holds.release();
        });
        third->start();
        QTest::qWait(200);
        QCOMPARE(through.load(), 0); // both wait
        QCOMPARE(DecodeMemory::reserved(), qint64(60));
        {
            // One that holds some already, or that must not wait, is not held back.
            const DecodeMemory decode;
            DecodeMemory::reserve(70, 100, false);
            QCOMPARE(DecodeMemory::reserved(), qint64(130));
            DecodeMemory::reserve(80, 100);
            QCOMPARE(DecodeMemory::reserved(), qint64(140));
        }
        QCOMPARE(through.load(), 0);
        mayEnd.release(); // the first decode ends: both go on
        QVERIFY(holds.tryAcquire(2, 5000));
        QCOMPARE(through.load(), 2);
        mayEnd.release();
        for (QThread *thread : {first, second, third}) {
            QVERIFY(thread->wait(5000));
            delete thread;
        }
        QCOMPARE(DecodeMemory::reserved(), qint64(0));
    }
};

// The toolbar (D-112): what fits a width and what goes under "…", which button takes a point (none
// is between two), the slideshow's button giving way to stop and pause under the pointer, the
// names, the texture drawn again only on a change; and the hiding of a bar while the pointer is still.
class ToolbarTest : public QObject {
    Q_OBJECT

    // The default toolbar's shape: buttons 1..14 with separators where the owner's design has them.
    static QList<Toolbar::Button> defaults()
    {
        QList<Toolbar::Button> buttons;
        int id = 0;
        const auto add = [&](int count) {
            for (int i = 0; i < count; ++i) {
                ++id;
                buttons.append({id, Glyph::Open, QStringLiteral("Button %1").arg(id)});
            }
        };
        const auto separator = [&] { buttons.append({-1, Glyph::More, {}, true, false, true}); };
        add(1);
        separator();
        add(5);
        separator();
        add(4);
        separator();
        add(3);
        separator();
        add(1);
        return buttons;
    }
    static Toolbar::Look look(bool names = false)
    {
        Toolbar::Look l;
        l.devicePixelRatio = 2.0;
        l.names = names;
        l.panel = QColor(0, 0, 0, 179);
        l.ink = QColor(240, 240, 240);
        l.on = QColor(80, 200, 120, 179);
        l.hover = QColor(80, 200, 120, 128);
        l.font = QGuiApplication::font();
        return l;
    }
    static QList<int> shownIds(const Toolbar &bar)
    {
        QList<int> ids;
        for (double x = 0; x < bar.size().width(); x += 1.0) {
            const int id = bar.buttonAt(QPointF(x, bar.size().height() / 2));
            if (ids.isEmpty() || ids.last() != id)
                ids << id;
        }
        return ids;
    }

private Q_SLOTS:
    void everyButtonFitsAWideWindow()
    {
        Toolbar bar;
        bar.setButtons(defaults());
        bar.setLook(look());
        bar.layout(2000);
        // 14 buttons, 9 spacings between neighbours, 4 separators, the padding at both ends.
        const double expected = 2 * Toolbar::kPadding + 14 * Toolbar::kButton + 9 * Toolbar::kSpacing + 4 * Toolbar::kSeparator;
        QCOMPARE(bar.size(), QSizeF(expected, 2 * Toolbar::kPadding + Toolbar::kButton));
        QVERIFY(bar.overflow().isEmpty());
        QList<int> all;
        for (int id = 1; id <= 14; ++id)
            all << id;
        QCOMPARE(shownIds(bar), all); // every point of the bar takes a button, in order
        QCOMPARE(bar.buttonAt(QPointF(-0.5, 10)), -1);
        QCOMPARE(bar.buttonAt(QPointF(10, bar.size().height())), -1);
        // Between two neighbours the point belongs to the one at its right.
        const QRectF first = bar.buttonRect(2), second = bar.buttonRect(3);
        const double between = (first.right() + second.left()) / 2;
        QCOMPARE(bar.buttonAt(QPointF(between - 0.01, 20)), 2);
        QCOMPARE(bar.buttonAt(QPointF(between, 20)), 3);
    }

    void aNarrowWindowPutsTheLastButtonsUnderMore()
    {
        Toolbar bar;
        bar.setButtons(defaults());
        bar.setLook(look());
        int previousHidden = 15;
        for (const double width : {300.0, 450.0, 600.0}) { // the whole bar needs 642
            bar.layout(width);
            QVERIFY(bar.size().width() <= width);
            const QList<int> ids = shownIds(bar);
            QCOMPARE(ids.last(), Toolbar::kMore);
            const QList<Toolbar::Button> hidden = bar.overflow();
            QVERIFY(!hidden.isEmpty());
            QVERIFY(int(hidden.size()) < previousHidden); // a wider window holds more
            previousHidden = int(hidden.size());
            // What shows and what is under "…" are every button once, in order, without separators.
            QList<int> together = ids.mid(0, ids.size() - 1);
            for (const Toolbar::Button &b : hidden) {
                QVERIFY(!b.separator);
                together << b.id;
            }
            QList<int> all;
            for (int id = 1; id <= 14; ++id)
                all << id;
            QCOMPARE(together, all);
            QVERIFY(bar.button(Toolbar::kMore));
        }
        bar.layout(10); // not even one button: "…" alone holds them all
        QCOMPARE(shownIds(bar), QList<int>{Toolbar::kMore});
        QCOMPARE(bar.overflow().size(), 14);
    }

    void startingTheSlideshowLeavesPauseUnderThePointer()
    {
        // The bar is centred in the window: when the slideshow's button becomes stop and pause, the
        // bar grows by one button and moves left by half of it. The pointer that clicked start is
        // then where stop and pause meet, and pause takes it.
        enum { Slideshow = 100, Stop, Pause };
        QList<Toolbar::Button> buttons = defaults();
        buttons[4] = {Slideshow, Glyph::Slideshow, QStringLiteral("Slideshow")};
        Toolbar bar;
        bar.setLook(look());
        bar.setButtons(buttons);
        bar.layout(2000);
        const double before = bar.size().width();
        const QPointF pointer = bar.buttonRect(Slideshow).center();
        buttons[4] = {Stop, Glyph::Stop, QStringLiteral("Stop")};
        buttons.insert(5, {Pause, Glyph::Pause, QStringLiteral("Pause")});
        QVERIFY(bar.setButtons(buttons));
        bar.layout(2000);
        const double shift = (bar.size().width() - before) / 2;
        QCOMPARE(shift, (Toolbar::kButton + Toolbar::kSpacing) / 2);
        QCOMPARE(bar.buttonAt(pointer + QPointF(shift, 0)), int(Pause));
    }

    void namesWidenTheColumnsAndAddTwoLines()
    {
        Toolbar bar;
        QList<Toolbar::Button> buttons{{1, Glyph::Open, QStringLiteral("Open")},
                                       {2, Glyph::Checkerboard, QStringLiteral("Checkerboard Background")},
                                       {3, Glyph::Settings, QStringLiteral("Supercalifragilisticexpialidocious")}};
        bar.setButtons(buttons);
        bar.setLook(look(true));
        bar.layout(4000);
        const QFontMetricsF metrics(QGuiApplication::font());
        QCOMPARE(bar.size().height(), 2 * Toolbar::kPadding + Toolbar::kButton + 2 * metrics.lineSpacing() + 2);
        // A short name keeps the least column; a long one wraps at the name's width, or at its
        // longest word, which is never cut.
        QVERIFY(bar.buttonRect(1).width() == Toolbar::kButton);
        QVERIFY(bar.texture().width() > 0);
        const double word = metrics.horizontalAdvance(QStringLiteral("Supercalifragilisticexpialidocious"));
        const double third = bar.size().width() - Toolbar::kPadding - bar.buttonRect(3).center().x();
        QVERIFY(2 * third >= word); // its column, centred on the button, holds the word
    }

    void theTextureIsDrawnAgainOnlyOnAChange()
    {
        Toolbar bar;
        bar.setButtons(defaults());
        bar.setLook(look());
        bar.layout(2000);
        const QImage first = bar.texture();
        QCOMPARE(first.size(), (bar.size() * 2.0).toSize());
        QCOMPARE(bar.texture().cacheKey(), first.cacheKey());
        QVERIFY(!bar.setButtons(defaults()));
        QVERIFY(!bar.setLook(look()));
        bar.layout(2000);
        QCOMPARE(bar.texture().cacheKey(), first.cacheKey());
        QVERIFY(bar.setHover(3));
        QVERIFY(!bar.setHover(3));
        const QImage hovered = bar.texture();
        QVERIFY(hovered.cacheKey() != first.cacheKey());
        QVERIFY(hovered != first); // the disc behind the hovered button
    }

    void theMorePanelHoldsWhatANarrowWindowCannot()
    {
        Toolbar bar;
        QList<Toolbar::Button> buttons = defaults();
        for (Toolbar::Button &b : buttons)
            if (!b.separator)
                b.shortcut = QStringLiteral("Ctrl+%1").arg(b.id);
        bar.setButtons(buttons);
        Toolbar::Look l = look();
        l.menuFont = QGuiApplication::font();
        bar.setLook(l);
        bar.layout(300);
        QVERIFY(!bar.menuOpen() && bar.menuSize().isEmpty() && bar.menuTexture().isNull());
        QVERIFY(bar.setMenuOpen(true));
        QVERIFY(!bar.setMenuOpen(true));
        const QList<Toolbar::Button> hidden = bar.overflow();
        QCOMPARE(bar.menuSize().height(), 2 * Toolbar::kPadding + hidden.size() * Toolbar::kRow);
        // A row each, in order; the padding above and below takes none.
        for (qsizetype i = 0; i < hidden.size(); ++i)
            QCOMPARE(bar.menuRowAt(QPointF(20, Toolbar::kPadding + (i + 0.5) * Toolbar::kRow)), hidden.at(i).id);
        QCOMPARE(bar.menuRowAt(QPointF(20, 2)), -1);
        QCOMPARE(bar.menuRowAt(QPointF(-1, Toolbar::kPadding + 5)), -1);
        const QImage first = bar.menuTexture();
        QCOMPARE(first.size(), (bar.menuSize() * 2.0).toSize());
        QCOMPARE(bar.menuTexture().cacheKey(), first.cacheKey());
        QVERIFY(bar.setMenuHover(hidden.first().id));
        QVERIFY(bar.menuTexture().cacheKey() != first.cacheKey());
        // Wide enough for every button: nothing under "…", the panel closes, and stays closed.
        bar.layout(2000);
        QVERIFY(!bar.menuOpen() && bar.menuTexture().isNull());
        bar.layout(300);
        QVERIFY(!bar.menuOpen());
    }

    void theLastGroupStaysOnTheBar()
    {
        // Settings and Quit, pinned after a separator of their own (the owner, D-115): "…" takes the
        // others and comes before them.
        QList<Toolbar::Button> buttons = defaults();
        buttons.append({-1, Glyph::More, {}, true, false, true});
        buttons.append({15, Glyph::Settings, QStringLiteral("Settings"), true, false, false, {}, true});
        buttons.append({16, Glyph::Quit, QStringLiteral("Quit"), true, false, false, {}, true});
        Toolbar bar;
        bar.setButtons(buttons);
        bar.setLook(look());
        bar.layout(2000);
        QList<int> all;
        for (int id = 1; id <= 16; ++id)
            all << id;
        QCOMPARE(shownIds(bar), all);
        bar.layout(400);
        const QList<int> shown = shownIds(bar);
        QCOMPARE(shown.mid(shown.size() - 3), QList<int>({Toolbar::kMore, 15, 16}));
        const QList<Toolbar::Button> hidden = bar.overflow();
        QVERIFY(!hidden.isEmpty());
        for (const Toolbar::Button &b : hidden)
            QVERIFY(b.id != 15 && b.id != 16);
        QCOMPARE(hidden.last().id, 14); // the last of the others, not of the bar
        // Only the last group: on the bar whatever the width, and no separator first.
        Toolbar alone;
        alone.setButtons({buttons.at(buttons.size() - 2), buttons.last()});
        alone.setLook(look());
        alone.layout(400);
        QCOMPARE(shownIds(alone), QList<int>({15, 16}));
        QCOMPARE(alone.size().width(), 2 * Toolbar::kPadding + 2 * Toolbar::kButton + Toolbar::kSpacing);
    }

    void autoHideWaitsForAStillPointer()
    {
        bool underPointer = false;
        AutoHide hide([&underPointer] { return underPointer; });
        QSignalSpy changed(&hide, &AutoHide::hiddenChanged);
        hide.configure(false, 30);
        QTest::qWait(80);
        QVERIFY(!hide.hidden()); // off: it never hides
        hide.configure(true, 30);
        QTRY_VERIFY_WITH_TIMEOUT(hide.hidden(), 2000);
        QCOMPARE(changed.size(), 1);
        hide.pointerMoved();
        QVERIFY(!hide.hidden());
        QCOMPARE(changed.size(), 2);
        underPointer = true; // never from under the pointer
        QTest::qWait(150);
        QVERIFY(!hide.hidden());
        underPointer = false;
        QTRY_VERIFY_WITH_TIMEOUT(hide.hidden(), 2000);
    }
};

// color::Encoder against color::Converter, the histogram's counting and the color picker's values
// and panel (D-115).
class ColorTest : public QObject {
    Q_OBJECT

    static color::Descriptor analytic(const color::Chromaticities &primaries, color::Transfer transfer,
                                      float gamma = 2.2f)
    {
        color::Descriptor d;
        d.source = color::Descriptor::Source::Cicp;
        d.primaries = primaries;
        d.transfer = transfer;
        d.gamma = gamma;
        return d;
    }
    static ColorPicker::Sample sample(double r, double g, double b, double alpha = 1.0) // sRGB-encoded in
    {
        ColorPicker::Sample s;
        s.rgb[0] = color::srgbToLinear(float(r));
        s.rgb[1] = color::srgbToLinear(float(g));
        s.rgb[2] = color::srgbToLinear(float(b));
        s.alpha = float(alpha);
        s.position = QPoint(12, 34);
        return s;
    }
    static QString row(const QList<ColorPicker::Row> &rows, const QString &label, bool copy = true)
    {
        for (const ColorPicker::Row &r : rows)
            if (r.label == label)
                return copy ? r.copy : r.text;
        return QStringLiteral("(none)");
    }

private Q_SLOTS:
    void theEncoderUndoesTheConverter()
    {
        // In float precision the file's signal comes back from linear scRGB, matrix and curve undone.
        const color::Descriptor descriptors[] = {
            analytic(color::kBt709, color::Transfer::Srgb),     analytic(color::kDisplayP3, color::Transfer::Srgb),
            analytic(color::kBt2020, color::Transfer::Pq),      analytic(color::kBt2020, color::Transfer::Hlg),
            analytic(color::kAdobeRgb, color::Transfer::Power), analytic(color::kBt709, color::Transfer::Bt1886),
        };
        for (const color::Descriptor &d : descriptors) {
            const color::Converter converter(d, 0);
            const color::Encoder encoder(d, false);
            QVERIFY(encoder.valid());
            // PQ, HLG and the pure power laws rise so steeply from black that a float's rounding in
            // the matrices moves their darkest signals by up to two 10-bit codes; sRGB starts linear.
            const bool steep = d.transfer != color::Transfer::Srgb;
            const float tolerance = steep ? 2e-3f : 2e-4f;
            for (const float r : {0.0f, 0.1f, 0.5f, 0.75f, 1.0f})
                for (const float g : {0.05f, 0.4f, 0.9f})
                    for (const float b : {0.0f, 0.3f, 1.0f}) {
                        float rgba[4] = {r, g, b, 1.0f};
                        converter.apply(rgba, 1);
                        encoder.encode(rgba);
                        const float in[3] = {r, g, b};
                        for (int c = 0; c < 3; ++c)
                            QVERIFY2(std::abs(rgba[c] - in[c]) < tolerance,
                                     qPrintable(QStringLiteral("%1: %2 back as %3").arg(d.primaries.r[0]).arg(in[c]).arg(rgba[c])));
                    }
        }
        color::Descriptor cmyk;
        QVERIFY(!color::Encoder(cmyk, true).valid()); // ink has no inverse here
    }

    void labIsCssLab()
    {
        // CSS Color 4's lab() of #F79134, of sRGB red and of white.
        const auto near = [](const std::array<double, 3> &lab, double l, double a, double b) {
            return std::abs(lab[0] - l) < 0.05 && std::abs(lab[1] - a) < 0.05 && std::abs(lab[2] - b) < 0.05;
        };
        QVERIFY(near(color::scRgbToLab(color::srgbToLinear(247 / 255.0f), color::srgbToLinear(145 / 255.0f),
                                       color::srgbToLinear(52 / 255.0f)),
                     70.32, 34.81, 63.10));
        QVERIFY(near(color::scRgbToLab(1, 0, 0), 54.29, 80.80, 69.89));
        QVERIFY(near(color::scRgbToLab(1, 1, 1), 100.0, 0.0, 0.0));
    }

    void theHistogramCountsTheFilesCodes()
    {
        // 8-bit sRGB samples: each colour in the bin of its code; a transparent pixel left out; the
        // luminance of a grey in the grey's bin.
        const HistogramCounter counter(analytic(color::kBt709, color::Transfer::Srgb), false, 8);
        const float signal[] = {10 / 255.0f, 128 / 255.0f, 255 / 255.0f, 1.0f, // a colour
                                128 / 255.0f, 128 / 255.0f, 128 / 255.0f, 1.0f, // a grey
                                200 / 255.0f, 0.0f, 0.0f, 0.0f};                 // transparent
        Histogram h = counter.empty();
        QCOMPARE(h.axis, QStringLiteral("sRGB"));
        QCOMPARE(h.referenceWhite, -1.0); // at the axis' end
        counter.countSignal(signal, 3, &h);
        float linear[12];
        std::copy(std::begin(signal), std::end(signal), linear);
        color::Converter(analytic(color::kBt709, color::Transfer::Srgb), 0).apply(linear, 3);
        counter.countLinear(linear, 3, &h);
        QCOMPARE(h.pixels, quint64(2));
        QCOMPARE(h.bins[Histogram::Red][10], quint32(1));
        QCOMPARE(h.bins[Histogram::Red][128], quint32(1));
        QCOMPARE(h.bins[Histogram::Green][128], quint32(2));
        QCOMPARE(h.bins[Histogram::Blue][255], quint32(1));
        QCOMPARE(h.bins[Histogram::Red][200], quint32(0));
        QCOMPARE(h.bins[Histogram::Luminance][128], quint32(1));
        quint64 luminance = 0;
        for (const quint32 n : h.bins[Histogram::Luminance])
            luminance += n;
        QCOMPARE(luminance, quint64(2));
        Histogram other = h;
        other.add(h);
        QCOMPARE(other.pixels, quint64(4));
        QCOMPARE(other.bins[Histogram::Green][128], quint32(4));
    }

    void theHistogramCountsAFilesFloatsWhateverTheyHold()
    {
        // Floats as a file stores them (an sRGB-encoded float TIFF or JPEG XL): past the axis the last
        // bin, below it or NaN the first; no value outside a long, nor infinity, is rounded into a bin
        // (on Windows, where a long has 32 bits, that gave a negative one).
        const HistogramCounter counter(analytic(color::kBt709, color::Transfer::Srgb), false, 0);
        const float signal[] = {std::numeric_limits<float>::infinity(), 1e30f, 0.5f, 1.0f,
                                -1e30f, std::numeric_limits<float>::quiet_NaN(), 0.25f, 1.0f,
                                3e9f, 8.5e6f, 1.0f, 1.0f};
        Histogram h = counter.empty();
        counter.countSignal(signal, 3, &h);
        QCOMPARE(h.bins[Histogram::Red][255], quint32(2));   // infinity, 3e9
        QCOMPARE(h.bins[Histogram::Green][255], quint32(2)); // 1e30, 8.5e6
        QCOMPARE(h.bins[Histogram::Blue][128], quint32(1));  // 0.5: 127.5 rounded away from zero
        QCOMPARE(h.bins[Histogram::Red][0], quint32(1));     // -1e30
        QCOMPARE(h.bins[Histogram::Green][0], quint32(1));   // NaN
        QCOMPARE(h.bins[Histogram::Blue][64], quint32(1));   // 0.25
        QCOMPARE(h.bins[Histogram::Blue][255], quint32(1));  // 1.0
    }

    void theHistogramsAxes()
    {
        // Narrow range: black and white at the axis' ends.
        color::Descriptor narrow = analytic(color::kBt709, color::Transfer::Bt1886);
        narrow.fullRange = false;
        const HistogramCounter video(narrow, false, 8);
        const float codes[] = {16 / 255.0f, 235 / 255.0f, 126 / 255.0f, 1.0f};
        Histogram v = video.empty();
        video.countSignal(codes, 1, &v);
        QCOMPARE(v.bins[Histogram::Red][0], quint32(1));
        QCOMPARE(v.bins[Histogram::Green][255], quint32(1));
        QCOMPARE(v.bins[Histogram::Blue][128], quint32(1)); // (126 − 16) / 219 of the way
        // PQ: SDR white marked where 203 cd/m² is.
        const Histogram pq = HistogramCounter(analytic(color::kBt2020, color::Transfer::Pq), false, 16).empty();
        QVERIFY(pq.axis.startsWith(QStringLiteral("PQ")));
        QVERIFY(std::abs(pq.referenceWhite - color::nitsToPq(203.0f)) < 1e-3);
        // Linear floats (EXR): counted converted, on PQ.
        const HistogramCounter floats(analytic(color::kBt709, color::Transfer::Linear), false, 0);
        Histogram f = floats.empty();
        QCOMPARE(f.axis, QStringLiteral("PQ"));
        const float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
        floats.countSignal(white, 1, &f); // not from the samples
        QCOMPARE(f.pixels, quint64(0));
        floats.countLinear(white, 1, &f);
        const int bin = int(std::lround(color::nitsToPq(203.0f) * 255.0));
        QCOMPARE(f.bins[Histogram::Red][std::size_t(bin)], quint32(1));
        QCOMPARE(f.bins[Histogram::Luminance][std::size_t(bin)], quint32(1));
        // CMYK: the converted colours on the sRGB curve.
        QCOMPARE(HistogramCounter(color::Descriptor(), true, 8).empty().axis, QStringLiteral("sRGB"));
    }

    void thePickersFilePixel()
    {
        // A texel stands for the file's pixel under its centre, each axis by its own ratio, within
        // the file: a 16386 × 3 image reduced for a 16384 texture is 8193 × 2, and its last texel
        // row is the file's row 2 (the width's ratio, 2, gave row 3, outside a 3-row file).
        QCOMPARE(ColorPicker::filePixel({8192, 1}, {8193, 2}, {16386, 3}), QPoint(16385, 2));
        QCOMPARE(ColorPicker::filePixel({0, 0}, {8193, 2}, {16386, 3}), QPoint(1, 0));
        QCOMPARE(ColorPicker::filePixel({4, 7}, {640, 480}, {640, 480}), QPoint(4, 7)); // not reduced
        // An SVG drawn at a scale: so many document pixels a texel, within the document.
        QCOMPARE(ColorPicker::filePixel({199, 99}, {200, 100}, {100, 50}, 0.5), QPoint(99, 49));
        QCOMPARE(ColorPicker::filePixel({100, 50}, {200, 100}, {100, 50}, 0.5), QPoint(50, 25));
    }

    void thePickersValues()
    {
        const QLocale c(QLocale::C);
        ColorPicker::Source srgb;
        QList<ColorPicker::Row> rows = ColorPicker::values(sample(247 / 255.0, 145 / 255.0, 52 / 255.0), srgb, c);
        QCOMPARE(row(rows, QStringLiteral("HEX")), QStringLiteral("#F79134"));
        QCOMPARE(row(rows, QStringLiteral("RGB")), QStringLiteral("rgb(247, 145, 52)"));
        QCOMPARE(row(rows, QStringLiteral("HSL")), QStringLiteral("hsl(29, 92%, 59%)"));
        QCOMPARE(row(rows, QStringLiteral("HSV")), QStringLiteral("hsv(29, 79%, 97%)"));
        QCOMPARE(row(rows, QStringLiteral("CMYK")), QStringLiteral("cmyk(0%, 41%, 79%, 3%)"));
        QCOMPARE(row(rows, QStringLiteral("Lab")), QStringLiteral("lab(70.3% 34.8 63.1)"));
        QCOMPARE(row(rows, QStringLiteral("Pixel")), QStringLiteral("12, 34"));
        QCOMPARE(row(rows, QStringLiteral("File")), QStringLiteral("(none)")); // 8-bit sRGB: the RGB row
        QCOMPARE(row(rows, QStringLiteral("Luminance")), QStringLiteral("(none)")); // SDR

        rows = ColorPicker::values(sample(247 / 255.0, 145 / 255.0, 52 / 255.0, 0.5), srgb, c);
        QCOMPARE(row(rows, QStringLiteral("HEX")), QStringLiteral("#F7913480"));
        QCOMPARE(row(rows, QStringLiteral("RGBA")), QStringLiteral("rgba(247, 145, 52, 0.5)"));
        QCOMPARE(row(rows, QStringLiteral("HSL")), QStringLiteral("hsla(29, 92%, 59%, 0.5)"));
        QCOMPARE(row(rows, QStringLiteral("Lab")), QStringLiteral("lab(70.3% 34.8 63.1 / 0.5)"));

        // Outside sRGB: clipped, and said so; Lab keeps it. HDR: the luminance in cd/m².
        ColorPicker::Sample bright;
        bright.rgb[0] = bright.rgb[1] = bright.rgb[2] = 2.0f;
        ColorPicker::Source hdr;
        hdr.colour = analytic(color::kBt2020, color::Transfer::Pq);
        hdr.bits = 10;
        hdr.hdr = true;
        rows = ColorPicker::values(bright, hdr, c);
        QCOMPARE(row(rows, QStringLiteral("HEX")), QStringLiteral("#FFFFFF"));
        QVERIFY(row(rows, QStringLiteral("HEX"), false).contains(QStringLiteral("sRGB")));
        QCOMPARE(row(rows, QStringLiteral("Luminance")), QStringLiteral("406"));
        QVERIFY(color::scRgbToLab(2, 2, 2)[0] > 100.0);
        // The file's value: "…" until read, then its codes.
        QCOMPARE(row(rows, QStringLiteral("File"), false), QStringLiteral("…"));
        ColorPicker picker;
        picker.pick(bright, hdr, c);
        QVERIFY(picker.waitsForFileValue());
        QVERIFY(picker.setFileValue({{668.0, 668.0, 668.0}, 10, false}));
        QVERIFY(!picker.waitsForFileValue());
        QCOMPARE(row(picker.rows(), QStringLiteral("File")), QStringLiteral("668, 668, 668"));
        QCOMPARE(row(picker.rows(), QStringLiteral("File"), false), QStringLiteral("668, 668, 668 (10-bit)"));
        QVERIFY(picker.setFileValue({}));
        QCOMPARE(row(picker.rows(), QStringLiteral("File"), false), QStringLiteral("—"));
        hdr.firstFrame = false; // an animation's later frame: not read
        QCOMPARE(row(ColorPicker::values(bright, hdr, c), QStringLiteral("File")), QStringLiteral("(none)"));
    }

    // The Pixel row's column and row have no group separators in any language (the owner, D-119 (6)):
    // English grouped them with the comma that separates the two, "12,345, 6,789". The locale's
    // digits stay; the copied text has neither.
    void thePickersPixelRow()
    {
        ColorPicker::Sample far = sample(0.5, 0.5, 0.5);
        far.position = QPoint(12345, 6789);
        const ColorPicker::Source srgb;
        const QList<ColorPicker::Row> english = ColorPicker::values(far, srgb, QLocale(QLocale::English, QLocale::UnitedStates));
        QCOMPARE(row(english, QStringLiteral("Pixel"), false), QStringLiteral("12345, 6789"));
        QCOMPARE(row(english, QStringLiteral("Pixel")), QStringLiteral("12345, 6789"));
        const QLocale arabic(QLocale::Arabic, QLocale::Egypt);
        QString digits; // "12345, 6789" in the locale's digits, each written alone
        for (const QChar ch : QStringLiteral("12345, 6789"))
            digits += ch.isDigit() ? arabic.toString(ch.digitValue()) : QString(ch);
        QCOMPARE(row(ColorPicker::values(far, srgb, arabic), QStringLiteral("Pixel"), false), digits);
    }

    void thePickersPanel()
    {
        ColorPicker picker;
        ColorPicker::Look look;
        look.devicePixelRatio = 2.0;
        look.panel = QColor(0, 0, 0, 179);
        look.ink = QColor(240, 240, 240);
        look.label = QColor(170, 170, 170);
        look.hover = QColor(80, 200, 120, 128);
        look.font = QGuiApplication::font();
        QVERIFY(picker.setLook(look));
        QVERIFY(!picker.setLook(look));
        // Before a pick: the hint, no value to click; the loupe, the swatches inside the panel.
        const QSizeF empty = picker.size();
        QVERIFY(empty.height() >= ColorPicker::kCells * ColorPicker::kCell + 2 * ColorPicker::kPadding);
        QCOMPARE(picker.rowAt(QPointF(empty.width() - 20, ColorPicker::kPadding + 5)), -1);
        const QRectF panel(QPointF(0, 0), empty);
        QVERIFY(panel.contains(picker.loupeRect()) && panel.contains(picker.currentRect()) && panel.contains(picker.pickedRect()));
        QVERIFY(!picker.loupeRect().intersects(picker.currentRect()) && !picker.currentRect().intersects(picker.pickedRect()));
        QImage texture = picker.texture();
        QCOMPARE(texture.size(), (empty * 2.0).toSize());
        // The holes are transparent but for the frame of the centre pixel.
        const QPointF inLoupe = (picker.loupeRect().topLeft() + QPointF(2, 2)) * 2.0;
        QCOMPARE(qAlpha(texture.pixel(inLoupe.toPoint())), 0);
        QCOMPARE(qAlpha(texture.pixel((picker.currentRect().center() * 2.0).toPoint())), 0);
        QVERIFY(qAlpha(texture.pixel(QPoint(2 * int(ColorPicker::kPadding / 2), 2 * int(empty.height() / 2)))) > 0);
        // A pick: a row a value, each under its own place.
        picker.pick(sample(1, 0.5, 0), ColorPicker::Source(), QLocale(QLocale::C));
        const QSizeF size = picker.size();
        QVERIFY(picker.rows().size() >= 7);
        for (int i = 0; i < picker.rows().size(); ++i)
            QCOMPARE(picker.rowAt(QPointF(size.width() - 30, ColorPicker::kPadding + (i + 0.5) * picker.rowHeight())), i);
        // The swatches under the loupe, side by side, as wide as it (the owner, D-116).
        QCOMPARE(picker.currentRect().top(), picker.loupeRect().bottom() + ColorPicker::kGap);
        QCOMPARE(picker.pickedRect().top(), picker.currentRect().top());
        QCOMPARE(picker.pickedRect().right(), picker.loupeRect().right());
        QCOMPARE(picker.rowAt(picker.pickedRect().center()), -1);
        QCOMPARE(picker.rowAt(QPointF(picker.loupeRect().center())), -1); // the loupe is not a value
        const qint64 before = picker.texture().cacheKey();
        QVERIFY(picker.setHover(1));
        QVERIFY(!picker.setHover(1));
        QVERIFY(picker.texture().cacheKey() != before);
        QVERIFY(picker.clear());
        QVERIFY(!picker.clear());
        QVERIFY(!picker.picked());
    }
};

int main(int argc, char **argv)
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    qRegisterMetaType<VectorDetail>();
    qRegisterMetaType<Image>();
    int status = 0;
    {
        SlideshowTest test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        GalleryTest test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        DetailTest test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        DecodeMemoryTest test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        ToolbarTest test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        ColorTest test;
        status |= QTest::qExec(&test, argc, argv);
    }
    return status;
}

#include "unit_tests.moc"
