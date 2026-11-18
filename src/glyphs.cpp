#include "glyphs.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

constexpr double kStroke = 1.75; // the lines' width, grid units

QPen linePen(const QColor &colour, double width = kStroke)
{
    return QPen(colour, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

// A point at `radius` and `degrees` from `centre`: 0 to the right, 90 up, as Qt's arcs count.
QPointF polar(QPointF centre, double radius, double degrees)
{
    const double a = degrees * std::numbers::pi / 180.0;
    return {centre.x() + radius * std::cos(a), centre.y() - radius * std::sin(a)};
}

// An open arrowhead at `tip`, pointing towards `degrees`.
void arrowHead(QPainter &p, QPointF tip, double degrees, double length = 4.2, double spread = 42)
{
    QPainterPath head;
    head.moveTo(polar(tip, length, degrees + 180 - spread));
    head.lineTo(tip);
    head.lineTo(polar(tip, length, degrees + 180 + spread));
    p.drawPath(head);
}

// A double-headed arrow from `a` to `b`.
void doubleArrow(QPainter &p, QPointF a, QPointF b, double head = 3.2)
{
    p.drawLine(a, b);
    const double degrees = std::atan2(a.y() - b.y(), b.x() - a.x()) * 180.0 / std::numbers::pi;
    arrowHead(p, b, degrees, head, 45);
    arrowHead(p, a, degrees + 180, head, 45);
}

// A closed polygon's outline in dashes, each side starting and ending with one: the corners stay
// drawn, so a dashed shape reads as the same size as a solid one.
void dashedPolygon(QPainter &p, const QList<QPointF> &corners)
{
    constexpr double gap = 3.6; // between dashes, from one's end to the next one's start (caps included: 1.85)
    for (qsizetype i = 0; i < corners.size(); ++i) {
        const QPointF a = corners.at(i), b = corners.at((i + 1) % corners.size());
        const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
        const int dashes = std::max(2, int(std::lround((length + gap) / (1.0 + gap))));
        const double dash = std::max(0.0, (length - (dashes - 1) * gap) / dashes);
        const QPointF unit = (b - a) / length;
        for (int k = 0; k < dashes; ++k) {
            const double from = k * (dash + gap);
            p.drawLine(a + unit * from, a + unit * (from + dash));
        }
    }
}

// A screen on its stand, the slideshow's shape: its frame and stand.
void screen(QPainter &p)
{
    p.drawRoundedRect(QRectF(3, 3.5, 18, 12.5), 1.6, 1.6);
    p.drawLine(QPointF(12, 16), QPointF(12, 17.4));
    p.drawLine(QPointF(12, 17.4), QPointF(8.6, 20.6));
    p.drawLine(QPointF(12, 17.4), QPointF(15.4, 20.6));
}

// Hills inside `frame`, a picture's sign.
void hills(QPainter &p, const QRectF &frame)
{
    const double bottom = frame.bottom() - 1.8, left = frame.left() + 2.2, width = frame.width() - 4.4;
    QPainterPath m;
    m.moveTo(left, bottom);
    m.lineTo(left + width * 0.36, bottom - frame.height() * 0.42);
    m.lineTo(left + width * 0.58, bottom - frame.height() * 0.2);
    m.lineTo(left + width * 0.74, bottom - frame.height() * 0.34);
    m.lineTo(left + width, bottom);
    p.drawPath(m);
}

// A circle's turn: the upper arc solid, ending in its arrowhead, the rest dashed; clockwise as drawn.
void rotation(QPainter &p, const QColor &colour)
{
    const QPointF centre(12, 12.5);
    const double r = 7.5;
    const QRectF circle(centre.x() - r, centre.y() - r, 2 * r, 2 * r);
    QPainterPath solid;
    solid.arcMoveTo(circle, 200);
    solid.arcTo(circle, 200, -165); // over the top to 35°
    p.drawPath(solid);
    arrowHead(p, polar(centre, r, 35), 35 - 90 + 8); // along the turn, a little into the curve
    QPen dashes(colour, kStroke, Qt::CustomDashLine, Qt::RoundCap, Qt::RoundJoin);
    dashes.setDashPattern({1.3, 1.9});
    p.setPen(dashes);
    QPainterPath rest;
    rest.arcMoveTo(circle, 10);
    rest.arcTo(circle, 10, -150);
    p.drawPath(rest);
}

// A mirror's two halves: the image solid at the left, its reflection dashed at the right; as tall
// as the other glyphs (the owner: the axis they once had, 3 to 21).
void flip(QPainter &p)
{
    p.drawPolygon(QPolygonF({QPointF(10.75, 3), QPointF(2.5, 21), QPointF(10.75, 21)}));
    dashedPolygon(p, {QPointF(13.25, 3), QPointF(21.5, 21), QPointF(13.25, 21)});
}

// A closed folder.
QPainterPath folder()
{
    QPainterPath f;
    f.moveTo(3.5, 17.8);
    f.lineTo(3.5, 6.2);
    f.quadTo(3.5, 5, 4.7, 5);
    f.lineTo(8.8, 5);
    f.lineTo(10.8, 7.2);
    f.lineTo(19.3, 7.2);
    f.quadTo(20.5, 7.2, 20.5, 8.4);
    f.lineTo(20.5, 17.8);
    f.quadTo(20.5, 19, 19.3, 19);
    f.lineTo(4.7, 19);
    f.quadTo(3.5, 19, 3.5, 17.8);
    return f;
}

// Two sheets, the front one at the lower left: a copy.
void sheets(QPainter &p, const QRectF &front)
{
    QPainterPath back; // only what the front sheet does not cover
    back.moveTo(front.left() + 3.5, front.top() - 0.5);
    back.lineTo(front.left() + 3.5, front.top() - 2.3);
    back.quadTo(front.left() + 3.5, front.top() - 3.5, front.left() + 4.7, front.top() - 3.5);
    back.lineTo(front.right() + 2.3, front.top() - 3.5);
    back.quadTo(front.right() + 3.5, front.top() - 3.5, front.right() + 3.5, front.top() - 2.3);
    back.lineTo(front.right() + 3.5, front.bottom() - 4.7);
    back.quadTo(front.right() + 3.5, front.bottom() - 3.5, front.right() + 2.3, front.bottom() - 3.5);
    back.lineTo(front.right() + 0.5, front.bottom() - 3.5);
    p.drawPath(back);
    p.drawRoundedRect(front, 1.4, 1.4);
}

// A sun: a disc's outline and eight rays.
void sun(QPainter &p, QPointF centre, double radius)
{
    p.drawEllipse(centre, radius, radius);
    for (int i = 0; i < 8; ++i)
        p.drawLine(polar(centre, radius + 2.4, i * 45.0), polar(centre, radius + 4.2, i * 45.0));
}

void triangle(QPainter &p, double left, double right, double top, double bottom)
{
    p.drawPolygon(QPolygonF({QPointF(left, top), QPointF(right, (top + bottom) / 2), QPointF(left, bottom)}));
}

} // namespace

void drawGlyph(QPainter &p, Glyph glyph, const QColor &colour)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(linePen(colour));
    p.setBrush(Qt::NoBrush);
    // Mirrored glyphs are drawn as their other half: First as Last, Previous as Next, and so on.
    const auto mirror = [&p] {
        p.translate(24, 0);
        p.scale(-1, 1);
    };
    switch (glyph) {
    case Glyph::Open: { // an open folder
        QPainterPath back;
        back.moveTo(3.5, 18.5);
        back.lineTo(3.5, 6.2);
        back.quadTo(3.5, 5, 4.7, 5);
        back.lineTo(8.8, 5);
        back.lineTo(10.8, 7.2);
        back.lineTo(17.3, 7.2);
        back.quadTo(18.5, 7.2, 18.5, 8.4);
        back.lineTo(18.5, 10);
        p.drawPath(back);
        QPainterPath front;
        front.moveTo(3.5, 18.5);
        front.lineTo(6.2, 11.1);
        front.quadTo(6.6, 10, 7.8, 10);
        front.lineTo(20.6, 10);
        front.quadTo(21.8, 10, 21.4, 11.2);
        front.lineTo(19, 17.6);
        front.quadTo(18.7, 18.5, 17.6, 18.5);
        front.closeSubpath();
        p.drawPath(front);
        break;
    }
    case Glyph::ShowInFolder: // a folder, and a lens over it
        p.save();
        p.setClipRegion(QRegion(0, 0, 24, 24) - QRegion(12, 10, 12, 14, QRegion::Ellipse));
        p.drawPath(folder());
        p.restore();
        p.drawEllipse(QPointF(16.2, 15.6), 3.4, 3.4);
        p.drawLine(QPointF(18.7, 18.1), QPointF(21, 20.4));
        break;
    case Glyph::OpenWith: { // a box with an arrow out of it
        QPainterPath box;
        box.moveTo(10.5, 5);
        box.lineTo(5.6, 5);
        box.quadTo(4, 5, 4, 6.6);
        box.lineTo(4, 18.4);
        box.quadTo(4, 20, 5.6, 20);
        box.lineTo(17.4, 20);
        box.quadTo(19, 20, 19, 18.4);
        box.lineTo(19, 13.5);
        p.drawPath(box);
        p.drawLine(QPointF(11, 13), QPointF(20, 4));
        QPainterPath head;
        head.moveTo(14.5, 4);
        head.lineTo(20, 4);
        head.lineTo(20, 9.5);
        p.drawPath(head);
        break;
    }
    case Glyph::CopyImage: { // two sheets, a picture on the front one
        const QRectF front(3.5, 7.5, 13, 13);
        sheets(p, front);
        hills(p, front);
        break;
    }
    case Glyph::CopyPath: { // two sheets, lines of text on the front one
        const QRectF front(3.5, 7.5, 13, 13);
        sheets(p, front);
        p.drawLine(QPointF(6.5, 11.5), QPointF(13.5, 11.5));
        p.drawLine(QPointF(6.5, 14.5), QPointF(13.5, 14.5));
        p.drawLine(QPointF(6.5, 17.5), QPointF(10.5, 17.5));
        break;
    }
    case Glyph::Rename: { // a pencil on a line
        p.save();
        p.translate(12, 11);
        p.rotate(-45);
        QPainterPath pencil;
        pencil.moveTo(-9.5, 0);
        pencil.lineTo(-6.6, -2.4);
        pencil.lineTo(6.4, -2.4);
        pencil.quadTo(8.6, -2.4, 8.6, 0);
        pencil.quadTo(8.6, 2.4, 6.4, 2.4);
        pencil.lineTo(-6.6, 2.4);
        pencil.closeSubpath();
        p.drawPath(pencil);
        p.drawLine(QPointF(-6.6, -2.4), QPointF(-6.6, 2.4));
        p.drawLine(QPointF(4.2, -2.4), QPointF(4.2, 2.4));
        p.restore();
        p.drawLine(QPointF(11.5, 20.5), QPointF(20.5, 20.5));
        break;
    }
    case Glyph::MoveToTrash:
    case Glyph::DeletePermanently: { // a bin; a cross on it for good
        p.drawLine(QPointF(4, 6.5), QPointF(20, 6.5));
        QPainterPath lid;
        lid.moveTo(9, 6.5);
        lid.lineTo(9, 4.4);
        lid.lineTo(15, 4.4);
        lid.lineTo(15, 6.5);
        p.drawPath(lid);
        QPainterPath body;
        body.moveTo(6, 6.5);
        body.lineTo(7, 19.4);
        body.quadTo(7.1, 20.5, 8.2, 20.5);
        body.lineTo(15.8, 20.5);
        body.quadTo(16.9, 20.5, 17, 19.4);
        body.lineTo(18, 6.5);
        p.drawPath(body);
        if (glyph == Glyph::MoveToTrash) {
            p.drawLine(QPointF(10, 10), QPointF(10, 17));
            p.drawLine(QPointF(14, 10), QPointF(14, 17));
        } else {
            p.drawLine(QPointF(9.5, 10.5), QPointF(14.5, 16.5));
            p.drawLine(QPointF(14.5, 10.5), QPointF(9.5, 16.5));
        }
        break;
    }
    case Glyph::UndoTrash: { // an arrow that turns back
        QPainterPath back;
        back.moveTo(5, 9);
        back.lineTo(15, 9);
        back.cubicTo(18.6, 9, 20.5, 11.4, 20.5, 14);
        back.cubicTo(20.5, 16.6, 18.6, 19, 15, 19);
        back.lineTo(9.5, 19);
        p.drawPath(back);
        arrowHead(p, QPointF(5, 9), 180, 4.4, 45);
        break;
    }
    case Glyph::Settings: { // a gear: eight teeth, the hub's hole
        const QPointF o(12, 12);
        const double outer = 9.3, root = 6.9;
        QPainterPath gear;
        for (int i = 0; i < 8; ++i) {
            const double a = i * 45.0;
            const QPointF points[] = {polar(o, root, a - 13), polar(o, outer, a - 8.5), polar(o, outer, a + 8.5),
                                      polar(o, root, a + 13)};
            if (i == 0)
                gear.moveTo(points[0]);
            else
                gear.lineTo(points[0]);
            for (int k = 1; k < 4; ++k)
                gear.lineTo(points[k]);
        }
        gear.closeSubpath();
        p.drawPath(gear);
        p.drawEllipse(o, 3, 3);
        break;
    }
    case Glyph::Quit: { // the power sign
        const QRectF r(4.2, 4.7, 15.6, 15.6);
        QPainterPath arc;
        arc.arcMoveTo(r, 122);
        arc.arcTo(r, 122, 296);
        p.drawPath(arc);
        p.drawLine(QPointF(12, 3), QPointF(12, 11.5));
        break;
    }
    case Glyph::Next:
        mirror();
        [[fallthrough]];
    case Glyph::Previous: // an arrow
        p.drawLine(QPointF(4.5, 12), QPointF(19.5, 12));
        arrowHead(p, QPointF(4.5, 12), 180, 8.5, 45);
        break;
    case Glyph::Last:
        mirror();
        [[fallthrough]];
    case Glyph::First: { // a bar and a chevron
        p.drawLine(QPointF(6.5, 5.5), QPointF(6.5, 18.5));
        QPainterPath v;
        v.moveTo(16.5, 5.5);
        v.lineTo(10, 12);
        v.lineTo(16.5, 18.5);
        p.drawPath(v);
        break;
    }
    case Glyph::ZoomIn:
    case Glyph::ZoomOut: // a lens, with a plus or a minus
        p.drawEllipse(QPointF(10.5, 10.5), 6.8, 6.8);
        p.drawLine(QPointF(15.4, 15.4), QPointF(20.5, 20.5));
        p.drawLine(QPointF(7.6, 10.5), QPointF(13.4, 10.5));
        if (glyph == Glyph::ZoomIn)
            p.drawLine(QPointF(10.5, 7.6), QPointF(10.5, 13.4));
        break;
    case Glyph::Fit: // the window, the image to its size
        p.drawRoundedRect(QRectF(3, 4.5, 18, 15), 1.8, 1.8);
        doubleArrow(p, QPointF(7.4, 15.6), QPointF(16.6, 8.4));
        break;
    case Glyph::FitWidth:
        p.drawRoundedRect(QRectF(3, 4.5, 18, 15), 1.8, 1.8);
        doubleArrow(p, QPointF(6.6, 12), QPointF(17.4, 12));
        break;
    case Glyph::FitHeight:
        p.drawRoundedRect(QRectF(4.5, 3, 15, 18), 1.8, 1.8);
        doubleArrow(p, QPointF(12, 6.6), QPointF(12, 17.4));
        break;
    case Glyph::Fill: // the window, the image beyond its corners
        p.drawRoundedRect(QRectF(5.5, 7, 13, 10), 1.6, 1.6);
        for (const QPointF &corner : {QPointF(3, 4), QPointF(21, 4), QPointF(21, 20), QPointF(3, 20)}) {
            const QPointF inner(corner.x() < 12 ? corner.x() + 2.5 : corner.x() - 2.5,
                                corner.y() < 12 ? corner.y() + 2.4 : corner.y() - 2.4);
            p.drawLine(inner, corner);
            const double degrees = std::atan2(inner.y() - corner.y(), corner.x() - inner.x()) * 180.0 / std::numbers::pi;
            arrowHead(p, corner, degrees, 2.6, 50);
        }
        break;
    case Glyph::ActualSize: { // 1:1
        for (const double x : {7.6, 19.0}) {
            QPainterPath one;
            one.moveTo(x - 2.6, 8.6);
            one.lineTo(x, 6.5);
            one.lineTo(x, 17.5);
            p.drawPath(one);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        p.drawEllipse(QPointF(12.3, 9.5), 1.2, 1.2);
        p.drawEllipse(QPointF(12.3, 15), 1.2, 1.2);
        break;
    }
    case Glyph::LockZoom: { // a padlock
        QPainterPath shackle;
        shackle.moveTo(7.5, 11);
        shackle.lineTo(7.5, 8);
        shackle.cubicTo(7.5, 5.2, 9.5, 3.5, 12, 3.5);
        shackle.cubicTo(14.5, 3.5, 16.5, 5.2, 16.5, 8);
        shackle.lineTo(16.5, 11);
        p.drawPath(shackle);
        p.drawRoundedRect(QRectF(4.5, 11, 15, 10), 1.8, 1.8);
        p.drawLine(QPointF(12, 14.6), QPointF(12, 17.2));
        break;
    }
    case Glyph::FullScreen: { // a square left by an arrow
        QPainterPath outer;
        outer.moveTo(11, 3.5);
        outer.lineTo(5.8, 3.5);
        outer.quadTo(3.5, 3.5, 3.5, 5.8);
        outer.lineTo(3.5, 18.2);
        outer.quadTo(3.5, 20.5, 5.8, 20.5);
        outer.lineTo(18.2, 20.5);
        outer.quadTo(20.5, 20.5, 20.5, 18.2);
        outer.lineTo(20.5, 13);
        p.drawPath(outer);
        p.drawRoundedRect(QRectF(7, 12, 5, 5), 0.9, 0.9);
        p.drawLine(QPointF(12.5, 11.5), QPointF(20, 4));
        QPainterPath head;
        head.moveTo(14.8, 4);
        head.lineTo(20, 4);
        head.lineTo(20, 9.2);
        p.drawPath(head);
        break;
    }
    case Glyph::Minimize: // an arrow down to a bar
        p.drawLine(QPointF(12, 4), QPointF(12, 14.5));
        arrowHead(p, QPointF(12, 14.5), -90, 4.6, 45);
        p.drawLine(QPointF(5.5, 19.5), QPointF(18.5, 19.5));
        break;
    case Glyph::Info: // an i in a circle
        p.drawEllipse(QPointF(12, 12), 8.5, 8.5);
        p.drawLine(QPointF(12, 11), QPointF(12, 16.5));
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        p.drawEllipse(QPointF(12, 7.8), 1.15, 1.15);
        break;
    case Glyph::InfoOverlay: // a window, its top band with a line of text
        p.drawRoundedRect(QRectF(3, 4, 18, 16), 1.8, 1.8);
        p.drawLine(QPointF(3, 9), QPointF(21, 9));
        p.drawLine(QPointF(6, 6.5), QPointF(11, 6.5));
        break;
    case Glyph::Checkerboard: { // a square of three by three cells, every other one filled
        const QRectF box(4, 4, 16, 16);
        QPainterPath clip;
        clip.addRoundedRect(box, 2.2, 2.2);
        p.save();
        p.setClipPath(clip);
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        const double cell = box.width() / 3;
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x)
                if ((x + y) % 2 == 1)
                    p.drawRect(QRectF(box.left() + x * cell, box.top() + y * cell, cell, cell));
        p.restore();
        p.drawRoundedRect(box, 2.2, 2.2);
        break;
    }
    case Glyph::Gallery: { // a picture over three thumbnails
        const QRectF frame(3.5, 3, 17, 11.5);
        p.drawRoundedRect(frame, 1.6, 1.6);
        hills(p, frame);
        for (int i = 0; i < 3; ++i)
            p.drawRoundedRect(QRectF(3.5 + i * 6.5, 17.2, 4, 4), 0.9, 0.9);
        break;
    }
    case Glyph::RotateClockwise:
        rotation(p, colour);
        break;
    case Glyph::RotateCounterclockwise:
        mirror();
        rotation(p, colour);
        break;
    case Glyph::FlipHorizontal:
        flip(p);
        break;
    case Glyph::FlipVertical: // the same halves, the axis across: drawn transposed
        p.setTransform(QTransform(0, 1, 1, 0, 0, 0), true);
        flip(p);
        break;
    case Glyph::ExposureUp:
    case Glyph::ExposureDown:
        sun(p, QPointF(10, 14), 3.4);
        p.drawLine(QPointF(16.5, 5), QPointF(21.5, 5));
        if (glyph == Glyph::ExposureUp)
            p.drawLine(QPointF(19, 2.5), QPointF(19, 7.5));
        break;
    case Glyph::ExposureReset: // the sun alone: neither more nor less
        sun(p, QPointF(12, 12), 3.4);
        break;
    case Glyph::ToneMap: { // the axes and a curve that rolls off at the top (BT.2390)
        QPainterPath axes;
        axes.moveTo(4, 3.5);
        axes.lineTo(4, 20);
        axes.lineTo(20.5, 20);
        p.drawPath(axes);
        QPainterPath curve;
        curve.moveTo(4, 20);
        curve.lineTo(11, 11);
        curve.cubicTo(13.5, 7.8, 16, 6.6, 20.5, 6.2);
        p.drawPath(curve);
        break;
    }
    case Glyph::ClipWarning: { // a square in stripes, as a camera's zebra shows clipped highlights
        const QRectF box(4, 4, 16, 16);
        QPainterPath clip;
        clip.addRoundedRect(box, 2.2, 2.2);
        p.save();
        p.setClipPath(clip);
        for (const double d : {-10.0, -5.0, 0.0, 5.0, 10.0})
            p.drawLine(QPointF(4 + d, 20), QPointF(20 + d, 4));
        p.restore();
        p.drawRoundedRect(box, 2.2, 2.2);
        break;
    }
    case Glyph::PlayPause: // play and pause side by side
        triangle(p, 3.5, 11.5, 6, 18);
        p.drawLine(QPointF(15.5, 6.5), QPointF(15.5, 17.5));
        p.drawLine(QPointF(20, 6.5), QPointF(20, 17.5));
        break;
    case Glyph::NextFrame:
        mirror();
        [[fallthrough]];
    case Glyph::PreviousFrame: // a bar, and a step back to it
        p.drawLine(QPointF(6, 5.5), QPointF(6, 18.5));
        p.drawPolygon(QPolygonF({QPointF(18.5, 5.5), QPointF(9.5, 12), QPointF(18.5, 18.5)}));
        break;
    case Glyph::Slideshow: // a screen with play on it
        screen(p);
        triangle(p, 10.4, 14.6, 7.2, 12.3);
        break;
    case Glyph::SlideshowPicture: { // a screen with a picture on it
        screen(p);
        QPainterPath m;
        m.moveTo(6.5, 13);
        m.lineTo(10, 9.2);
        m.lineTo(12.6, 11.6);
        m.lineTo(14.6, 9.8);
        m.lineTo(17.5, 13);
        p.drawPath(m);
        break;
    }
    case Glyph::Pause:
        p.drawRoundedRect(QRectF(7, 5.5, 3.2, 13), 1, 1);
        p.drawRoundedRect(QRectF(13.8, 5.5, 3.2, 13), 1, 1);
        break;
    case Glyph::Play:
        triangle(p, 8, 19, 5.2, 18.8);
        break;
    case Glyph::Stop:
        p.drawRoundedRect(QRectF(6, 6, 12, 12), 1.6, 1.6);
        break;
    case Glyph::More:
        p.setPen(Qt::NoPen);
        p.setBrush(colour);
        for (const double x : {6.0, 12.0, 18.0})
            p.drawEllipse(QPointF(x, 12), 1.5, 1.5);
        break;
    }
    p.restore();
}

QIcon glyphIcon(Glyph glyph, const QColor &colour)
{
    QIcon icon;
    for (const int size : {16, 20, 24, 32}) {
        for (const qreal ratio : {1.0, 1.25, 1.5, 2.0, 3.0}) {
            QPixmap pixmap(QSize(size, size) * ratio);
            pixmap.setDevicePixelRatio(ratio);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.scale(size / 24.0, size / 24.0);
            drawGlyph(painter, glyph, colour);
            painter.end();
            icon.addPixmap(pixmap);
        }
    }
    return icon;
}
