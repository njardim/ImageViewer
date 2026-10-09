// The Settings dialog (decision D-29): one tab per group of preferences; Apply and OK hand
// the values to the viewer, which applies them at once.
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
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace {
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

SettingsDialog::SettingsDialog(const Settings &settings) : m_initial(settings)
{
    buildUi();
    setValues(settings);
    m_applied = this->settings(); // as the dialog shows them (normalised), so nothing reads as changed
    updateApplyButton();
}

void SettingsDialog::buildUi()
{
    setWindowTitle(tr("Settings"));
    auto *tabs = new QTabWidget;
    m_tabs = tabs;

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
    // D-51: the zoom of a new image, the window's size, the title bar.
    m_fitMode = new QComboBox;
    m_fitMode->addItem(tr("Fit to the window"), int(FitMode::Window));
    m_fitMode->addItem(tr("Fit to the width"), int(FitMode::Width));
    m_fitMode->addItem(tr("Fit to the height"), int(FitMode::Height));
    m_fitMode->addItem(tr("Fill the window"), int(FitMode::Fill));
    windowForm->addRow(tr("Zoom of a new image:"), m_fitMode);
    m_enlargeSmall = new QCheckBox(tr("Enlarge images smaller than the window"));
    windowForm->addRow(m_enlargeSmall);
    m_windowFit = new QComboBox;
    m_windowFit->addItem(tr("Keep its size"), int(WindowFit::Never));
    m_windowFit->addItem(tr("Fit it to the first image"), int(WindowFit::FirstImage));
    m_windowFit->addItem(tr("Fit it to every image"), int(WindowFit::EveryImage));
    windowForm->addRow(tr("Window size:"), m_windowFit);
    m_windowFitPercent = new QSpinBox;
    m_windowFitPercent->setRange(Settings::kMinWindowFitPercent, Settings::kMaxWindowFitPercent);
    m_windowFitPercent->setSingleStep(5);
    //: Unit after a percentage of the screen's size; keep the leading space if your language separates it.
    m_windowFitPercent->setSuffix(tr(" % of the screen"));
    windowForm->addRow(tr("At most:"), m_windowFitPercent);
    connect(m_windowFit, &QComboBox::currentIndexChanged, this,
            [this] { m_windowFitPercent->setEnabled(WindowFit(m_windowFit->currentData().toInt()) != WindowFit::Never); });
    m_titleMode = new QComboBox;
    m_titleMode->addItem(tr("The application's name"), int(TitleMode::Application));
    m_titleMode->addItem(tr("The file name"), int(TitleMode::Name));
    m_titleMode->addItem(tr("Name, position and dimensions"), int(TitleMode::Details));
    m_titleMode->addItem(tr("Name, position, dimensions, file size and zoom"), int(TitleMode::Everything));
    windowForm->addRow(tr("Title bar:"), m_titleMode);
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
        combo->addItem(tr("Always"), int(OverlayVisibility::Always));
        combo->addItem(tr("When the pointer is at the top"), int(OverlayVisibility::Hover));
        combo->addItem(tr("Never"), int(OverlayVisibility::Hidden));
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
    m_overlayDelay = new QDoubleSpinBox;
    m_overlayDelay->setRange(Settings::kMinOverlayHideDelayMs / 1000.0, Settings::kMaxOverlayHideDelayMs / 1000.0);
    m_overlayDelay->setDecimals(1);
    m_overlayDelay->setSingleStep(0.5);
    //: Unit after a number of seconds; keep the leading space if your language separates units.
    m_overlayDelay->setSuffix(tr(" s"));
    overlayForm->addRow(tr("Hide after:"), m_overlayDelay);
    informationLayout->addWidget(overlayBox);
    // One look for both panels (D-49).
    auto *styleBox = new QGroupBox(tr("Appearance of the panel and the overlay"));
    auto *styleForm = new QFormLayout(styleBox);
    m_overlayBackground = new QSpinBox;
    m_overlayBackground->setRange(0, 100);
    m_overlayBackground->setSingleStep(10);
    //: Unit after a percentage; keep the leading space if your language separates it.
    m_overlayBackground->setSuffix(tr(" %"));
    styleForm->addRow(tr("Background opacity:"), m_overlayBackground);
    m_overlayText = new QSpinBox;
    m_overlayText->setRange(Settings::kMinOverlayTextOpacity, 100);
    m_overlayText->setSingleStep(10);
    m_overlayText->setSuffix(tr(" %"));
    styleForm->addRow(tr("Text opacity:"), m_overlayText);
    m_overlayOutline = new QCheckBox(tr("Outline the text"));
    styleForm->addRow(m_overlayOutline);
    informationLayout->addWidget(styleBox);
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
    m_sortBy->addItem(tr("Name"), int(FolderSort::Name));
    m_sortBy->addItem(tr("Date modified"), int(FolderSort::Modified));
    m_sortBy->addItem(tr("Size"), int(FolderSort::Size));
    m_sortDescending = new QCheckBox(tr("Descending"));
    auto *sortRow = new QHBoxLayout;
    sortRow->addWidget(m_sortBy);
    sortRow->addWidget(m_sortDescending);
    sortRow->addStretch();
    navigationForm->addRow(tr("Sort images by:"), sortRow);
    m_preload = new QCheckBox(tr("Load the next and previous images in advance"));
    navigationForm->addRow(m_preload);
    m_slideshowSeconds = new QSpinBox;
    m_slideshowSeconds->setRange(Settings::kMinSlideshowSeconds, Settings::kMaxSlideshowSeconds);
    //: Unit after a number of seconds; keep the leading space if your language separates units.
    m_slideshowSeconds->setSuffix(tr(" s"));
    //: %1: the key that starts and stops the slideshow, e.g. "S".
    navigationForm->addRow(tr("Slideshow (%1), time per image:").arg(QKeySequence(Qt::Key_S).toString(QKeySequence::NativeText)),
                           m_slideshowSeconds);
    tabs->addTab(navigation, tr("Navigation"));

    // Color & HDR
    auto *color = new QWidget;
    auto *colorForm = new QFormLayout(color);
    m_output = new QComboBox;
    m_output->addItem(tr("Automatic (HDR when the display supports it)"),
                      int(Renderer::OutputPreference::Automatic));
    m_output->addItem(tr("SDR (sRGB)"), int(Renderer::OutputPreference::Sdr));
    m_output->addItem(tr("HDR10 (PQ)"), int(Renderer::OutputPreference::Hdr10));
    colorForm->addRow(tr("Display output:"), m_output);
    m_toneMap = new QCheckBox(tr("Tone map HDR images that exceed the display (ITU-R BT.2390)"));
    colorForm->addRow(m_toneMap);
    //: "&&" is shown as a single "&".
    tabs->addTab(color, tr("Color && HDR"));

    auto *buttons = new QDialogButtonBox;
    QPushButton *ok = buttons->addButton(tr("OK"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    m_apply = buttons->addButton(tr("Apply"), QDialogButtonBox::ApplyRole);
    QPushButton *defaults = buttons->addButton(tr("Restore Defaults"), QDialogButtonBox::ResetRole);
    ok->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject); // keeps what Apply applied
    connect(m_apply, &QPushButton::clicked, this, &SettingsDialog::apply);
    connect(defaults, &QPushButton::clicked, this, [this] {
        // setValues() only touches what the dialog shows; settings() keeps the rest from m_initial.
        Settings fresh;
        fresh.language = m_language->currentData().toString(); // the language is not a "setting to reset"
        setValues(fresh);
    });

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);

    // Apply is enabled while the dialog shows anything other than what the viewer uses.
    // (Every widget is the dialog's descendant only from here on, through the layout.)
    for (QAbstractButton *button : findChildren<QAbstractButton *>())
        if (button->isCheckable())
            connect(button, &QAbstractButton::toggled, this, &SettingsDialog::updateApplyButton);
    for (QComboBox *combo : findChildren<QComboBox *>())
        connect(combo, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateApplyButton);
    for (QSpinBox *spin : findChildren<QSpinBox *>())
        connect(spin, &QSpinBox::valueChanged, this, &SettingsDialog::updateApplyButton);
    connect(m_overlayDelay, &QDoubleSpinBox::valueChanged, this, &SettingsDialog::updateApplyButton);
    connect(m_overlayFields, &QListWidget::itemChanged, this, &SettingsDialog::updateApplyButton);
    // Reordering: by dragging (rows moved) or with Move Up/Down (taken and inserted).
    QAbstractItemModel *fields = m_overlayFields->model();
    connect(fields, &QAbstractItemModel::rowsMoved, this, &SettingsDialog::updateApplyButton);
    connect(fields, &QAbstractItemModel::rowsInserted, this, &SettingsDialog::updateApplyButton);
}

void SettingsDialog::apply()
{
    const Settings values = settings();
    if (values == m_applied)
        return;
    m_applied = values;
    updateApplyButton();
    Q_EMIT applied(values);
}

void SettingsDialog::updateApplyButton()
{
    if (m_apply)
        m_apply->setEnabled(settings() != m_applied);
}

void SettingsDialog::changeEvent(QEvent *event)
{
    // Applying another language retranslates everything else at once; this dialog is rebuilt
    // after the click that applied it has returned (its button is among what gets replaced).
    if (event->type() == QEvent::LanguageChange && m_tabs && !m_rebuildQueued) {
        m_rebuildQueued = true;
        QMetaObject::invokeMethod(this, &SettingsDialog::rebuildUi, Qt::QueuedConnection);
    }
    QDialog::changeEvent(event);
}

void SettingsDialog::rebuildUi()
{
    m_rebuildQueued = false;
    const Settings shown = settings(); // including changes not applied yet
    const int tab = m_tabs->currentIndex();
    m_apply = nullptr; // updateApplyButton() is called while the new widgets are filled
    delete layout();
    delete m_backgroundGroup; // owned by the dialog, not by a widget
    qDeleteAll(findChildren<QWidget *>(Qt::FindDirectChildrenOnly));
    buildUi();
    setValues(shown);
    m_tabs->setCurrentIndex(tab);
    m_tabs->setFocus(); // the focused widget was replaced: keep the keyboard in the dialog
    updateApplyButton();
}

void SettingsDialog::setValues(const Settings &settings)
{
    m_language->setCurrentIndex(std::max(0, m_language->findData(settings.language)));
    m_confirmTrash->setChecked(settings.confirmTrash);
    m_reopenLast->setChecked(settings.reopenLastImage);
    setBackground(settings.background);
    m_checkerboard->setChecked(settings.checkerboard);
    m_rememberGeometry->setChecked(settings.rememberGeometry);
    m_fitMode->setCurrentIndex(std::max(0, m_fitMode->findData(int(settings.fitMode))));
    m_enlargeSmall->setChecked(settings.enlargeSmallImages);
    m_windowFit->setCurrentIndex(std::max(0, m_windowFit->findData(int(settings.windowFit))));
    m_windowFitPercent->setValue(settings.windowFitPercent);
    m_windowFitPercent->setEnabled(settings.windowFit != WindowFit::Never);
    m_titleMode->setCurrentIndex(std::max(0, m_titleMode->findData(int(settings.titleMode))));
    m_showInfo->setChecked(settings.showInfo);
    m_overlayFullScreen->setCurrentIndex(
        std::max(0, m_overlayFullScreen->findData(int(settings.overlayFullScreen))));
    m_overlayWindow->setCurrentIndex(
        std::max(0, m_overlayWindow->findData(int(settings.overlayWindow))));
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
    m_sortBy->setCurrentIndex(std::max(0, m_sortBy->findData(int(settings.sortBy))));
    m_sortDescending->setChecked(settings.sortDescending);
    m_preload->setChecked(settings.preload);
    m_slideshowSeconds->setValue(settings.slideshowSeconds);
    m_toneMap->setChecked(settings.toneMap);
    m_output->setCurrentIndex(std::max(0, m_output->findData(int(settings.output))));
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
    updateApplyButton(); // a new custom colour toggles no button
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
    s.fitMode = FitMode(m_fitMode->currentData().toInt());
    s.enlargeSmallImages = m_enlargeSmall->isChecked();
    s.windowFit = WindowFit(m_windowFit->currentData().toInt());
    s.windowFitPercent = m_windowFitPercent->value();
    s.titleMode = TitleMode(m_titleMode->currentData().toInt());
    s.showInfo = m_showInfo->isChecked();
    s.overlayFullScreen = OverlayVisibility(m_overlayFullScreen->currentData().toInt());
    s.overlayWindow = OverlayVisibility(m_overlayWindow->currentData().toInt());
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
    s.sortBy = FolderSort(m_sortBy->currentData().toInt());
    s.sortDescending = m_sortDescending->isChecked();
    s.preload = m_preload->isChecked();
    s.slideshowSeconds = m_slideshowSeconds->value();
    s.toneMap = m_toneMap->isChecked();
    s.output = Renderer::OutputPreference(m_output->currentData().toInt());
    return s;
}
