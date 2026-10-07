// The main window: a QWindow presented through QRhi so it can use an HDR
// swapchain (decision D-09). Owns navigation, view state, input and overlays.
// viewer.cpp: display, view state and input; commands.cpp: the command table, menus,
// file operations and dialogs (decision D-30).
#pragma once

#include "image.h"
#include "renderer.h"
#include "settings.h"

#include <QFutureWatcher>
#include <QImage>
#include <QKeySequence>
#include <QList>
#include <QPointF>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QWindow>

class QMenu;
class QVulkanInstance;

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
        Open, ShowInFolder, CopyImage, CopyPath, MoveToTrash, Settings, Quit,
        Previous, Next, First, Last,
        ZoomIn, ZoomOut, Fit, ActualSize, FullScreen, Info,
        RotateClockwise, RotateCounterclockwise, FlipHorizontal, FlipVertical,
        ExposureUp, ExposureDown, ExposureReset, ToneMap, ClipWarning,
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
    void startLoading(int index);
    void imageDecoded();
    void step(int delta);
    bool hasNeighbour(int delta) const; // false at either end when navigation does not loop
    void toggleFullScreen();
    void applySettings(const Settings &settings);
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
    void updateOverlay();
    void updateNavigationButtons();
    void showNotice(const QString &text); // brief message in the information panel
    Renderer::Frame imageFrame() const; // colour-related fields of the current frame
    int textureLimit(const QString &path) const; // longest side the GPU texture may have
    void recoverFromDeviceLoss();
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
    void showInFolder();
    void copyImage();
    void imageCopied();
    void copyPath();
    void moveToTrash();
    void showSettings();
    void showAbout();
    bool currentFileIsShown() const; // the displayed image is the current entry, nothing loading

    Renderer m_renderer;
    bool m_rendererReady = false;
    bool m_rendererFailed = false;
    Settings m_settings;
    QString m_lastDirectory;

    QStringList m_files;
    int m_index = -1;
    int m_pendingIndex = -1; // requested while a decode was running; starts when it ends
    QString m_textureCapPath; // the file whose upload the GPU refused...
    int m_textureCap = 0;     // ...and the size that file is decoded at now
    // Decodes run on their own thread: decodeImage() itself fans out on the global pool
    // with blockingMap, which deadlocks if the decode occupies the pool's only thread.
    QThreadPool m_decodePool;
    QFutureWatcher<Image> m_watcher;
    QFutureWatcher<QImage> m_copyWatcher; // full-resolution decode for the clipboard
    QString m_copyPath;
    Image m_image; // metadata of the displayed image (pixels live on the GPU)
    QString m_message; // loading status or error; cleared when an image arrives
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
    QRect m_normalGeometry;  // last geometry while neither maximized nor full screen

    bool m_dragging = false;
    QPointF m_dragOrigin;
    QPointF m_panOrigin;
    Zone m_hoverZone = Zone::None;
    Zone m_pressZone = Zone::None; // a press that may become a click on a side zone
    bool m_pressMoved = false;     // the press turned into a drag
};
