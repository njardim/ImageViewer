#include "settings.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLibraryInfo>
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

Settings Settings::load()
{
    const QSettings store;
    const Settings defaults;
    Settings s;
    s.language = store.value(QStringLiteral("general/language"), defaults.language).toString();
    if (!s.language.isEmpty() && !isKnownLanguage(s.language))
        s.language.clear();
    s.confirmTrash = store.value(QStringLiteral("general/confirmTrash"), defaults.confirmTrash).toBool();
    s.reopenLastImage = store.value(QStringLiteral("general/reopenLastImage"), defaults.reopenLastImage).toBool();

    const QColor background(store.value(QStringLiteral("window/background")).toString());
    s.background = background.isValid() ? background.toRgb() : defaults.background;
    s.background.setAlpha(255);
    s.rememberGeometry = store.value(QStringLiteral("window/rememberGeometry"), defaults.rememberGeometry).toBool();
    s.showInfo = store.value(QStringLiteral("window/showInfo"), defaults.showInfo).toBool();

    s.loop = store.value(QStringLiteral("navigation/loop"), defaults.loop).toBool();
    s.sideZones = store.value(QStringLiteral("navigation/sideZones"), defaults.sideZones).toBool();
    bool ok = false;
    const int width = store.value(QStringLiteral("navigation/sideZoneWidth"), defaults.sideZoneWidth).toInt(&ok);
    s.sideZoneWidth = ok ? std::clamp(width, kMinSideZoneWidth, kMaxSideZoneWidth) : defaults.sideZoneWidth;

    s.toneMap = store.value(QStringLiteral("color/toneMap"), defaults.toneMap).toBool();
    s.output = outputFromKey(store.value(QStringLiteral("color/output")).toString());
    return s;
}

void Settings::save() const
{
    QSettings store;
    store.setValue(QStringLiteral("version"), kSettingsVersion);
    store.setValue(QStringLiteral("general/language"), language);
    store.setValue(QStringLiteral("general/confirmTrash"), confirmTrash);
    store.setValue(QStringLiteral("general/reopenLastImage"), reopenLastImage);
    store.setValue(QStringLiteral("window/background"), background.name(QColor::HexRgb));
    store.setValue(QStringLiteral("window/rememberGeometry"), rememberGeometry);
    store.setValue(QStringLiteral("window/showInfo"), showInfo);
    store.setValue(QStringLiteral("navigation/loop"), loop);
    store.setValue(QStringLiteral("navigation/sideZones"), sideZones);
    store.setValue(QStringLiteral("navigation/sideZoneWidth"), sideZoneWidth);
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
    s.maximized = store.value(QStringLiteral("session/maximized"), false).toBool();
    s.fullScreen = store.value(QStringLiteral("session/fullScreen"), false).toBool();
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

const QList<UiLanguage> &uiLanguages()
{
    // English first (the source language), then by total speakers, Ethnologue 2026 (docs/PLAN.md, D-27).
    static const QList<UiLanguage> languages = {
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("zh_CN"), QStringLiteral("简体中文")},
        {QStringLiteral("hi"), QStringLiteral("हिन्दी")},
        {QStringLiteral("es"), QStringLiteral("Español")},
        {QStringLiteral("ar"), QStringLiteral("العربية")},
        {QStringLiteral("fr"), QStringLiteral("Français")},
        {QStringLiteral("bn"), QStringLiteral("বাংলা")},
        {QStringLiteral("pt"), QStringLiteral("Português")},
        {QStringLiteral("id"), QStringLiteral("Bahasa Indonesia")},
        {QStringLiteral("ur"), QStringLiteral("اردو")},
        {QStringLiteral("ru"), QStringLiteral("Русский")},
        {QStringLiteral("de"), QStringLiteral("Deutsch")},
        {QStringLiteral("ja"), QStringLiteral("日本語")},
        {QStringLiteral("mr"), QStringLiteral("मराठी")},
        {QStringLiteral("vi"), QStringLiteral("Tiếng Việt")},
        {QStringLiteral("te"), QStringLiteral("తెలుగు")},
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

SettingsDialog::SettingsDialog(const Settings &settings)
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
    m_confirmTrash = new QCheckBox(tr("Confirm before deleting an image"));
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
    m_rememberGeometry = new QCheckBox(tr("Remember the window size and position"));
    windowForm->addRow(m_rememberGeometry);
    m_showInfo = new QCheckBox(tr("Show the information panel"));
    windowForm->addRow(m_showInfo);
    tabs->addTab(window, tr("Window"));

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
    m_rememberGeometry->setChecked(settings.rememberGeometry);
    m_showInfo->setChecked(settings.showInfo);
    m_loop->setChecked(settings.loop);
    m_sideZones->setChecked(settings.sideZones);
    m_sideZoneWidth->setValue(settings.sideZoneWidth);
    m_sideZoneWidth->setEnabled(settings.sideZones);
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

Settings SettingsDialog::settings() const
{
    Settings s;
    s.language = m_language->currentData().toString();
    s.confirmTrash = m_confirmTrash->isChecked();
    s.reopenLastImage = m_reopenLast->isChecked();
    s.background = m_background;
    s.rememberGeometry = m_rememberGeometry->isChecked();
    s.showInfo = m_showInfo->isChecked();
    s.loop = m_loop->isChecked();
    s.sideZones = m_sideZones->isChecked();
    s.sideZoneWidth = m_sideZoneWidth->value();
    s.toneMap = m_toneMap->isChecked();
    s.output = outputFromKey(m_output->currentData().toString());
    return s;
}
