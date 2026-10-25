// The Settings dialog's Shortcuts tab (decision D-52): every command with a shortcut and an
// alternative, recorded in place; a shortcut assigned to one command is taken from the one that
// had it. Split from settingsdialog.cpp, past 800 lines (D-08, D-83).
#include "settings.h"

#include <QCheckBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

QString leftToRight(const QString &text)
{
    return QChar(0x2066) + text + QChar(0x2069);
}

namespace {
// A key sequence in a line of text: left to right even in Arabic or Urdu ("Ctrl+," not ",Ctrl+").
QString isolatedShortcut(const QKeySequence &sequence)
{
    return leftToRight(sequence.toString(QKeySequence::NativeText));
}
} // namespace

QWidget *SettingsDialog::buildShortcutsTab()
{
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
    // Sorted from either header; until then, in the menus' order (no column sorted: -1).
    m_shortcutTable->header()->setSortIndicator(-1, Qt::AscendingOrder);
    m_shortcutTable->setSortingEnabled(true);
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
    connect(m_shortcutTable, &QTreeWidget::currentItemChanged, this, [this] { showShortcutsOf(currentCommand()); });
    m_shortcutEdit->installEventFilter(this);
    m_alternativeEdit->installEventFilter(this);
    const auto edited = [this] {
        const int row = currentCommand();
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
        if (const int row = currentCommand(); row >= 0)
            setShortcuts(row, m_commands.at(row).defaults);
    });
    connect(allDefaults, &QPushButton::clicked, this, [this] {
        for (const ShortcutCommand &command : std::as_const(m_commands))
            m_shortcuts.insert(command.key, command.defaults);
        m_shortcutNote->clear();
        fillShortcutTable();
        updateApplyButton();
    });
    return shortcuts;
}

bool SettingsDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress && (watched == m_shortcutEdit || watched == m_alternativeEdit)) {
        m_keyInField = true;
        QTimer::singleShot(0, this, [this] { m_keyInField = false; }); // once the press is handled
    }
    return QDialog::eventFilter(watched, event);
}

int SettingsDialog::currentCommand() const
{
    // Rows can be sorted: each item carries its command's index in m_commands.
    const QTreeWidgetItem *item = m_shortcutTable->currentItem();
    return item ? item->data(0, Qt::UserRole).toInt() : -1;
}

void SettingsDialog::fillShortcutTable()
{
    const int command = std::max(0, currentCommand());
    m_shortcutTable->clear();
    QTreeWidgetItem *current = nullptr;
    for (int i = 0; i < m_commands.size(); ++i) {
        QString name = m_commands.at(i).name;
        name.remove(QLatin1Char('&')); // menu mnemonics; "&&" is not used in command names
        auto *item = new QTreeWidgetItem(m_shortcutTable, {name, QString()});
        item->setData(0, Qt::UserRole, i);
        if (i == command)
            current = item;
    }
    refreshShortcutTexts();
    if (current)
        m_shortcutTable->setCurrentItem(current);
    showShortcutsOf(current ? command : -1);
}

void SettingsDialog::refreshShortcutTexts()
{
    // The items first: sorted by the shortcuts' column, the table moves a row as soon as its text
    // changes, and walking it by index would then skip rows.
    QList<QTreeWidgetItem *> items;
    for (int i = 0; i < m_shortcutTable->topLevelItemCount(); ++i)
        items << m_shortcutTable->topLevelItem(i);
    for (QTreeWidgetItem *item : std::as_const(items)) {
        const int command = item->data(0, Qt::UserRole).toInt();
        if (command < 0 || command >= m_commands.size())
            continue;
        QStringList shown;
        for (const QKeySequence &sequence : m_shortcuts.value(m_commands.at(command).key))
            shown << isolatedShortcut(sequence);
        item->setText(1, shown.join(QStringLiteral(", ")));
    }
    // The labels that name a command's key name its first shortcut now, or none.
    const auto keyName = [this](const char *command) {
        const QList<QKeySequence> list = m_shortcuts.value(QString::fromLatin1(command));
        return list.isEmpty() ? QStringLiteral("—") : isolatedShortcut(list.first());
    };
    //: %1: the keyboard shortcut, e.g. "P".
    m_showInfo->setText(tr("Show the information panel (%1)").arg(keyName("Info")));
    //: %1: the keyboard shortcut, e.g. "I".
    m_showOverlay->setText(tr("Show the information overlay at the top (%1)").arg(keyName("InfoOverlay")));
    //: %1: the key that shows and hides the gallery, e.g. "G".
    m_galleryBox->setTitle(tr("Gallery (%1)").arg(keyName("Gallery")));
    //: %1: the key that starts and stops the slideshow, e.g. "S".
    m_slideshowBox->setTitle(tr("Slideshow (%1)").arg(keyName("Slideshow")));
    //: %1: the keyboard shortcut, e.g. "Z". The zoom of the image shown stays for the next ones.
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
