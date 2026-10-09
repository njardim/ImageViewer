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
#include <QHeaderView>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
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

// A time in seconds: every one in the Settings moves in half seconds.
QDoubleSpinBox *secondsBox(double minimum, double maximum)
{
    auto *box = new QDoubleSpinBox;
    box->setRange(minimum, maximum);
    box->setDecimals(1);
    box->setSingleStep(0.5);
    //: Unit after a number of seconds; keep the leading space if your language separates units.
    box->setSuffix(QCoreApplication::translate("SettingsDialog", " s"));
    return box;
}

} // namespace

SettingsDialog::SettingsDialog(const Settings &settings, std::function<QList<ShortcutCommand>()> commands)
    : m_initial(settings), m_commandsSource(std::move(commands))
{
    buildUi();
    setValues(settings);
    m_applied = this->settings(); // as the dialog shows them (normalised), so nothing reads as changed
    updateApplyButton();
    resize(sizeHint() + QSize(100, 0)); // room for the longer labels of some languages
}

void SettingsDialog::buildUi()
{
    setWindowTitle(tr("Settings"));
    auto *tabs = new QTabWidget;
    tabs->setUsesScrollButtons(false); // every tab in sight: the dialog widens to fit them
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
    m_lockZoom = new QCheckBox; // its text names the command's shortcut (refreshShortcutTexts)
    windowForm->addRow(m_lockZoom);
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
    m_pointerHide = secondsBox(0.0, Settings::kMaxPointerHideMs / 1000.0);
    //: Shown instead of 0 s: the pointer never hides by itself.
    m_pointerHide->setSpecialValueText(tr("Never"));
    windowForm->addRow(tr("Hide a still pointer after:"), m_pointerHide);
    tabs->addTab(window, tr("Window"));

    // Information (D-34): the detailed panel and the compact overlay at the top.
    auto *information = new QWidget;
    auto *informationLayout = new QVBoxLayout(information);
    m_showInfo = new QCheckBox; // its text names the command's shortcut (updateShortcutLabels)
    informationLayout->addWidget(m_showInfo);
    m_overlayBox = new QGroupBox;
    auto *overlayForm = new QFormLayout(m_overlayBox);
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
    m_overlayDelay = secondsBox(Settings::kMinOverlayHideDelayMs / 1000.0, Settings::kMaxOverlayHideDelayMs / 1000.0);
    overlayForm->addRow(tr("Hide after:"), m_overlayDelay);
    informationLayout->addWidget(m_overlayBox);
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
    m_slideshowSeconds = secondsBox(Settings::kMinSlideshowSeconds, Settings::kMaxSlideshowSeconds);
    m_slideshowLabel = new QLabel;
    navigationForm->addRow(m_slideshowLabel, m_slideshowSeconds);
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
    m_clipWarning = new QCheckBox(tr("Highlight pixels altered by clipping or tone mapping (magenta)"));
    colorForm->addRow(m_clipWarning);
    //: "&&" is shown as a single "&".
    tabs->addTab(color, tr("Color && HDR"));

    // Shortcuts (D-52): every command, with a shortcut and an alternative.
    m_commands = m_commandsSource();
    auto *shortcuts = new QWidget;
    auto *shortcutsLayout = new QVBoxLayout(shortcuts);
    m_shortcutTable = new QTreeWidget;
    m_shortcutTable->setColumnCount(2);
    m_shortcutTable->setHeaderLabels({tr("Command"), tr("Shortcuts")});
    m_shortcutTable->setRootIsDecorated(false);
    m_shortcutTable->setUniformRowHeights(true);
    m_shortcutTable->setAllColumnsShowFocus(true);
    m_shortcutTable->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    shortcutsLayout->addWidget(m_shortcutTable, 1);
    auto *editForm = new QFormLayout;
    const auto sequenceEdit = [] {
        auto *edit = new QKeySequenceEdit;
        edit->setMaximumSequenceLength(1); // one key with its modifiers, as the viewer matches them
        edit->setClearButtonEnabled(true);
        return edit;
    };
    m_shortcutEdit = sequenceEdit();
    m_alternativeEdit = sequenceEdit();
    editForm->addRow(tr("Shortcut:"), m_shortcutEdit);
    editForm->addRow(tr("Alternative:"), m_alternativeEdit);
    shortcutsLayout->addLayout(editForm);
    m_shortcutNote = new QLabel;
    m_shortcutNote->setWordWrap(true);
    shortcutsLayout->addWidget(m_shortcutNote);
    auto *shortcutButtons = new QHBoxLayout;
    m_shortcutDefault = new QPushButton(tr("Default for This Command"));
    auto *allDefaults = new QPushButton(tr("Defaults for All Commands"));
    shortcutButtons->addWidget(m_shortcutDefault);
    shortcutButtons->addWidget(allDefaults);
    shortcutButtons->addStretch();
    shortcutsLayout->addLayout(shortcutButtons);
    tabs->addTab(shortcuts, tr("Shortcuts"));
    connect(m_shortcutTable, &QTreeWidget::currentItemChanged, this,
            [this] { showShortcutsOf(m_shortcutTable->indexOfTopLevelItem(m_shortcutTable->currentItem())); });
    m_shortcutEdit->installEventFilter(this);
    m_alternativeEdit->installEventFilter(this);
    const auto edited = [this] {
        const int row = m_shortcutTable->indexOfTopLevelItem(m_shortcutTable->currentItem());
        if (row < 0 || row >= m_commands.size())
            return;
        const QList<QKeySequence> current = m_shortcuts.value(m_commands.at(row).key);
        QList<QKeySequence> list;
        // The third and later shortcuts (Previous Image has three) have no field: they stay.
        for (const QKeySequence &sequence : QList<QKeySequence>{m_shortcutEdit->keySequence(), m_alternativeEdit->keySequence()} + current.mid(2))
            if (!sequence.isEmpty()
                && std::none_of(list.cbegin(), list.cend(), [&sequence](const QKeySequence &s) { return sameShortcut(s, sequence); }))
                list.append(sequence);
        if (list != current) // leaving a field changes nothing
            setShortcuts(row, list);
    };
    // The clear button empties a field without editingFinished. A recording starts by emptying
    // it too, from the key press (eventFilter): that one waits for editingFinished, as showing
    // the shortcuts again in the field would fill it and drop the key being pressed.
    for (QKeySequenceEdit *edit : {m_shortcutEdit, m_alternativeEdit}) {
        connect(edit, &QKeySequenceEdit::editingFinished, this, edited);
        connect(edit, &QKeySequenceEdit::keySequenceChanged, this, [this, edited](const QKeySequence &s) {
            if (s.isEmpty() && !m_keyInField)
                edited();
        });
    }
    connect(m_shortcutDefault, &QPushButton::clicked, this, [this] {
        const int row = m_shortcutTable->indexOfTopLevelItem(m_shortcutTable->currentItem());
        if (row >= 0)
            setShortcuts(row, m_commands.at(row).defaults);
    });
    connect(allDefaults, &QPushButton::clicked, this, [this] {
        for (const ShortcutCommand &command : std::as_const(m_commands))
            m_shortcuts.insert(command.key, command.defaults);
        m_shortcutNote->clear();
        fillShortcutTable();
        updateApplyButton();
    });

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
    for (QDoubleSpinBox *spin : findChildren<QDoubleSpinBox *>())
        connect(spin, &QDoubleSpinBox::valueChanged, this, &SettingsDialog::updateApplyButton);
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
    m_lockZoom->setChecked(settings.lockZoom);
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
    m_pointerHide->setValue(settings.pointerHideMs / 1000.0);
    m_loop->setChecked(settings.loop);
    m_sideZones->setChecked(settings.sideZones);
    m_sideZoneWidth->setValue(settings.sideZoneWidth);
    m_sideZoneWidth->setEnabled(settings.sideZones);
    m_sortBy->setCurrentIndex(std::max(0, m_sortBy->findData(int(settings.sortBy))));
    m_sortDescending->setChecked(settings.sortDescending);
    m_preload->setChecked(settings.preload);
    m_slideshowSeconds->setValue(settings.slideshowSeconds);
    m_toneMap->setChecked(settings.toneMap);
    m_clipWarning->setChecked(settings.clipWarning);
    m_output->setCurrentIndex(std::max(0, m_output->findData(int(settings.output))));
    m_shortcuts.clear();
    for (const ShortcutCommand &command : std::as_const(m_commands))
        m_shortcuts.insert(command.key, settings.shortcuts.value(command.key, command.defaults));
    m_shortcutNote->clear();
    fillShortcutTable();
}

bool SettingsDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress && (watched == m_shortcutEdit || watched == m_alternativeEdit)) {
        m_keyInField = true;
        QTimer::singleShot(0, this, [this] { m_keyInField = false; }); // once the press is handled
    }
    return QDialog::eventFilter(watched, event);
}

namespace {
// A key sequence in a line of text: left to right even in Arabic or Urdu ("Ctrl+," not ",Ctrl+").
QString isolatedShortcut(const QKeySequence &sequence)
{
    return QChar(0x2066) + sequence.toString(QKeySequence::NativeText) + QChar(0x2069); // LRI … PDI
}
} // namespace

void SettingsDialog::fillShortcutTable()
{
    const int row = std::max(0, m_shortcutTable->indexOfTopLevelItem(m_shortcutTable->currentItem()));
    m_shortcutTable->clear();
    for (const ShortcutCommand &command : std::as_const(m_commands)) {
        QString name = command.name;
        name.remove(QLatin1Char('&')); // menu mnemonics; "&&" is not used in command names
        new QTreeWidgetItem(m_shortcutTable, {name, QString()});
    }
    refreshShortcutTexts();
    if (row < m_shortcutTable->topLevelItemCount())
        m_shortcutTable->setCurrentItem(m_shortcutTable->topLevelItem(row));
    showShortcutsOf(row);
}

void SettingsDialog::refreshShortcutTexts()
{
    for (int i = 0; i < m_commands.size() && i < m_shortcutTable->topLevelItemCount(); ++i) {
        QStringList shown;
        for (const QKeySequence &sequence : m_shortcuts.value(m_commands.at(i).key))
            shown << isolatedShortcut(sequence);
        m_shortcutTable->topLevelItem(i)->setText(1, shown.join(QStringLiteral(", ")));
    }
    // The labels that name a command's key name its first shortcut now, or none.
    const auto keyName = [this](const char *command) {
        const QList<QKeySequence> list = m_shortcuts.value(QString::fromLatin1(command));
        return list.isEmpty() ? QStringLiteral("—") : isolatedShortcut(list.first());
    };
    //: %1: the keyboard shortcut, e.g. "I".
    m_showInfo->setText(tr("Show the information panel (%1)").arg(keyName("Info")));
    //: %1: the keyboard shortcut, e.g. "Shift+I".
    m_overlayBox->setTitle(tr("Overlay at the top (%1)").arg(keyName("InfoOverlay")));
    //: %1: the key that starts and stops the slideshow, e.g. "S".
    m_slideshowLabel->setText(tr("Slideshow (%1), time per image:").arg(keyName("Slideshow")));
    //: %1: the keyboard shortcut, e.g. "L". The zoom of the image shown stays for the next ones.
    m_lockZoom->setText(tr("Keep the zoom for the next images (%1)").arg(keyName("LockZoom")));
}

void SettingsDialog::showShortcutsOf(int row)
{
    const bool valid = row >= 0 && row < m_commands.size();
    const QList<QKeySequence> list = valid ? m_shortcuts.value(m_commands.at(row).key) : QList<QKeySequence>();
    for (QKeySequenceEdit *edit : {m_shortcutEdit, m_alternativeEdit}) {
        const QSignalBlocker block(edit);
        edit->setEnabled(valid);
        edit->setKeySequence(list.value(edit == m_shortcutEdit ? 0 : 1));
    }
    m_shortcutDefault->setEnabled(valid);
}

void SettingsDialog::setShortcuts(int row, QList<QKeySequence> shortcuts)
{
    if (row < 0 || row >= m_commands.size())
        return;
    // A shortcut does one thing: assigned here, it is taken from the command that had it, by the
    // rule the viewer matches keys with ("Ctrl+Shift+=" is "Ctrl+=").
    QStringList takenFrom;
    for (int i = 0; i < m_commands.size(); ++i) {
        if (i == row)
            continue;
        QList<QKeySequence> &other = m_shortcuts[m_commands.at(i).key];
        const auto assigned = [&shortcuts](const QKeySequence &s) {
            return std::any_of(shortcuts.cbegin(), shortcuts.cend(), [&s](const QKeySequence &t) { return sameShortcut(s, t); });
        };
        if (other.removeIf(assigned) > 0) {
            QString name = m_commands.at(i).name;
            takenFrom << name.remove(QLatin1Char('&'));
        }
    }
    m_shortcuts.insert(m_commands.at(row).key, shortcuts);
    //: %1: names of commands, e.g. "Zoom In"; their shortcut now belongs to the selected command.
    m_shortcutNote->setText(takenFrom.isEmpty() ? QString()
                                                : tr("Taken from: %1").arg(takenFrom.join(QStringLiteral(", "))));
    refreshShortcutTexts();
    showShortcutsOf(row); // the fields follow the list (a cleared shortcut gives its place to the next)
    updateApplyButton();
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
    s.lockZoom = m_lockZoom->isChecked();
    s.windowFit = WindowFit(m_windowFit->currentData().toInt());
    s.windowFitPercent = m_windowFitPercent->value();
    s.titleMode = TitleMode(m_titleMode->currentData().toInt());
    s.pointerHideMs = int(std::lround(m_pointerHide->value() * 1000.0));
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
    s.clipWarning = m_clipWarning->isChecked();
    s.output = Renderer::OutputPreference(m_output->currentData().toInt());
    // Kept: the shortcuts of commands this version does not have (another version wrote them).
    s.shortcuts.removeIf([this](const auto &entry) {
        return std::any_of(m_commands.cbegin(), m_commands.cend(), [&entry](const ShortcutCommand &c) { return c.key == entry.key(); });
    });
    for (const ShortcutCommand &command : std::as_const(m_commands)) {
        const QList<QKeySequence> list = m_shortcuts.value(command.key, command.defaults);
        if (list != command.defaults)
            s.shortcuts.insert(command.key, list);
    }
    return s;
}
