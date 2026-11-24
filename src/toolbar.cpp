#include "toolbar.h"

#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>

namespace {

// A disabled button's ink: about a third of the look's.
QColor disabled(QColor ink)
{
    ink.setAlphaF(ink.alphaF() * 0.35f);
    return ink;
}
constexpr double kMinColumn = 48.0;       // a named button's column, at least
constexpr double kNameWidth = 64.0;       // a name wraps at this width, or at its longest word

} // namespace

bool Toolbar::setButtons(const QList<Button> &buttons)
{
    if (buttons == m_buttons)
        return false;
    m_buttons = buttons;
    m_width = -1; // laid out again
    m_dirty = true;
    return true;
}

bool Toolbar::setLook(const Look &look)
{
    if (look == m_look)
        return false;
    m_look = look;
    m_width = -1;
    m_dirty = true;
    return true;
}

double Toolbar::nameHeight() const
{
    return m_look.names ? 2 * QFontMetricsF(m_look.font).lineSpacing() + 2 : 0.0;
}

QStringList Toolbar::nameLines(const QString &name, double width) const
{
    const QFontMetricsF metrics(m_look.font);
    const QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList lines;
    QString line;
    for (const QString &word : words) {
        const QString longer = line.isEmpty() ? word : line + QLatin1Char(' ') + word;
        if (line.isEmpty() || metrics.horizontalAdvance(longer) <= width) {
            line = longer;
            continue;
        }
        lines << line;
        line = word;
    }
    if (!line.isEmpty())
        lines << line;
    if (lines.size() > 2) // the rest on the second line, cut short
        lines = {lines.first(), lines.mid(1).join(QLatin1Char(' '))};
    for (QString &l : lines)
        l = metrics.elidedText(l, Qt::ElideRight, width);
    return lines;
}

double Toolbar::columnWidth(const Button &button) const
{
    if (button.separator)
        return kSeparator;
    if (!m_look.names || button.name.isEmpty())
        return kButton;
    // A name of one line when it is short; else two, as wide as its longest word needs.
    const QFontMetricsF metrics(m_look.font);
    double longestWord = 0;
    for (const QString &word : button.name.split(QLatin1Char(' '), Qt::SkipEmptyParts))
        longestWord = std::max(longestWord, metrics.horizontalAdvance(word));
    const double text = std::min(metrics.horizontalAdvance(button.name), std::max(kNameWidth, longestWord));
    return std::max(kMinColumn, std::ceil(text) + 12);
}

void Toolbar::layout(double width)
{
    if (width == m_width)
        return;
    m_width = width;
    m_dirty = true;

    // The buttons pinned at the end (the last group, D-115) stay on the bar; "…" takes the others.
    const qsizetype main = pinnedStart();
    // The width of the first `count` items, without separators at the ends, "…" after them when
    // `more`, then the pinned group after a separator of its own.
    const Button moreButton{kMore, Glyph::More, {}, true, false, false, {}};
    const auto build = [this, &moreButton, main](qsizetype count, bool more) {
        QList<Slot> columns;
        for (qsizetype i = 0; i < count; ++i) {
            const Button &b = m_buttons.at(i);
            if (b.separator && (columns.isEmpty() || columns.last().button.separator))
                continue; // none first, none twice
            columns.append({b});
        }
        while (!columns.isEmpty() && columns.last().button.separator)
            columns.removeLast();
        if (more)
            columns.append({moreButton});
        if (main < m_buttons.size() && !columns.isEmpty()) {
            Button separator;
            separator.separator = true;
            columns.append({separator});
        }
        for (qsizetype i = main; i < m_buttons.size(); ++i)
            if (!m_buttons.at(i).separator)
                columns.append({m_buttons.at(i)});
        double x = kPadding;
        for (qsizetype i = 0; i < columns.size(); ++i) {
            Slot &slot = columns[i];
            if (i > 0 && !slot.button.separator && !columns.at(i - 1).button.separator)
                x += kSpacing;
            slot.width = columnWidth(slot.button);
            slot.left = x + (slot.width - (slot.button.separator ? slot.width : kButton)) / 2;
            slot.from = x; // the column, for now; the areas that take the pointer below
            slot.to = x + slot.width;
            x += slot.width;
        }
        return std::pair{columns, x + kPadding};
    };

    auto [columns, total] = build(main, false);
    m_shownButtons = int(main);
    if (total > width) { // the most that fit with "…" after them; at least "…" alone
        for (qsizetype count = main - 1; count >= 0; --count) {
            std::tie(columns, total) = build(count, true);
            m_shownButtons = int(count);
            if (total <= width)
                break;
        }
    }
    if (std::none_of(columns.cbegin(), columns.cend(), [](const Slot &s) { return !s.button.separator; })) {
        m_slots.clear();
        m_size = QSizeF();
        return;
    }
    // The buttons' areas touch: each reaches halfway to its neighbours' columns, across the
    // spacing or the separator between them, and the first and last to the bar's ends.
    QList<qsizetype> buttons;
    for (qsizetype i = 0; i < columns.size(); ++i)
        if (!columns.at(i).button.separator)
            buttons << i;
    QList<double> boundaries{0.0}; // between each two neighbours, from their columns
    for (qsizetype k = 1; k < buttons.size(); ++k)
        boundaries << (columns.at(buttons.at(k - 1)).to + columns.at(buttons.at(k)).from) / 2;
    boundaries << total;
    for (qsizetype k = 0; k < buttons.size(); ++k) {
        columns[buttons.at(k)].from = boundaries.at(k);
        columns[buttons.at(k)].to = boundaries.at(k + 1);
    }
    m_slots = columns;
    m_size = QSizeF(total, 2 * kPadding + kButton + nameHeight());
    layoutMenu();
}

void Toolbar::layoutMenu()
{
    // A row: the glyph in a button's square, its name, its key at the right.
    m_menuDirty = true;
    const QList<Button> rows = overflow();
    if (rows.isEmpty()) { // nothing under "…": the panel closes, and stays closed when the window narrows again
        m_menuSize = QSizeF();
        m_menuOpen = false;
        m_menuHover = -1;
        return;
    }
    const QFontMetricsF metrics(m_look.menuFont);
    double names = 0, keys = 0;
    for (const Button &b : rows) {
        names = std::max(names, metrics.horizontalAdvance(b.name));
        keys = std::max(keys, metrics.horizontalAdvance(b.shortcut));
    }
    m_menuNameWidth = std::ceil(names);
    const double width = kPadding + kRow + 8 + m_menuNameWidth + (keys > 0 ? 24 + std::ceil(keys) : 0) + 12 + kPadding;
    m_menuSize = QSizeF(width, 2 * kPadding + rows.size() * kRow);
}

bool Toolbar::setMenuOpen(bool open)
{
    if (open == m_menuOpen)
        return false;
    m_menuOpen = open;
    m_menuHover = -1;
    m_menuDirty = true;
    m_dirty = true; // "…" shows the panel is open
    return true;
}

int Toolbar::menuRowAt(const QPointF &position) const
{
    const QSizeF size = menuSize();
    if (size.isEmpty() || position.x() < 0 || position.x() >= size.width() || position.y() < kPadding
        || position.y() >= size.height() - kPadding)
        return -1;
    const QList<Button> rows = overflow();
    const int row = int((position.y() - kPadding) / kRow);
    return row >= 0 && row < rows.size() ? rows.at(row).id : -1;
}

bool Toolbar::setMenuHover(int id)
{
    if (id == m_menuHover)
        return false;
    m_menuHover = id;
    m_menuDirty = true;
    return true;
}

QImage Toolbar::menuTexture()
{
    if (!menuOpen()) {
        m_menuTexture = QImage();
        return m_menuTexture;
    }
    if (!m_menuDirty && !m_menuTexture.isNull())
        return m_menuTexture;
    m_menuDirty = false;
    const qreal dpr = m_look.devicePixelRatio;
    m_menuTexture = QImage((m_menuSize * dpr).toSize(), QImage::Format_RGBA8888_Premultiplied);
    m_menuTexture.setDevicePixelRatio(dpr);
    m_menuTexture.fill(Qt::transparent);
    {
        QPainter painter(&m_menuTexture);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_look.panel);
        painter.drawRoundedRect(QRectF(QPointF(0, 0), m_menuSize), m_look.radius, m_look.radius);
        painter.setFont(m_look.menuFont);
        double y = kPadding;
        for (const Button &b : overflow()) {
            const QRectF square(kPadding, y, kRow, kRow);
            const QRectF disc(square.center() - QPointF(14, 14), QSizeF(28, 28));
            painter.setPen(Qt::NoPen);
            if (b.enabled && b.id == m_menuHover) { // the row under the pointer as the gallery's hovered tile
                painter.setBrush(m_look.hover);
                painter.drawRoundedRect(QRectF(kPadding, y + 1, m_menuSize.width() - 2 * kPadding, kRow - 2),
                                        m_look.radius / 2, m_look.radius / 2);
            }
            if (b.on) { // a switch that is on: as behind the gallery's current thumbnail
                painter.setBrush(m_look.on);
                painter.drawEllipse(disc);
            }
            const QColor ink = b.enabled ? m_look.ink : disabled(m_look.ink);
            painter.save();
            painter.translate(square.center() - QPointF(10, 10));
            painter.scale(20 / 24.0, 20 / 24.0); // a little smaller than the bar's, as a menu's icons are
            drawGlyph(painter, b.glyph, ink);
            painter.restore();
            const double textLeft = kPadding + kRow + 8;
            painter.setPen(ink);
            painter.drawText(QRectF(textLeft, y, m_menuNameWidth + 1, kRow), Qt::AlignLeft | Qt::AlignVCenter, b.name);
            if (!b.shortcut.isEmpty()) {
                // The panels' label grey (D-49), at the text's opacity.
                QColor key(190, 190, 190, m_look.ink.alpha());
                painter.setPen(b.enabled ? key : disabled(key));
                painter.drawText(QRectF(textLeft, y, m_menuSize.width() - kPadding - 12 - textLeft, kRow),
                                 Qt::AlignRight | Qt::AlignVCenter, b.shortcut);
            }
            y += kRow;
        }
    }
    return m_menuTexture;
}

qsizetype Toolbar::pinnedStart() const
{
    // The pinned buttons at the end, and the separators among and before them.
    qsizetype start = m_buttons.size();
    while (start > 0 && (m_buttons.at(start - 1).pinned || m_buttons.at(start - 1).separator))
        --start;
    while (start < m_buttons.size() && m_buttons.at(start).separator) // a separator before them belongs to no one
        ++start;
    return start;
}

QList<Toolbar::Button> Toolbar::overflow() const
{
    QList<Button> hidden;
    const qsizetype main = pinnedStart();
    for (qsizetype i = m_shownButtons; i < main; ++i)
        if (!m_buttons.at(i).separator)
            hidden << m_buttons.at(i);
    return hidden;
}

int Toolbar::buttonAt(const QPointF &position) const
{
    if (m_size.isEmpty() || position.y() < 0 || position.y() >= m_size.height())
        return -1;
    for (const Slot &slot : m_slots)
        if (!slot.button.separator && position.x() >= slot.from && position.x() < slot.to)
            return slot.button.id;
    return -1;
}

const Toolbar::Button *Toolbar::button(int id) const
{
    for (const Slot &slot : m_slots)
        if (!slot.button.separator && slot.button.id == id)
            return &slot.button;
    return nullptr;
}

QRectF Toolbar::buttonRect(int id) const
{
    for (const Slot &slot : m_slots)
        if (!slot.button.separator && slot.button.id == id)
            return QRectF(slot.left, kPadding, kButton, kButton);
    return {};
}

bool Toolbar::setHover(int id)
{
    if (id == m_hover)
        return false;
    m_hover = id;
    m_dirty = true;
    return true;
}

bool Toolbar::setPressed(int id)
{
    if (id == m_pressed)
        return false;
    m_pressed = id;
    m_dirty = true;
    return true;
}

void Toolbar::drawButton(QPainter &painter, const Slot &slot) const
{
    const Button &b = slot.button;
    const QRectF square(slot.left, kPadding, kButton, kButton);
    const QRectF disc(square.center() - QPointF(kDisc / 2, kDisc / 2), QSizeF(kDisc, kDisc));
    painter.setPen(Qt::NoPen);
    // The gallery's highlight (the owner, D-114): a switch that is on, a pressed button and "…"
    // while its panel shows as the current thumbnail; the button under the pointer as the hovered one.
    const bool open = b.id == kMore && menuOpen();
    const bool active = b.on || open || (b.enabled && b.id == m_pressed);
    if (active || (b.enabled && b.id == m_hover)) {
        painter.setBrush(active ? m_look.on : m_look.hover);
        painter.drawEllipse(disc);
    }
    const QColor glyph = b.enabled ? m_look.ink : disabled(m_look.ink);
    painter.save();
    painter.translate(square.center() - QPointF(kGlyph / 2, kGlyph / 2));
    painter.scale(kGlyph / 24.0, kGlyph / 24.0);
    drawGlyph(painter, b.glyph, glyph);
    painter.restore();
    if (m_look.names && !b.name.isEmpty()) {
        painter.setFont(m_look.font);
        painter.setPen(glyph);
        const QFontMetricsF metrics(m_look.font);
        const QStringList lines = nameLines(b.name, slot.width - 8);
        double y = kPadding + kButton - 2;
        for (const QString &line : lines) {
            painter.drawText(QRectF(slot.from, y, slot.to - slot.from, metrics.lineSpacing()), Qt::AlignHCenter | Qt::AlignTop, line);
            y += metrics.lineSpacing();
        }
    }
}

QImage Toolbar::texture()
{
    if (!m_dirty)
        return m_texture;
    m_dirty = false;
    if (m_size.isEmpty()) {
        m_texture = QImage();
        return m_texture;
    }
    const qreal dpr = m_look.devicePixelRatio;
    m_texture = QImage((m_size * dpr).toSize(), QImage::Format_RGBA8888_Premultiplied);
    m_texture.setDevicePixelRatio(dpr);
    m_texture.fill(Qt::transparent);
    {
        QPainter painter(&m_texture);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_look.panel);
        painter.drawRoundedRect(QRectF(QPointF(0, 0), m_size), m_look.radius, m_look.radius);
        for (const Slot &slot : m_slots) {
            if (slot.button.separator) {
                // One device pixel wide, on the pixel grid.
                const double x = (std::floor(slot.left * dpr + slot.width * dpr / 2) + 0.5) / dpr;
                QColor line = m_look.ink;
                line.setAlphaF(line.alphaF() * 0.3f);
                painter.setPen(QPen(line, 1.0 / dpr));
                painter.drawLine(QPointF(x, kPadding + 9), QPointF(x, kPadding + kButton - 9 + nameHeight()));
                continue;
            }
            drawButton(painter, slot);
        }
    } // the painting ends before the image is handed out
    return m_texture;
}

AutoHide::AutoHide(std::function<bool()> underPointer, QObject *parent)
    : QObject(parent), m_underPointer(std::move(underPointer))
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &AutoHide::timeout);
}

void AutoHide::configure(bool enabled, int delayMs)
{
    m_enabled = enabled;
    m_timer.setInterval(delayMs);
    show();
}

void AutoHide::pointerMoved()
{
    show();
}

void AutoHide::show()
{
    if (m_enabled)
        m_timer.start();
    else
        m_timer.stop();
    if (m_hidden) {
        m_hidden = false;
        Q_EMIT hiddenChanged(false);
    }
}

void AutoHide::timeout()
{
    if (m_underPointer && m_underPointer()) {
        m_timer.start(); // never from under the pointer
        return;
    }
    if (!m_hidden) {
        m_hidden = true;
        Q_EMIT hiddenChanged(true);
    }
}
