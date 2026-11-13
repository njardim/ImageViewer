// Unit tests of the window's parts with state of their own (D-102): the gallery's thumbnails and
// layout, the slideshow's clock and the zoom's detail of an SVG, each driven alone, its decoding
// handed in. The rules here are the ones a defect once broke: a folder that keeps changing never got
// its thumbnails (D-100), a slow image was skipped by the slideshow (D-100), a failed detail was
// asked for again without end (D-95); and the scale an SVG is drawn at for its window, by which the
// window draws an SVG again when the window outgrows it (D-106). Run by CI on the three systems; no
// argument needed (the offscreen platform unless QT_QPA_PLATFORM says otherwise).
#include "decoders.h"
#include "detail.h"
#include "gallery.h"
#include "slideshow.h"

#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMutex>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QWaitCondition>

#include <atomic>
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
        std::atomic_int order{0}, secondAt{0}, thirdAt{0};
        QThread *first = QThread::create([&] {
            const DecodeMemory decode;
            DecodeMemory::reserve(60, 100);
            holds.release();
            mayEnd.acquire();
        });
        first->start();
        holds.acquire();
        // 60 are held: 50 more do not fit, and 30, asked for later, do not pass the 50 (in the order asked).
        QThread *second = QThread::create([&] {
            const DecodeMemory decode;
            DecodeMemory::reserve(50, 100);
            secondAt = ++order;
            holds.release();
            mayEnd.acquire();
        });
        second->start();
        QTest::qWait(100);
        QThread *third = QThread::create([&] {
            const DecodeMemory decode;
            DecodeMemory::reserve(30, 100);
            thirdAt = ++order;
            holds.release();
        });
        third->start();
        QTest::qWait(200);
        QCOMPARE(order.load(), 0); // both wait
        QCOMPARE(DecodeMemory::reserved(), qint64(60));
        {
            // One that holds some already, or that must not wait, is not held back.
            const DecodeMemory decode;
            DecodeMemory::reserve(70, 100, false);
            QCOMPARE(DecodeMemory::reserved(), qint64(130));
            DecodeMemory::reserve(80, 100);
            QCOMPARE(DecodeMemory::reserved(), qint64(140));
        }
        QCOMPARE(order.load(), 0);
        mayEnd.release(); // the first decode ends
        QVERIFY(holds.tryAcquire(2, 5000));
        QCOMPARE(secondAt.load(), 1);
        QCOMPARE(thirdAt.load(), 2);
        mayEnd.release();
        for (QThread *thread : {first, second, third}) {
            QVERIFY(thread->wait(5000));
            delete thread;
        }
        QCOMPARE(DecodeMemory::reserved(), qint64(0));
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
    return status;
}

#include "unit_tests.moc"
