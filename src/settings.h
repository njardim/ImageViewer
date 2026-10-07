// User preferences and session state, persisted with QSettings (decision D-29), the
// Settings dialog, and the user-interface languages (decision D-27).
#pragma once

#include "renderer.h"

#include <QColor>
#include <QDialog>
#include <QList>
#include <QRect>
#include <QString>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QToolButton;

// Preferences the user sets (Settings dialog and a few toggles that persist).
struct Settings {
    // General
    QString language;          // empty: follow the system; otherwise a code from uiLanguages()
    bool confirmTrash = true;  // ask before moving a file to the trash
    bool reopenLastImage = false;
    // Window
    QColor background = QColor(0x21, 0x21, 0x21); // sRGB; shown at SDR white
    bool rememberGeometry = true;
    bool showInfo = true;
    // Navigation
    bool loop = true;          // wrap from the last image to the first and back
    bool sideZones = true;     // click the left/right edge of the window for previous/next
    int sideZoneWidth = 200;   // logical pixels
    // Color & HDR
    bool toneMap = true;       // BT.2390 tone mapping on at startup
    Renderer::OutputPreference output = Renderer::OutputPreference::Automatic;

    static constexpr int kMinSideZoneWidth = 80;
    static constexpr int kMaxSideZoneWidth = 400;

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

// User-interface languages: English (the source language) and the translations that
// ship in the executable, with their names written in the language itself.
struct UiLanguage {
    QString code;       // Qt locale name, e.g. "pt" or "zh_CN"
    QString nativeName;
};
const QList<UiLanguage> &uiLanguages();

// Loads the translations for `code` (empty: the system's preferred languages), also Qt's
// own where they are installed, and sets the layout direction (right to left for Arabic
// and Urdu). Returns the language actually in use ("en" when nothing matched).
QString applyLanguage(const QString &code);

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const Settings &settings);
    Settings settings() const;

private:
    void setValues(const Settings &settings);
    void setBackground(const QColor &color);
    void chooseCustomBackground();

    QComboBox *m_language = nullptr;
    QCheckBox *m_confirmTrash = nullptr;
    QCheckBox *m_reopenLast = nullptr;
    QButtonGroup *m_backgroundGroup = nullptr;
    QToolButton *m_customBackground = nullptr;
    QColor m_background;
    QCheckBox *m_rememberGeometry = nullptr;
    QCheckBox *m_showInfo = nullptr;
    QCheckBox *m_loop = nullptr;
    QCheckBox *m_sideZones = nullptr;
    QSpinBox *m_sideZoneWidth = nullptr;
    QCheckBox *m_toneMap = nullptr;
    QComboBox *m_output = nullptr;
};
