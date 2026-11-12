// A CMYK JPEG with a CMYK ICC profile, read by libjpeg-turbo (D-100): OpenImageIO converts a
// JPEG's CMYK to RGB itself, by the textbook formula and without the profile, so its ink comes from
// here; oiio.cpp sends such a file here and the rest of the JPEG files to OpenImageIO. Split from
// codecs.cpp (D-08). Shared helpers are in decoders.cpp.
#include "decoders.h"

#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <jpeglib.h>

namespace {

// libjpeg reports a fatal error by calling error_exit, which must not return: it jumps back to the
// reader's setjmp with the message. No C++ object lives between that setjmp and the libjpeg calls.
struct JpegFailure {
    jpeg_error_mgr manager; // first: libjpeg's err points here
    std::jmp_buf jump;
    char message[JMSG_LENGTH_MAX];
};

[[noreturn]] void jpegFail(j_common_ptr info)
{
    auto *failure = reinterpret_cast<JpegFailure *>(info->err);
    (*info->err->format_message)(info, failure->message);
    std::longjmp(failure->jump, 1);
}

void jpegSilent(j_common_ptr) {} // warnings (a damaged file read on) stay out of the console

// The ink of a CMYK (or YCCK) JPEG and the APP markers that describe it, read by libjpeg-turbo.
// Plain C: every resource is released on both paths; the caller turns the result into C++ objects.
struct CmykJpeg {
    unsigned char *pixels = nullptr; // width x height x 4, new[]
    JOCTET *icc = nullptr;           // malloc'd by libjpeg
    unsigned int iccBytes = 0;
    unsigned char *exif = nullptr;   // the APP1 "Exif" payload after its 6-byte name, new[]
    std::size_t exifBytes = 0;
    int width = 0, height = 0;
    bool inverted = false;
};

int readCmykJpeg(const unsigned char *data, std::size_t size, qint64 maxPixels, CmykJpeg *out, char *message, bool *overLimit)
{
    jpeg_decompress_struct info;
    JpegFailure failure;
    info.err = jpeg_std_error(&failure.manager);
    failure.manager.error_exit = jpegFail;
    failure.manager.output_message = jpegSilent;
    failure.message[0] = '\0';
    unsigned char *volatile pixels = nullptr;
    if (setjmp(failure.jump)) {
        std::memcpy(message, failure.message, JMSG_LENGTH_MAX);
        jpeg_destroy_decompress(&info);
        delete[] pixels;
        return 0;
    }
    jpeg_create_decompress(&info);
    jpeg_mem_src(&info, data, static_cast<unsigned long>(size));
    jpeg_save_markers(&info, JPEG_APP0 + 1, 0xFFFF); // EXIF: orientation, shooting data
    jpeg_save_markers(&info, JPEG_APP0 + 2, 0xFFFF); // ICC profile (in chunks)
    jpeg_read_header(&info, TRUE);
    if (info.jpeg_color_space != JCS_CMYK && info.jpeg_color_space != JCS_YCCK) {
        std::snprintf(message, JMSG_LENGTH_MAX, "not a CMYK JPEG");
        jpeg_destroy_decompress(&info);
        return 0;
    }
    const qint64 width = info.image_width, height = info.image_height;
    // The limits every back end applies (checkDimensions(), done by the caller with the real values):
    // here only what must hold before libjpeg allocates anything.
    if (width <= 0 || height <= 0 || width > kMaxPixels || height > kMaxPixels || width * height > kMaxPixels
        || (maxPixels > 0 && width * height > maxPixels)) {
        *overLimit = maxPixels > 0 && width > 0 && height > 0 && width <= kMaxPixels && height <= kMaxPixels
                     && width * height <= kMaxPixels;
        std::snprintf(message, JMSG_LENGTH_MAX, "invalid dimensions");
        jpeg_destroy_decompress(&info);
        return 0;
    }
    // Within this machine's memory (fitsInMemory()'s rule): the ink, its conversion and libjpeg's own
    // buffers (a progressive file keeps every coefficient), before anything that size is allocated.
    if (const qint64 budget = decodeMemoryBudget(); budget > 0 && width * height * (4 + 8 + kConversionBytesPerPixel) > budget) {
        std::snprintf(message, JMSG_LENGTH_MAX, "larger than the memory available");
        jpeg_destroy_decompress(&info);
        return 0;
    }
    out->inverted = info.saw_Adobe_marker != 0; // Adobe's CMYK JPEGs store 1 - ink
    info.out_color_space = JCS_CMYK;          // YCCK becomes CMYK
    jpeg_start_decompress(&info);
    if (info.output_components != 4 || qint64(info.output_width) != width || qint64(info.output_height) != height) {
        std::snprintf(message, JMSG_LENGTH_MAX, "unexpected output");
        jpeg_destroy_decompress(&info);
        return 0;
    }
    const std::size_t row = std::size_t(width) * 4;
    pixels = new (std::nothrow) unsigned char[row * std::size_t(height)];
    if (!pixels) {
        std::snprintf(message, JMSG_LENGTH_MAX, "out of memory");
        jpeg_destroy_decompress(&info);
        return 0;
    }
    while (info.output_scanline < info.output_height) {
        JSAMPROW line = pixels + row * info.output_scanline;
        if (jpeg_read_scanlines(&info, &line, 1) != 1)
            break;
    }
    if (info.output_scanline != info.output_height) { // a truncated file: libjpeg fills nothing more
        std::snprintf(message, JMSG_LENGTH_MAX, "truncated");
        jpeg_destroy_decompress(&info);
        delete[] pixels;
        return 0;
    }
    JOCTET *icc = nullptr;
    unsigned int iccBytes = 0;
    if (jpeg_read_icc_profile(&info, &icc, &iccBytes)) {
        out->icc = icc;
        out->iccBytes = iccBytes;
    }
    for (jpeg_saved_marker_ptr marker = info.marker_list; marker; marker = marker->next) {
        if (marker->marker == JPEG_APP0 + 1 && marker->data_length > 6 && std::memcmp(marker->data, "Exif\0\0", 6) == 0) {
            out->exifBytes = marker->data_length - 6;
            out->exif = new (std::nothrow) unsigned char[out->exifBytes];
            if (out->exif)
                std::memcpy(out->exif, marker->data + 6, out->exifBytes);
            else
                out->exifBytes = 0;
            break;
        }
    }
    jpeg_finish_decompress(&info);
    jpeg_destroy_decompress(&info);
    out->pixels = pixels;
    out->width = int(width);
    out->height = int(height);
    return 1;
}

} // namespace

bool decodeCmykJpeg(const QByteArray &bytes, qint64 maxPixels, Decoded *out, QString *error)
{
    CmykJpeg jpeg;
    char message[JMSG_LENGTH_MAX] = {};
    bool overLimit = false;
    const bool read = readCmykJpeg(reinterpret_cast<const unsigned char *>(bytes.constData()), std::size_t(bytes.size()),
                                   maxPixels, &jpeg, message, &overLimit) != 0;
    std::unique_ptr<unsigned char[]> pixels(jpeg.pixels);
    const std::unique_ptr<unsigned char[]> exif(jpeg.exif);
    const std::unique_ptr<JOCTET, void (*)(void *)> icc(jpeg.icc, std::free);
    if (!read) {
        out->overLimit = overLimit;
        *error = libraryError("libjpeg-turbo:", message, "jpeg");
        return false;
    }
    if (!checkDimensions(jpeg.width, jpeg.height, 4, 4, maxPixels, out, error))
        return false;
    const QByteArray profile = icc ? QByteArray(reinterpret_cast<const char *>(icc.get()), qsizetype(jpeg.iccBytes)) : QByteArray();
    out->width = jpeg.width;
    out->height = jpeg.height;
    out->sample = Decoded::Sample::U8;
    out->channels = 4;
    out->sourceChannels = 4;
    out->cmyk = true;
    out->inkInverted = jpeg.inverted;
    out->bits = 8;
    out->codec = QStringLiteral("libjpeg-turbo (CMYK)");
    if (exif)
        out->camera = cameraFromExif(QByteArrayView(reinterpret_cast<const char *>(exif.get()), qsizetype(jpeg.exifBytes)),
                                     &out->orientation);
    if (color::isCmykProfile(profile)) {
        describeIcc(profile, &out->colour);
    } else {
        assumeDefault(false, &out->colour);
        out->colour.description = cmykWithoutProfile();
    }
    out->data = std::move(pixels);
    return true;
}
