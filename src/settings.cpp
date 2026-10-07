#include "settings.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLibraryInfo>
#include <QListWidget>
#include <QLocale>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QTranslator>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace {

// Bumped when a stored value changes meaning; load() can then migrate older files.
constexpr int kSettingsVersion = 1;

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

// Background presets, sRGB. The default (dark grey) keeps the image's own contrast readable.
const QColor kBackgroundPresets[] = {QColor(0x00, 0x00, 0x00), QColor(0x21, 0x21, 0x21), QColor(0x80, 0x80, 0x80),
                                     QColor(0xd0, 0xd0, 0xd0), QColor(0xff, 0xff, 0xff)};

QIcon swatchIcon(const QColor &color)
{
    QPixmap pixmap(28, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QColor(128, 128, 128));
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(0.5, 0.5, 27, 19), 3, 3);
    return QIcon(pixmap);
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
    s.overlayHideDelayMs = boundedInt(store, QStringLiteral("overlay/hideDelayMs"), defaults.overlayHideDelayMs,
                                      kMinOverlayHideDelayMs, kMaxOverlayHideDelayMs);

    s.loop = boolValue(store, QStringLiteral("navigation/loop"), defaults.loop);
    s.sideZones = boolValue(store, QStringLiteral("navigation/sideZones"), defaults.sideZones);
    s.sideZoneWidth = boundedInt(store, QStringLiteral("navigation/sideZoneWidth"), defaults.sideZoneWidth,
                                 kMinSideZoneWidth, kMaxSideZoneWidth);
    s.sortBy = sortFromKey(store.value(QStringLiteral("navigation/sortBy")).toString());
    s.sortDescending = boolValue(store, QStringLiteral("navigation/sortDescending"), defaults.sortDescending);
    s.preload = boolValue(store, QStringLiteral("navigation/preload"), defaults.preload);

    s.toneMap = boolValue(store, QStringLiteral("color/toneMap"), defaults.toneMap);
    s.output = outputFromKey(store.value(QStringLiteral("color/output")).toString());
    return s;
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
    store.setValue(QStringLiteral("color/toneMap"), toneMap);
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
    QSettings store;
    store.setValue(QStringLiteral("version"), kSettingsVersion);
    if (geometry.isValid())
        store.setValue(QStringLiteral("session/geometry"), geometry);
    store.setValue(QStringLiteral("session/maximized"), maximized);
    store.setValue(QStringLiteral("session/fullScreen"), fullScreen);
    store.setValue(QStringLiteral("session/lastFile"), lastFile);
    store.setValue(QStringLiteral("session/lastDirectory"), lastDirectory);
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

SettingsDialog::SettingsDialog(const Settings &settings) : m_initial(settings)
{
    setWindowTitle(tr("Settings"));
    auto *tabs = new QTabWidget;

    // General
    auto *general = new QWidget;
    auto *generalForm = new QFormLayout(general);
    m_language = new QComboBox;
    m_language->addItem(tr("System default"), QString());
    for (const UiLanguage &language : uiLanguages())
        m_language->addItem(language.nativeName, language.code);
    generalForm->addRow(tr("Language:"), m_language);
    auto *translationNote = new QLabel(tr("Languages other than English are machine translations "
                                          "awaiting review by native speakers."));
    translationNote->setWordWrap(true);
    translationNote->setForegroundRole(QPalette::PlaceholderText); // secondary, but still read by screen readers
    generalForm->addRow(QString(), translationNote);
    m_confirmTrash = new QCheckBox(tr("Confirm before moving an image to the trash"));
    generalForm->addRow(m_confirmTrash);
    m_reopenLast = new QCheckBox(tr("Reopen the last image at startup"));
    generalForm->addRow(m_reopenLast);
    tabs->addTab(general, tr("General"));

    // Window
    auto *window = new QWidget;
    auto *windowForm = new QFormLayout(window);
    auto *swatches = new QHBoxLayout;
    m_backgroundGroup = new QButtonGroup(this);
    m_backgroundGroup->setExclusive(true);
    const QString presetNames[] = {tr("Black"), tr("Dark gray"), tr("Gray"), tr("Light gray"), tr("White")};
    for (int i = 0; i < int(std::size(kBackgroundPresets)); ++i) {
        auto *button = new QToolButton;
        button->setCheckable(true);
        button->setIcon(swatchIcon(kBackgroundPresets[i]));
        button->setIconSize(QSize(28, 20));
        button->setToolTip(presetNames[i]);
        button->setAccessibleName(presetNames[i]);
        m_backgroundGroup->addButton(button, i);
        swatches->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, i] { setBackground(kBackgroundPresets[i]); });
    }
    m_customBackground = new QToolButton;
    m_customBackground->setCheckable(true);
    m_customBackground->setText(tr("Custom…"));
    m_customBackground->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_customBackground->setIconSize(QSize(28, 20));
    m_backgroundGroup->addButton(m_customBackground, int(std::size(kBackgroundPresets)));
    swatches->addWidget(m_customBackground);
    swatches->addStretch();
    connect(m_customBackground, &QToolButton::clicked, this, &SettingsDialog::chooseCustomBackground);
    windowForm->addRow(tr("Background:"), swatches);
    m_checkerboard = new QCheckBox(tr("Show a checkerboard behind transparent areas"));
    windowForm->addRow(m_checkerboard);
    m_rememberGeometry = new QCheckBox(tr("Remember the window size and position"));
    windowForm->addRow(m_rememberGeometry);
    tabs->addTab(window, tr("Window"));

    // Information (D-34): the detailed panel and the compact overlay at the top.
    auto *information = new QWidget;
    auto *informationLayout = new QVBoxLayout(information);
    // Key names as the platform writes them (⇧I on macOS).
    const auto keyName = [](QKeyCombination key) { return QKeySequence(key).toString(QKeySequence::NativeText); };
    //: %1: the keyboard shortcut, e.g. "I".
    m_showInfo = new QCheckBox(tr("Show the information panel (%1)").arg(keyName(Qt::Key_I)));
    informationLayout->addWidget(m_showInfo);
    //: %1: the keyboard shortcut, e.g. "Shift+I".
    auto *overlayBox = new QGroupBox(tr("Overlay at the top (%1)").arg(keyName(Qt::SHIFT | Qt::Key_I)));
    auto *overlayForm = new QFormLayout(overlayBox);
    const auto fillVisibility = [](QComboBox *combo) {
        combo->addItem(tr("Always"), QString::fromLatin1(visibilityKey(OverlayVisibility::Always)));
        combo->addItem(tr("When the pointer is at the top"), QString::fromLatin1(visibilityKey(OverlayVisibility::Hover)));
        combo->addItem(tr("Never"), QString::fromLatin1(visibilityKey(OverlayVisibility::Hidden)));
    };
    m_overlayFullScreen = new QComboBox;
    fillVisibility(m_overlayFullScreen);
    overlayForm->addRow(tr("In full screen:"), m_overlayFullScreen);
    m_overlayWindow = new QComboBox;
    fillVisibility(m_overlayWindow);
    overlayForm->addRow(tr("In a window:"), m_overlayWindow);
    m_overlayFields = new QListWidget;
    m_overlayFields->setDragDropMode(QAbstractItemView::InternalMove);
    m_overlayFields->setDefaultDropAction(Qt::MoveAction);
    m_overlayFields->setSelectionMode(QAbstractItemView::SingleSelection);
    m_overlayFields->setAccessibleName(tr("Fields"));
    auto *moveUp = new QPushButton(tr("Move Up"));
    auto *moveDown = new QPushButton(tr("Move Down"));
    connect(moveUp, &QPushButton::clicked, this, [this] { moveOverlayField(-1); });
    connect(moveDown, &QPushButton::clicked, this, [this] { moveOverlayField(+1); });
    auto *moveButtons = new QVBoxLayout;
    moveButtons->addWidget(moveUp);
    moveButtons->addWidget(moveDown);
    moveButtons->addStretch();
    auto *fieldsRow = new QHBoxLayout;
    fieldsRow->addWidget(m_overlayFields, 1);
    fieldsRow->addLayout(moveButtons);
    overlayForm->addRow(tr("Fields:"), fieldsRow);
    m_overlayBackground = new QSpinBox;
    m_overlayBackground->setRange(0, 100);
    m_overlayBackground->setSingleStep(10);
    //: Unit after a percentage; keep the leading space if your language separates it.
    m_overlayBackground->setSuffix(tr(" %"));
    overlayForm->addRow(tr("Background opacity:"), m_overlayBackground);
    m_overlayText = new QSpinBox;
    m_overlayText->setRange(Settings::kMinOverlayTextOpacity, 100);
    m_overlayText->setSingleStep(10);
    m_overlayText->setSuffix(tr(" %"));
    overlayForm->addRow(tr("Text opacity:"), m_overlayText);
    m_overlayOutline = new QCheckBox(tr("Outline the text"));
    overlayForm->addRow(m_overlayOutline);
    m_overlayDelay = new QDoubleSpinBox;
    m_overlayDelay->setRange(Settings::kMinOverlayHideDelayMs / 1000.0, Settings::kMaxOverlayHideDelayMs / 1000.0);
    m_overlayDelay->setDecimals(1);
    m_overlayDelay->setSingleStep(0.5);
    //: Unit after a number of seconds; keep the leading space if your language separates units.
    m_overlayDelay->setSuffix(tr(" s"));
    overlayForm->addRow(tr("Hide after:"), m_overlayDelay);
    informationLayout->addWidget(overlayBox);
    informationLayout->addStretch();
    tabs->addTab(information, tr("Information"));

    // Navigation
    auto *navigation = new QWidget;
    auto *navigationForm = new QFormLayout(navigation);
    m_loop = new QCheckBox(tr("After the last image, continue with the first"));
    navigationForm->addRow(m_loop);
    m_sideZones = new QCheckBox(tr("Click the left or right side of the window for the previous or next image"));
    navigationForm->addRow(m_sideZones);
    m_sideZoneWidth = new QSpinBox;
    m_sideZoneWidth->setRange(Settings::kMinSideZoneWidth, Settings::kMaxSideZoneWidth);
    m_sideZoneWidth->setSingleStep(10);
    //: Unit after a number of pixels; keep the leading space if your language separates units.
    m_sideZoneWidth->setSuffix(tr(" px"));
    navigationForm->addRow(tr("Width of each side:"), m_sideZoneWidth);
    connect(m_sideZones, &QCheckBox::toggled, m_sideZoneWidth, &QWidget::setEnabled);
    m_sortBy = new QComboBox;
    m_sortBy->addItem(tr("Name"), QString::fromLatin1(sortKey(FolderSort::Name)));
    m_sortBy->addItem(tr("Date modified"), QString::fromLatin1(sortKey(FolderSort::Modified)));
    m_sortBy->addItem(tr("Size"), QString::fromLatin1(sortKey(FolderSort::Size)));
    m_sortDescending = new QCheckBox(tr("Descending"));
    auto *sortRow = new QHBoxLayout;
    sortRow->addWidget(m_sortBy);
    sortRow->addWidget(m_sortDescending);
    sortRow->addStretch();
    navigationForm->addRow(tr("Sort images by:"), sortRow);
    m_preload = new QCheckBox(tr("Load the next and previous images in advance"));
    navigationForm->addRow(m_preload);
    tabs->addTab(navigation, tr("Navigation"));

    // Color & HDR
    auto *color = new QWidget;
    auto *colorForm = new QFormLayout(color);
    m_output = new QComboBox;
    m_output->addItem(tr("Automatic (HDR when the display supports it)"),
                      QString::fromLatin1(outputKey(Renderer::OutputPreference::Automatic)));
    m_output->addItem(tr("SDR (sRGB)"), QString::fromLatin1(outputKey(Renderer::OutputPreference::Sdr)));
    m_output->addItem(tr("HDR10 (PQ)"), QString::fromLatin1(outputKey(Renderer::OutputPreference::Hdr10)));
    colorForm->addRow(tr("Display output:"), m_output);
    m_toneMap = new QCheckBox(tr("Tone map HDR images that exceed the display (ITU-R BT.2390)"));
    colorForm->addRow(m_toneMap);
    //: "&&" is shown as a single "&".
    tabs->addTab(color, tr("Color && HDR"));

    auto *buttons = new QDialogButtonBox;
    QPushButton *ok = buttons->addButton(tr("OK"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    QPushButton *defaults = buttons->addButton(tr("Restore Defaults"), QDialogButtonBox::ResetRole);
    ok->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(defaults, &QPushButton::clicked, this, [this] {
        // setValues() only touches what the dialog shows; settings() keeps the rest from m_initial.
        Settings fresh;
        fresh.language = m_language->currentData().toString(); // the language is not a "setting to reset"
        setValues(fresh);
    });

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
    setValues(settings);
}

void SettingsDialog::setValues(const Settings &settings)
{
    m_language->setCurrentIndex(std::max(0, m_language->findData(settings.language)));
    m_confirmTrash->setChecked(settings.confirmTrash);
    m_reopenLast->setChecked(settings.reopenLastImage);
    setBackground(settings.background);
    m_checkerboard->setChecked(settings.checkerboard);
    m_rememberGeometry->setChecked(settings.rememberGeometry);
    m_showInfo->setChecked(settings.showInfo);
    m_overlayFullScreen->setCurrentIndex(
        std::max(0, m_overlayFullScreen->findData(QString::fromLatin1(visibilityKey(settings.overlayFullScreen)))));
    m_overlayWindow->setCurrentIndex(
        std::max(0, m_overlayWindow->findData(QString::fromLatin1(visibilityKey(settings.overlayWindow)))));
    // The chosen fields first, in their order, then the others in their usual order.
    m_overlayFields->clear();
    QList<OverlayField> order = settings.overlayFields;
    for (OverlayField field : Settings::allOverlayFields())
        if (!order.contains(field))
            order.append(field);
    for (OverlayField field : order) {
        auto *item = new QListWidgetItem(overlayFieldName(field), m_overlayFields);
        item->setData(Qt::UserRole, int(field));
        item->setFlags((item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled) & ~Qt::ItemIsDropEnabled);
        item->setCheckState(settings.overlayFields.contains(field) ? Qt::Checked : Qt::Unchecked);
    }
    // Every field visible at once: the list is short, and scrolling would hide the order.
    m_overlayFields->setMinimumHeight(m_overlayFields->sizeHintForRow(0) * m_overlayFields->count()
                                      + 2 * m_overlayFields->frameWidth() + 2);
    m_overlayBackground->setValue(settings.overlayBackgroundOpacity);
    m_overlayText->setValue(settings.overlayTextOpacity);
    m_overlayOutline->setChecked(settings.overlayOutline);
    m_overlayDelay->setValue(settings.overlayHideDelayMs / 1000.0);
    m_loop->setChecked(settings.loop);
    m_sideZones->setChecked(settings.sideZones);
    m_sideZoneWidth->setValue(settings.sideZoneWidth);
    m_sideZoneWidth->setEnabled(settings.sideZones);
    m_sortBy->setCurrentIndex(std::max(0, m_sortBy->findData(QString::fromLatin1(sortKey(settings.sortBy)))));
    m_sortDescending->setChecked(settings.sortDescending);
    m_preload->setChecked(settings.preload);
    m_toneMap->setChecked(settings.toneMap);
    m_output->setCurrentIndex(std::max(0, m_output->findData(QString::fromLatin1(outputKey(settings.output)))));
}

void SettingsDialog::setBackground(const QColor &color)
{
    m_background = color;
    const auto preset = std::find(std::begin(kBackgroundPresets), std::end(kBackgroundPresets), color);
    if (preset != std::end(kBackgroundPresets)) {
        m_backgroundGroup->button(int(preset - std::begin(kBackgroundPresets)))->setChecked(true);
        m_customBackground->setIcon(QIcon());
    } else {
        m_customBackground->setChecked(true);
        m_customBackground->setIcon(swatchIcon(color));
    }
}

void SettingsDialog::chooseCustomBackground()
{
    const QColor chosen = QColorDialog::getColor(m_background, this, tr("Background Color"));
    setBackground(chosen.isValid() ? chosen.toRgb() : m_background); // cancelled: restore the selection
}

void SettingsDialog::moveOverlayField(int delta)
{
    const int row = m_overlayFields->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= m_overlayFields->count())
        return;
    QListWidgetItem *item = m_overlayFields->takeItem(row);
    m_overlayFields->insertItem(target, item);
    m_overlayFields->setCurrentRow(target);
}

Settings SettingsDialog::settings() const
{
    Settings s = m_initial;
    s.language = m_language->currentData().toString();
    s.confirmTrash = m_confirmTrash->isChecked();
    s.reopenLastImage = m_reopenLast->isChecked();
    s.background = m_background;
    s.checkerboard = m_checkerboard->isChecked();
    s.rememberGeometry = m_rememberGeometry->isChecked();
    s.showInfo = m_showInfo->isChecked();
    s.overlayFullScreen = visibilityFromKey(m_overlayFullScreen->currentData().toString(), OverlayVisibility::Hover);
    s.overlayWindow = visibilityFromKey(m_overlayWindow->currentData().toString(), OverlayVisibility::Hidden);
    s.overlayFields.clear();
    for (int i = 0; i < m_overlayFields->count(); ++i) {
        const QListWidgetItem *item = m_overlayFields->item(i);
        if (item->checkState() == Qt::Checked)
            s.overlayFields.append(OverlayField(item->data(Qt::UserRole).toInt()));
    }
    s.overlayBackgroundOpacity = m_overlayBackground->value();
    s.overlayTextOpacity = m_overlayText->value();
    s.overlayOutline = m_overlayOutline->isChecked();
    s.overlayHideDelayMs = int(std::lround(m_overlayDelay->value() * 1000.0));
    s.loop = m_loop->isChecked();
    s.sideZones = m_sideZones->isChecked();
    s.sideZoneWidth = m_sideZoneWidth->value();
    s.sortBy = sortFromKey(m_sortBy->currentData().toString());
    s.sortDescending = m_sortDescending->isChecked();
    s.preload = m_preload->isChecked();
    s.toneMap = m_toneMap->isChecked();
    s.output = outputFromKey(m_output->currentData().toString());
    return s;
}
