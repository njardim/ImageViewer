// User preferences and session state, persisted with QSettings (decision D-29), the
// Settings dialog, and the user-interface languages (decision D-27).
#pragma once

#include "folder.h"
#include "renderer.h"

#include <QColor>
#include <QDialog>
#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QListWidget;
class QSpinBox;
class QToolButton;

// When the top information overlay (E14, decision D-34) is shown.
enum class OverlayVisibility { Always, Hover, Hidden };

// The facts the top overlay can show, in the order the user chose.
enum class OverlayField { Name, Dimensions, FileSize, Zoom, ColorSpace, Modified, Position, Output };

// Preferences the user sets (Settings dialog and a few toggles that persist).
struct Settings {
    // General
    QString language;          // empty: follow the system; otherwise a code from uiLanguages()
    bool confirmTrash = true;  // ask before moving a file to the trash
    bool reopenLastImage = false;
    // Window
    QColor background = QColor(0x21, 0x21, 0x21); // sRGB; shown at SDR white
    bool checkerboard = false; // checks behind transparent pixels instead of the plain background
    bool rememberGeometry = true;
    // Information
    bool showInfo = true;      // the information panel (I)
    OverlayVisibility overlayFullScreen = OverlayVisibility::Hover;
    OverlayVisibility overlayWindow = OverlayVisibility::Hidden;
    QList<OverlayField> overlayFields = defaultOverlayFields(); // shown, in this order
    int overlayBackgroundOpacity = 60; // percent
    int overlayTextOpacity = 100;      // percent
    bool overlayOutline = false;       // dark outline around the text
    int overlayHideDelayMs = 1500;     // on-hover mode: hidden this long after the pointer leaves
    // Navigation
    bool loop = true;          // wrap from the last image to the first and back
    bool sideZones = true;     // click the left/right edge of the window for previous/next
    int sideZoneWidth = 200;   // logical pixels
    FolderSort sortBy = FolderSort::Name;
    bool sortDescending = false;
    bool preload = true;       // decode the next and previous images in advance (D-33)
    // Color & HDR
    bool toneMap = true;       // BT.2390 tone mapping on at startup
    Renderer::OutputPreference output = Renderer::OutputPreference::Automatic;

    static constexpr int kMinSideZoneWidth = 80;
    static constexpr int kMaxSideZoneWidth = 400;
    static constexpr int kMinOverlayTextOpacity = 20; // never invisible
    static constexpr int kMinOverlayHideDelayMs = 300;
    static constexpr int kMaxOverlayHideDelayMs = 10000;

    // The six fields of ImageGlass issue #2475 (E14).
    static QList<OverlayField> defaultOverlayFields();
    static const QList<OverlayField> &allOverlayFields();

    static Settings load(); // invalid or out-of-range stored values fall back to the defaults
    void save() const;
};

// Where the window was and what was open when the application last closed.
struct SessionState {
    QRect geometry;         // normal (not maximized) geometry, without the frame
    bool maximized = false;
    bool fullScreen = false;
    QString lastFile;
    QString lastDirectory;  // for the Open dialog

    static SessionState load();
    void save() const;
};

// Recently opened files, most recent first (Open Recent menu). Saved at once, not at exit.
constexpr int kMaxRecentFiles = 10;
QStringList loadRecentFiles();
void saveRecentFiles(const QStringList &files);

// User-interface languages: English (the source language) and the translations that
// ship in the executable, with their names written in the language itself (D-27, D-32).
struct UiLanguage {
    QString code;       // Qt locale name, e.g. "pt" or "zh_CN"
    QString nativeName;
};
const QList<UiLanguage> &uiLanguages();

// Loads the translations for `code` (empty: the system's preferred languages), also Qt's
// own where they are installed, and sets the layout direction (right to left for Arabic
// and Urdu). Returns the language actually in use ("en" when nothing matched).
QString applyLanguage(const QString &code);

// The translated name of an overlay field (also used by the viewer's tooltips).
QString overlayFieldName(OverlayField field);

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const Settings &settings);
    Settings settings() const;

private:
    void setValues(const Settings &settings);
    void setBackground(const QColor &color);
    void chooseCustomBackground();
    void moveOverlayField(int delta);

    Settings m_initial; // fields the dialog does not show are kept as they were

    QComboBox *m_language = nullptr;
    QCheckBox *m_confirmTrash = nullptr;
    QCheckBox *m_reopenLast = nullptr;
    QButtonGroup *m_backgroundGroup = nullptr;
    QToolButton *m_customBackground = nullptr;
    QColor m_background;
    QCheckBox *m_checkerboard = nullptr;
    QCheckBox *m_rememberGeometry = nullptr;
    QCheckBox *m_showInfo = nullptr;
    QComboBox *m_overlayFullScreen = nullptr;
    QComboBox *m_overlayWindow = nullptr;
    QListWidget *m_overlayFields = nullptr;
    QSpinBox *m_overlayBackground = nullptr;
    QSpinBox *m_overlayText = nullptr;
    QCheckBox *m_overlayOutline = nullptr;
    QDoubleSpinBox *m_overlayDelay = nullptr;
    QCheckBox *m_loop = nullptr;
    QCheckBox *m_sideZones = nullptr;
    QSpinBox *m_sideZoneWidth = nullptr;
    QComboBox *m_sortBy = nullptr;
    QCheckBox *m_sortDescending = nullptr;
    QCheckBox *m_preload = nullptr;
    QCheckBox *m_toneMap = nullptr;
    QComboBox *m_output = nullptr;
};
