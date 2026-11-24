// The main window: a QWindow presented through QRhi so it can use an HDR
// swapchain (decision D-09). ViewerWindow is implemented in ten files:
//   viewer.cpp      display and view state;
//   windowstates.cpp the window's states: the session's place, full screen (the image gliding), minimized;
//   navigation.cpp  the folder, loading, preloading (D-33) and watching for changes;
//   overlays.cpp    what is drawn over the image: information panel, top overlay (D-34),
//                   navigation buttons;
//   toolbars.cpp    the toolbar at the top (D-87, D-112) and the slideshow's (D-65), and the hiding
//                   of those and of the gallery while the pointer is still;
//   commands.cpp    the command table, menus and dialogs (D-30);
//   files.cpp       file operations: show, copy, rename, trash and undo, delete (D-41);
//   playback.cpp    animation playback and the slideshow (E6, E11, D-41);
//   input.cpp       the keyboard, the mouse and the wheel, the side zones and the pointer.
// Its parts with state of their own are classes (D-102): Gallery (gallery.h), Slideshow
// (slideshow.h, the slideshow's clock), VectorDetails (detail.h, the zoom's detail of an SVG) and
// Toolbar and AutoHide (toolbar.h).
#pragma once

#include "cache.h"
#include "colorpicker.h"
#include "detail.h"
#include "gallery.h"
#include "histogram.h"
#include "image.h"
#include "renderer.h"
#include "settings.h"
#include "slideshow.h"
#include "toolbar.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QImage>
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QLocale>
#include <QPainterPath>
#include <QPointF>
#include <QSet>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QWindow>

#include <QLoggingCategory>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

// What is drawn over the image (panels, overlay, gallery) keeps kEdgeMargin from the window's
// edges and rounds its corners by kPanelRadius (D-58, D-60), logical pixels.
inline constexpr double kEdgeMargin = 16.0;
inline constexpr double kPanelRadius = 8.0;
// The window's least size, logical pixels; also when the window takes an image's size.
inline constexpr QSize kMinWindowSize(320, 240);
// File sizes as the platform's file manager shows them: Explorer counts in 1024s, Finder and the
// Linux desktops in 1000s (overlays.cpp).
QString fileSizeText(qint64 bytes, const QLocale &locale);
// Between the top overlay's facts, the title's and a gallery tooltip's.
inline const QString kFieldSeparator = QStringLiteral("   ·   ");

// One step of Zoom In or Zoom Out, and of a wheel notch.
inline constexpr double kZoomStep = 1.25;

class QAction;
class QMenu;
class QMenuBar;
class QTranslator;
class QVulkanInstance;
class QWidget;

// Dialogs and menus are widgets; the viewer is a QWindow. Parenting their native window
// keeps them above it, centred on it and, on Wayland, positioned at all.
void makeTransient(QWidget &widget, QWindow *parent);

// The interface's response times, for the tests (D-94): main.cpp's event-loop ticks, render()'s long frames.
Q_DECLARE_LOGGING_CATEGORY(lcLatency)

class ViewerWindow : public QWindow {
    Q_OBJECT

public:
    explicit ViewerWindow(QVulkanInstance *vulkan = nullptr);
    ~ViewerWindow() override;

    void openFile(const QString &path);
    // Shows the window where the previous session left it (when remembered), else at a default size.
    void showRestored(const SessionState &session);

    // Every user command: one table drives the keyboard, the context menu and its submenus.
    enum class Command {
        Open, ClearRecent, ShowInFolder, OpenWith, OpenWithOther, CopyImage, CopyPath,
        Rename, MoveToTrash, DeletePermanently, UndoTrash, Settings, Quit,
        Previous, Next, First, Last,
        ZoomIn, ZoomOut, Fit, FitWidth, FitHeight, Fill, ActualSize, LockZoom,
        FullScreen, Minimize, Info, InfoOverlay, Checkerboard, Gallery, Toolbar,
        RotateClockwise, RotateCounterclockwise, FlipHorizontal, FlipVertical, ColorPicker,
        ExposureUp, ExposureDown, ExposureReset, ToneMap, ClipWarning,
        PlayPause, PreviousFrame, NextFrame, Slideshow,
        About, AboutQt,
    };
    Q_ENUM(Command) // its names are the keys of the user's shortcuts (D-52)

protected:
    bool event(QEvent *e) override;
    void exposeEvent(QExposeEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void moveEvent(QMoveEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    enum class Zone { None, Previous, Next }; // clickable window sides (D-31)

    // viewer.cpp
    void initializeRenderer();
    void render();
    // windowstates.cpp: full screen and back with the image gliding (D-114), minimized and back.
    void toggleFullScreen();
    void leaveFullScreen(); // back to maximized or normal, whichever it was
    void minimize();
    void returnFromMinimized(); // full screen or maximized again, as before minimize()
    void startGlide();            // before the window changes: where the image is on the screen
    void windowChanged();         // the window has its new size: the glide starts
    void presentChangedWindow();  // Windows: the first frame of the new size at once
    QRectF glidedImageRect(const QRectF &target); // device pixels: on its way there during a glide
    void applySettings(const Settings &settings);
    // Sets one preference changed outside the Settings dialog (a toggle) and saves it into the
    // stored preferences rather than saving all of this instance's: another instance may have
    // saved others since this one started.
    void savePreference(const std::function<void(Settings &)> &change);
    void saveSession() const;

    QSizeF deviceSize() const;
    QSizeF displayedImageSize() const; // after view rotation
    double fitZoom(FitMode mode, QSizeF view = {}) const; // in `view` (default: the window's device size)
    double currentZoom() const; // of the texture: device pixels per texel
    double sourceScale() const; // the file's pixels per texel: above 1 for a texture drawn smaller
    double naturalZoom() const; // the texture's zoom at 100 %: an SVG's own pixels one per device pixel (D-94)
    double shownZoom() const;   // of the file's own pixels (the overlay's per cent)
    QRectF imageRect() const;
    void zoomAt(double factor, const QPointF &devicePos);
    void chooseFit(FitMode mode); // a command: the mode stays for the next images (D-65)
    void setFit(FitMode mode);
    void resetPan();      // centred; a width-fitted image from its top
    bool canPan() const;  // the image is larger than the window
    void clampPan();
    void matchWindowToImage(); // the window at the image's size, within Settings::windowFitPercent of the screen
    Renderer::Frame imageFrame() const; // colour-related fields of the current frame
    int textureLimit(const QString &path) const; // longest side the GPU texture may have
    void recoverFromDeviceLoss();
    QRectF zoneButtonRect(Zone zone) const;      // device pixels (overlays.cpp)
    // input.cpp
    double sideZoneWidth() const;                // logical pixels; 0 without side zones
    bool inSideStrip(const QPointF &position) const; // over a side zone, even one with nothing to do
    Zone zoneAt(const QPointF &position) const;  // logical pixels
    void setHoverZone(Zone zone);
    void cancelPress();   // a press whose release goes elsewhere clicks nothing
    void pointerActive(); // shows the pointer again and restarts its hiding timer
    void updateCursor();
    // The color picker (m_picker, D-115): while it is on, a click on the image picks the pixel under
    // the pointer instead of the side zones and full screen; its panel, above the information panel,
    // shows the loupe and the values, a click on one copies it.
    void togglePicker();
    ColorPicker::Look pickerLook() const;
    ColorPicker::Source pickerSource() const;
    void updatePicker();           // its panel laid out and drawn when it changed
    QRectF pickerRect() const;     // device pixels: its panel; empty when off
    int pickerRowAt(const QPointF &position) const; // logical pixels: a value's index, or -1
    bool overPicker(const QPointF &position) const; // logical pixels: on its panel
    // The texture's pixel under `position` (logical pixels); none outside the image.
    std::optional<QPoint> texelAt(const QPointF &position) const;
    void pickAt(const QPointF &position);
    void fillFileValue(); // the picked pixel's value in the file, from m_fileSamples or once read
    void fileSamplesRead();
    void placePicker(Renderer::Frame *frame) const;
    void copyPickerValue(int row);

    // View actions.
    void rotate(int quarterTurns);
    void flipHorizontal();
    void flipVertical();
    void adjustExposure(float ev);
    void resetExposure();
    void toggleToneMap();
    void toggleClipWarning();
    void toggleInfo();

    // navigation.cpp
    QString currentPath() const; // the requested entry of m_files, empty when there is none
    void startLoading(int index);
    // Another image of the folder, asked for by the user or the slideshow: `direction` (1 or -1)
    // says which neighbour is preloaded first, and a slideshow's interval starts again.
    void goTo(int index, int direction);
    void step(int delta);
    bool hasNeighbour(int delta) const; // false at either end when navigation does not loop
    QStringList neighbourhood() const;  // current image, then the neighbours, direction of travel first
    bool needsDisplay(const QString &path, int limit) const; // not what the window shows now
    void scheduleWork();                // show from the cache, else decode; then preload
    void startDecode(const QString &path, int limit, qint64 maxPixels, bool redraw = false);
    void decodeFinished();
    // The zoom's detail (D-93, m_details): the view at every frame, and what is drawn or fails.
    void updateDetail();
    // An SVG on screen drawn for a smaller window than this one (D-106): the window to draw it again
    // for, where that would show it without a detail; empty: none.
    QSize redrawWindow(const DetailView &view) const;
    // The window as an SVG is drawn for it (DecodeOptions::view), its sides exchanged for a view
    // turned a quarter (D-111).
    QSize vectorWindow(int quarterTurns) const;
    void detailDrawn(const VectorDetail &detail, const Image &image);
    void detailFailed(const VectorDetail &detail, const QString &error);
    void showImage(Image image, int limit, bool redrawn = false); // redrawn: the same view, for this window
    void clearShownImage();             // nothing on screen: no image, no animation
    void watchFile(const QString &path); // the one file watched, the image on screen; empty: none
    void reloadCurrent();               // decode the shown file again, keeping the view
    void setFolder(const QString &folder);
    void relist();                      // folder sorted per the settings, keeping the current file
    // Lists `folder` on a worker thread (a large folder took half a second) and hands the
    // sorted files to `done` on this thread, unless a newer listing was started meanwhile.
    void listFolder(const QString &folder, std::function<void(FolderListing)> done);
    void applyListing(FolderListing listing);
    void refreshFolder();               // after a change on disk
    void removeCurrentFromList();       // the file is gone: the next one takes its place
    void showFolderEmpty();             // nothing left to show in the folder
    void reportOpenFailure(const QString &why); // a notice over a shown image, else the error
    void addRecentFile(const QString &path);
    // Every instance shares the recent files: a change is made to the stored list, which may
    // have grown in another instance meanwhile, and m_recent follows it.
    void editRecentFiles(const std::function<void(QStringList &)> &edit);

    // playback.cpp: animation (E6) and slideshow (E11).
    struct FrameResult {
        int generation = 0; // m_frameGeneration when it was asked for
        int requested = 0;  // the index asked for (the reader wraps to 0 past the end)
        Animation::Frame frame;
        QString error;
    };
    bool onScreen() const; // exposed and not minimized: what is drawn is seen
    void resumeOnScreen(); // what waited for the window to come back goes on
    void startAnimation();
    void stopAnimation();
    void requestFrame(int index);
    void frameDecoded();
    void frameTimeout();
    void presentFrame(const Animation::Frame &frame);
    void togglePause();
    void toggleSlideshowPause();
    void stepFrame(int delta);
    void toggleSlideshow();
    void stopSlideshow();
    void slideshowDue(); // m_slideshow's interval ended: the next image, or the end

    // toolbars.cpp: the toolbar at the top (m_mainToolbar, D-87, D-112), its buttons the user's
    // commands, its "…" panel (D-113); the slideshow's toolbar (m_slideshowBar, D-65), stop and pause
    // or play below the top overlay, in the same look (D-113); the hiding of the bars, the gallery,
    // the top overlay and the information panel while the pointer is still (AutoHide).
    static std::optional<Glyph> glyphFor(Command command); // none: not a toolbar button
    QString buttonName(Command command) const; // its menu text, without the dialog's "…"
    Toolbar::Look toolbarLook() const;         // both bars'
    QList<Toolbar::Button> mainToolbarButtons() const;
    QList<Toolbar::Button> slideshowBarButtons() const; // stop, then pause or play
    void updateMainToolbar();       // its buttons' states, laid out for the window, drawn when changed
    QRectF mainToolbarRect() const; // device pixels; empty when not shown
    QRectF toolbarMenuRect() const; // device pixels: the "…" panel; empty when closed
    double mainToolbarReserve() const; // logical pixels it takes from the window's top while it shows
    int mainToolbarAt(const QPointF &position) const; // logical pixels: a button's id, Toolbar::kMore, or -1
    int toolbarMenuAt(const QPointF &position) const; // logical pixels: a row's id, or -1
    void setMainToolbarHover(int id);
    void setSlideshowBarHover(int id);
    // Tooltips over the image (D-116): drawn by us in the panels' look on the hover's colour, kTipGap
    // from what they name (below a button, above a thumbnail) or from the pointer (a picker's value).
    enum class TipSource { None, Toolbar, Gallery, Picker };
    enum class TipPlace { Below, Above, Pointer };
    void showTip(TipSource source, const QString &text, const QRectF &anchor, TipPlace place); // anchor: logical pixels
    void hideTip(TipSource source = TipSource::None); // None: whichever shows
    QRectF tipRect() const;         // device pixels; empty: none
    void showPendingTip();          // the pointer rests on a toolbar's button or a thumbnail
    void showToolbarTooltip();      // the hovered button's name and key
    void toolbarButtonClicked(int id); // a button of either bar, or a row of the "…" panel
    void closeToolbarMenu();
    void showOpenWithMenu(const QPoint &globalPos);
    void fillOpenWithMenu(QMenu *menu); // the system's applications for the image, and "Other Application…"
    void toggleMainToolbar();
    bool slideshowInMainToolbar() const; // its buttons show there: the slideshow's own toolbar does not (D-112)
    bool galleryShown() const;           // on screen: on, and not hidden by itself
    QRectF slideshowToolbarRect() const; // device pixels; empty when hidden
    int slideshowBarAt(const QPointF &position) const; // logical pixels: a button's id, or -1
    void showSlideshowToolbar(); // when the slideshow starts
    void updateSlideshowToolbar();
    void configureAutoHide();    // from the settings: one delay, a switch each (D-113)

    // overlays.cpp
    void updateOverlay();               // information panel (and the top overlay's content)
    void updateTopOverlay();
    void updateMessage();               // m_error, in the middle of the window (D-63)
    // The gallery (m_gallery, D-60), laid out for the window; navigation.cpp.
    void updateGallery();
    void galleryClicked(int slot);
    void toggleGallery();
    // The top overlay's facts for `fields`, in its order and wording; also the title's (D-58).
    QStringList overlayParts(const QList<OverlayField> &fields, int *nameIndex = nullptr) const;
    // A gallery thumbnail's tooltip (D-116): the top overlay's facts of its file that hold without
    // showing it (no zoom, no output), its name on a line of its own.
    QString galleryTipText(int file) const;
    void updateTitle(); // per Settings::titleMode
    OverlayVisibility topOverlayMode() const; // for full screen or window, whichever applies
    bool topOverlayVisible() const;
    void setPointerAtTop(bool atTop);
    double topActivationHeight() const; // logical pixels
    void placeOverlays(Renderer::Frame *frame) const;
    QRectF infoPanelRect() const; // device pixels: the information panel with its notices; empty: none
    bool overHistogramChoice(const QPointF &position) const; // logical pixels: on the panel's histogram choice
    void showHistogramMenu();     // what the histogram shows, from the panel (D-116)
    void updateNavigationButtons();
    void showNotice(const QString &text); // brief message, in a panel over the information panel

    // commands.cpp
    struct CommandInfo {
        Command command;
        QList<QKeySequence> shortcuts;
        bool repeats; // acts again while the key is held (navigation, zoom); toggles and file actions do not
    };
    static const QList<CommandInfo> &commands();
    static QString commandKey(Command command);
    QList<QKeySequence> shortcutsFor(Command command) const; // the user's, else the defaults
    QString commandText(Command command) const;
    bool isCommandEnabled(Command command) const;
    bool isCommandChecked(Command command, bool *checkable) const;
    void run(Command command);     // every way of giving a command comes here: keys, menus, mouse
    void execute(Command command); // run()'s work, once the command is enabled
    bool executeShortcut(QKeyEvent *e); // true when a command took the key
    QAction *addCommand(QMenu *menu, Command command);
    void showContextMenu(const QPoint &globalPos);
    void installApplicationMenu();   // macOS: About and Settings in the application menu (D-96)
    void translateApplicationMenu(); // ... named in the language shown
    void showOpenDialog();
    void toggleTopOverlay();
    void toggleCheckerboard();
    void toggleLockZoom();
    void showSettings();
    void showAbout();
    bool currentFileIsShown() const; // the displayed image is the current entry of the folder

    // files.cpp
    void showInFolder();
    void openWithOtherApplication();
    void copyImage();
    void startCopy(const QString &path, int frame); // frame: of an animation, the one on screen
    void imageCopied();
    void copyPath();
    void renameFile();
    void moveToTrash();
    void deletePermanently();
    void undoTrash();

    Renderer m_renderer;
    bool m_rendererReady = false;
    bool m_rendererFailed = false;
    Settings m_settings;
    QString m_lastDirectory;
    QStringList m_recent; // Open Recent, most recent first

    QStringList m_files;
    int m_index = -1;
    int m_direction = 1; // of the last step: the neighbour ahead is preloaded first
    QString m_textureCapPath; // the file whose upload the GPU refused...
    int m_textureCap = 0;     // ...and the size that file is decoded at now
    // Decodes run on their own thread: decodeImage() itself fans out on the global pool
    // with blockingMap, which deadlocks if the decode occupies the pool's only thread.
    // One decode at a time: they cannot be cancelled, and each needs a full image of memory.
    QThreadPool m_decodePool;
    QFutureWatcher<Image> m_watcher;
    // Animation (playback.cpp): the next frame is decoded on its own thread while the shown
    // one is on screen; a frame is shown when both its turn has come and it is ready.
    std::shared_ptr<Animation> m_animation;
    QThreadPool m_framePool;
    QFutureWatcher<FrameResult> m_frameWatcher;
    bool m_frameBusy = false;   // as m_decodeBusy, for m_frameWatcher
    int m_frameGeneration = 0;  // changes when the animation shown changes
    int m_frameIndex = 0;       // the frame on screen
    PixelBuffer m_framePixels;  // ...its pixels (none for the first frame, which m_image's file gives), for the clipboard
    std::shared_ptr<const Histogram> m_frameHistogram; // ...and its histogram (none for the first frame)
    int m_frameDurationMs = 0;  // ...and how long it stays
    QElapsedTimer m_panelRefreshed; // while playing, the panel's frame number follows at intervals
    int m_wantedFrame = -1;     // the frame to show next; -1: none
    std::optional<Animation::Frame> m_readyFrame; // decoded, waiting for its turn
    bool m_frameDue = false;    // its turn has come: show it as soon as it is decoded
    bool m_animationPaused = false;
    bool m_slideshowLeavesFullScreen = false; // it entered full screen (Settings::slideshowFullScreen)
    int m_loopsDone = 0;
    bool m_frameWaitsForScreen = false; // nothing is drawn while the window is off the screen
    QTimer m_frameTimer;
    Slideshow m_slideshow;
    Toolbar m_slideshowBar;       // the slideshow's toolbar (D-65, D-113)
    AutoHide m_toolbarHide;       // ...hidden while the pointer is still (Settings::slideshowToolbarAutoHide)
    qint64 m_slideshowBarTexture = 0; // the cache key of the texture the renderer has; 0: none
    bool m_slideshowBarShown = false;
    Toolbar m_mainToolbar;        // the toolbar at the top (D-112)
    AutoHide m_mainToolbarHide;   // ...hidden while the pointer is still (Settings::toolbarAutoHide)
    qint64 m_mainToolbarTexture = 0; // as m_slideshowBarTexture
    qint64 m_toolbarMenuTexture = 0; // ...its "…" panel's
    bool m_slideshowInMainToolbar = false; // as last laid out
    int m_toolbarMenuPress = -1;  // a row of the "…" panel pressed, a click when released on it
    QTimer m_tooltipTimer;        // a button's or a thumbnail's tooltip, once the pointer rests on it
    TipSource m_tipSource = TipSource::None; // the tooltip shown...
    QString m_tipText;
    QRectF m_tipAnchor;           // ...what it names, logical pixels...
    TipPlace m_tipPlace = TipPlace::Below;
    QSize m_tipSize;              // ...its texture's size, device pixels...
    qreal m_tipDpr = 1.0;         // ...at this pixel ratio
    AutoHide m_galleryHide;       // the gallery (Settings::galleryAutoHide)
    AutoHide m_overlayHide;       // the top overlay (Settings::overlayAutoHide, D-113)
    AutoHide m_panelHide;         // the information panel (Settings::panelAutoHide); its notices stay
    // Set from startDecode() until decodeFinished(). Not m_watcher.isRunning(): that is false
    // as soon as the worker ends, while the finished signal is still queued, and setting a
    // new future then would discard the result that was never delivered.
    bool m_decodeBusy = false;
    int m_jobLimit = 0;                   // texture limit of the running decode
    int m_jobGeneration = 0;              // ...and the m_decodeGeneration it started in
    QString m_jobPath;                    // ...its file
    // ...set when its image is no longer wanted: a decode in a worker process ends at once (D-93)
    std::shared_ptr<std::atomic_bool> m_jobCancel;
    QSize m_jobRedraw; // ...the window it draws the SVG on screen again for (D-106); empty: no redraw
    // The SVG on screen drawn again for a window that outgrew it (D-106): the window wanted, asked
    // for once it has stayed as long as a detail waits for its view; one that failed, or gave no
    // larger image, is not asked for again while the window stays.
    QSize m_redrawWanted;
    QTimer m_redrawTimer;
    QString m_redrawFailedPath;
    QSize m_redrawFailed;
    VectorDetails m_details; // the zoom's detail of an SVG, drawn beside the decodes
    std::unique_ptr<QMenuBar> m_menuBar; // macOS: the application's menu bar (installApplicationMenu())
    std::unique_ptr<QTranslator> m_menuTranslator; // macOS: its About and Settings in our words
    QAction *m_aboutAction = nullptr, *m_settingsAction = nullptr;
    int m_decodeGeneration = 0;           // changes when decoded descriptions become outdated (language)
    QFutureWatcher<QImage> m_copyWatcher; // full-resolution decode for the clipboard
    bool m_copyBusy = false;              // as m_decodeBusy, for m_copyWatcher
    QString m_copyPath;                   // being copied
    int m_copyFrame = 0;                  // ...that frame of it
    QString m_copyNext;                   // asked for while another copy ran; empty: none
    int m_copyNextFrame = 0;
    ImageCache m_cache;
    Gallery m_gallery; // reads m_settings and m_files, declared before it
    Image m_image; // metadata of the displayed image (pixels live on the GPU and in the cache)
    int m_shownLimit = 0;     // texture limit m_image was decoded at
    bool m_imageStale = false; // the shown image must be decoded or uploaded again
    QString m_folder;
    QFileSystemWatcher m_folderWatcher; // the folder and the shown file
    QTimer m_folderTimer;               // changes come in bursts: re-list once they settle
    quint64 m_listing = 0;              // generation of the latest folder listing
    struct TrashedFile {
        QString original;
        QString inTrash;
        qint64 size = -1;   // what was moved, so Undo never restores another file that
        QDateTime modified; // later took the same name in the trash
    };
    QList<TrashedFile> m_trashed; // for Undo, most recent last
    QString m_message; // status (an image reduced for the GPU), with the notices; cleared when an image arrives
    QString m_error;   // why no image is shown, in the middle of the window; cleared likewise
    QString m_awaitedPath; // asked for while another image is on screen; empty: none
    QString m_notice;  // confirmation of a command; disappears after a few seconds
    QTimer m_noticeTimer;

    // The zoom follows the window in this mode (D-51); without one, it is m_zoom.
    std::optional<FitMode> m_fit = m_settings.fitMode; // the first image follows the setting, locked or not
    double m_zoom = 1.0; // when not fitting: the zoom over naturalZoom(), 1 at 100 %
    bool m_windowMatched = false; // the window took the first image's size (WindowFit::FirstImage)
    QPointF m_pan;       // offset of the image centre from the window centre, device pixels
    QSizeF m_laidOutIn;  // the device size m_pan was last laid out in (resizeEvent)
    int m_quarterTurns = 0;
    bool m_mirrored = false;
    float m_exposureEv = 0.0f;
    QSize m_overlaySize; // device pixels; empty when no overlay is shown
    QSize m_messageSize; // likewise for the error in the middle of the window
    QString m_messageKey;
    QString m_overlayOutput; // output description the overlay was built with
    QSize m_topOverlaySize;  // device pixels; empty when there is nothing to show
    QString m_topOverlayKey; // what the top overlay texture shows, to skip identical uploads
    QString m_panelKey;      // likewise for the information panel
    QRectF m_histogramChoice; // the panel's histogram choice, logical pixels in its texture; empty: none (D-116)
    bool m_histogramChoiceHover = false;
    bool m_grabbing = false;  // the image follows a drag: the closed hand (D-116)
    bool m_pointerAtTop = false; // on-hover mode: the overlay is shown
    QTimer m_topOverlayTimer;    // hides it after the pointer has left the top band
    QRect m_normalGeometry;  // last geometry while neither maximized nor full screen
    QRectF m_glideFrom;      // where the image was on the screen, global logical pixels (D-114)
    QElapsedTimer m_glideClock; // running while the image glides to its new place
    bool m_glidePending = false; // the window is about to change: the glide starts when it has
    bool m_maximizedBeforeFullScreen = false;
    Qt::WindowStates m_statesBeforeMinimize; // full screen or maximized: given back by returnFromMinimized()
    std::function<void()> m_menuChoice; // what the context menu chose: run once the menu is gone

    bool m_dragging = false;
    QPointF m_dragOrigin;
    QPointF m_panOrigin;
    Zone m_hoverZone = Zone::None;
    QTimer m_pointerTimer;       // hides a still pointer (Settings::pointerAutoHide, autoHideMs)
    bool m_pointerHidden = false;
    Zone m_pressZone = Zone::None; // a press that may become a click on a side zone
    bool m_pressMoved = false;     // the press turned into a drag
    QPointF m_pointer;             // where the pointer is, logical pixels...
    bool m_pointerIn = false;      // ...when it is over the window

    ColorPicker m_picker;          // the color picker (D-115)
    bool m_pickerOn = false;
    qint64 m_pickerTexture = 0;    // the cache key of its panel's texture the renderer has; 0: none
    QSize m_pickerTextureSize;     // ...and its size
    int m_pickerPress = -1;        // a value pressed, copied when released on it
    bool m_pickPress = false;      // a press on the image that picks when released without a drag
    QString m_pickPath;            // the file of the pixel picked
    double m_pickerOffset = 0;     // logical pixels from the information layer's bottom to the panel's

    // The file's own samples, decoded again for the picker's value of the file (D-115), on m_samplesPool.
    std::shared_ptr<const FileSamples> m_fileSamples; // of m_image's file; null: not read
    QThreadPool m_samplesPool;
    QFutureWatcher<std::shared_ptr<const FileSamples>> m_samplesWatcher;
    bool m_samplesBusy = false;    // as m_decodeBusy
    QString m_samplesPath;         // the file being read
};
