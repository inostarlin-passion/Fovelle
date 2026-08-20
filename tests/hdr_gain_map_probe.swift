#!/usr/bin/env swift

import CoreGraphics
import CoreImage
import Foundation

enum ProbeError: Error {
    case usage
    case missingImage(String)
    case missingColorSpace
}

func extentRecord(_ image: CIImage) -> [String: Double] {
    [
        "x": image.extent.origin.x,
        "y": image.extent.origin.y,
        "width": image.extent.width,
        "height": image.extent.height,
    ]
}

func averageRGB(
    _ image: CIImage,
    rect: CGRect,
    context: CIContext,
    colorSpace: CGColorSpace
) throws -> [Float] {
    guard let reduced = CIFilter(
        name: "CIAreaAverage",
        parameters: [
            kCIInputImageKey: image,
            kCIInputExtentKey: CIVector(cgRect: rect),
        ]
    )?.outputImage else {
        throw ProbeError.missingImage("CIAreaAverage")
    }
    var pixel = [Float](repeating: 0, count: 4)
    context.render(
        reduced,
        toBitmap: &pixel,
        rowBytes: MemoryLayout<Float>.stride * pixel.count,
        bounds: CGRect(x: 0, y: 0, width: 1, height: 1),
        format: .RGBAf,
        colorSpace: colorSpace
    )
    return pixel
}

do {
    guard CommandLine.arguments.count == 2 || CommandLine.arguments.count == 4,
          CommandLine.arguments.count == 2 || CommandLine.arguments[2] == "--preview-png"
    else { throw ProbeError.usage }
    let url = URL(fileURLWithPath: CommandLine.arguments[1])
    let common: [CIImageOption: Any] = [
        .applyOrientationProperty: true,
        .cacheImmediately: true,
    ]
    var gainOptions = common
    gainOptions[.auxiliaryHDRGainMap] = true
    guard let base = CIImage(contentsOf: url, options: common) else {
        throw ProbeError.missingImage("base")
    }
    guard let gainMap = CIImage(contentsOf: url, options: gainOptions) else {
        throw ProbeError.missingImage("gain-map")
    }
    let applied = base.applyingGainMap(gainMap)
    guard let rawFilter = CIRAWFilter(imageURL: url),
          let processedPreview = rawFilter.previewImage else {
        throw ProbeError.missingImage("CIRAWFilter.previewImage")
    }
    guard let colorSpace = CGColorSpace(name: CGColorSpace.extendedLinearDisplayP3) else {
        throw ProbeError.missingColorSpace
    }
    let context = CIContext(options: [
        .workingColorSpace: colorSpace,
        .outputColorSpace: colorSpace,
        .workingFormat: CIFormat.RGBAh,
        .cacheIntermediates: true,
    ])
    let extent = applied.extent
    let halfWidth = extent.width / 2
    let halfHeight = extent.height / 2
    let quadrants = [
        CGRect(x: extent.minX, y: extent.minY, width: halfWidth, height: halfHeight),
        CGRect(x: extent.minX + halfWidth, y: extent.minY, width: halfWidth, height: halfHeight),
        CGRect(x: extent.minX, y: extent.minY + halfHeight, width: halfWidth, height: halfHeight),
        CGRect(x: extent.minX + halfWidth, y: extent.minY + halfHeight,
               width: halfWidth, height: halfHeight),
    ]
    let toneMap = CIFilter(name: "CIToneMapHeadroom", parameters: [
        kCIInputImageKey: applied,
        "inputSourceHeadroom": applied.contentHeadroom,
        "inputTargetHeadroom": applied.contentHeadroom,
    ])?.outputImage
    let directAtFullHeadroom = base.applyingGainMap(
        gainMap, headroom: applied.contentHeadroom
    )
    let toneMapExtent: Any = toneMap.map { extentRecord($0) } ?? NSNull()
    let toneMapAverages: Any = try toneMap.map { image in
        try quadrants.map {
            try averageRGB(image, rect: $0, context: context, colorSpace: colorSpace)
        }
    } ?? NSNull()
    let record: [String: Any] = [
        "schema_version": "1.0",
        "base_extent": extentRecord(base),
        "gain_map_extent": extentRecord(gainMap),
        "processed_preview_extent": extentRecord(processedPreview),
        "applied_extent": extentRecord(applied),
        "applied_content_headroom": applied.contentHeadroom,
        "quadrant_average_rgba": try quadrants.map {
            try averageRGB(applied, rect: $0, context: context, colorSpace: colorSpace)
        },
        "tone_map_extent": toneMapExtent,
        "tone_map_quadrant_average_rgba": toneMapAverages,
        "direct_headroom_quadrant_average_rgba": try quadrants.map {
            try averageRGB(directAtFullHeadroom, rect: $0,
                           context: context, colorSpace: colorSpace)
        },
        "gain_map_properties_description": String(describing: gainMap.properties),
    ]
    if CommandLine.arguments.count == 4 {
        let outputURL = URL(fileURLWithPath: CommandLine.arguments[3])
        let scale = min(1024.0 / processedPreview.extent.width,
                        1024.0 / processedPreview.extent.height)
        let scaled = processedPreview.transformed(
            by: CGAffineTransform(scaleX: scale, y: scale)
        )
        guard let outputColorSpace = CGColorSpace(name: CGColorSpace.sRGB) else {
            throw ProbeError.missingColorSpace
        }
        try context.writePNGRepresentation(
            of: scaled,
            to: outputURL,
            format: .RGBA8,
            colorSpace: outputColorSpace
        )
    }
    let data = try JSONSerialization.data(withJSONObject: record, options: [.prettyPrinted, .sortedKeys])
    print(String(decoding: data, as: UTF8.self))
} catch {
    fputs("\(error)\n", stderr)
    exit(2)
}
