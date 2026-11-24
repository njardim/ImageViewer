// The color picker (D-115): a mode in which a click on the image picks the colour of the pixel under
// the pointer, and a panel over the image, above the information panel, that shows it. On the
// panel's left a loupe of the pixels around the pointer, its centre framed, and under it the colour
// under the pointer and the colour picked side by side (the owner, D-116); the renderer draws those three from the image itself, through
// the output stage (the panel leaves holes for them), so that they show the exact colour, HDR and
// wide gamut included; where there is no image, the checkerboard (the owner). On the right the
// picked colour's values, each copied by a click: in sRGB (HEX, RGB, HSL, HSV, CMYK, clipped to it),
// as CIE Lab, as the file stores it (read from the file's own samples, FileSamples in image.h: the
// working buffer's half floats cannot give every wide-gamut colour back exactly), and in cd/m² for HDR.
//
// This class holds the picked colour, writes its values and draws the panel; the window samples the
// pixels, places the panel and runs the copies (input.cpp), so the unit tests drive it alone
// (tests/unit_tests.cpp, D-102).
#pragma once

#include "color.h"

#include <QColor>
#include <QFont>
#include <QImage>
#include <QList>
#include <QLocale>
#include <QPoint>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <optional>

class ColorPicker {
public:
    // A pixel of the image and its colour.
    struct Sample {
        QPoint position;          // in the image's own pixels, as oriented on screen before the view's turns
        float rgb[3] = {0, 0, 0}; // linear scRGB with straight alpha: 1.0 is SDR white
        float alpha = 1.0f;
    };
    // What the values need to know of the image the sample came from.
    struct Source {
        color::Descriptor colour;
        bool cmyk = false;
        bool gray = false;
        bool isFloat = false;
        int bits = 8;          // of an integer source
        bool hasAlpha = false;
        bool hdr = false;      // its values reach above SDR white: the luminance is given in cd/m²
        bool firstFrame = true; // an animation's other frames: the file's value is not read for them
    };
    // The values the file stores at the pixel: its colour's, then its alpha's.
    struct FileValue {
        QList<double> samples;
        int bits = 8;          // integer codes of so many bits
        bool isFloat = false;
    };
    // A value: its label, the text shown, and what a click copies (CSS syntax where there is one).
    struct Row {
        QString label;
        QString text;
        QString copy;
    };
    struct Look {
        qreal devicePixelRatio = 1.0;
        QColor panel;  // the panels' background
        QColor ink;    // the values
        QColor label;  // the labels and the hint
        QColor hover;  // behind the value under the pointer (the gallery's hovered tile)
        QFont font;
        double radius = 8.0; // the panel's corners (kPanelRadius)
        bool operator==(const Look &) const = default;
    };

    static constexpr int kCells = 11;        // the loupe's pixels per side; the centre one is picked
    static constexpr double kCell = 10.0;    // one of them, logical pixels
    static constexpr double kSwatch = 50.0;  // the colour under the pointer and the picked one, under the loupe
    static constexpr double kPadding = 10.0;
    static constexpr double kGap = 10.0;     // between the loupe, the swatches and the values

    // Whether the file stores 8-bit sRGB: its values are the RGB row's.
    static bool storesSrgb8(const Source &source);
    // Whether the file stores other values than the sRGB ones shown: a row for them.
    static bool showsFileValue(const Source &source);
    // The values of `sample` from an image described by `source`, in the order shown; the file's
    // "…" while `file` is null.
    static QList<Row> values(const Sample &sample, const Source &source, const QLocale &locale,
                             const FileValue *file = nullptr);

    // The colour picked and the values it had when it was picked (they stay when another image
    // shows); none: nothing picked yet. Each returns whether the panel changed.
    bool pick(const Sample &sample, const Source &source, const QLocale &locale);
    bool setFileValue(const FileValue &file); // the file's value of the colour picked, once read
    bool waitsForFileValue() const { return m_picked && showsFileValue(m_source) && !m_file; }
    bool clear();
    const std::optional<Sample> &picked() const { return m_picked; }
    const QList<Row> &rows() const { return m_rows; }

    bool setLook(const Look &look);
    bool setHover(int row); // a value under the pointer, -1: none
    int hover() const { return m_hover; }

    // Logical pixels, in the panel.
    QSizeF size() const;
    QRectF loupeRect() const;
    QRectF currentRect() const; // the colour under the pointer
    QRectF pickedRect() const;
    int rowAt(const QPointF &position) const; // a value's index, or -1
    double rowHeight() const;                 // a value's row

    // The panel at the look's pixel ratio, drawn again only when it changed; the loupe and the two
    // swatches are holes (transparent) but for the frame of the loupe's centre pixel.
    QImage texture();

private:
    void layout();

    std::optional<Sample> m_picked;
    Source m_source;     // ...the image it came from
    QLocale m_locale;
    std::optional<FileValue> m_file;
    QList<Row> m_rows;
    Look m_look;
    int m_hover = -1;
    QSizeF m_size;
    double m_labelWidth = 0; // the values' columns
    double m_textWidth = 0;
    bool m_laidOut = false;
    QImage m_texture;
    bool m_dirty = true;
};
