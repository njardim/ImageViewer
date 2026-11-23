// The Settings dialog (decision D-29): one tab per group of preferences; Apply and OK hand
// the values to the viewer, which applies them at once.
#include "settings.h"

#include "appearance.h"
#include "formats.h"

#include <QAction>
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
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>

namespace {
// Background presets, sRGB. The default (dark grey) keeps the image's own contrast readable.
const QColor kBackgroundPresets[] = {QColor(0x00, 0x00, 0x00), QColor(0x21, 0x21, 0x21), QColor(0x80, 0x80, 0x80),
                                     QColor(0xd0, 0xd0, 0xd0), QColor(0xff, 0xff, 0xff)};
// The gallery's highlight, sRGB (D-65): ruby, emerald (the default), sapphire (lighter than the
// stone's #0F52BA, which hardly shows on the black bar), gold and diamond.
const QColor kHighlightPresets[] = {QColor(224, 17, 95), QColor(80, 200, 120), QColor(30, 100, 220),
                                    QColor(255, 215, 0), QColor(235, 242, 255)};

// An extension, shown left to right and sorted without the isolation marks: with them, ".avif"
// + PDI sorted after ".avifs".
class ExtensionItem final : public QTableWidgetItem {
public:
    explicit ExtensionItem(const QString &suffix) : QTableWidgetItem(leftToRight(suffix))
    {
        setData(Qt::UserRole, suffix);
    }
    bool operator<(const QTableWidgetItem &other) const override
    {
        return data(Qt::UserRole).toString() < other.data(Qt::UserRole).toString();
    }
};

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

// Every number moves by one of the two steps of Settings (sizes by 5, times by half a second),
// and a typed value goes to the nearest one, as it is stored: a typed 22 becomes 20.
QSpinBox *sizeBox(int minimum, int maximum, const QString &suffix)
{
    auto *box = new QSpinBox;
    box->setRange(minimum, maximum);
    box->setSingleStep(Settings::kSizeStep);
    box->setSuffix(suffix);
    QObject::connect(box, &QSpinBox::editingFinished, box, [box] {
        box->setValue(int(std::lround(double(box->value()) / Settings::kSizeStep)) * Settings::kSizeStep);
    });
    return box;
}

QDoubleSpinBox *secondsBox(double minimum, double maximum)
{
    constexpr double step = Settings::kTimeStepMs / 1000.0;
    auto *box = new QDoubleSpinBox;
    box->setRange(minimum, maximum);
    box->setDecimals(1);
    box->setSingleStep(step);
    //: Unit after a number of seconds; keep the leading space if your language separates units.
    box->setSuffix(QCoreApplication::translate("SettingsDialog", " s"));
    QObject::connect(box, &QDoubleSpinBox::editingFinished, box,
                     [box, step] { box->setValue(std::round(box->value() / step) * step); });
    return box;
}

QString percent()
{
    //: Unit after a percentage; keep the leading space if your language separates it.
    return QCoreApplication::translate("SettingsDialog", " %");
}

QString pixels()
{
    //: Unit after a number of pixels; keep the leading space if your language separates units.
    return QCoreApplication::translate("SettingsDialog", " px");
}

} // namespace

SettingsDialog::SettingsDialog(const Settings &settings, std::function<QList<ShortcutCommand>()> commands)
    : m_initial(settings), m_commandsSource(std::move(commands))
{
    buildUi();
    m_tabs->setCurrentIndex(std::clamp(loadSettingsTab(), 0, m_tabs->count() - 1)); // where it was last closed
    setValues(settings);
    m_applied = this->settings(); // as the dialog shows them (normalised), so nothing reads as changed
    updateApplyButton();
    resize(sizeHint() + QSize(qRound(100 * fontScale()), 0)); // room for the longer labels of some languages, at every font size (D-89)
}

void SettingsDialog::buildUi()
{
    setWindowTitle(tr("Settings"));
    auto *tabs = new QTabWidget;
    tabs->setUsesScrollButtons(false); // every tab in sight: the dialog widens to fit them
    m_tabs = tabs;

    // General, in two groups (the owner, D-113, D-114): the language and the text; startup and files.
    auto *general = new QWidget;
    auto *generalLayout = new QVBoxLayout(general);
    auto *languageBox = new QGroupBox(tr("Language and text"));
    auto *languageForm = new QFormLayout(languageBox);
    m_language = new QComboBox;
    m_language->addItem(tr("System default"), QString());
    for (const UiLanguage &language : uiLanguages())
        m_language->addItem(language.nativeName, language.code);
    languageForm->addRow(tr("Language:"), m_language);
    auto *translationNote = new QLabel(tr("Languages other than English are machine translations "
                                          "awaiting review by native speakers."));
    translationNote->setWordWrap(true);
    translationNote->setForegroundRole(QPalette::PlaceholderText); // secondary, but still read by screen readers
    languageForm->addRow(QString(), translationNote);
    m_fontSize = new QComboBox; // D-85: the system's size, then 2 and 4 pixels more
    m_fontSize->addItem(tr("Small", "font size"), int(FontSize::Small));
    m_fontSize->addItem(tr("Medium", "font size"), int(FontSize::Medium));
    m_fontSize->addItem(tr("Large", "font size"), int(FontSize::Large));
    languageForm->addRow(tr("Font size:"), m_fontSize);
    generalLayout->addWidget(languageBox);
    auto *startupBox = new QGroupBox(tr("Startup and files"));
    auto *startupForm = new QFormLayout(startupBox);
    m_welcomeScreen = new QCheckBox(tr("Show the welcome screen at startup"));
    startupForm->addRow(m_welcomeScreen);
    m_reopenLast = new QCheckBox(tr("Reopen the last image at startup"));
    startupForm->addRow(m_reopenLast);
    m_confirmTrash = new QCheckBox(tr("Confirm before moving an image to the trash"));
    startupForm->addRow(m_confirmTrash);
    generalLayout->addWidget(startupBox);
    generalLayout->addStretch();
    tabs->addTab(general, tr("General"));

    // Window, in four groups (the owner, D-114): the colours; the zoom; the window; what is drawn over
    // the image, its opacity and its hiding, the one place for every element.
    auto *window = new QWidget;
    auto *windowLayout = new QVBoxLayout(window);
    auto *colorsBox = new QGroupBox(tr("Colors"));
    auto *colorsForm = new QFormLayout(colorsBox);
    const QString backgroundNames[] = {tr("Black"), tr("Dark gray"), tr("Gray"), tr("Light gray"), tr("White")};
    colorsForm->addRow(tr("Background:"), colorRow(m_background, kBackgroundPresets, backgroundNames,
                                                   int(std::size(kBackgroundPresets)), tr("Background Color")));
    // The interface's accent over the image: the gallery's highlight, a toolbar's button (D-112, D-114).
    const QString highlightNames[] = {tr("Ruby"), tr("Emerald"), tr("Sapphire"), tr("Gold"), tr("Diamond")};
    colorsForm->addRow(tr("Highlight color:"), colorRow(m_highlight, kHighlightPresets, highlightNames,
                                                        int(std::size(kHighlightPresets)), tr("Highlight Color")));
    m_checkerboard = new QCheckBox; // its text names the command's shortcut (refreshShortcutTexts)
    colorsForm->addRow(m_checkerboard);
    windowLayout->addWidget(colorsBox);
    // D-51: the zoom of a new image, the window's size, the title bar.
    auto *zoomBox = new QGroupBox(QStringLiteral("Zoom")); // a technical name, never translated (D-59)
    auto *zoomForm = new QFormLayout(zoomBox);
    m_fitMode = new QComboBox;
    m_fitMode->addItem(tr("Fit to the window"), int(FitMode::Window));
    m_fitMode->addItem(tr("Fit to the width"), int(FitMode::Width));
    m_fitMode->addItem(tr("Fit to the height"), int(FitMode::Height));
    m_fitMode->addItem(tr("Fill the window"), int(FitMode::Fill));
    m_fitMode->addItem(tr("Actual size (100 %)"), int(FitMode::Actual));
    zoomForm->addRow(tr("Zoom of a new image:"), m_fitMode);
    m_enlargeSmall = new QCheckBox(tr("Enlarge images smaller than the window"));
    zoomForm->addRow(m_enlargeSmall);
    m_lockZoom = new QCheckBox; // its text names the command's shortcut (refreshShortcutTexts)
    zoomForm->addRow(m_lockZoom);
    windowLayout->addWidget(zoomBox);
    //: The group of the window's own options: its size, its position, its title bar.
    auto *frameBox = new QGroupBox(tr("Window", "settings group"));
    auto *frameForm = new QFormLayout(frameBox);
    m_rememberGeometry = new QCheckBox(tr("Remember the window size and position"));
    frameForm->addRow(m_rememberGeometry);
    m_windowFit = new QComboBox;
    m_windowFit->addItem(tr("Keep its size"), int(WindowFit::Never));
    m_windowFit->addItem(tr("Fit it to the first image"), int(WindowFit::FirstImage));
    m_windowFit->addItem(tr("Fit it to every image"), int(WindowFit::EveryImage));
    frameForm->addRow(tr("Window size:"), m_windowFit);
    m_windowFitPercent = sizeBox(Settings::kMinWindowFitPercent, Settings::kMaxWindowFitPercent,
                                 //: Unit after a percentage of the screen's size; keep the leading space if your language separates it.
                                 tr(" % of the screen"));
    frameForm->addRow(tr("At most:"), m_windowFitPercent);
    connect(m_windowFit, &QComboBox::currentIndexChanged, this,
            [this] { m_windowFitPercent->setEnabled(WindowFit(m_windowFit->currentData().toInt()) != WindowFit::Never); });
    m_titleMode = new QComboBox;
    m_titleMode->addItem(tr("The file name"), int(TitleMode::Name));
    m_titleMode->addItem(tr("Name, position and dimensions"), int(TitleMode::Details));
    m_titleMode->addItem(tr("Name, position, dimensions, file size and zoom"), int(TitleMode::Everything));
    m_titleMode->addItem(tr("The same as the overlay at the top"), int(TitleMode::Overlay));
    frameForm->addRow(tr("Title bar:"), m_titleMode);
    windowLayout->addWidget(frameBox);
    // One opacity for every background and one for every text and icon over the image, and one
    // delay for everything that hides while the pointer is still (D-113, D-114).
    auto *overImageBox = new QGroupBox(tr("Over the image"));
    auto *overImageForm = new QFormLayout(overImageBox);
    m_backgroundOpacity = sizeBox(0, 100, percent());
    m_backgroundOpacity->setToolTip(tr("The panels, the overlay, the gallery, the toolbars and the side buttons"));
    overImageForm->addRow(tr("Background opacity:"), m_backgroundOpacity);
    m_textOpacity = sizeBox(Settings::kMinTextOpacity, 100, percent());
    overImageForm->addRow(tr("Text and icon opacity:"), m_textOpacity);
    m_pointerAutoHide = new QCheckBox(tr("Hide the pointer while it is still"));
    overImageForm->addRow(m_pointerAutoHide);
    m_autoHide = secondsBox(Settings::kMinAutoHideMs / 1000.0, Settings::kMaxAutoHideMs / 1000.0);
    m_autoHide->setToolTip(tr("For everything set to hide while the pointer is still: the bars, the gallery, "
                              "the overlay, the panel and the pointer."));
    overImageForm->addRow(tr("Auto-hide elements after:"), m_autoHide);
    windowLayout->addWidget(overImageBox);
    windowLayout->addStretch();
    tabs->addTab(window, tr("Window"));

    // Information (D-34): the detailed panel and the compact overlay at the top.
    auto *information = new QWidget;
    auto *informationLayout = new QVBoxLayout(information);
    m_showInfo = new QCheckBox; // its text names the command's shortcut (updateShortcutLabels)
    informationLayout->addWidget(m_showInfo);
    m_showOverlay = new QCheckBox; // likewise
    informationLayout->addWidget(m_showOverlay);
    auto *overlayBox = new QGroupBox(tr("Overlay at the top"));
    auto *overlayForm = new QFormLayout(overlayBox);
    // One choice for a window and full screen; never is the box above, and I (the owner, D-113).
    m_overlayVisibility = new QComboBox;
    m_overlayVisibility->addItem(tr("Always"), int(OverlayVisibility::Always));
    m_overlayVisibility->addItem(tr("When the pointer is at the top"), int(OverlayVisibility::Hover));
    overlayForm->addRow(tr("Display the overlay:"), m_overlayVisibility);
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
    informationLayout->addWidget(overlayBox);
    // One look for both panels (D-49).
    auto *styleBox = new QGroupBox(tr("Appearance of the panel and the overlay"));
    auto *styleForm = new QFormLayout(styleBox);
    m_overlayOutline = new QCheckBox(tr("Outline the text"));
    styleForm->addRow(m_overlayOutline);
    m_panelAutoHide = new QCheckBox(tr("Hide the information panel while the pointer is still"));
    styleForm->addRow(m_panelAutoHide);
    m_overlayAutoHide = new QCheckBox(tr("Hide the information overlay while the pointer is still"));
    styleForm->addRow(m_overlayAutoHide);
    informationLayout->addWidget(styleBox);
    informationLayout->addStretch();
    tabs->addTab(information, tr("Information"));

    // Navigation: the loop for everything that steps through the folder, then a group each for
    // moving between images, the slideshow and the gallery.
    auto *navigation = new QWidget;
    auto *navigationLayout = new QVBoxLayout(navigation);
    m_loop = new QCheckBox(tr("After the last image, continue with the first"));
    navigationLayout->addWidget(m_loop);
    auto *imagesBox = new QGroupBox(tr("Images"));
    auto *imagesForm = new QFormLayout(imagesBox);
    m_sideZones = new QCheckBox(tr("Click the left or right side of the window for the previous or next image"));
    imagesForm->addRow(m_sideZones);
    m_sideZoneWidth = sizeBox(Settings::kMinSideZoneWidth, Settings::kMaxSideZoneWidth, pixels());
    imagesForm->addRow(tr("Width of each side:"), m_sideZoneWidth);
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
    imagesForm->addRow(tr("Sort images by:"), sortRow);
    m_preload = new QCheckBox(tr("Load the next and previous images in advance"));
    imagesForm->addRow(m_preload);
    navigationLayout->addWidget(imagesBox);
    m_slideshowBox = new QGroupBox; // its title names the command's key (refreshShortcutTexts)
    auto *slideshowForm = new QFormLayout(m_slideshowBox);
    m_slideshowFullScreen = new QCheckBox(tr("Start the slideshow in full screen"));
    slideshowForm->addRow(m_slideshowFullScreen);
    m_slideshowSeconds = secondsBox(Settings::kMinSlideshowSeconds, Settings::kMaxSlideshowSeconds);
    slideshowForm->addRow(tr("Time per image:"), m_slideshowSeconds);
    m_toolbarAutoHide = new QCheckBox(tr("Hide the slideshow toolbar while the pointer is still"));
    slideshowForm->addRow(m_toolbarAutoHide);
    navigationLayout->addWidget(m_slideshowBox);
    // The gallery (D-60, D-61).
    m_galleryBox = new QGroupBox; // its title names the command's key (refreshShortcutTexts)
    auto *galleryForm = new QFormLayout(m_galleryBox);
    m_showGallery = new QCheckBox(tr("Show the gallery"));
    galleryForm->addRow(m_showGallery);
    m_galleryThumbnailSize = new QComboBox;
    const QString sizeNames[] = {tr("Small"), tr("Medium"), tr("Large")};
    for (std::size_t i = 0; i < Settings::kGalleryThumbnailSizes.size(); ++i)
        m_galleryThumbnailSize->addItem(sizeNames[i], Settings::kGalleryThumbnailSizes[i]);
    galleryForm->addRow(tr("Thumbnail size:"), m_galleryThumbnailSize);
    m_galleryZoom = new QCheckBox(tr("Enlarge the thumbnail under the pointer"));
    galleryForm->addRow(m_galleryZoom);
    m_galleryEnlargement = sizeBox(Settings::kMinGalleryEnlargement, Settings::kMaxGalleryEnlargement, pixels());
    galleryForm->addRow(tr("Enlargement:"), m_galleryEnlargement);
    connect(m_galleryZoom, &QCheckBox::toggled, m_galleryEnlargement, &QWidget::setEnabled);
    m_galleryAutoHide = new QCheckBox(tr("Hide the gallery while the pointer is still"));
    galleryForm->addRow(m_galleryAutoHide);

    navigationLayout->addWidget(m_galleryBox);
    navigationLayout->addStretch();
    tabs->addTab(navigation, tr("Navigation"));
    // After Navigation (the owner, 2026-11-18).
    tabs->addTab(buildToolbarTab(), tr("Toolbar"));

    // Color & HDR
    auto *color = new QWidget;
    auto *colorForm = new QFormLayout(color);
    m_output = new QComboBox;
    m_output->addItem(tr("Automatic (HDR when the display supports it)"),
                      int(Renderer::OutputPreference::Automatic));
    m_output->addItem(tr("SDR (sRGB)"), int(Renderer::OutputPreference::Sdr));
    m_output->addItem(tr("HDR10 (PQ)"), int(Renderer::OutputPreference::Hdr10));
    colorForm->addRow(tr("Display output:"), m_output);
    m_toneMap = new QCheckBox; // its text names the command's shortcut (refreshShortcutTexts)
    colorForm->addRow(m_toneMap);
    m_clipWarning = new QCheckBox; // likewise
    colorForm->addRow(m_clipWarning);
    //: "&&" is shown as a single "&".
    tabs->addTab(color, tr("Color && HDR"));

    // Image formats (D-58): what this build opens, straight from the format registry.
    auto *formatsPage = new QWidget;
    auto *formatsLayout = new QVBoxLayout(formatsPage);
    const QList<SuffixRow> suffixRows = supportedSuffixTable();
    //: %1: how many file name extensions the viewer opens, e.g. "134".
    auto *formatsTotal = new QLabel(tr("Total supported image formats: %1").arg(QLocale().toString(suffixRows.size())));
    formatsTotal->setAlignment(Qt::AlignTrailing | Qt::AlignVCenter);
    formatsLayout->addWidget(formatsTotal);
    // A search in every column but "#" (D-78), in the table's room: the dialog keeps its height.
    auto *formatsSearch = new QLineEdit;
    //: Placeholder of the field that filters the table of image formats.
    formatsSearch->setPlaceholderText(tr("Search extensions, formats and decoders"));
    formatsSearch->setClearButtonEnabled(true);
    // A magnifying glass at the field's start says "search" at a glance; a click on it goes to the field.
    QAction *searchMark = formatsSearch->addAction(searchIcon(formatsSearch->palette().color(QPalette::PlaceholderText)),
                                                   QLineEdit::LeadingPosition);
    searchMark->setText(tr("Search"));
    connect(searchMark, &QAction::triggered, formatsSearch, [formatsSearch] { formatsSearch->setFocus(); });
    auto *searchRow = new QHBoxLayout;
    auto *searchLabel = new QLabel(tr("Search:")); // names the field for screen readers too
    searchLabel->setBuddy(formatsSearch);
    searchRow->addWidget(searchLabel);
    searchRow->addWidget(formatsSearch, 1);
    formatsLayout->addLayout(searchRow);
    auto *formatsTable = new QTableWidget(int(suffixRows.size()), 5);
    // "#" is a symbol, the same in every language: it numbers the rows.
    formatsTable->setHorizontalHeaderLabels({QStringLiteral("#"), tr("Extension"), tr("Format"), tr("Decoder"), tr("Decoder version")});
    formatsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    formatsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    formatsTable->verticalHeader()->hide();
    formatsTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    for (int row = 0; row < suffixRows.size(); ++row) {
        const SuffixRow &r = suffixRows.at(row);
        auto *number = new QTableWidgetItem;
        number->setTextAlignment(Qt::AlignTrailing | Qt::AlignVCenter);
        formatsTable->setItem(row, 0, number);
        // Isolated left to right: in Arabic and Urdu ".jxl" read "jxl." (as the Shortcuts tab's keys).
        formatsTable->setItem(row, 1, new ExtensionItem(r.suffix));
        formatsTable->setItem(row, 2, new QTableWidgetItem(r.formats));
        formatsTable->setItem(row, 3, new QTableWidgetItem(r.decoder));
        formatsTable->setItem(row, 4, new QTableWidgetItem(r.version));
    }
    // "#" counts the rows as shown, 1 to the number shown, whatever the order and the search, and
    // is not a sort key: a click on it changes nothing. The table sorts itself here rather than
    // through the view's own sorting, which would sort again by "#" whenever its numbers change.
    // The search matches the extension without its isolation marks (".cr" finds ".cr2"), in any case.
    const auto renumber = [formatsTable, formatsSearch] {
        const QString text = formatsSearch->text().trimmed();
        int shown = 0;
        for (int row = 0; row < formatsTable->rowCount(); ++row) {
            bool match = text.isEmpty();
            for (int column = 1; column < formatsTable->columnCount() && !match; ++column) {
                const QTableWidgetItem *item = formatsTable->item(row, column);
                match = (column == 1 ? item->data(Qt::UserRole).toString() : item->text()).contains(text, Qt::CaseInsensitive);
            }
            formatsTable->setRowHidden(row, !match);
            if (match)
                formatsTable->item(row, 0)->setData(Qt::DisplayRole, ++shown);
        }
    };
    connect(formatsSearch, &QLineEdit::textChanged, formatsTable, renumber);
    QHeaderView *formatsHeader = formatsTable->horizontalHeader();
    formatsHeader->setSectionsClickable(true);
    formatsHeader->setSortIndicatorShown(true);
    formatsTable->sortItems(1, Qt::AscendingOrder); // by extension
    formatsHeader->setSortIndicator(1, Qt::AscendingOrder);
    renumber();
    auto sorted = std::make_shared<std::pair<int, Qt::SortOrder>>(1, Qt::AscendingOrder);
    connect(formatsHeader, &QHeaderView::sortIndicatorChanged, formatsTable,
            [formatsTable, formatsHeader, renumber, sorted](int column, Qt::SortOrder order) {
                if (column == 0) { // the header moved its indicator to "#": back where it was
                    const QSignalBlocker block(formatsHeader);
                    formatsHeader->setSortIndicator(sorted->first, sorted->second);
                    return;
                }
                *sorted = {column, order};
                formatsTable->sortItems(column, order);
                renumber();
            });
    // The table sizes its columns to their contents; Format and Decoder share what is left,
    // equally, so the two are as wide as each other and the widest.
    formatsHeader->setSectionResizeMode(QHeaderView::ResizeToContents);
    formatsHeader->setSectionResizeMode(2, QHeaderView::Stretch);
    formatsHeader->setSectionResizeMode(3, QHeaderView::Stretch);
    formatsLayout->addWidget(formatsTable, 1);
    tabs->addTab(formatsPage, tr("Image Formats"));
    tabs->addTab(buildShortcutsTab(), tr("Shortcuts"));

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
        // Every option the dialog shows, the language included; settings() keeps the rest.
        setValues(Settings());
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
    connect(m_toolbarButtons, &QListWidget::itemChanged, this, &SettingsDialog::updateApplyButton);
    QAbstractItemModel *buttonsModel = m_toolbarButtons->model();
    connect(buttonsModel, &QAbstractItemModel::rowsMoved, this, &SettingsDialog::updateApplyButton);
    connect(buttonsModel, &QAbstractItemModel::rowsInserted, this, &SettingsDialog::updateApplyButton);
    connect(buttonsModel, &QAbstractItemModel::rowsRemoved, this, &SettingsDialog::updateApplyButton);
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
    // Another font size applied (D-85): measured once the widget classes' fonts have changed too.
    if (event->type() == QEvent::FontChange && m_tabs)
        QMetaObject::invokeMethod(this, &SettingsDialog::fitToFont, Qt::QueuedConnection);
    QDialog::changeEvent(event);
}

void SettingsDialog::done(int result)
{
    saveSettingsTab(m_tabs->currentIndex());
    QDialog::done(result);
}

void SettingsDialog::rebuildUi()
{
    m_rebuildQueued = false;
    const Settings shown = settings(); // including changes not applied yet
    const int tab = m_tabs->currentIndex();
    m_apply = nullptr; // updateApplyButton() is called while the new widgets are filled
    delete layout();
    delete m_background.group; // owned by the dialog, not by a widget
    delete m_highlight.group;
    qDeleteAll(findChildren<QWidget *>(Qt::FindDirectChildrenOnly));
    buildUi();
    setValues(shown);
    m_tabs->setCurrentIndex(tab);
    m_tabs->setFocus(); // the focused widget was replaced: keep the keyboard in the dialog
    updateApplyButton();
}

void SettingsDialog::fitOverlayFields()
{
    // Every field visible at once: the list is short, and scrolling would hide the order.
    m_overlayFields->setMinimumHeight(m_overlayFields->sizeHintForRow(0) * m_overlayFields->count()
                                      + 2 * m_overlayFields->frameWidth() + 2);
}

void SettingsDialog::fitToFont()
{
    fitOverlayFields();
    resize(size().expandedTo(sizeHint() + QSize(qRound(100 * fontScale()), 0))); // as when opened
}

void SettingsDialog::setValues(const Settings &settings)
{
    m_language->setCurrentIndex(std::max(0, m_language->findData(settings.language)));
    m_fontSize->setCurrentIndex(std::max(0, m_fontSize->findData(int(settings.fontSize))));
    m_confirmTrash->setChecked(settings.confirmTrash);
    m_reopenLast->setChecked(settings.reopenLastImage);
    m_welcomeScreen->setChecked(settings.welcomeScreen);
    m_autoHide->setValue(settings.autoHideMs / 1000.0);
    setColor(m_highlight, settings.highlight);
    setColor(m_background, settings.background);
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
    m_showOverlay->setChecked(settings.showOverlay);
    m_overlayVisibility->setCurrentIndex(std::max(0, m_overlayVisibility->findData(int(settings.overlayVisibility))));
    m_overlayAutoHide->setChecked(settings.overlayAutoHide);
    m_panelAutoHide->setChecked(settings.panelAutoHide);
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
    fitOverlayFields();
    m_backgroundOpacity->setValue(settings.backgroundOpacity);
    m_textOpacity->setValue(settings.textOpacity);
    m_overlayOutline->setChecked(settings.overlayOutline);
    m_pointerAutoHide->setChecked(settings.pointerAutoHide);
    m_loop->setChecked(settings.loop);
    m_sideZones->setChecked(settings.sideZones);
    m_sideZoneWidth->setValue(settings.sideZoneWidth);
    m_sideZoneWidth->setEnabled(settings.sideZones);
    m_sortBy->setCurrentIndex(std::max(0, m_sortBy->findData(int(settings.sortBy))));
    m_sortDescending->setChecked(settings.sortDescending);
    m_preload->setChecked(settings.preload);
    m_slideshowFullScreen->setChecked(settings.slideshowFullScreen);
    m_showGallery->setChecked(settings.showGallery);
    m_galleryThumbnailSize->setCurrentIndex(std::max(0, m_galleryThumbnailSize->findData(settings.galleryThumbnailSize)));
    m_galleryZoom->setChecked(settings.galleryZoom);
    m_galleryEnlargement->setEnabled(settings.galleryZoom);
    m_galleryEnlargement->setValue(settings.galleryEnlargement);
    m_galleryAutoHide->setChecked(settings.galleryAutoHide);
    m_showToolbar->setChecked(settings.showToolbar);
    m_toolbarNames->setChecked(settings.toolbarNames);
    m_mainToolbarAutoHide->setChecked(settings.toolbarAutoHide);
    m_slideshowSeconds->setValue(settings.slideshowSeconds);
    m_toolbarAutoHide->setChecked(settings.slideshowToolbarAutoHide);
    m_toneMap->setChecked(settings.toneMap);
    m_clipWarning->setChecked(settings.clipWarning);
    m_output->setCurrentIndex(std::max(0, m_output->findData(int(settings.output))));
    m_shortcuts.clear();
    for (const ShortcutCommand &command : std::as_const(m_commands))
        m_shortcuts.insert(command.key, settings.shortcuts.value(command.key, command.defaults));
    m_shortcutNote->clear();
    fillShortcutTable();
    fillToolbarButtons(settings.toolbarButtons); // with m_commands, which the Shortcuts tab asked for
}

QLayout *SettingsDialog::colorRow(ColorChoice &choice, const QColor *presets, const QString *names, int count,
                                  const QString &dialogTitle)
{
    choice.presets = presets;
    choice.presetCount = count;
    choice.dialogTitle = dialogTitle;
    choice.group = new QButtonGroup(this);
    choice.group->setExclusive(true);
    auto *row = new QHBoxLayout;
    for (int i = 0; i < count; ++i) {
        auto *button = new QToolButton;
        button->setCheckable(true);
        button->setIcon(swatchIcon(presets[i]));
        button->setIconSize(QSize(28, 20));
        button->setToolTip(names[i]);
        button->setAccessibleName(names[i]);
        choice.group->addButton(button, i);
        row->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, &choice, i] { setColor(choice, choice.presets[i]); });
    }
    choice.custom = new QToolButton;
    choice.custom->setCheckable(true);
    choice.custom->setText(tr("Custom…"));
    choice.custom->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    choice.custom->setIconSize(QSize(28, 20));
    choice.group->addButton(choice.custom, count);
    row->addWidget(choice.custom);
    row->addStretch();
    connect(choice.custom, &QToolButton::clicked, this, [this, &choice] {
        const QColor chosen = QColorDialog::getColor(choice.color, this, choice.dialogTitle);
        setColor(choice, chosen.isValid() ? chosen.toRgb() : choice.color); // cancelled: restore the selection
    });
    return row;
}

void SettingsDialog::setColor(ColorChoice &choice, const QColor &color)
{
    choice.color = color;
    const QColor *end = choice.presets + choice.presetCount;
    const QColor *preset = std::find(choice.presets, end, color);
    if (preset != end) {
        choice.group->button(int(preset - choice.presets))->setChecked(true);
        choice.custom->setIcon(QIcon());
    } else {
        choice.custom->setChecked(true);
        choice.custom->setIcon(swatchIcon(color));
    }
    updateApplyButton(); // a new custom colour toggles no button
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
    s.fontSize = FontSize(m_fontSize->currentData().toInt());
    s.confirmTrash = m_confirmTrash->isChecked();
    s.reopenLastImage = m_reopenLast->isChecked();
    s.welcomeScreen = m_welcomeScreen->isChecked();
    s.autoHideMs = int(std::lround(m_autoHide->value() * 1000.0));
    s.highlight = m_highlight.color;
    s.background = m_background.color;
    s.checkerboard = m_checkerboard->isChecked();
    s.rememberGeometry = m_rememberGeometry->isChecked();
    s.fitMode = FitMode(m_fitMode->currentData().toInt());
    s.enlargeSmallImages = m_enlargeSmall->isChecked();
    s.lockZoom = m_lockZoom->isChecked();
    s.windowFit = WindowFit(m_windowFit->currentData().toInt());
    s.windowFitPercent = m_windowFitPercent->value();
    s.titleMode = TitleMode(m_titleMode->currentData().toInt());
    s.pointerAutoHide = m_pointerAutoHide->isChecked();
    s.showInfo = m_showInfo->isChecked();
    s.showOverlay = m_showOverlay->isChecked();
    s.overlayVisibility = OverlayVisibility(m_overlayVisibility->currentData().toInt());
    s.overlayAutoHide = m_overlayAutoHide->isChecked();
    s.panelAutoHide = m_panelAutoHide->isChecked();
    s.overlayFields.clear();
    for (int i = 0; i < m_overlayFields->count(); ++i) {
        const QListWidgetItem *item = m_overlayFields->item(i);
        if (item->checkState() == Qt::Checked)
            s.overlayFields.append(OverlayField(item->data(Qt::UserRole).toInt()));
    }
    s.backgroundOpacity = m_backgroundOpacity->value();
    s.textOpacity = m_textOpacity->value();
    s.overlayOutline = m_overlayOutline->isChecked();
    s.loop = m_loop->isChecked();
    s.sideZones = m_sideZones->isChecked();
    s.sideZoneWidth = m_sideZoneWidth->value();
    s.sortBy = FolderSort(m_sortBy->currentData().toInt());
    s.sortDescending = m_sortDescending->isChecked();
    s.preload = m_preload->isChecked();
    s.slideshowSeconds = m_slideshowSeconds->value();
    s.slideshowFullScreen = m_slideshowFullScreen->isChecked();
    s.slideshowToolbarAutoHide = m_toolbarAutoHide->isChecked();
    s.showGallery = m_showGallery->isChecked();
    s.galleryThumbnailSize = m_galleryThumbnailSize->currentData().toInt();
    s.galleryZoom = m_galleryZoom->isChecked();
    s.galleryEnlargement = m_galleryEnlargement->value();
    s.galleryAutoHide = m_galleryAutoHide->isChecked();
    s.showToolbar = m_showToolbar->isChecked();
    s.toolbarNames = m_toolbarNames->isChecked();
    s.toolbarAutoHide = m_mainToolbarAutoHide->isChecked();
    s.toolbarButtons = toolbarButtonsShown();
    s.toneMap = m_toneMap->isChecked();
    s.clipWarning = m_clipWarning->isChecked();
    s.output = Renderer::OutputPreference(m_output->currentData().toInt());
    s.shortcuts.clear(); // the commands of this version only (D-59)
    for (const ShortcutCommand &command : std::as_const(m_commands)) {
        const QList<QKeySequence> list = m_shortcuts.value(command.key, command.defaults);
        if (list != command.defaults)
            s.shortcuts.insert(command.key, list);
    }
    return s;
}
