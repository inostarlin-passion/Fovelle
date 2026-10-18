#include "native_color_fixtures.h"
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <CoreGraphics/CoreGraphics.h>

QString createColorMultiImage(const QString &directory, const QString &format)
{
    @autoreleasepool {
        const QString path = directory + "/multi." + format;
        CFStringRef type = format == "tiff" ? CFSTR("public.tiff") : CFSTR("com.apple.icns");
        NSURL *url = [NSURL fileURLWithPath:path.toNSString()];
        CGImageDestinationRef destination =
                CGImageDestinationCreateWithURL((CFURLRef)url, type, 2, nullptr);
        if (!destination)
            return { };
        CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        for (int i = 0; i < 2; ++i) {
            const size_t size = format == "tiff" ? 16 : (i == 0 ? 16 : 32);
            CGContextRef context =
                    CGBitmapContextCreate(nullptr, size, size, 8, size * 4, space,
                                          kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
            if (!context) {
                CGColorSpaceRelease(space);
                CFRelease(destination);
                return { };
            }
            CGContextSetRGBFillColor(context, i == 0 ? 1 : 0, i == 0 ? 0 : 1, 0, 1);
            CGContextFillRect(context, CGRectMake(0, 0, size, size));
            CGImageRef image = CGBitmapContextCreateImage(context);
            CGImageDestinationAddImage(destination, image, nullptr);
            CGImageRelease(image);
            CGContextRelease(context);
        }
        CGColorSpaceRelease(space);
        const bool finalized = CGImageDestinationFinalize(destination);
        CFRelease(destination);
        if (!finalized)
            return { };
        CGImageSourceRef source = CGImageSourceCreateWithURL((CFURLRef)url, nullptr);
        bool valid = source && CGImageSourceGetCount(source) >= 2;
        if (valid)
            for (size_t i = 0; i < 2; ++i) {
                CGImageRef image = CGImageSourceCreateImageAtIndex(source, i, nullptr);
                valid = valid && image;
                if (image)
                    CGImageRelease(image);
            }
        if (source)
            CFRelease(source);
        return valid ? path : QString();
    }
}
QByteArray createColorP3Pdf(bool adobeGreen)
{
    CFMutableDataRef bytes = CFDataCreateMutable(nullptr, 0);
    CGDataConsumerRef consumer = CGDataConsumerCreateWithCFData(bytes);
    CGRect page = CGRectMake(0, 0, 16, 16);
    CGContextRef context = CGPDFContextCreate(consumer, &page, nullptr);
    CGDataConsumerRelease(consumer);
    if (!context) {
        CFRelease(bytes);
        return { };
    }
    CGPDFContextBeginPage(context, nullptr);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(adobeGreen ? kCGColorSpaceAdobeRGB1998
                                                                  : kCGColorSpaceDisplayP3);
    const CGFloat components[] = { adobeGreen ? 0.0 : 1.0, adobeGreen ? 1.0 : 0.0, 0, 1 };
    CGColorRef color = CGColorCreate(space, components);
    CGContextSetFillColorWithColor(context, color);
    CGContextFillRect(context, page);
    CGColorRelease(color);
    CGColorSpaceRelease(space);
    CGPDFContextEndPage(context);
    CGPDFContextClose(context);
    CGContextRelease(context);
    QByteArray result(reinterpret_cast<const char *>(CFDataGetBytePtr(bytes)),
                      CFDataGetLength(bytes));
    CFRelease(bytes);
    return result;
}
