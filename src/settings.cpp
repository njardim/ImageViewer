// Preferences and session state stored with QSettings (decision D-29), recent files, and the
// user-interface languages (decision D-27). The Settings dialog is in settingsdialog.cpp.
#include "settings.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QKeySequence>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace {

// Bumped when a stored value changes meaning; load() can then migrate older files.
// 2 (0.3): the side zones' default width went from 200 to 100 px (D-36).
// 3 (0.4): the panels' style applies to both panels, with new defaults (D-49).
constexpr int kSettingsVersion = 3;

const char *outputKey(Renderer::OutputPreference preference)
{
    switch (preference) {
    case Renderer::OutputPreference::Sdr: return "sdr";
    case Renderer::OutputPreference::Hdr10: return "hdr10";
    case Renderer::OutputPreference::Automatic: break;
    }
    return "automatic";
}

Renderer::OutputPreference outputFromKey(const QString &key)
{
    if (key == QLatin1String("sdr"))
        return Renderer::OutputPreference::Sdr;
    if (key == QLatin1String("hdr10"))
        return Renderer::OutputPreference::Hdr10;
    return Renderer::OutputPreference::Automatic;
}

const char *visibilityKey(OverlayVisibility visibility)
{
    switch (visibility) {
    case OverlayVisibility::Always: return "always";
    case OverlayVisibility::Hidden: return "hidden";
    case OverlayVisibility::Hover: break;
    }
    return "hover";
}

OverlayVisibility visibilityFromKey(const QString &key, OverlayVisibility fallback)
{
    for (OverlayVisibility v : {OverlayVisibility::Always, OverlayVisibility::Hover, OverlayVisibility::Hidden})
        if (key == QLatin1String(visibilityKey(v)))
            return v;
    return fallback;
}

const char *fieldKey(OverlayField field)
{
    switch (field) {
    case OverlayField::Name: return "name";
    case OverlayField::Dimensions: return "dimensions";
    case OverlayField::FileSize: return "fileSize";
    case OverlayField::Zoom: return "zoom";
    case OverlayField::ColorSpace: return "colorSpace";
    case OverlayField::Modified: return "modified";
    case OverlayField::Position: return "position";
    case OverlayField::Output: return "output";
    }
    return "";
}

const char *sortKey(FolderSort sort)
{
    switch (sort) {
    case FolderSort::Modified: return "modified";
    case FolderSort::Size: return "size";
    case FolderSort::Name: break;
    }
    return "name";
}

// Stored names of the view preferences of D-51.
constexpr std::pair<FitMode, const char *> kFitModes[] = {
    {FitMode::Window, "window"}, {FitMode::Width, "width"}, {FitMode::Height, "height"}, {FitMode::Fill, "fill"}};
constexpr std::pair<WindowFit, const char *> kWindowFits[] = {
    {WindowFit::Never, "never"}, {WindowFit::FirstImage, "first"}, {WindowFit::EveryImage, "every"}};
constexpr std::pair<TitleMode, const char *> kTitleModes[] = {{TitleMode::Application, "application"},
                                                              {TitleMode::Name, "name"},
                                                              {TitleMode::Details, "details"},
                                                              {TitleMode::Everything, "everything"}};

template <typename Enum, std::size_t N>
const char *keyOf(const std::pair<Enum, const char *> (&table)[N], Enum value)
{
    for (const auto &[e, key] : table)
        if (e == value)
            return key;
    return table[0].second;
}

template <typename Enum, std::size_t N>
Enum fromKey(const std::pair<Enum, const char *> (&table)[N], const QString &key, Enum fallback)
{
    for (const auto &[e, name] : table)
        if (key == QLatin1String(name))
            return e;
    return fallback;
}

FolderSort sortFromKey(const QString &key)
{
    if (key == QLatin1String("modified"))
        return FolderSort::Modified;
    if (key == QLatin1String("size"))
        return FolderSort::Size;
    return FolderSort::Name;
}

// A stored boolean; anything but true/false/1/0 (a hand-edited or corrupt file) is the default.
bool boolFrom(const QVariant &value, bool fallback)
{
    if (value.typeId() == QMetaType::Bool)
        return value.toBool();
    const QString text = value.toString().trimmed().toLower();
    if (text == QLatin1String("true") || text == QLatin1String("1"))
        return true;
    if (text == QLatin1String("false") || text == QLatin1String("0"))
        return false;
    return fallback;
}

bool boolValue(const QSettings &store, const QString &key, bool fallback)
{
    return boolFrom(store.value(key), fallback);
}

// The "app" group was called "general" up to 0.2: in INI files (Linux) QSettings writes that
// group as [%General] and reads it back as "General", so the values never loaded. Read the
// old spellings once; save() writes the new key and removes them.
QVariant appValue(const QSettings &store, const char *name)
{
    for (const char *group : {"app/", "general/", "General/"}) {
        const QString key = QLatin1String(group) + QLatin1String(name);
        if (store.contains(key))
            return store.value(key);
    }
    return {};
}

int boundedInt(const QSettings &store, const QString &key, int fallback, int low, int high)
{
    bool ok = false;
    const int value = store.value(key, fallback).toInt(&ok);
    return ok ? std::clamp(value, low, high) : fallback;
}

bool isKnownLanguage(const QString &code)
{
    const QList<UiLanguage> &languages = uiLanguages();
    return std::any_of(languages.cbegin(), languages.cend(), [&code](const UiLanguage &l) { return l.code == code; });
}

} // namespace

QList<OverlayField> Settings::defaultOverlayFields()
{
    return {OverlayField::Name, OverlayField::Dimensions, OverlayField::FileSize,
            OverlayField::Zoom, OverlayField::ColorSpace, OverlayField::Modified};
}

const QList<OverlayField> &Settings::allOverlayFields()
{
    static const QList<OverlayField> all = {OverlayField::Name, OverlayField::Dimensions, OverlayField::FileSize,
                                            OverlayField::Zoom, OverlayField::ColorSpace, OverlayField::Modified,
                                            OverlayField::Position, OverlayField::Output};
    return all;
}

QString overlayFieldName(OverlayField field)
{
    switch (field) {
    case OverlayField::Name: return QCoreApplication::translate("Overlay", "File name");
    case OverlayField::Dimensions: return QCoreApplication::translate("Overlay", "Dimensions");
    case OverlayField::FileSize: return QCoreApplication::translate("Overlay", "File size");
    case OverlayField::Zoom: return QCoreApplication::translate("Overlay", "Zoom");
    case OverlayField::ColorSpace: return QCoreApplication::translate("Overlay", "Color space");
    case OverlayField::Modified: return QCoreApplication::translate("Overlay", "Date modified");
    case OverlayField::Position: return QCoreApplication::translate("Overlay", "Position in the folder");
    case OverlayField::Output: return QCoreApplication::translate("Overlay", "Display output");
    }
    return {};
}

Settings Settings::load()
{
    const QSettings store;
    const Settings defaults;
    Settings s;
    s.language = appValue(store, "language").toString();
    if (!s.language.isEmpty() && !isKnownLanguage(s.language))
        s.language.clear();
    s.confirmTrash = boolFrom(appValue(store, "confirmTrash"), defaults.confirmTrash);
    s.reopenLastImage = boolFrom(appValue(store, "reopenLastImage"), defaults.reopenLastImage);

    const QColor background(store.value(QStringLiteral("window/background")).toString());
    s.background = background.isValid() ? background.toRgb() : defaults.background;
    s.background.setAlpha(255);
    s.checkerboard = boolValue(store, QStringLiteral("window/checkerboard"), defaults.checkerboard);
    s.rememberGeometry = boolValue(store, QStringLiteral("window/rememberGeometry"), defaults.rememberGeometry);
    s.fitMode = fromKey(kFitModes, store.value(QStringLiteral("view/fitMode")).toString(), defaults.fitMode);
    s.enlargeSmallImages = boolValue(store, QStringLiteral("view/enlargeSmallImages"), defaults.enlargeSmallImages);
    s.lockZoom = boolValue(store, QStringLiteral("view/lockZoom"), defaults.lockZoom);
    s.windowFit = fromKey(kWindowFits, store.value(QStringLiteral("window/matchImage")).toString(), defaults.windowFit);
    s.windowFitPercent = boundedInt(store, QStringLiteral("window/matchImagePercent"), defaults.windowFitPercent,
                                    kMinWindowFitPercent, kMaxWindowFitPercent);
    s.titleMode = fromKey(kTitleModes, store.value(QStringLiteral("window/title")).toString(), defaults.titleMode);
    s.pointerHideMs = boundedInt(store, QStringLiteral("window/pointerHideMs"), defaults.pointerHideMs, 0, kMaxPointerHideMs);
    s.showInfo = boolValue(store, QStringLiteral("window/showInfo"), defaults.showInfo);

    s.overlayFullScreen = visibilityFromKey(store.value(QStringLiteral("overlay/fullScreen")).toString(),
                                            defaults.overlayFullScreen);
    s.overlayWindow = visibilityFromKey(store.value(QStringLiteral("overlay/window")).toString(), defaults.overlayWindow);
    if (store.contains(QStringLiteral("overlay/fields"))) {
        // Comma-separated ids; an empty value means "no fields". Unknown or repeated ids are dropped.
        s.overlayFields.clear();
        // Written as one comma-separated string; a hand-edited file without quotes reads as a list.
        const QVariant stored = store.value(QStringLiteral("overlay/fields"));
        const QString joined = stored.typeId() == QMetaType::QStringList ? stored.toStringList().join(QLatin1Char(','))
                                                                          : stored.toString();
        const QStringList ids = joined.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &id : ids)
            for (OverlayField field : allOverlayFields())
                if (id == QLatin1String(fieldKey(field)) && !s.overlayFields.contains(field))
                    s.overlayFields.append(field);
    }
    s.overlayBackgroundOpacity = boundedInt(store, QStringLiteral("overlay/backgroundOpacity"),
                                            defaults.overlayBackgroundOpacity, 0, 100);
    s.overlayTextOpacity = boundedInt(store, QStringLiteral("overlay/textOpacity"), defaults.overlayTextOpacity,
                                      kMinOverlayTextOpacity, 100);
    s.overlayOutline = boolValue(store, QStringLiteral("overlay/outline"), defaults.overlayOutline);
    // Up to 0.3 these styled the top overlay only, and every save stored their defaults like a
    // choice; from 0.4 they style both panels, with the new defaults (D-49).
    // Likewise the overlay in full screen, on hover at the top up to 0.3, always from 0.4.
    if (store.value(QStringLiteral("version")).toInt() < 3) {
        if (s.overlayBackgroundOpacity == 60)
            s.overlayBackgroundOpacity = defaults.overlayBackgroundOpacity;
        s.overlayOutline = defaults.overlayOutline;
        if (s.overlayFullScreen == OverlayVisibility::Hover)
            s.overlayFullScreen = defaults.overlayFullScreen;
    }
    s.overlayHideDelayMs = boundedInt(store, QStringLiteral("overlay/hideDelayMs"), defaults.overlayHideDelayMs,
                                      kMinOverlayHideDelayMs, kMaxOverlayHideDelayMs);

    s.loop = boolValue(store, QStringLiteral("navigation/loop"), defaults.loop);
    s.sideZones = boolValue(store, QStringLiteral("navigation/sideZones"), defaults.sideZones);
    s.sideZoneWidth = boundedInt(store, QStringLiteral("navigation/sideZoneWidth"), defaults.sideZoneWidth,
                                 kMinSideZoneWidth, kMaxSideZoneWidth);
    // Up to 0.2 every save stored the old 200 px default like a choice; it becomes the new one.
    if (store.value(QStringLiteral("version")).toInt() < 2 && s.sideZoneWidth == 200)
        s.sideZoneWidth = defaults.sideZoneWidth;
    s.sortBy = sortFromKey(store.value(QStringLiteral("navigation/sortBy")).toString());
    s.sortDescending = boolValue(store, QStringLiteral("navigation/sortDescending"), defaults.sortDescending);
    s.preload = boolValue(store, QStringLiteral("navigation/preload"), defaults.preload);
    bool numeric = false;
    const double seconds = store.value(QStringLiteral("navigation/slideshowSeconds")).toDouble(&numeric);
    s.slideshowSeconds = numeric && seconds >= kMinSlideshowSeconds && seconds <= kMaxSlideshowSeconds
                             ? seconds : defaults.slideshowSeconds;

    // "Ctrl+," has a comma, which QSettings may read back as a list: join it again.
    const QString group = QStringLiteral("shortcuts/");
    for (const QString &name : store.allKeys()) {
        if (!name.startsWith(group))
            continue;
        const QString key = name.mid(group.size());
        const QVariant stored = store.value(name);
        const QString text = stored.typeId() == QMetaType::QStringList ? stored.toStringList().join(QLatin1Char(','))
                                                                        : stored.toString();
        QList<QKeySequence> list;
        const QStringList parts = text.split(QStringLiteral("; "), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            const QKeySequence sequence = QKeySequence::fromString(part.trimmed(), QKeySequence::PortableText);
            // Single keys only (the viewer matches those); an unknown name parses as Key_unknown.
            const Qt::Key k = sequence.count() == 1 ? sequence[0].key() : Qt::Key_unknown;
            const bool modifierOnly = k == Qt::Key_Shift || k == Qt::Key_Control || k == Qt::Key_Meta
                                      || k == Qt::Key_Alt || k == Qt::Key_AltGr;
            if (k != Qt::Key_unknown && !modifierOnly
                && std::none_of(list.cbegin(), list.cend(), [&sequence](const QKeySequence &other) { return sameShortcut(other, sequence); }))
                list.append(sequence);
        }
        if (list.isEmpty() && !parts.isEmpty())
            continue; // nothing usable in a hand-edited value: the defaults stay
        s.shortcuts.insert(key, list); // empty: the user removed every shortcut of the command
    }
    s.toneMap = boolValue(store, QStringLiteral("color/toneMap"), defaults.toneMap);
    s.clipWarning = boolValue(store, QStringLiteral("color/clipWarning"), defaults.clipWarning);
    s.output = outputFromKey(store.value(QStringLiteral("color/output")).toString());
    return s;
}

QKeyCombination comparableKey(QKeyCombination combination)
{
    Qt::KeyboardModifiers modifiers = combination.keyboardModifiers() & ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
    const int key = combination.key();
    const bool character = key > Qt::Key_Space && key < Qt::Key_Escape; // Key_Escape starts the function keys
    if (character && !QChar::isLetter(char32_t(key)))
        modifiers &= ~Qt::ShiftModifier;
    return QKeyCombination(modifiers, combination.key());
}

bool sameShortcut(const QKeySequence &a, const QKeySequence &b)
{
    return a.count() == 1 && b.count() == 1 && comparableKey(a[0]) == comparableKey(b[0]);
}

QColor Settings::panelBackground() const
{
    return QColor(0, 0, 0, int(std::lround(overlayBackgroundOpacity * 2.55)));
}

QColor Settings::panelText(int grey) const
{
    return QColor(grey, grey, grey, int(std::lround(overlayTextOpacity * 2.55)));
}

bool Settings::storedIsOutdated()
{
    const QSettings store;
    return store.value(QStringLiteral("version")).toInt() < kSettingsVersion && !store.allKeys().isEmpty();
}

void Settings::save() const
{
    QSettings store;
    store.setValue(QStringLiteral("version"), kSettingsVersion);
    store.remove(QStringLiteral("general")); // pre-0.2 spellings (see appValue())
    store.remove(QStringLiteral("General"));
    store.setValue(QStringLiteral("app/language"), language);
    store.setValue(QStringLiteral("app/confirmTrash"), confirmTrash);
    store.setValue(QStringLiteral("app/reopenLastImage"), reopenLastImage);
    store.setValue(QStringLiteral("window/background"), background.name(QColor::HexRgb));
    store.setValue(QStringLiteral("window/checkerboard"), checkerboard);
    store.setValue(QStringLiteral("window/rememberGeometry"), rememberGeometry);
    store.setValue(QStringLiteral("view/fitMode"), QString::fromLatin1(keyOf(kFitModes, fitMode)));
    store.setValue(QStringLiteral("view/enlargeSmallImages"), enlargeSmallImages);
    store.setValue(QStringLiteral("view/lockZoom"), lockZoom);
    store.setValue(QStringLiteral("window/matchImage"), QString::fromLatin1(keyOf(kWindowFits, windowFit)));
    store.setValue(QStringLiteral("window/matchImagePercent"), windowFitPercent);
    store.setValue(QStringLiteral("window/title"), QString::fromLatin1(keyOf(kTitleModes, titleMode)));
    store.setValue(QStringLiteral("window/pointerHideMs"), pointerHideMs);
    store.setValue(QStringLiteral("window/showInfo"), showInfo);
    store.setValue(QStringLiteral("overlay/fullScreen"), QString::fromLatin1(visibilityKey(overlayFullScreen)));
    store.setValue(QStringLiteral("overlay/window"), QString::fromLatin1(visibilityKey(overlayWindow)));
    QStringList ids;
    for (OverlayField field : overlayFields)
        ids << QString::fromLatin1(fieldKey(field));
    store.setValue(QStringLiteral("overlay/fields"), ids.join(QLatin1Char(',')));
    store.setValue(QStringLiteral("overlay/backgroundOpacity"), overlayBackgroundOpacity);
    store.setValue(QStringLiteral("overlay/textOpacity"), overlayTextOpacity);
    store.setValue(QStringLiteral("overlay/outline"), overlayOutline);
    store.setValue(QStringLiteral("overlay/hideDelayMs"), overlayHideDelayMs);
    store.setValue(QStringLiteral("navigation/loop"), loop);
    store.setValue(QStringLiteral("navigation/sideZones"), sideZones);
    store.setValue(QStringLiteral("navigation/sideZoneWidth"), sideZoneWidth);
    store.setValue(QStringLiteral("navigation/sortBy"), QString::fromLatin1(sortKey(sortBy)));
    store.setValue(QStringLiteral("navigation/sortDescending"), sortDescending);
    store.setValue(QStringLiteral("navigation/preload"), preload);
    store.setValue(QStringLiteral("navigation/slideshowSeconds"), slideshowSeconds);
    store.remove(QStringLiteral("shortcuts"));
    for (auto it = shortcuts.cbegin(); it != shortcuts.cend(); ++it) {
        QStringList parts;
        for (const QKeySequence &sequence : it.value())
            parts << sequence.toString(QKeySequence::PortableText);
        store.setValue(QStringLiteral("shortcuts/") + it.key(), parts.join(QStringLiteral("; ")));
    }
    store.setValue(QStringLiteral("color/toneMap"), toneMap);
    store.setValue(QStringLiteral("color/clipWarning"), clipWarning);
    store.setValue(QStringLiteral("color/output"), QString::fromLatin1(outputKey(output)));
}

SessionState SessionState::load()
{
    const QSettings store;
    SessionState s;
    const QRect geometry = store.value(QStringLiteral("session/geometry")).toRect();
    if (geometry.isValid() && geometry.width() >= 160 && geometry.height() >= 120)
        s.geometry = geometry;
    s.maximized = boolValue(store, QStringLiteral("session/maximized"), false);
    s.fullScreen = boolValue(store, QStringLiteral("session/fullScreen"), false);
    s.lastFile = store.value(QStringLiteral("session/lastFile")).toString();
    s.lastDirectory = store.value(QStringLiteral("session/lastDirectory")).toString();
    return s;
}

void SessionState::save() const
{
    QSettings store; // "version" describes the preferences: only Settings::save() writes it
    if (geometry.isValid())
        store.setValue(QStringLiteral("session/geometry"), geometry);
    store.setValue(QStringLiteral("session/maximized"), maximized);
    store.setValue(QStringLiteral("session/fullScreen"), fullScreen);
    store.setValue(QStringLiteral("session/lastFile"), lastFile);
    store.setValue(QStringLiteral("session/lastDirectory"), lastDirectory);
}

void adoptEarlierSettingsFile()
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    const QString current = QSettings().fileName();
    const QString earlier = QFileInfo(current).absolutePath() + QStringLiteral("/imageViewer.conf");
    if (!QFileInfo::exists(current) && QFileInfo::exists(earlier))
        QFile::rename(earlier, current);
#endif
}

QStringList loadRecentFiles()
{
    const QSettings store;
    QStringList files;
    for (const QString &file : store.value(QStringLiteral("recent/files")).toStringList())
        if (!file.isEmpty() && !files.contains(file) && files.size() < kMaxRecentFiles)
            files << file;
    return files;
}

void saveRecentFiles(const QStringList &files)
{
    QSettings store;
    if (files.isEmpty())
        store.remove(QStringLiteral("recent/files"));
    else
        store.setValue(QStringLiteral("recent/files"), files.mid(0, kMaxRecentFiles));
}

const QList<UiLanguage> &uiLanguages()
{
    // Sorted by the English name of the language, so the order is the same whatever the
    // interface language (D-32): the 16 most spoken languages (D-27), Korean, Italian and
    // Turkish, and every official language of the European Union.
    static const QList<UiLanguage> languages = {
        {QStringLiteral("ar"), QStringLiteral("العربية")},
        {QStringLiteral("bn"), QStringLiteral("বাংলা")},
        {QStringLiteral("bg"), QStringLiteral("Български")},
        {QStringLiteral("zh_CN"), QStringLiteral("简体中文")},
        {QStringLiteral("hr"), QStringLiteral("Hrvatski")},
        {QStringLiteral("cs"), QStringLiteral("Čeština")},
        {QStringLiteral("da"), QStringLiteral("Dansk")},
        {QStringLiteral("nl"), QStringLiteral("Nederlands")},
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("et"), QStringLiteral("Eesti")},
        {QStringLiteral("fi"), QStringLiteral("Suomi")},
        {QStringLiteral("fr"), QStringLiteral("Français")},
        {QStringLiteral("de"), QStringLiteral("Deutsch")},
        {QStringLiteral("el"), QStringLiteral("Ελληνικά")},
        {QStringLiteral("hi"), QStringLiteral("हिन्दी")},
        {QStringLiteral("hu"), QStringLiteral("Magyar")},
        {QStringLiteral("id"), QStringLiteral("Bahasa Indonesia")},
        {QStringLiteral("ga"), QStringLiteral("Gaeilge")},
        {QStringLiteral("it"), QStringLiteral("Italiano")},
        {QStringLiteral("ja"), QStringLiteral("日本語")},
        {QStringLiteral("ko"), QStringLiteral("한국어")},
        {QStringLiteral("lv"), QStringLiteral("Latviešu")},
        {QStringLiteral("lt"), QStringLiteral("Lietuvių")},
        {QStringLiteral("mt"), QStringLiteral("Malti")},
        {QStringLiteral("mr"), QStringLiteral("मराठी")},
        {QStringLiteral("pl"), QStringLiteral("Polski")},
        {QStringLiteral("pt"), QStringLiteral("Português")},
        {QStringLiteral("ro"), QStringLiteral("Română")},
        {QStringLiteral("ru"), QStringLiteral("Русский")},
        {QStringLiteral("sk"), QStringLiteral("Slovenčina")},
        {QStringLiteral("sl"), QStringLiteral("Slovenščina")},
        {QStringLiteral("es"), QStringLiteral("Español")},
        {QStringLiteral("sv"), QStringLiteral("Svenska")},
        {QStringLiteral("te"), QStringLiteral("తెలుగు")},
        {QStringLiteral("tr"), QStringLiteral("Türkçe")},
        {QStringLiteral("ur"), QStringLiteral("اردو")},
        {QStringLiteral("vi"), QStringLiteral("Tiếng Việt")},
    };
    return languages;
}

QString applyLanguage(const QString &code)
{
    // Installed for the lifetime of the application; reloading replaces their contents.
    static QTranslator app;
    static QTranslator qt;
    QCoreApplication::removeTranslator(&app);
    QCoreApplication::removeTranslator(&qt);

    // The system locale lists the user's preferred languages in order. An English catalog
    // ships too (plural forms only), so an English-first user never falls through to a
    // later preference.
    const QLocale locale = code.isEmpty() ? QLocale::system() : QLocale(code);
    QString used = QStringLiteral("en");
    if (app.load(locale, QStringLiteral("imageviewer"), QStringLiteral("_"), QStringLiteral(":/i18n"))) {
        QCoreApplication::installTranslator(&app);
        used = app.language();
    }
    // Qt's own strings (standard dialogs); present only where the Qt translations are installed.
    if (qt.load(QLocale(used), QStringLiteral("qtbase"), QStringLiteral("_"),
                QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(&qt);
    QLocale::setDefault(code.isEmpty() ? QLocale::system() : QLocale(used)); // numbers follow the UI
    QGuiApplication::setLayoutDirection(QLocale(used).textDirection());
    return used;
}
