// minedict - BSD 3-Clause License; see README.
//
// Draws the app icon on the macOS icon grid: a 1024-point canvas holding an
// 824-point rounded square with continuous corners and a soft shadow below.
// Writes the ten PNGs of an iconset plus a 1024-pixel preview.
//
//   DrawIcon <output directory>    ->  <dir>/AppIcon.iconset/*.png, <dir>/preview.png
import AppKit
import ImageIO
import SwiftUI
import UniformTypeIdentifiers

// MARK: - Colours

func rgb(_ hex: UInt32, _ alpha: CGFloat = 1) -> CGColor {
    CGColor(srgbRed: CGFloat((hex >> 16) & 0xff) / 255, green: CGFloat((hex >> 8) & 0xff) / 255,
            blue: CGFloat(hex & 0xff) / 255, alpha: alpha)
}

enum Palette {
    static let backgroundTop = rgb(0x4a4d64)
    static let backgroundBottom = rgb(0x1d1e27)
    static let coverTop = rgb(0xc7a6ff)
    static let coverBottom = rgb(0x9262f0)
    static let spineTop = rgb(0x7447dc)
    static let spineBottom = rgb(0x5a35b8)
    static let pagesTop = rgb(0xf8f2e3)
    static let pagesBottom = rgb(0xe6dec4)
    static let tabs = [0xff79c6, 0xffb86c, 0xf1fa8c, 0x50fa7b, 0x8be9fd, 0xff5555].map { rgb($0) }
    static let ribbon = rgb(0xff5555)
    static let ribbonFold = rgb(0xd93c3c)
    static let ink = rgb(0xf8f8f2)
}

let colorSpace = CGColorSpace(name: CGColorSpace.sRGB)!

func gradient(_ top: CGColor, _ bottom: CGColor) -> CGGradient {
    CGGradient(colorsSpace: colorSpace, colors: [top, bottom] as CFArray, locations: [0, 1])!
}

// MARK: - Geometry (1024-point grid, y grows downwards)

let canvas: CGFloat = 1024
let tile = CGRect(x: 100, y: 100, width: 824, height: 824)
let tileRadius: CGFloat = 184

let cover = CGRect(x: 246, y: 212, width: 460, height: 540)
let spineWidth: CGFloat = 40
let pages = CGRect(x: 284, y: 236, width: 460, height: 540)
let tabWidth: CGFloat = 34
let tabHeight: CGFloat = 62
let tabStep: CGFloat = 80
let firstTabY: CGFloat = 268
let ribbon = CGRect(x: 556, y: 700, width: 30, height: 138)

/// Apple's continuous-curvature rounded square.
func tilePath() -> CGPath {
    RoundedRectangle(cornerRadius: tileRadius, style: .continuous).path(in: tile).cgPath
}

func roundedPath(_ rect: CGRect, topLeft: CGFloat, topRight: CGFloat, bottomRight: CGFloat,
                 bottomLeft: CGFloat) -> CGPath {
    let path = CGMutablePath()
    path.move(to: CGPoint(x: rect.minX + topLeft, y: rect.minY))
    path.addArc(tangent1End: CGPoint(x: rect.maxX, y: rect.minY), tangent2End: CGPoint(x: rect.maxX, y: rect.maxY),
                radius: topRight)
    path.addArc(tangent1End: CGPoint(x: rect.maxX, y: rect.maxY), tangent2End: CGPoint(x: rect.minX, y: rect.maxY),
                radius: bottomRight)
    path.addArc(tangent1End: CGPoint(x: rect.minX, y: rect.maxY), tangent2End: CGPoint(x: rect.minX, y: rect.minY),
                radius: bottomLeft)
    path.addArc(tangent1End: CGPoint(x: rect.minX, y: rect.minY), tangent2End: CGPoint(x: rect.maxX, y: rect.minY),
                radius: topLeft)
    path.closeSubpath()
    return path
}

func roundedPath(_ rect: CGRect, radius: CGFloat) -> CGPath {
    roundedPath(rect, topLeft: radius, topRight: radius, bottomRight: radius, bottomLeft: radius)
}

// MARK: - Drawing

struct Painter {
    let context: CGContext
    /// Device pixels per grid point; shadows are specified in device space.
    let scale: CGFloat
    /// Below 64 pixels the lettering and the ribbon would only blur the shape.
    let detailed: Bool

    func shadow(dy: CGFloat, blur: CGFloat, alpha: CGFloat) {
        context.setShadow(offset: CGSize(width: 0, height: -dy * scale), blur: blur * scale, color: rgb(0x000000, alpha))
    }

    func fill(_ path: CGPath, with gradient: CGGradient, from top: CGFloat, to bottom: CGFloat) {
        context.saveGState()
        context.addPath(path)
        context.clip()
        context.drawLinearGradient(gradient, start: CGPoint(x: 0, y: top), end: CGPoint(x: 0, y: bottom), options: [])
        context.restoreGState()
    }

    func fill(_ path: CGPath, _ color: CGColor) {
        context.saveGState()
        context.addPath(path)
        context.setFillColor(color)
        context.fillPath()
        context.restoreGState()
    }

    func draw() {
        drawTile()
        context.saveGState()
        context.addPath(tilePath())
        context.clip()
        drawBook()
        context.restoreGState()
    }

    func drawTile() {
        let path = tilePath()
        // The standard shadow under the rounded square.
        context.saveGState()
        shadow(dy: 10, blur: 22, alpha: 0.32)
        fill(path, Palette.backgroundBottom)
        context.restoreGState()
        fill(path, with: gradient(Palette.backgroundTop, Palette.backgroundBottom), from: tile.minY, to: tile.maxY)
        // A soft light from above.
        context.saveGState()
        context.addPath(path)
        context.clip()
        let glow = CGGradient(colorsSpace: colorSpace, colors: [rgb(0xffffff, 0.14), rgb(0xffffff, 0)] as CFArray,
                              locations: [0, 1])!
        context.drawRadialGradient(glow, startCenter: CGPoint(x: 512, y: 150), startRadius: 0,
                                   endCenter: CGPoint(x: 512, y: 150), endRadius: 620, options: [])
        context.restoreGState()
    }

    func drawBook() {
        // Shadow of the whole book on the tile.
        let silhouette = CGMutablePath()
        silhouette.addPath(roundedPath(cover, radius: 26))
        silhouette.addPath(roundedPath(pages, radius: 16))
        context.saveGState()
        shadow(dy: 18, blur: 40, alpha: 0.45)
        fill(silhouette, Palette.backgroundBottom)
        context.restoreGState()

        // The page block with its thumb index.
        fill(roundedPath(pages, radius: 16), with: gradient(Palette.pagesTop, Palette.pagesBottom),
             from: pages.minY, to: pages.maxY)
        for (index, color) in Palette.tabs.enumerated() {
            let tab = CGRect(x: pages.maxX - 6, y: firstTabY + CGFloat(index) * tabStep, width: tabWidth + 6,
                             height: tabHeight)
            context.saveGState()
            shadow(dy: 3, blur: 6, alpha: 0.25)
            fill(roundedPath(tab, topLeft: 0, topRight: 10, bottomRight: 10, bottomLeft: 0), color)
            context.restoreGState()
        }

        if detailed {
            // The ribbon comes out from under the cover.
            let fold = CGMutablePath()
            fold.move(to: CGPoint(x: ribbon.minX, y: ribbon.minY))
            fold.addLine(to: CGPoint(x: ribbon.maxX, y: ribbon.minY))
            fold.addLine(to: CGPoint(x: ribbon.maxX, y: ribbon.maxY))
            fold.addLine(to: CGPoint(x: ribbon.midX, y: ribbon.maxY - 16))
            fold.addLine(to: CGPoint(x: ribbon.minX, y: ribbon.maxY))
            fold.closeSubpath()
            context.saveGState()
            shadow(dy: 6, blur: 10, alpha: 0.3)
            fill(fold, Palette.ribbon)
            context.restoreGState()
            fill(CGPath(rect: CGRect(x: ribbon.minX, y: ribbon.minY, width: 6, height: ribbon.height - 10),
                        transform: nil), Palette.ribbonFold)
        }

        // The cover, casting a shadow on the pages.
        let coverPath = roundedPath(cover, radius: 26)
        context.saveGState()
        shadow(dy: 8, blur: 18, alpha: 0.35)
        fill(coverPath, Palette.coverBottom)
        context.restoreGState()
        fill(coverPath, with: gradient(Palette.coverTop, Palette.coverBottom), from: cover.minY, to: cover.maxY)

        // The spine and its hinge.
        let spine = CGRect(x: cover.minX, y: cover.minY, width: spineWidth, height: cover.height)
        fill(roundedPath(spine, topLeft: 26, topRight: 0, bottomRight: 0, bottomLeft: 26),
             with: gradient(Palette.spineTop, Palette.spineBottom), from: spine.minY, to: spine.maxY)
        fill(CGPath(rect: CGRect(x: spine.maxX, y: cover.minY + 4, width: 3, height: cover.height - 8), transform: nil),
             rgb(0xffffff, 0.28))
        fill(CGPath(rect: CGRect(x: spine.maxX + 3, y: cover.minY + 4, width: 5, height: cover.height - 8),
                    transform: nil), rgb(0x000000, 0.12))

        // A highlight along the top edge of the cover.
        context.saveGState()
        context.addPath(coverPath)
        context.clip()
        let sheen = CGGradient(colorsSpace: colorSpace, colors: [rgb(0xffffff, 0.22), rgb(0xffffff, 0)] as CFArray,
                               locations: [0, 1])!
        context.drawLinearGradient(sheen, start: CGPoint(x: 0, y: cover.minY), end: CGPoint(x: 0, y: cover.minY + 160),
                                   options: [])
        context.restoreGState()

        if detailed { drawLettering() }
    }

    func drawLettering() {
        let base = NSFont.systemFont(ofSize: 236, weight: .bold)
        let font = NSFont(descriptor: base.fontDescriptor.withDesign(.rounded) ?? base.fontDescriptor, size: 236) ?? base
        let text = NSAttributedString(string: "Aa", attributes: [.font: font, .foregroundColor: NSColor(cgColor: Palette.ink)!])
        let line = CTLineCreateWithAttributedString(text)
        let bounds = CTLineGetBoundsWithOptions(line, [.useGlyphPathBounds])
        let center = CGPoint(x: (cover.minX + spineWidth + cover.maxX) / 2, y: cover.midY)
        context.saveGState()
        shadow(dy: 5, blur: 10, alpha: 0.35)
        context.translateBy(x: center.x, y: center.y)
        context.scaleBy(x: 1, y: -1)  // Core Text draws with y upwards
        context.textPosition = CGPoint(x: -bounds.midX, y: -bounds.midY)
        CTLineDraw(line, context)
        context.restoreGState()
    }
}

func render(pixels: Int) -> CGImage {
    let context = CGContext(data: nil, width: pixels, height: pixels, bitsPerComponent: 8, bytesPerRow: 0,
                            space: colorSpace, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    context.setAllowsAntialiasing(true)
    context.setShouldAntialias(true)
    context.interpolationQuality = .high
    let scale = CGFloat(pixels) / canvas
    context.translateBy(x: 0, y: CGFloat(pixels))
    context.scaleBy(x: scale, y: -scale)
    Painter(context: context, scale: scale, detailed: pixels >= 64).draw()
    return context.makeImage()!
}

func write(_ image: CGImage, to url: URL) {
    let destination = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil)!
    CGImageDestinationAddImage(destination, image, nil)
    guard CGImageDestinationFinalize(destination) else {
        FileHandle.standardError.write(Data("could not write \(url.path)\n".utf8))
        exit(1)
    }
}

// MARK: - Main

let arguments = CommandLine.arguments
guard arguments.count == 2 else {
    FileHandle.standardError.write(Data("usage: DrawIcon <output directory>\n".utf8))
    exit(2)
}
let output = URL(fileURLWithPath: arguments[1], isDirectory: true)
let iconset = output.appendingPathComponent("AppIcon.iconset", isDirectory: true)
try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)

// Each point size at 1x and 2x, named the way iconutil expects.
for points in [16, 32, 128, 256, 512] {
    write(render(pixels: points), to: iconset.appendingPathComponent("icon_\(points)x\(points).png"))
    write(render(pixels: points * 2), to: iconset.appendingPathComponent("icon_\(points)x\(points)@2x.png"))
}
write(render(pixels: 1024), to: output.appendingPathComponent("preview.png"))
print("wrote \(iconset.path)")
