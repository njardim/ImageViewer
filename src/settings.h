// User preferences and session state, persisted with QSettings (decision D-29), the
// Settings dialog, and the user-interface languages (decision D-27).
#pragma once

#include "folder.h"
#include "renderer.h"

#include <functional>

#include <QColor>
#include <QDialog>
#include <QKeySequence>
#include <QList>
#include <QMap>
#include <QRect>
#include <QString>
#include <QStringList>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QKeySequenceEdit;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QToolButton;
class QTreeWidget;

// When the top information overlay (E14, decision D-34) is shown.
enum class OverlayVisibility { Always, Hover, Hidden };

// The facts the top overlay can show, in the order the user chose.
enum class OverlayField { Name, Dimensions, FileSize, Zoom, ColorSpace, Modified, Position, Output };

// How a newly shown image is zoomed (D-51): to fit the window, its width or its height, or to
// fill it. Images smaller than that stay at 100 % unless Settings::enlargeSmallImages.
enum class FitMode { Window, Width, Height, Fill };
// When the window takes the size of the image (D-51).
enum class WindowFit { Never, FirstImage, EveryImage };
// What the title bar shows (D-51), from the least to the most.
enum class TitleMode { Application, Name, Details, Everything };

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
    FitMode fitMode = FitMode::Window;
    bool enlargeSmallImages = false;
    bool lockZoom = false;           // new images keep the zoom (a toggle, L)
    WindowFit windowFit = WindowFit::Never;
    int windowFitPercent = 80;       // largest share of the screen the window takes for an image
    TitleMode titleMode = TitleMode::Name;
    // Information
    bool showInfo = true;      // the information panel (I)
    OverlayVisibility overlayFullScreen = OverlayVisibility::Hover;
    OverlayVisibility overlayWindow = OverlayVisibility::Hidden;
    QList<OverlayField> overlayFields = defaultOverlayFields(); // shown, in this order
    // The look of both information panels (D-49).
    int overlayBackgroundOpacity = 70; // percent
    int overlayTextOpacity = 100;      // percent
    bool overlayOutline = true;        // dark outline around the text
    int overlayHideDelayMs = 1500;     // on-hover mode: hidden this long after the pointer leaves
    // Navigation
    bool loop = true;          // wrap from the last image to the first and back
    bool sideZones = true;     // click the left/right edge of the window for previous/next
    int sideZoneWidth = 100;   // logical pixels (200 until 0.2, D-36)
    FolderSort sortBy = FolderSort::Name;
    bool sortDescending = false;
    bool preload = true;       // decode the next and previous images in advance (D-33)
    int slideshowSeconds = 5;  // between images in the slideshow (E11)
    // Color & HDR
    bool toneMap = true;       // BT.2390 tone mapping on at startup
    Renderer::OutputPreference output = Renderer::OutputPreference::Automatic;
    // Shortcuts (D-52): the user's own, per command key; a command not listed has its defaults.
    QMap<QString, QList<QKeySequence>> shortcuts;

    static constexpr int kMinWindowFitPercent = 20;
    static constexpr int kMaxWindowFitPercent = 100;
    static constexpr int kMinSideZoneWidth = 80;
    static constexpr int kMaxSideZoneWidth = 400;
    static constexpr int kMinSlideshowSeconds = 1;
    static constexpr int kMaxSlideshowSeconds = 3600;
    static constexpr int kMinOverlayTextOpacity = 20; // never invisible
    // Both information panels (D-49): values and labels in these greys over black at
    // overlayBackgroundOpacity; at the default 70 % the labels keep 4.5:1 over a white image.
    static constexpr int kPanelValueGrey = 240;
    static constexpr int kPanelLabelGrey = 190;
    QColor panelBackground() const;
    QColor panelText(int grey) const;
    static constexpr int kMinOverlayHideDelayMs = 300;
    static constexpr int kMaxOverlayHideDelayMs = 10000;

    // The six fields of ImageGlass issue #2475 (E14).
    static QList<OverlayField> defaultOverlayFields();
    static const QList<OverlayField> &allOverlayFields();

    static Settings load(); // invalid or out-of-range stored values fall back to the defaults
    void save() const;
    // Whether the stored preferences predate this version's form: load() migrates them, and
    // saving them once at startup makes that permanent (they are not saved at exit).
    static bool storedIsOutdated();

    bool operator==(const Settings &) const = default;
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

// A key as shortcuts compare it, pressed or recorded: Shift is part of a printed symbol ("!" is
// Shift+1 and "+" Shift+= on a US keyboard), so it is ignored for every character key but
// letters and Space. The viewer's matching and the Settings' conflicts use the same rule.
QKeyCombination comparableKey(QKeyCombination combination);
bool sameShortcut(const QKeySequence &a, const QKeySequence &b);

// A command as the Shortcuts tab lists it: its stored key, its name in the interface language
// and its default shortcuts (the viewer's command table, D-30).
struct ShortcutCommand {
    QString key;
    QString name;
    QList<QKeySequence> defaults;
};

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    // `commands` lists the commands in the current language (asked again after a language change).
    SettingsDialog(const Settings &settings, std::function<QList<ShortcutCommand>()> commands);
    Settings settings() const;

Q_SIGNALS:
    // Apply, or OK with changes not applied yet: the viewer takes the values at once.
    void applied(const Settings &settings);

protected:
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void rebuildUi(); // in the new language, keeping what the dialog shows
    void apply();
    void updateApplyButton();
    void setValues(const Settings &settings);
    void setBackground(const QColor &color);
    void chooseCustomBackground();
    void moveOverlayField(int delta);
    void fillShortcutTable();
    void refreshShortcutTexts();     // the table's shortcuts and the labels that name keys
    void showShortcutsOf(int row);   // in the two edit fields
    void setShortcuts(int row, QList<QKeySequence> shortcuts); // taken away from any other command

    Settings m_initial; // fields the dialog does not show are kept as they were
    Settings m_applied; // what the viewer uses now; Apply is enabled while the dialog differs
    bool m_rebuildQueued = false;

    std::function<QList<ShortcutCommand>()> m_commandsSource;
    QList<ShortcutCommand> m_commands;
    QMap<QString, QList<QKeySequence>> m_shortcuts; // what the Shortcuts tab shows, for every command
    QTreeWidget *m_shortcutTable = nullptr;
    QKeySequenceEdit *m_shortcutEdit = nullptr;
    QKeySequenceEdit *m_alternativeEdit = nullptr;
    QPushButton *m_shortcutDefault = nullptr;
    QLabel *m_shortcutNote = nullptr;
    bool m_keyInField = false; // a key press in a shortcut field is being handled
    QGroupBox *m_overlayBox = nullptr;  // its title names the command's key
    QLabel *m_slideshowLabel = nullptr; // likewise

    QTabWidget *m_tabs = nullptr;
    QPushButton *m_apply = nullptr;

    QComboBox *m_language = nullptr;
    QCheckBox *m_confirmTrash = nullptr;
    QCheckBox *m_reopenLast = nullptr;
    QButtonGroup *m_backgroundGroup = nullptr;
    QToolButton *m_customBackground = nullptr;
    QColor m_background;
    QCheckBox *m_checkerboard = nullptr;
    QCheckBox *m_rememberGeometry = nullptr;
    QComboBox *m_fitMode = nullptr;
    QCheckBox *m_enlargeSmall = nullptr;
    QCheckBox *m_lockZoom = nullptr;
    QComboBox *m_windowFit = nullptr;
    QSpinBox *m_windowFitPercent = nullptr;
    QComboBox *m_titleMode = nullptr;
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
    QSpinBox *m_slideshowSeconds = nullptr;
    QCheckBox *m_toneMap = nullptr;
    QComboBox *m_output = nullptr;
};
