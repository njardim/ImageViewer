// The main window: a QWindow presented through QRhi so it can use an HDR
// swapchain (decision D-09). ViewerWindow is implemented in six files:
//   viewer.cpp      display, view state and input;
//   navigation.cpp  the folder, loading, preloading (D-33) and watching for changes;
//   overlays.cpp    information panel, top overlay (D-34) and navigation buttons;
//   commands.cpp    the command table, menus and dialogs (D-30);
//   files.cpp       file operations: show, copy, rename, trash and undo, delete (D-41);
//   playback.cpp    animation playback and the slideshow (E6, E11, D-41).
#pragma once

#include "cache.h"
#include "image.h"
#include "renderer.h"
#include "settings.h"

#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QImage>
#include <QKeySequence>
#include <QList>
#include <QPointF>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QWindow>

#include <functional>
#include <optional>

class QMenu;
class QVulkanInstance;
class QWidget;

// Dialogs and menus are widgets; the viewer is a QWindow. Parenting their native window
// keeps them above it, centred on it and, on Wayland, positioned at all.
void makeTransient(QWidget &widget, QWindow *parent);

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
        Open, ClearRecent, ShowInFolder, CopyImage, CopyPath,
        Rename, MoveToTrash, DeletePermanently, UndoTrash, Settings, Quit,
        Previous, Next, First, Last,
        ZoomIn, ZoomOut, Fit, ActualSize, FullScreen, Info, InfoOverlay, Checkerboard,
        RotateClockwise, RotateCounterclockwise, FlipHorizontal, FlipVertical,
        ExposureUp, ExposureDown, ExposureReset, ToneMap, ClipWarning,
        PlayPause, PreviousFrame, NextFrame, Slideshow,
        About, AboutQt,
    };

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
    void applySettings(const Settings &settings);
    // Sets one preference changed outside the Settings dialog (a toggle) and saves it into the
    // stored preferences rather than saving all of this instance's: another instance may have
    // saved others since this one started.
    void savePreference(const std::function<void(Settings &)> &change);
    void saveSession() const;

    QSizeF deviceSize() const;
    QSizeF displayedImageSize() const; // after view rotation
    double fitZoom() const;
    double currentZoom() const;
    QRectF imageRect() const;
    void zoomAt(double factor, const QPointF &devicePos);
    void setActualSize();
    void setFit();
    void clampPan();
    Renderer::Frame imageFrame() const; // colour-related fields of the current frame
    int textureLimit(const QString &path) const; // longest side the GPU texture may have
    void recoverFromDeviceLoss();
    double sideZoneWidth() const;                // logical pixels; 0 without side zones
    bool inSideStrip(const QPointF &position) const; // over a side zone, even one with nothing to do
    Zone zoneAt(const QPointF &position) const;  // logical pixels
    QRectF zoneButtonRect(Zone zone) const;      // device pixels
    void setHoverZone(Zone zone);

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
    void showImage(Image image, int limit);
    void reloadCurrent();               // decode the shown file again, keeping the view
    void setFolder(const QString &folder);
    void relist();                      // folder sorted per the settings, keeping the current file
    void refreshFolder();               // after a change on disk
    void removeCurrentFromList();       // the file is gone: the next one takes its place
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
    void startAnimation();
    void stopAnimation();
    void requestFrame(int index);
    void frameDecoded();
    void frameTimeout();
    void presentFrame(const Animation::Frame &frame);
    void togglePause();
    void stepFrame(int delta);
    void toggleSlideshow();
    void stopSlideshow();
    void slideshowTimeout();

    // overlays.cpp
    void updateOverlay();               // information panel (and the top overlay's content)
    void updateTopOverlay();
    OverlayVisibility topOverlayMode() const; // for full screen or window, whichever applies
    bool topOverlayVisible() const;
    void setPointerAtTop(bool atTop);
    double topActivationHeight() const; // logical pixels
    void placeOverlays(Renderer::Frame *frame) const;
    void updateNavigationButtons();
    void showNotice(const QString &text); // brief message in the information panel

    // commands.cpp
    struct CommandInfo {
        Command command;
        QList<QKeySequence> shortcuts;
        bool repeats; // acts again while the key is held (navigation, zoom); toggles and file actions do not
    };
    static const QList<CommandInfo> &commands();
    QString commandText(Command command) const;
    bool isCommandEnabled(Command command) const;
    bool isCommandChecked(Command command, bool *checkable) const;
    void execute(Command command);
    bool executeShortcut(QKeyEvent *e); // true when a command took the key
    void addCommand(QMenu *menu, Command command);
    void showContextMenu(const QPoint &globalPos);
    void showOpenDialog();
    void toggleTopOverlay();
    void toggleCheckerboard();
    void showSettings();
    void showAbout();
    bool currentFileIsShown() const; // the displayed image is the current entry of the folder

    // files.cpp
    void showInFolder();
    void copyImage();
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
    int m_frameDurationMs = 0;  // ...and how long it stays
    int m_wantedFrame = -1;     // the frame to show next; -1: none
    std::optional<Animation::Frame> m_readyFrame; // decoded, waiting for its turn
    bool m_frameDue = false;    // its turn has come: show it as soon as it is decoded
    bool m_animationPaused = false;
    int m_loopsDone = 0;
    bool m_waitingForExpose = false; // nothing is drawn while the window is hidden
    QTimer m_frameTimer;
    bool m_slideshow = false;
    QTimer m_slideshowTimer;
    // Set from startDecode() until decodeFinished(). Not m_watcher.isRunning(): that is false
    // as soon as the worker ends, while the finished signal is still queued, and setting a
    // new future then would discard the result that was never delivered.
    bool m_decodeBusy = false;
    int m_jobLimit = 0;                   // texture limit of the running decode
    int m_jobGeneration = 0;              // ...and the m_decodeGeneration it started in
    int m_decodeGeneration = 0;           // changes when decoded descriptions become outdated (language)
    QFutureWatcher<QImage> m_copyWatcher; // full-resolution decode for the clipboard
    bool m_copyBusy = false;              // as m_decodeBusy, for m_copyWatcher
    QString m_copyPath;
    ImageCache m_cache;
    Image m_image; // metadata of the displayed image (pixels live on the GPU and in the cache)
    int m_shownLimit = 0;     // texture limit m_image was decoded at
    bool m_imageStale = false; // the shown image must be decoded or uploaded again
    QString m_folder;
    QFileSystemWatcher m_folderWatcher; // the folder and the shown file
    QTimer m_folderTimer;               // changes come in bursts: re-list once they settle
    struct TrashedFile {
        QString original;
        QString inTrash;
        qint64 size = -1;   // what was moved, so Undo never restores another file that
        QDateTime modified; // later took the same name in the trash
    };
    QList<TrashedFile> m_trashed; // for Undo, most recent last
    QString m_message; // loading status or error; cleared when an image arrives
    QString m_loadingMessage; // the "Loading…" text in m_message, while it is there
    QString m_notice;  // confirmation of a command; disappears after a few seconds
    QTimer m_noticeTimer;

    bool m_fit = true;
    double m_zoom = 1.0; // device pixels per image pixel when not fitting
    QPointF m_pan;       // offset of the image centre from the window centre, device pixels
    int m_quarterTurns = 0;
    bool m_mirrored = false;
    float m_exposureEv = 0.0f;
    bool m_clipWarning = false;
    bool m_toneMap = true;
    bool m_showInfo = true;
    QSize m_overlaySize; // device pixels; empty when no overlay is shown
    QString m_overlayOutput; // output description the overlay was built with
    QSize m_topOverlaySize;  // device pixels; empty when there is nothing to show
    QString m_topOverlayKey; // what the top overlay texture shows, to skip identical uploads
    bool m_pointerAtTop = false; // on-hover mode: the overlay is shown
    // What Shift+I turns the overlay back on to, in a window and in full screen.
    OverlayVisibility m_overlayRestore[2] = {OverlayVisibility::Always, OverlayVisibility::Hover};
    QTimer m_topOverlayTimer;    // hides it after the pointer has left the top band
    QRect m_normalGeometry;  // last geometry while neither maximized nor full screen
    bool m_maximizedBeforeFullScreen = false;

    bool m_dragging = false;
    QPointF m_dragOrigin;
    QPointF m_panOrigin;
    Zone m_hoverZone = Zone::None;
    Zone m_pressZone = Zone::None; // a press that may become a click on a side zone
    bool m_pressMoved = false;     // the press turned into a drag
};
