// The Settings dialog's Toolbar tab (D-87, D-112): whether the toolbar shows, the names under its
// buttons, its hiding while the pointer is still, and its buttons: every command the toolbar can
// have, each with its glyph, those chosen first in their order with the separators between them,
// in the model of the overlay's fields (a box to choose, dragging or Move Up and Down to order).
#include "settings.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

namespace {

// Each item carries its command's key, or Settings::kToolbarSeparator.
constexpr int kKeyRole = Qt::UserRole;

bool isSeparator(const QListWidgetItem *item)
{
    return item && item->data(kKeyRole).toString() == Settings::kToolbarSeparator;
}

} // namespace

QWidget *SettingsDialog::buildToolbarTab(QDoubleSpinBox *hideAfter)
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    m_toolbarBox = new QGroupBox; // its title names the command's key (refreshShortcutTexts)
    auto *form = new QFormLayout(m_toolbarBox);
    m_showToolbar = new QCheckBox(tr("Show the toolbar"));
    form->addRow(m_showToolbar);
    m_toolbarNames = new QCheckBox(tr("Show the names under the buttons"));
    form->addRow(m_toolbarNames);
    m_mainToolbarAutoHide = new QCheckBox(tr("Hide the toolbar while the pointer is still"));
    form->addRow(m_mainToolbarAutoHide);
    m_mainToolbarHide = hideAfter;
    form->addRow(tr("Hide after:"), m_mainToolbarHide);
    connect(m_mainToolbarAutoHide, &QCheckBox::toggled, m_mainToolbarHide, &QWidget::setEnabled);
    layout->addWidget(m_toolbarBox);

    auto *buttonsBox = new QGroupBox(tr("Buttons"));
    auto *buttonsLayout = new QVBoxLayout(buttonsBox);
    m_toolbarButtons = new QListWidget;
    m_toolbarButtons->setDragDropMode(QAbstractItemView::InternalMove);
    m_toolbarButtons->setDefaultDropAction(Qt::MoveAction);
    m_toolbarButtons->setSelectionMode(QAbstractItemView::SingleSelection);
    m_toolbarButtons->setAccessibleName(tr("Buttons"));
    m_toolbarButtons->setIconSize(QSize(20, 20));
    auto *moveUp = new QPushButton(tr("Move Up"));
    auto *moveDown = new QPushButton(tr("Move Down"));
    auto *addSeparator = new QPushButton(tr("Add Separator"));
    auto *removeSeparator = new QPushButton(tr("Remove Separator"));
    auto *defaults = new QPushButton(tr("Default Buttons"));
    connect(moveUp, &QPushButton::clicked, this, [this] { moveToolbarButton(-1); });
    connect(moveDown, &QPushButton::clicked, this, [this] { moveToolbarButton(+1); });
    connect(addSeparator, &QPushButton::clicked, this, [this] {
        // After the selected button, or after the last chosen one.
        int row = m_toolbarButtons->currentRow();
        if (row < 0)
            for (int i = 0; i < m_toolbarButtons->count(); ++i)
                if (m_toolbarButtons->item(i)->checkState() == Qt::Checked || isSeparator(m_toolbarButtons->item(i)))
                    row = i;
        m_toolbarButtons->insertItem(row + 1, separatorItem());
        m_toolbarButtons->setCurrentRow(row + 1);
    });
    connect(removeSeparator, &QPushButton::clicked, this, [this] {
        if (isSeparator(m_toolbarButtons->currentItem())) {
            delete m_toolbarButtons->takeItem(m_toolbarButtons->currentRow());
            updateApplyButton();
        }
    });
    connect(m_toolbarButtons, &QListWidget::currentItemChanged, removeSeparator,
            [removeSeparator](QListWidgetItem *current) { removeSeparator->setEnabled(isSeparator(current)); });
    removeSeparator->setEnabled(false);
    connect(defaults, &QPushButton::clicked, this, [this] { fillToolbarButtons(Settings::defaultToolbarButtons()); });
    auto *side = new QVBoxLayout;
    for (QPushButton *button : {moveUp, moveDown, addSeparator, removeSeparator, defaults})
        side->addWidget(button);
    side->addStretch();
    auto *row = new QHBoxLayout;
    row->addWidget(m_toolbarButtons, 1);
    row->addLayout(side);
    buttonsLayout->addLayout(row);
    auto *note = new QLabel(tr("Quit, when chosen, always comes last, after a separator."));
    note->setWordWrap(true);
    note->setForegroundRole(QPalette::PlaceholderText); // secondary, but still read by screen readers
    buttonsLayout->addWidget(note);
    layout->addWidget(buttonsBox, 1);
    return page;
}

QListWidgetItem *SettingsDialog::separatorItem() const
{
    //: In the list of the toolbar's buttons: the line between two groups of buttons.
    auto *item = new QListWidgetItem(tr("Separator"));
    item->setData(kKeyRole, QString(Settings::kToolbarSeparator));
    item->setFlags((item->flags() | Qt::ItemIsDragEnabled) & ~(Qt::ItemIsDropEnabled | Qt::ItemIsUserCheckable));
    QFont font = item->font();
    font.setItalic(true);
    item->setFont(font);
    return item;
}

void SettingsDialog::fillToolbarButtons(const QStringList &buttons)
{
    // The chosen buttons first, in their order with their separators, then every other command the
    // toolbar can have, in the command table's order.
    const QColor ink = m_toolbarButtons->palette().color(QPalette::Text);
    m_toolbarButtons->clear();
    const auto add = [this, &ink](const ShortcutCommand &command, bool chosen) {
        auto *item = new QListWidgetItem(glyphIcon(*command.glyph, ink), command.buttonName);
        item->setData(kKeyRole, command.key);
        item->setFlags((item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled) & ~Qt::ItemIsDropEnabled);
        item->setCheckState(chosen ? Qt::Checked : Qt::Unchecked);
        m_toolbarButtons->addItem(item);
    };
    const QStringList shown = Settings::normalizedToolbar(buttons);
    for (const QString &key : shown) {
        if (key == Settings::kToolbarSeparator) {
            m_toolbarButtons->addItem(separatorItem());
            continue;
        }
        for (const ShortcutCommand &command : std::as_const(m_commands))
            if (command.key == key && command.glyph)
                add(command, true);
    }
    for (const ShortcutCommand &command : std::as_const(m_commands))
        if (command.glyph && !shown.contains(command.key))
            add(command, false);
    updateApplyButton();
}

QStringList SettingsDialog::toolbarButtonsShown() const
{
    QStringList buttons;
    for (int i = 0; i < m_toolbarButtons->count(); ++i) {
        const QListWidgetItem *item = m_toolbarButtons->item(i);
        if (isSeparator(item) || item->checkState() == Qt::Checked)
            buttons << item->data(kKeyRole).toString();
    }
    return Settings::normalizedToolbar(buttons);
}

void SettingsDialog::moveToolbarButton(int delta)
{
    const int row = m_toolbarButtons->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= m_toolbarButtons->count())
        return;
    QListWidgetItem *item = m_toolbarButtons->takeItem(row);
    m_toolbarButtons->insertItem(target, item);
    m_toolbarButtons->setCurrentRow(target);
}
