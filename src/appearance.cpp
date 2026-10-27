// The interface's icons (decision D-86) and the user's font size (D-85). Our icons are the same on
// Windows, macOS and Linux; everything else keeps each system's own look: its widget style, its
// palette (light or dark, as the system says), its windows and dialogs, its typeface.
#include "appearance.h"

#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QProxyStyle>
#include <QScreen>
#include <QStyle>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <utility>

namespace {

const QColor kAccent(30, 100, 220); // the sapphire of the gallery's presets
const QColor kWarning(245, 166, 35);
const QColor kCritical(220, 53, 69);

// Drawn on a grid of `design` pixels, at each of `sizes` logical pixels and the usual device
// pixel ratios: sharp on any screen.
QIcon drawnIcon(int design, std::initializer_list<int> sizes, const std::function<void(QPainter &)> &draw)
{
    QIcon icon;
    for (const int size : sizes) {
        for (const qreal ratio : {1.0, 1.25, 1.5, 2.0, 3.0}) {
            QPixmap pixmap(QSize(size, size) * ratio);
            pixmap.setDevicePixelRatio(ratio);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.scale(qreal(size) / design, qreal(size) / design);
            draw(painter);
            painter.end();
            icon.addPixmap(pixmap);
        }
    }
    return icon;
}

QPen roundPen(const QColor &colour, qreal width)
{
    return QPen(colour, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

// The empty-field button of line edits: a cross cut out of a disc.
QIcon clearIcon(const QColor &colour)
{
    return drawnIcon(16, {16}, [colour](QPainter &p) {
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        p.drawEllipse(QPointF(8, 8), 6.5, 6.5);
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.setPen(roundPen(Qt::black, 1.6));
        p.drawLine(QPointF(5.4, 5.4), QPointF(10.6, 10.6));
        p.drawLine(QPointF(10.6, 5.4), QPointF(5.4, 10.6));
    });
}

// The message boxes' signs, in fixed colours that read on light and dark windows: 32 logical
// pixels in Windows' and Linux's boxes, 64 in macOS's.
QIcon questionIcon()
{
    return drawnIcon(32, {32, 64}, [](QPainter &p) {
        p.setPen(Qt::NoPen);
        p.setBrush(kAccent);
        p.drawEllipse(QPointF(16, 16), 15, 15);
        p.setPen(roundPen(Qt::white, 3));
        p.setBrush(Qt::NoBrush);
        QPainterPath mark;
        mark.arcMoveTo(QRectF(11, 7, 10, 10), 160);
        mark.arcTo(QRectF(11, 7, 10, 10), 160, -200); // over the top and down the right side
        mark.quadTo(QPointF(16, 16.5), QPointF(16, 19.5));
        p.drawPath(mark);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(16, 24.5), 1.9, 1.9);
    });
}

QIcon informationIcon()
{
    return drawnIcon(32, {32, 64}, [](QPainter &p) {
        p.setPen(Qt::NoPen);
        p.setBrush(kAccent);
        p.drawEllipse(QPointF(16, 16), 15, 15);
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(16, 9.5), 1.9, 1.9);
        p.setPen(roundPen(Qt::white, 3.2));
        p.drawLine(QPointF(16, 14.5), QPointF(16, 23.5));
    });
}

QIcon warningIcon()
{
    return drawnIcon(32, {32, 64}, [](QPainter &p) {
        // A wide round-joined pen of the fill's colour rounds the triangle's corners.
        p.setPen(roundPen(kWarning, 3));
        p.setBrush(kWarning);
        p.drawPolygon(QPolygonF({QPointF(16, 3.5), QPointF(29.5, 27.5), QPointF(2.5, 27.5)}));
        const QColor mark(40, 30, 10);
        p.setPen(roundPen(mark, 3.2));
        p.drawLine(QPointF(16, 11), QPointF(16, 19));
        p.setPen(Qt::NoPen);
        p.setBrush(mark);
        p.drawEllipse(QPointF(16, 23.5), 1.9, 1.9);
    });
}

QIcon criticalIcon()
{
    return drawnIcon(32, {32, 64}, [](QPainter &p) {
        p.setPen(Qt::NoPen);
        p.setBrush(kCritical);
        p.drawEllipse(QPointF(16, 16), 15, 15);
        p.setPen(roundPen(Qt::white, 3.2));
        p.drawLine(QPointF(11, 11), QPointF(21, 21));
        p.drawLine(QPointF(21, 11), QPointF(11, 21));
    });
}

QIcon messageIcon(QMessageBox::Icon icon)
{
    switch (icon) {
    case QMessageBox::Question: return questionIcon();
    case QMessageBox::Information: return informationIcon();
    case QMessageBox::Warning: return warningIcon();
    case QMessageBox::Critical: return criticalIcon();
    case QMessageBox::NoIcon: break;
    }
    return {};
}

// The system's own style (or the one named by -style or QT_STYLE_OVERRIDE), with our icons where
// it would take the system's.
class AppStyle final : public QProxyStyle {
public:
    QIcon standardIcon(StandardPixmap icon, const QStyleOption *option, const QWidget *widget) const override
    {
        switch (icon) {
        case SP_LineEditClearButton:
            return clearIcon((widget ? widget->palette() : QApplication::palette()).color(QPalette::PlaceholderText));
        case SP_MessageBoxQuestion: return messageIcon(QMessageBox::Question);
        case SP_MessageBoxInformation: return messageIcon(QMessageBox::Information);
        case SP_MessageBoxWarning: return messageIcon(QMessageBox::Warning);
        case SP_MessageBoxCritical: return messageIcon(QMessageBox::Critical);
        default: return QProxyStyle::standardIcon(icon, option, widget);
        }
    }

    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *data) const override
    {
        if (hint == SH_DialogButtonBox_ButtonsHaveIcons) // some Linux desktops put the system's icons on buttons
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, data);
    }
};

// The system's fonts: the application's, and those it gives some widget classes of their own
// (on macOS menus, tooltips, lists and table headers). The class names are those Qt fills from
// the system's theme `[code: qapplication.cpp initializeWidgetFontHash()]`.
struct SystemFonts {
    QFont application;
    QList<std::pair<QByteArray, QFont>> classes;
};

SystemFonts systemFonts()
{
    SystemFonts fonts{QApplication::font(), {}};
    for (const char *name : {"QMenu", "QMenuBar", "QMenuItem", "QMessageBox", "QLabel", "QTipLabel", "QTitleBar",
                             "QStatusBar", "QMdiSubWindowTitleBar", "QDockWidgetTitle", "QPushButton", "QCheckBox",
                             "QRadioButton", "QToolButton", "QAbstractItemView", "QListView", "QHeaderView",
                             "QListBox", "QComboMenuItem", "QComboLineEdit", "QSmallFont", "QMiniFont"}) {
        const QFont font = QApplication::font(name); // the application's where the class has none
        if (font != fonts.application)
            fonts.classes.append({QByteArray(name), font});
    }
    return fonts;
}

} // namespace

void applyAppearance(FontSize size)
{
    QApplication::setStyle(new AppStyle); // the application owns it
    // After the style: setting one gives back the system's fonts of the widget classes, which
    // the font size reads first.
    applyFontSize(size);
}

void applyFontSize(FontSize size)
{
    // Small is the system's own fonts, untouched; Medium and Large add 2 and 4 pixels (the
    // owner's choice, D-85) to each of them, on whatever unit the system gave it in.
    static const SystemFonts system = systemFonts(); // read before the first change
    static FontSize applied = FontSize::Small;
    if (size == applied)
        return;
    applied = size;
    const int extra = size == FontSize::Large ? 4 : size == FontSize::Medium ? 2 : 0;
    const QScreen *screen = QGuiApplication::primaryScreen();
    const qreal dpi = screen ? screen->logicalDotsPerInchY() : 96.0;
    const auto grown = [extra, dpi](QFont font) {
        if (font.pixelSize() > 0)
            font.setPixelSize(font.pixelSize() + extra);
        else
            font.setPointSizeF(font.pointSizeF() + extra * 72.0 / dpi);
        return font;
    };
    // The widgets', and the panels' over the image (QGuiApplication::font()); it clears the
    // classes' fonts, which follow.
    QApplication::setFont(grown(system.application));
    for (const auto &[name, font] : system.classes)
        QApplication::setFont(grown(font), name.constData());
}

void setMessageIcon(QMessageBox &box, QMessageBox::Icon icon)
{
#if defined(Q_OS_MACOS)
    // macOS's own box (NSAlert) shows the system's sign for a standard icon, and a picture as it is.
    const int size = box.style()->pixelMetric(QStyle::PM_MessageBoxIconSize, nullptr, &box);
    box.setIconPixmap(messageIcon(icon).pixmap(QSize(size, size), std::max(box.devicePixelRatio(), 2.0)));
#else
    // Qt's own box, whose sign comes from the style (AppStyle); the standard icon also keeps
    // Windows' sound for warnings and errors.
    box.setIcon(icon);
#endif
}

QIcon searchIcon(const QColor &colour)
{
    return drawnIcon(16, {16}, [colour](QPainter &p) {
        p.setPen(roundPen(colour, 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(6.75, 6.75), 4.75, 4.75);
        p.setPen(roundPen(colour, 2.1));
        p.drawLine(QPointF(10.4, 10.4), QPointF(14, 14));
    });
}
