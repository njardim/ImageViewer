// The operating system's decoders (decision D-39): HEIC/HEIF through ImageIO on macOS and WIC
// on Windows, whose HEVC decoders are licensed with the system. imageViewer ships no HEVC
// decoder; where the system has none, the message says how to add one.
#include "decoders.h"

#include <QCoreApplication>
#include <QFile>

#include <cstring>
#include <memory>
#include <type_traits>

#if defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <propidl.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif

namespace {

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
bool checkSize(qint64 width, qint64 height, int bytesPerPixel, qint64 maxPixels, Decoded *out, QString *error)
{
    const qint64 pixels = width * height;
    if (width <= 0 || height <= 0 || pixels > kMaxPixels) {
        *error = QCoreApplication::translate("Image", "invalid dimensions (%1×%2×%3)").arg(width).arg(height).arg(4);
        return false;
    }
    if (maxPixels > 0 && pixels > maxPixels) {
        out->overLimit = true;
        return false;
    }
    return fitsInMemory(pixels, bytesPerPixel, error);
}
#endif

#if defined(Q_OS_MACOS)
// CoreFoundation objects released when they go out of scope.
template <typename Ref>
using CfPtr = std::unique_ptr<std::remove_pointer_t<Ref>, decltype(&CFRelease)>;

template <typename Ref>
CfPtr<Ref> owned(Ref ref)
{
    return CfPtr<Ref>(ref, &CFRelease);
}

int intProperty(CFDictionaryRef properties, CFStringRef key, int fallback)
{
    const auto number = static_cast<CFNumberRef>(properties ? CFDictionaryGetValue(properties, key) : nullptr);
    int value = fallback;
    if (number && CFGetTypeID(number) == CFNumberGetTypeID())
        CFNumberGetValue(number, kCFNumberIntType, &value);
    return value;
}

QString cfString(CFStringRef string)
{
    if (!string)
        return {};
    const CFIndex length = CFStringGetLength(string);
    QString text(qsizetype(length), Qt::Uninitialized);
    CFStringGetCharacters(string, CFRangeMake(0, length), reinterpret_cast<UniChar *>(text.data()));
    return text;
}
#elif defined(Q_OS_WIN)
using Microsoft::WRL::ComPtr;

// COM on the calling thread for the duration of a decode (decodes run on worker threads).
struct ComScope {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope()
    {
        if (SUCCEEDED(result))
            CoUninitialize();
    }
};

ComPtr<IWICImagingFactory> imagingFactory()
{
    ComPtr<IWICImagingFactory> factory;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    return factory;
}

// Errors that mean "no HEIF or HEVC decoder on this machine", not a damaged file.
bool isMissingCodec(HRESULT result)
{
    return result == WINCODEC_ERR_COMPONENTNOTFOUND || result == WINCODEC_ERR_UNSUPPORTEDOPERATION
           || result == HRESULT(0xC00D5212L); // MF_E_TOPO_CODEC_NOT_FOUND: the HEVC extension is missing
}
#endif

} // namespace

QString missingHevcMessage()
{
#if defined(Q_OS_WIN)
    return QCoreApplication::translate("Image", "HEIC images need Microsoft's “HEIF Image Extensions” and “HEVC Video "
                                                "Extensions”, from the Microsoft Store.");
#else
    return QCoreApplication::translate("Image", "HEIC images need an HEVC decoder, which imageViewer does not include on "
                                                "this system (patents); convert them to another format first.");
#endif
}

bool systemHeicAvailable()
{
#if defined(Q_OS_MACOS)
    return true; // ImageIO decodes HEIC since macOS 10.13
#elif defined(Q_OS_WIN)
    static const bool available = [] {
        ComScope com;
        const ComPtr<IWICImagingFactory> factory = imagingFactory();
        ComPtr<IWICBitmapDecoder> decoder;
        return factory && SUCCEEDED(factory->CreateDecoder(GUID_ContainerFormatHeif, nullptr, &decoder));
    }();
    return available;
#else
    return false;
#endif
}

bool decodeSystemHeic(const QString &path, qint64 maxPixels, Decoded *out, QString *error)
{
#if defined(Q_OS_MACOS)
    // ImageIO decodes; ColorSync converts into a float context in extended linear sRGB, which
    // is the working space itself (D-10): the colour conversion of the pipeline is identity.
    const QByteArray name = QFile::encodeName(path);
    const auto url = owned(CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(name.constData()),
                                                                   name.size(), false));
    const auto source = owned(url ? CGImageSourceCreateWithURL(url.get(), nullptr) : nullptr);
    if (!source || CGImageSourceGetCount(source.get()) < 1) {
        *error = damaged("HEIC");
        return false;
    }
    // The size before decoding, so that an image over the limit is not decoded; then the
    // decoded image's own size, which is what is drawn.
    const auto properties = owned(CGImageSourceCopyPropertiesAtIndex(source.get(), 0, nullptr));
    if (!checkSize(intProperty(properties.get(), kCGImagePropertyPixelWidth, 0),
                   intProperty(properties.get(), kCGImagePropertyPixelHeight, 0), 16, maxPixels, out, error))
        return false;
    const auto image = owned(CGImageSourceCreateImageAtIndex(source.get(), 0, nullptr));
    const auto target = owned(CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB));
    if (!image || !target) {
        *error = damaged("HEIC");
        return false;
    }
    const qint64 width = qint64(CGImageGetWidth(image.get())), height = qint64(CGImageGetHeight(image.get()));
    if (!checkSize(width, height, 16, maxPixels, out, error))
        return false;
    const std::size_t rowBytes = std::size_t(width) * 4 * sizeof(float);
    out->data.reset(new unsigned char[rowBytes * std::size_t(height)]);
    std::memset(out->data.get(), 0, rowBytes * std::size_t(height));
    const auto context = owned(CGBitmapContextCreate(out->data.get(), std::size_t(width), std::size_t(height), 32, rowBytes, target.get(),
                                                     kCGImageAlphaPremultipliedLast | kCGBitmapFloatComponents | kCGBitmapByteOrder32Host));
    if (!context) {
        *error = damaged("HEIC");
        return false;
    }
    CGContextSetBlendMode(context.get(), kCGBlendModeCopy);
    CGContextDrawImage(context.get(), CGRectMake(0, 0, width, height), image.get()); // row 0 in memory is the top
    const int orientation = intProperty(properties.get(), kCGImagePropertyOrientation, 1);
    const auto sourceName = owned(CGColorSpaceCopyName(CGImageGetColorSpace(image.get())));
    out->width = int(width);
    out->height = int(height);
    out->sample = Decoded::Sample::F32;
    out->channels = 4;
    out->alphaIndex = CGImageGetAlphaInfo(image.get()) == kCGImageAlphaNone ? -1 : 3;
    out->associatedAlpha = true;
    out->sourceChannels = out->alphaIndex >= 0 ? 4 : 3;
    out->bits = intProperty(properties.get(), kCGImagePropertyDepth, 8);
    out->orientation = orientation >= 1 && orientation <= 8 ? orientation : 1;
    out->codec = QStringLiteral("ImageIO (macOS)");
    out->colour.source = color::Descriptor::Source::FormatAttributes;
    out->colour.primaries = color::kBt709;
    out->colour.transfer = color::Transfer::Linear;
    const QString from = cfString(sourceName.get()).remove(QStringLiteral("kCGColorSpace"));
    out->colour.description = QStringLiteral("%1 → linear BT.709 (ColorSync)").arg(from.isEmpty() ? QStringLiteral("ICC") : from);
    return true;
#elif defined(Q_OS_WIN)
    ComScope com;
    const ComPtr<IWICImagingFactory> factory = imagingFactory();
    if (!factory) {
        *error = missingHevcMessage();
        return false;
    }
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    HRESULT result = factory->CreateDecoderFromFilename(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, GENERIC_READ,
                                                        WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(result))
        result = decoder->GetFrame(0, &frame);
    UINT width = 0, height = 0;
    if (SUCCEEDED(result))
        result = frame->GetSize(&width, &height);
    if (FAILED(result)) {
        *error = isMissingCodec(result) ? missingHevcMessage() : damaged("HEIC");
        return false;
    }
    if (!checkSize(width, height, 4, maxPixels, out, error))
        return false;
    // The embedded profile, if any; the pipeline converts from it.
    UINT contexts = 0;
    if (SUCCEEDED(frame->GetColorContexts(0, nullptr, &contexts)) && contexts > 0 && contexts < 16) {
        ComPtr<IWICColorContext> list[16];
        IWICColorContext *raw[16] = {};
        for (UINT i = 0; i < contexts; ++i) {
            factory->CreateColorContext(&list[i]);
            raw[i] = list[i].Get();
        }
        if (SUCCEEDED(frame->GetColorContexts(contexts, raw, &contexts))) {
            for (UINT i = 0; i < contexts; ++i) {
                WICColorContextType type{};
                UINT size = 0;
                if (raw[i] && SUCCEEDED(raw[i]->GetType(&type)) && type == WICColorContextProfile
                    && SUCCEEDED(raw[i]->GetProfileBytes(0, nullptr, &size)) && size > 0 && size < (1u << 26)) {
                    QByteArray icc(qsizetype(size), Qt::Uninitialized);
                    if (SUCCEEDED(raw[i]->GetProfileBytes(size, reinterpret_cast<BYTE *>(icc.data()), &size))) {
                        describeIcc(icc, &out->colour);
                        break;
                    }
                }
            }
        }
    }
    if (out->colour.source != color::Descriptor::Source::Icc)
        assumeDefault(false, &out->colour);
    int orientation = 1;
    ComPtr<IWICMetadataQueryReader> metadata;
    if (SUCCEEDED(frame->GetMetadataQueryReader(&metadata))) {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(metadata->GetMetadataByName(L"System.Photo.Orientation", &value)) && value.vt == VT_UI2)
            orientation = value.uiVal >= 1 && value.uiVal <= 8 ? value.uiVal : 1;
        PropVariantClear(&value);
    }
    bool alpha = false;
    UINT channelCount = 0;
    WICPixelFormatGUID native{};
    ComPtr<IWICComponentInfo> info;
    ComPtr<IWICPixelFormatInfo2> pixelInfo;
    if (SUCCEEDED(frame->GetPixelFormat(&native)) && SUCCEEDED(factory->CreateComponentInfo(native, &info))
        && SUCCEEDED(info.As(&pixelInfo))) {
        BOOL transparency = FALSE;
        pixelInfo->SupportsTransparency(&transparency);
        alpha = transparency != FALSE;
        pixelInfo->GetChannelCount(&channelCount);
    }
    // 8 bits per channel, straight alpha, in the file's own encoding, which the profile above
    // describes. Not 16: WIC treats its 16-bit integer formats as linear (gamma 1.0) and would
    // change the curve on the way; 8-bit formats are only reordered. The HEIF extension decodes
    // 8-bit HEIC (phones); 10-bit files lose their extra precision here.
    ComPtr<IWICFormatConverter> converter;
    const quint64 stride = quint64(width) * 4, bytes = stride * height;
    if (bytes > 0xFFFFFFFFull) { // CopyPixels counts in 32 bits
        *error = damaged("HEIC");
        return false;
    }
    out->data.reset(new unsigned char[std::size_t(bytes)]);
    result = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(result))
        result = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                       WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(result))
        result = converter->CopyPixels(nullptr, UINT(stride), UINT(bytes), out->data.get());
    if (FAILED(result)) {
        *error = isMissingCodec(result) ? missingHevcMessage() : damaged("HEIC");
        return false;
    }
    out->width = int(width);
    out->height = int(height);
    out->sample = Decoded::Sample::U8;
    out->channels = 4;
    out->alphaIndex = alpha ? 3 : -1;
    out->sourceChannels = channelCount > 0 ? int(channelCount) : (alpha ? 4 : 3);
    out->bits = 8;
    out->orientation = orientation;
    out->codec = QStringLiteral("WIC (Windows)");
    return true;
#else
    Q_UNUSED(path);
    Q_UNUSED(maxPixels);
    Q_UNUSED(out);
    *error = missingHevcMessage();
    return false;
#endif
}
