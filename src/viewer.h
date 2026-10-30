// The main window: a QWindow presented through QRhi so it can use an HDR
// swapchain (decision D-09). ViewerWindow is implemented in eight files:
//   viewer.cpp      display, view state, side zones and the pointer;
//   navigation.cpp  the folder, loading, preloading (D-33) and watching for changes;
//   overlays.cpp    what is drawn over the image: information panel, top overlay (D-34),
//                   navigation buttons, the slideshow's toolbar;
//   commands.cpp    the command table, menus and dialogs (D-30);
//   files.cpp       file operations: show, copy, rename, trash and undo, delete (D-41);
//   playback.cpp    animation playback and the slideshow, its toolbar's behaviour (E6, E11, D-41);
//   gallery.cpp     the gallery (D-60);
//   input.cpp       the keyboard, the mouse and the wheel.
#pragma once

#include "cache.h"
#include "image.h"
#include "renderer.h"
#include "settings.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QImage>
#include <QHash>
#include <QKeySequence>
#include <QList>
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
// One step of Zoom In or Zoom Out, and of a wheel notch.
inline constexpr double kZoomStep = 1.25;

// A gallery thumbnail (D-60): sRGB, 8 bits, premultiplied; null when the file is unreadable. The
// file's size and time say when it was written again since.
struct Thumbnail {
    QImage image;
    qint64 fileSize = -1;
    QDateTime modified;
    qreal devicePixelRatio = 1.0; // the screen's it was decoded for: a sharper screen asks for it again
};

class QMenu;
class QMenuBar;
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
        Open, ClearRecent, ShowInFolder, OpenWithOther, CopyImage, CopyPath,
        Rename, MoveToTrash, DeletePermanently, UndoTrash, Settings, Quit,
        Previous, Next, First, Last,
        ZoomIn, ZoomOut, Fit, FitWidth, FitHeight, Fill, ActualSize, LockZoom,
        FullScreen, Minimize, Info, InfoOverlay, Checkerboard, Gallery,
        RotateClockwise, RotateCounterclockwise, FlipHorizontal, FlipVertical,
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
    void toggleFullScreen();
    void leaveFullScreen(); // back to maximized or normal, whichever it was
    void minimize();
    void returnFromMinimized(); // full screen or maximized again, as before minimize()
    void applySettings(const Settings &settings);
    // Sets one preference changed outside the Settings dialog (a toggle) and saves it into the
    // stored preferences rather than saving all of this instance's: another instance may have
    // saved others since this one started.
    void savePreference(const std::function<void(Settings &)> &change);
    void saveSession() const;

    QSizeF deviceSize() const;
    QSizeF displayedImageSize() const; // after view rotation
    double fitZoom(FitMode mode, QSizeF view = {}) const; // in `view` (default: the window's device size)
    double currentZoom() const;
    double shownZoom() const; // of the file's own pixels: a texture reduced for the GPU has fewer
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
    double sideZoneWidth() const;                // logical pixels; 0 without side zones
    bool inSideStrip(const QPointF &position) const; // over a side zone, even one with nothing to do
    Zone zoneAt(const QPointF &position) const;  // logical pixels
    QRectF zoneButtonRect(Zone zone) const;      // device pixels
    void setHoverZone(Zone zone);
    void cancelPress();   // input.cpp: a press whose release goes elsewhere clicks nothing
    void pointerActive(); // shows the pointer again and restarts its hiding timer
    void updateCursor();

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
    void step(int delta);
    bool hasNeighbour(int delta) const; // false at either end when navigation does not loop
    QStringList neighbourhood() const;  // current image, then the neighbours, direction of travel first
    bool needsDisplay(const QString &path, int limit) const; // not what the window shows now
    void scheduleWork();                // show from the cache, else decode; then preload
    void startDecode(const QString &path, int limit, qint64 maxPixels);
    void decodeFinished();
    // The zoom's detail (D-93, navigation.cpp): a vector image (SVG) enlarged on screen is drawn
    // again by the decode worker at the zoom shown, for the part of it in the window, and replaces
    // the enlarged image there; a change of the view asks for another once it settles.
    struct Detail {
        QString path;       // the image it is drawn from
        double scale = 0.0; // device pixels per pixel of the document
        QRectF area;        // pixels of the document drawn at `scale`, on the screen's pixel grid
        QRectF rect;        // the same part in the image's texture coordinates
        bool operator==(const Detail &) const = default;
    };
    std::optional<Detail> wantedDetail() const;
    void updateDetail(); // at every frame: the view may have changed
    void startDetail();
    void detailFinished();
    void dropDetail(); // another image, or the same one again
    void showImage(Image image, int limit);
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
    void restartSlideshowInterval(); // each new image is shown a full interval
    void stepFrame(int delta);
    void toggleSlideshow();
    void stopSlideshow();
    void slideshowTimeout();
    // The slideshow's toolbar (D-65): pause or play, and stop, below the top overlay.
    enum class ToolbarButton { None, Stop, PlayPause }; // left to right
    QRectF slideshowToolbarRect() const;                         // device pixels; empty when hidden
    ToolbarButton toolbarButtonAt(const QPointF &position) const; // logical pixels
    void showSlideshowToolbar(); // when the pointer moves; it hides again when it stays still
    void updateSlideshowToolbar();
    void setToolbarHover(ToolbarButton button);
    void toolbarClicked(ToolbarButton button);

    // overlays.cpp
    void updateOverlay();               // information panel (and the top overlay's content)
    void updateTopOverlay();
    void updateMessage();               // m_error, in the middle of the window (D-63)
    // The gallery (gallery.cpp, D-60). Slots count from the first thumbnail shown.
    bool galleryShown() const;
    double galleryBottomMargin() const;     // logical pixels below the gallery's bar
    double galleryReserve() const;          // logical pixels the gallery takes, from the window's bottom
    int galleryCapacity() const;            // thumbnails that fit the window's width
    double galleryThumbnail() const;        // logical pixels, the side of a thumbnail
    int galleryZoom() const;                // logical pixels added to the thumbnail under the pointer
    double galleryOverhang() const;         // how far its tile reaches beyond the bar, above and below
    int galleryFile(int slot) const;        // the index in m_files shown there; -1: none
    void updateGallery();
    void startThumbnails();                 // from m_thumbnailQueue, while the pool has a thread free
    void decodeThumbnail(const QString &path);
    void thumbnailReady(const QString &path, const Thumbnail &thumbnail);
    void forgetChangedThumbnails(const QHash<QString, FileStamp> &stamps); // after a listing: other or rewritten files
    int galleryIndexAt(const QPointF &position) const; // logical pixels; -1 off the thumbnails
    bool galleryContains(const QPointF &position) const;
    void setGalleryHover(int slot);
    void galleryClicked(int slot);
    void scrollGallery(int steps);
    void toggleGallery();
    // The top overlay's facts for `fields`, in its order and wording; also the title's (D-58).
    QStringList overlayParts(const QList<OverlayField> &fields, int *nameIndex = nullptr) const;
    void updateTitle(); // per Settings::titleMode
    OverlayVisibility topOverlayMode() const; // for full screen or window, whichever applies
    bool topOverlayVisible() const;
    void setPointerAtTop(bool atTop);
    double topActivationHeight() const; // logical pixels
    void placeOverlays(Renderer::Frame *frame) const;
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
    void addCommand(QMenu *menu, Command command);
    void showContextMenu(const QPoint &globalPos);
    void installApplicationMenu(); // macOS: Settings in the application menu (D-96)
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
    void startCopy(const QString &path);
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
    QHash<QString, Thumbnail> m_thumbnails; // by path
    QSet<QString> m_thumbnailsPending; // being decoded
    QStringList m_thumbnailQueue;      // to decode next, most wanted first (updateGallery)
    int m_thumbnailGeneration = 0; // changes when the folder does: thumbnails decoded before are dropped
    QList<QRectF> m_gallerySlots; // logical pixels in the gallery's texture
    QPainterPath m_galleryShape;  // likewise: its background, where the pointer is over the gallery
    QRectF m_galleryRect;         // device pixels
    QSize m_gallerySize;          // device pixels; empty: hidden
    QString m_galleryKey;
    int m_galleryFirst = 0;       // the file shown in the first slot
    int m_galleryHover = -1;      // slot under the pointer
    int m_galleryCentredOn = -2;  // the file the gallery last centred
    int m_galleryWheel = 0;       // wheel angle not yet turned into a step (1/8 degrees)
    QThreadPool m_thumbnailPool;  // two threads; waited for in ~ViewerWindow()
    QFutureWatcher<FrameResult> m_frameWatcher;
    bool m_frameBusy = false;   // as m_decodeBusy, for m_frameWatcher
    int m_frameGeneration = 0;  // changes when the animation shown changes
    int m_frameIndex = 0;       // the frame on screen
    int m_frameDurationMs = 0;  // ...and how long it stays
    QElapsedTimer m_panelRefreshed; // while playing, the panel's frame number follows at intervals
    int m_wantedFrame = -1;     // the frame to show next; -1: none
    std::optional<Animation::Frame> m_readyFrame; // decoded, waiting for its turn
    bool m_frameDue = false;    // its turn has come: show it as soon as it is decoded
    bool m_animationPaused = false;
    bool m_slideshowPaused = false;
    bool m_slideshowLeavesFullScreen = false; // it entered full screen (Settings::slideshowFullScreen)
    int m_loopsDone = 0;
    bool m_frameWaitsForScreen = false; // nothing is drawn while the window is off the screen
    QTimer m_frameTimer;
    bool m_slideshow = false;
    QTimer m_slideshowTimer;
    bool m_slideshowWaitsForScreen = false; // its timer stopped while the window was off the screen
    QTimer m_toolbarTimer;        // hides the slideshow's toolbar (Settings::slideshowToolbarAutoHide)
    bool m_toolbarHidden = false;
    ToolbarButton m_toolbarHover = ToolbarButton::None;
    ToolbarButton m_toolbarPress = ToolbarButton::None; // a press that becomes a click when released on it
    QSize m_toolbarSize;          // device pixels; empty when not shown
    QString m_toolbarKey;         // what its texture shows
    // Set from startDecode() until decodeFinished(). Not m_watcher.isRunning(): that is false
    // as soon as the worker ends, while the finished signal is still queued, and setting a
    // new future then would discard the result that was never delivered.
    bool m_decodeBusy = false;
    int m_jobLimit = 0;                   // texture limit of the running decode
    int m_jobGeneration = 0;              // ...and the m_decodeGeneration it started in
    QString m_jobPath;                    // ...its file
    // ...set when its image is no longer wanted: a decode in a worker process ends at once (D-93)
    std::shared_ptr<std::atomic_bool> m_jobCancel;
    QThreadPool m_detailPool; // the zoom's detail, beside the decodes
    QFutureWatcher<Image> m_detailWatcher;
    std::unique_ptr<QMenuBar> m_menuBar; // macOS: the application's menu bar (installApplicationMenu())
    QTimer m_detailTimer; // a view that has stopped changing gets its detail
    bool m_detailBusy = false;
    std::shared_ptr<std::atomic_bool> m_detailCancel;
    Detail m_detailJob;                    // what the running job draws
    Detail m_detail;                       // what the renderer has; no path: none
    std::optional<Detail> m_detailWanted; // what the view last asked for
    Detail m_detailFailed;                 // not asked for again while the view stays
    int m_decodeGeneration = 0;           // changes when decoded descriptions become outdated (language)
    QFutureWatcher<QImage> m_copyWatcher; // full-resolution decode for the clipboard
    bool m_copyBusy = false;              // as m_decodeBusy, for m_copyWatcher
    QString m_copyPath;                   // being copied
    QString m_copyNext;                   // asked for while another copy ran; empty: none
    ImageCache m_cache;
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
    double m_zoom = 1.0; // device pixels per image pixel when not fitting
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
    bool m_pointerAtTop = false; // on-hover mode: the overlay is shown
    QTimer m_topOverlayTimer;    // hides it after the pointer has left the top band
    QRect m_normalGeometry;  // last geometry while neither maximized nor full screen
    bool m_maximizedBeforeFullScreen = false;
    Qt::WindowStates m_statesBeforeMinimize; // full screen or maximized: given back by returnFromMinimized()
    std::function<void()> m_menuChoice; // what the context menu chose: run once the menu is gone

    bool m_dragging = false;
    QPointF m_dragOrigin;
    QPointF m_panOrigin;
    Zone m_hoverZone = Zone::None;
    QTimer m_pointerTimer;       // hides a still pointer (Settings::pointerHideMs)
    bool m_pointerHidden = false;
    Zone m_pressZone = Zone::None; // a press that may become a click on a side zone
    bool m_pressMoved = false;     // the press turned into a drag
};
