// The interface's look (decision D-86): one widget style (Qt's Fusion) with our palette and our own
// icons on Windows, macOS and Linux, and the user's font size (D-85). The systems keep what does
// more than look: their file dialogs, window frames, button order and typeface (it carries the
// scripts of the 36 languages, which no single bundled font does).
#include "appearance.h"

#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QProxyStyle>
#include <QScreen>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

#include <functional>

namespace {

// The accent: the sapphire of the gallery's presets, on which white text keeps 5.4:1.
const QColor kAccent(30, 100, 220);
const QColor kWarning(245, 166, 35);
const QColor kCritical(220, 53, 69);

// Drawn in a square of `size` logical pixels at the usual device pixel ratios: sharp on any screen.
QIcon drawnIcon(int size, const std::function<void(QPainter &)> &draw)
{
    QIcon icon;
    for (const qreal ratio : {1.0, 1.25, 1.5, 2.0, 3.0}) {
        QPixmap pixmap(QSize(size, size) * ratio);
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        draw(painter);
        painter.end();
        icon.addPixmap(pixmap);
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
    return drawnIcon(16, [colour](QPainter &p) {
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        p.drawEllipse(QPointF(8, 8), 6.5, 6.5);
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.setPen(roundPen(Qt::black, 1.6));
        p.drawLine(QPointF(5.4, 5.4), QPointF(10.6, 10.6));
        p.drawLine(QPointF(10.6, 5.4), QPointF(5.4, 10.6));
    });
}

// The message boxes' signs, 32 logical pixels, in fixed colours that read on both palettes.
QIcon questionIcon()
{
    return drawnIcon(32, [](QPainter &p) {
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
    return drawnIcon(32, [](QPainter &p) {
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
    return drawnIcon(32, [](QPainter &p) {
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
    return drawnIcon(32, [](QPainter &p) {
        p.setPen(Qt::NoPen);
        p.setBrush(kCritical);
        p.drawEllipse(QPointF(16, 16), 15, 15);
        p.setPen(roundPen(Qt::white, 3.2));
        p.drawLine(QPointF(11, 11), QPointF(21, 21));
        p.drawLine(QPointF(21, 11), QPointF(11, 21));
    });
}

// Fusion draws every control; the icons it would take from the system are ours.
class AppStyle final : public QProxyStyle {
public:
    AppStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

    QIcon standardIcon(StandardPixmap icon, const QStyleOption *option, const QWidget *widget) const override
    {
        switch (icon) {
        case SP_LineEditClearButton:
            return clearIcon((widget ? widget->palette() : QApplication::palette()).color(QPalette::PlaceholderText));
        case SP_MessageBoxQuestion: return questionIcon();
        case SP_MessageBoxInformation: return informationIcon();
        case SP_MessageBoxWarning: return warningIcon();
        case SP_MessageBoxCritical: return criticalIcon();
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

QPalette palette(bool dark)
{
    // Neutral greys, the accent for selections and links; text keeps at least 4.5:1 on its ground.
    struct Greys {
        QColor window, base, alternate, button, text, placeholder, light, midlight, mid, darker, shadow,
            disabledText, link, visited;
    };
    const Greys g = dark ? Greys{{40, 40, 40}, {28, 28, 28}, {36, 36, 36}, {52, 52, 52}, {232, 232, 232},
                                 {150, 150, 150}, {75, 75, 75}, {62, 62, 62}, {45, 45, 45}, {22, 22, 22},
                                 {10, 10, 10}, {120, 120, 120}, {110, 165, 255}, {170, 140, 255}}
                         : Greys{{240, 240, 240}, {255, 255, 255}, {245, 245, 245}, {240, 240, 240}, {24, 24, 24},
                                 {115, 115, 115}, {255, 255, 255}, {227, 227, 227}, {190, 190, 190}, {160, 160, 160},
                                 {105, 105, 105}, {160, 160, 160}, kAccent, {110, 60, 190}};
    QPalette p;
    p.setColor(QPalette::Window, g.window);
    p.setColor(QPalette::WindowText, g.text);
    p.setColor(QPalette::Base, g.base);
    p.setColor(QPalette::AlternateBase, g.alternate);
    p.setColor(QPalette::Text, g.text);
    p.setColor(QPalette::PlaceholderText, g.placeholder);
    p.setColor(QPalette::Button, g.button);
    p.setColor(QPalette::ButtonText, g.text);
    p.setColor(QPalette::BrightText, dark ? QColor(255, 110, 110) : QColor(200, 0, 0));
    p.setColor(QPalette::ToolTipBase, g.button);
    p.setColor(QPalette::ToolTipText, g.text);
    p.setColor(QPalette::Light, g.light);
    p.setColor(QPalette::Midlight, g.midlight);
    p.setColor(QPalette::Mid, g.mid);
    p.setColor(QPalette::Dark, g.darker);
    p.setColor(QPalette::Shadow, g.shadow);
    p.setColor(QPalette::Highlight, kAccent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Accent, kAccent);
    p.setColor(QPalette::Link, g.link);
    p.setColor(QPalette::LinkVisited, g.visited);
    for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, g.disabledText);
    p.setColor(QPalette::Disabled, QPalette::Highlight, g.mid);
    return p;
}

void applyPalette()
{
    // A system that does not say (some Linux desktops) gets the light one, as its own controls are.
    QApplication::setPalette(palette(QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark));
}

} // namespace

void applyAppearance(FontSize size)
{
    QApplication::setStyle(new AppStyle); // the application owns it
    applyPalette();
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, qApp, &applyPalette);
    // After the style: setting one gives back the system's own fonts of some widget classes (on
    // macOS buttons, labels, lists, menus), which the font size then replaces with its own.
    applyFontSize(size);
}

void applyFontSize(FontSize size)
{
    // Small is the system's own size; Medium and Large add 2 and 4 pixels (the owner's choice,
    // D-85), on whatever unit the system gave it in. Every widget class takes it, as on the
    // systems that give them no fonts of their own.
    static const QFont system = QGuiApplication::font(); // read before the first change
    const int extra = size == FontSize::Large ? 4 : size == FontSize::Medium ? 2 : 0;
    QFont font = system;
    if (system.pixelSize() > 0) {
        font.setPixelSize(system.pixelSize() + extra);
    } else {
        const QScreen *screen = QGuiApplication::primaryScreen();
        const qreal dpi = screen ? screen->logicalDotsPerInchY() : 96.0;
        font.setPointSizeF(system.pointSizeF() + extra * 72.0 / dpi);
    }
    QApplication::setFont(font); // the widgets', and the panels' over the image (QGuiApplication::font())
}

QIcon searchIcon(const QColor &colour)
{
    return drawnIcon(16, [colour](QPainter &p) {
        p.setPen(roundPen(colour, 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(6.75, 6.75), 4.75, 4.75);
        p.setPen(roundPen(colour, 2.1));
        p.drawLine(QPointF(10.4, 10.4), QPointF(14, 14));
    });
}
