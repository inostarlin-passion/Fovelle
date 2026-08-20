#!/usr/bin/env swift

import CoreGraphics
import CoreImage
import Foundation

enum ProbeError: Error, CustomStringConvertible {
    case usage
    case rawFilterCreation(String)
    case missingOutput(String)
    case missingReduction(String)

    var description: String {
        switch self {
        case .usage:
            return "usage: hdr_raw_baseline_probe.swift RAW_FILE"
        case .rawFilterCreation(let label):
            return "CIRAWFilter creation failed: \(label)"
        case .missingOutput(let label):
            return "CIRAWFilter output missing: \(label)"
        case .missingReduction(let label):
            return "CIAreaMaximum output missing: \(label)"
        }
    }
}

func rawFilter(url: URL, label: String) throws -> CIRAWFilter {
    guard let filter = CIRAWFilter(imageURL: url) else {
        throw ProbeError.rawFilterCreation(label)
    }
    return filter
}

func maximumComponent(
    filter: CIRAWFilter,
    label: String,
    context: CIContext,
    colorSpace: CGColorSpace
) throws -> Float {
    guard let image = filter.outputImage else {
        throw ProbeError.missingOutput(label)
    }
    guard let reduction = CIFilter(
        name: "CIAreaMaximum",
        parameters: [
            kCIInputImageKey: image,
            kCIInputExtentKey: CIVector(cgRect: image.extent),
        ]
    )?.outputImage else {
        throw ProbeError.missingReduction(label)
    }

    var pixel = [Float](repeating: 0, count: 4)
    context.render(
        reduction,
        toBitmap: &pixel,
        rowBytes: MemoryLayout<Float>.stride * pixel.count,
        bounds: CGRect(x: 0, y: 0, width: 1, height: 1),
        format: .RGBAf,
        colorSpace: colorSpace
    )
    return max(pixel[0], pixel[1], pixel[2])
}

do {
    guard CommandLine.arguments.count == 2 else {
        throw ProbeError.usage
    }
    let path = CommandLine.arguments[1]
    let url = URL(fileURLWithPath: path)
    guard let colorSpace = CGColorSpace(name: CGColorSpace.extendedLinearDisplayP3) else {
        throw ProbeError.missingOutput("extended-linear Display P3 color space")
    }
    let context = CIContext(options: [
        .workingColorSpace: colorSpace,
        .outputColorSpace: colorSpace,
        .workingFormat: CIFormat.RGBAh,
        .cacheIntermediates: true,
    ])

    let metadataFilter = try rawFilter(url: url, label: "metadata")
    let defaultBaselineExposure = metadataFilter.baselineExposure

    let sdrFilter = try rawFilter(url: url, label: "SDR")
    sdrFilter.extendedDynamicRangeAmount = 0
    let sdrMaximum = try maximumComponent(
        filter: sdrFilter, label: "SDR", context: context, colorSpace: colorSpace
    )

    let defaultHDRFilter = try rawFilter(url: url, label: "default-HDR")
    defaultHDRFilter.extendedDynamicRangeAmount = 1
    guard let defaultHDRImage = defaultHDRFilter.outputImage else {
        throw ProbeError.missingOutput("default-HDR metadata")
    }
    let reportedContentHeadroom = defaultHDRImage.contentHeadroom
    let defaultHDRMaximum = try maximumComponent(
        filter: defaultHDRFilter,
        label: "default-HDR",
        context: context,
        colorSpace: colorSpace
    )

    let neutralHDRFilter = try rawFilter(url: url, label: "neutral-baseline-HDR")
    neutralHDRFilter.baselineExposure = 0
    neutralHDRFilter.extendedDynamicRangeAmount = 1
    let neutralHDRMaximum = try maximumComponent(
        filter: neutralHDRFilter,
        label: "neutral-baseline-HDR",
        context: context,
        colorSpace: colorSpace
    )

    let record: [String: Any] = [
        "schema_version": "1.0",
        "raw_path": path,
        "default_baseline_exposure": defaultBaselineExposure,
        "raw_ciimage_reported_content_headroom": reportedContentHeadroom,
        "sdr_maximum_component": sdrMaximum,
        "default_baseline_hdr_maximum_component": defaultHDRMaximum,
        "neutral_baseline_hdr_maximum_component": neutralHDRMaximum,
    ]
    let data = try JSONSerialization.data(withJSONObject: record, options: [.sortedKeys])
    print(String(decoding: data, as: UTF8.self))
} catch {
    fputs("\(error)\n", stderr)
    exit(2)
}
