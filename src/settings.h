// User preferences and session state, persisted with QSettings (decision D-29), the
// Settings dialog, and the user-interface languages (decision D-27).
#pragma once

#include "folder.h"
#include "glyphs.h"
#include "renderer.h"

#include <array>
#include <functional>
#include <optional>

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
class QListWidgetItem;
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
enum class FitMode { Window, Width, Height, Fill, Actual }; // Actual: 100 %
// When the window takes the size of the image (D-51).
enum class WindowFit { Never, FirstImage, EveryImage };
// What the title bar shows (D-51, D-58): the top overlay's fields, or a fixed set in its style.
enum class TitleMode { Overlay, Name, Details, Everything };
// The interface's text (D-85): the system's size, 2 or 4 pixels larger.
enum class FontSize { Small, Medium, Large };

// Preferences the user sets (Settings dialog and a few toggles that persist).
struct Settings {
    // General
    QString language;          // empty: follow the system; otherwise a code from uiLanguages()
    FontSize fontSize = FontSize::Small; // dialogs, menus and the panels over the image
    bool confirmTrash = true;  // ask before moving a file to the trash
    bool reopenLastImage = false;
    bool welcomeScreen = false; // the logo for 2 s before the window opens (the owner, D-113)
    // How long the pointer stays still before what hides by itself hides: the bars, the overlay at
    // the top, the information panel and the pointer, each by its own switch (the owner, D-113).
    int autoHideMs = 2000;
    // sRGB, the interface's accent over the image: behind the gallery's current and hovered
    // thumbnails (D-65) and a toolbar's switch that is on (D-112). Emerald.
    QColor highlight = QColor(80, 200, 120);
    // Window
    QColor background = QColor(0x21, 0x21, 0x21); // sRGB; shown at SDR white
    bool checkerboard = false; // checks behind transparent pixels instead of the plain background
    bool rememberGeometry = true;
    FitMode fitMode = FitMode::Window; // the zoom mode chosen last: new images open in it (D-65)
    bool enlargeSmallImages = false;
    bool lockZoom = false;           // new images keep the zoom, free or fitted (a toggle, Z)
    WindowFit windowFit = WindowFit::Never;
    int windowFitPercent = 80;       // largest share of the screen the window takes for an image
    TitleMode titleMode = TitleMode::Overlay; // the owner's default (2026-11-18)
    bool pointerAutoHide = true;     // the pointer hides while it is still (autoHideMs)
    // Information
    bool showInfo = false;     // the information panel (P)
    bool showOverlay = true;   // the top overlay (I), where its mode below is not "never"
    OverlayVisibility overlayVisibility = OverlayVisibility::Always; // in a window and in full screen: always or at the top (D-113)
    bool overlayAutoHide = false;    // the top overlay hides while the pointer is still
    bool panelAutoHide = false;      // the information panel likewise (its notices stay)
    QList<OverlayField> overlayFields = defaultOverlayFields(); // shown, in this order
    // The look of both information panels (D-49).
    int overlayBackgroundOpacity = 70; // percent
    int overlayTextOpacity = 100;      // percent
    bool overlayOutline = false;       // dark outline around the text, for HDR highlights (D-58)
    // Navigation
    bool loop = true;          // wrap from the last image to the first and back: keys, mouse, slideshow, gallery
    bool sideZones = true;     // click the left/right edge of the window for previous/next
    int sideZoneWidth = 100;   // logical pixels (200 until 0.2, D-36)
    FolderSort sortBy = FolderSort::Name;
    bool sortDescending = false;
    bool preload = true;       // decode the next and previous images in advance (D-33)
    double slideshowSeconds = 4.0; // between images in the slideshow (E11)
    bool slideshowFullScreen = false; // the slideshow starts in full screen (and leaves it when it ends)
    bool slideshowToolbarAutoHide = false; // its toolbar hides while the pointer is still (D-65); the user opts in
    bool galleryAutoHide = false;     // likewise the gallery (D-112)
    bool showGallery = true;          // the thumbnails at the bottom (G, D-60)
    int galleryThumbnailSize = 96;    // logical pixels, one of kGalleryThumbnailSizes (D-63)
    bool galleryZoom = false;         // the thumbnail under the pointer enlarges
    int galleryOpacity = 70;          // percent, the gallery's background
    int galleryEnlargement = 50;      // logical pixels added to the thumbnail under the pointer
    // Toolbar (D-87, D-112): off by default, the screen is not meant to be full of controls.
    bool showToolbar = false;         // a toggle, T
    bool toolbarNames = false;        // each button's name under it
    bool toolbarAutoHide = false;     // it hides while the pointer is still; the user opts in
    QStringList toolbarButtons = defaultToolbarButtons(); // command keys (D-52), kToolbarSeparator between groups
    // Color & HDR
    bool toneMap = true;       // BT.2390 tone mapping (a toggle, T)
    bool clipWarning = false;  // clipped or tone-mapped pixels shown in magenta (a toggle, C)
    Renderer::OutputPreference output = Renderer::OutputPreference::Automatic;
    // Shortcuts (D-52): the user's own, per command key; a command not listed has its defaults.
    QMap<QString, QList<QKeySequence>> shortcuts;

    // Every number in the Settings moves by one of two steps, so the whole interface responds the
    // same way: sizes (pixels and percentages) by 5, times by half a second.
    static constexpr int kSizeStep = 5;
    static constexpr int kTimeStepMs = 500;
    static constexpr int kMinWindowFitPercent = 20;
    static constexpr int kMaxWindowFitPercent = 100;
    static constexpr int kMinSideZoneWidth = 80;
    static constexpr int kMaxSideZoneWidth = 400;
    static constexpr double kMinSlideshowSeconds = 0.5;
    static constexpr double kMaxSlideshowSeconds = 3600.0;
    static constexpr int kMinGalleryEnlargement = 10;
    static constexpr int kMaxGalleryEnlargement = 100;
    static constexpr std::array<int, 3> kGalleryThumbnailSizes = {64, 96, 128}; // small, medium, large
    static constexpr int kMinOverlayTextOpacity = 20; // never invisible
    // Both information panels (D-49): values and labels in these greys over black at
    // overlayBackgroundOpacity; at the default 70 % the labels keep 4.5:1 over a white image.
    static constexpr int kPanelValueGrey = 240;
    static constexpr int kPanelLabelGrey = 190;
    QColor panelBackground() const;
    QColor panelText(int grey) const;
    static constexpr int kMinAutoHideMs = 500;
    static constexpr int kMaxAutoHideMs = 10000;

    // The toolbar's buttons as the owner designed them (2026-11-18): open; first, previous, the
    // slideshow, next, last; the rotations and flips; the checkerboard, the gallery, full screen;
    // settings.
    static constexpr QLatin1StringView kToolbarSeparator{"Separator"};
    static QStringList defaultToolbarButtons() // in the header, as defaultOverlayFields()
    {
        const QString separator(kToolbarSeparator);
        return {QStringLiteral("Open"), separator, QStringLiteral("First"), QStringLiteral("Previous"),
                QStringLiteral("Slideshow"), QStringLiteral("Next"), QStringLiteral("Last"), separator,
                QStringLiteral("RotateCounterclockwise"), QStringLiteral("RotateClockwise"),
                QStringLiteral("FlipHorizontal"), QStringLiteral("FlipVertical"), separator,
                QStringLiteral("Checkerboard"), QStringLiteral("Gallery"), QStringLiteral("FullScreen"), separator,
                QStringLiteral("Settings")};
    }
    // `buttons` as the toolbar keeps them: each command once, no separator first, last or twice in
    // a row, and Quit at the end after a separator of its own (D-87). Names are not checked
    // against the commands, which the window does.
    static QStringList normalizedToolbar(const QStringList &buttons);

    // The six fields of ImageGlass issue #2475 (E14).
    static QList<OverlayField> defaultOverlayFields() // in the header: a Settings needs nothing else to be made (the unit tests)
    {
        return {OverlayField::Name, OverlayField::Dimensions, OverlayField::FileSize,
                OverlayField::Zoom, OverlayField::ColorSpace, OverlayField::Modified};
    }
    static const QList<OverlayField> &allOverlayFields();

    static Settings load(); // invalid or out-of-range stored values fall back to the defaults
    void save() const;

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

// The Settings dialog's tab shown when it last closed; it opens there again. Saved at once.
int loadSettingsTab();
void saveSettingsTab(int tab);

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

// Text that reads left to right even in Arabic or Urdu, isolated (LRI … PDI) from the line around
// it: an extension, a key sequence ("Ctrl+," not ",Ctrl+") (shortcuts.cpp).
QString leftToRight(const QString &text);

// A command as the Shortcuts tab lists it: its stored key, its name in the interface language
// and its default shortcuts (the viewer's command table, D-30).
struct ShortcutCommand {
    QString key;
    QString name;
    QList<QKeySequence> defaults;
    std::optional<Glyph> glyph; // a button the toolbar can have (D-112), drawn so
    QString buttonName;         // ...and named so
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
    void done(int result) override; // OK, Cancel, Esc or the close button: remembers the tab shown
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void rebuildUi(); // in the new language, keeping what the dialog shows
    void apply();
    void updateApplyButton();
    void setValues(const Settings &settings);
    void fitOverlayFields();
    void fitToFont(); // after another font size: the overlay's fields all shown, room for the labels
    // A row of colour swatches and "Custom…" (the window's background, the interface's highlight).
    struct ColorChoice {
        QButtonGroup *group = nullptr; // owned by the dialog, not by a widget
        QToolButton *custom = nullptr;
        const QColor *presets = nullptr;
        int presetCount = 0;
        QString dialogTitle;
        QColor color;
    };
    QLayout *colorRow(ColorChoice &choice, const QColor *presets, const QString *names, int count,
                      const QString &dialogTitle);
    void setColor(ColorChoice &choice, const QColor &color);
    void moveOverlayField(int delta);
    // toolbarsettings.cpp: the Toolbar tab (D-112).
    QWidget *buildToolbarTab();
    QListWidgetItem *separatorItem() const;
    void fillToolbarButtons(const QStringList &buttons);
    QStringList toolbarButtonsShown() const; // the chosen buttons and separators, in order
    void moveToolbarButton(int delta);
    // shortcuts.cpp: the Shortcuts tab (D-52).
    QWidget *buildShortcutsTab();
    void fillShortcutTable();
    int currentCommand() const;      // the selected row's index in m_commands; -1: none
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
    QGroupBox *m_slideshowBox = nullptr; // their titles name the commands' keys
    QGroupBox *m_galleryBox = nullptr;
    QGroupBox *m_toolbarBox = nullptr;

    QTabWidget *m_tabs = nullptr;
    QPushButton *m_apply = nullptr;

    QComboBox *m_language = nullptr;
    QComboBox *m_fontSize = nullptr;
    QCheckBox *m_confirmTrash = nullptr;
    QCheckBox *m_reopenLast = nullptr;
    QCheckBox *m_welcomeScreen = nullptr;
    QDoubleSpinBox *m_autoHide = nullptr;
    ColorChoice m_highlight;
    ColorChoice m_background;
    QCheckBox *m_checkerboard = nullptr;
    QCheckBox *m_rememberGeometry = nullptr;
    QComboBox *m_fitMode = nullptr;
    QCheckBox *m_enlargeSmall = nullptr;
    QCheckBox *m_lockZoom = nullptr;
    QComboBox *m_windowFit = nullptr;
    QSpinBox *m_windowFitPercent = nullptr;
    QComboBox *m_titleMode = nullptr;
    QCheckBox *m_showInfo = nullptr;
    QCheckBox *m_showOverlay = nullptr;
    QComboBox *m_overlayVisibility = nullptr;
    QCheckBox *m_overlayAutoHide = nullptr;
    QCheckBox *m_panelAutoHide = nullptr;
    QListWidget *m_overlayFields = nullptr;
    QSpinBox *m_overlayBackground = nullptr;
    QSpinBox *m_overlayText = nullptr;
    QCheckBox *m_overlayOutline = nullptr;
    QCheckBox *m_pointerAutoHide = nullptr;
    QCheckBox *m_loop = nullptr;
    QCheckBox *m_sideZones = nullptr;
    QSpinBox *m_sideZoneWidth = nullptr;
    QComboBox *m_sortBy = nullptr;
    QCheckBox *m_sortDescending = nullptr;
    QCheckBox *m_preload = nullptr;
    QCheckBox *m_slideshowFullScreen = nullptr;
    QCheckBox *m_toolbarAutoHide = nullptr;
    QCheckBox *m_showGallery = nullptr;
    QComboBox *m_galleryThumbnailSize = nullptr;
    QCheckBox *m_galleryZoom = nullptr;
    QSpinBox *m_galleryOpacity = nullptr;
    QSpinBox *m_galleryEnlargement = nullptr;
    QCheckBox *m_galleryAutoHide = nullptr;
    QCheckBox *m_showToolbar = nullptr;
    QCheckBox *m_toolbarNames = nullptr;
    QCheckBox *m_mainToolbarAutoHide = nullptr;
    QListWidget *m_toolbarButtons = nullptr;
    QDoubleSpinBox *m_slideshowSeconds = nullptr;
    QCheckBox *m_toneMap = nullptr;
    QCheckBox *m_clipWarning = nullptr;
    QComboBox *m_output = nullptr;
};
