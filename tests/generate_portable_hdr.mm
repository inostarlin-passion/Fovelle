// SPDX-License-Identifier: CC0-1.0
// Original synthetic signal; no photographs or external assets are embedded.
#import <CoreImage/CoreImage.h>
#import <Foundation/Foundation.h>
#include <vector>
int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc != 2)
            return 2;
        const size_t width = 256, height = 128;
        std::vector<float> sdr(width * height * 4), hdr(width * height * 4);
        for (size_t y = 0; y < height; ++y)
            for (size_t x = 0; x < width; ++x) {
                const size_t i = (y * width + x) * 4;
                for (size_t c = 0; c < 3; ++c) {
                    float value = 0.1f + 0.7f * float((x + (c + 1) * y) % 256) / 255.f;
                    sdr[i + c] = value;
                    hdr[i + c] = value * (1.f + 3.f * float(x) / 255.f);
                }
                sdr[i + 3] = hdr[i + 3] = 1.f;
            }
        CGColorSpaceRef linear = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearDisplayP3);
        CGColorSpaceRef output = CGColorSpaceCreateWithName(kCGColorSpaceDisplayP3);
        CIImage *base =
                [CIImage imageWithBitmapData:[NSData dataWithBytes:sdr.data()
                                                            length:sdr.size() * sizeof(float)]
                                 bytesPerRow:width * 16
                                        size:CGSizeMake(width, height)
                                      format:kCIFormatRGBAf
                                  colorSpace:linear];
        CIImage *extended =
                [CIImage imageWithBitmapData:[NSData dataWithBytes:hdr.data()
                                                            length:hdr.size() * sizeof(float)]
                                 bytesPerRow:width * 16
                                        size:CGSizeMake(width, height)
                                      format:kCIFormatRGBAf
                                  colorSpace:linear];
        CIContext *context = [CIContext
                contextWithOptions:@{ kCIContextWorkingColorSpace : (__bridge id)linear }];
        NSData *encoded =
                [context JPEGRepresentationOfImage:base
                                        colorSpace:output
                                           options:@{ kCIImageRepresentationHDRImage : extended }];
        BOOL ok = encoded &&
                [encoded writeToFile:[NSString stringWithUTF8String:argv[1]] atomically:YES];
        CGColorSpaceRelease(linear);
        CGColorSpaceRelease(output);
        return ok ? 0 : 1;
    }
}
