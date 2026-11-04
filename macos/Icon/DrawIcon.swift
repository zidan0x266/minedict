// minedict - BSD 3-Clause License; see README.
//
// Draws the app icon on the macOS icon grid: a 1024-point canvas holding an
// 824-point rounded square with continuous corners and a soft shadow below.
// The subject is a magnifier with "Aa" in the lens, on a burnt orange tile.
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
    /// UT burnt orange (#bf5700), lit from above.
    static let tileTop = rgb(0xd46a17)
    static let tileBottom = rgb(0x9c4300)
    static let metalTop = rgb(0xffffff)
    static let metalBottom = rgb(0xf3e6da)
    static let glassLight = rgb(0xfff6ee)
    static let glassDark = rgb(0xffd9bf)
    static let lettering = rgb(0xbf5700)
}

let colorSpace = CGColorSpace(name: CGColorSpace.sRGB)!

func gradient(_ colors: [CGColor], _ locations: [CGFloat]) -> CGGradient {
    CGGradient(colorsSpace: colorSpace, colors: colors as CFArray, locations: locations)!
}

// MARK: - Geometry (1024-point grid, y grows downwards)

let canvas: CGFloat = 1024
let tile = CGRect(x: 100, y: 100, width: 824, height: 824)
let tileRadius: CGFloat = 184

/// The lens sits up and to the left so that the handle, pointing to the
/// lower right, balances it around the tile's centre.
let lensCenter = CGPoint(x: 468, y: 468)
let ringOuterRadius: CGFloat = 252
let ringWidth: CGFloat = 46
let handleWidth: CGFloat = 78
let handleLength: CGFloat = 176
let letteringSize: CGFloat = 232

/// Apple's continuous-curvature rounded square.
func tilePath() -> CGPath {
    RoundedRectangle(cornerRadius: tileRadius, style: .continuous).path(in: tile).cgPath
}

func circle(radius: CGFloat) -> CGPath {
    CGPath(ellipseIn: CGRect(x: lensCenter.x - radius, y: lensCenter.y - radius, width: radius * 2, height: radius * 2),
           transform: nil)
}

/// The handle as a filled shape: a thick line with round ends along the diagonal.
func handlePath() -> CGPath {
    let direction = CGPoint(x: 1 / sqrt(2), y: 1 / sqrt(2))
    let start = CGPoint(x: lensCenter.x + direction.x * (ringOuterRadius - 30),
                        y: lensCenter.y + direction.y * (ringOuterRadius - 30))
    let end = CGPoint(x: start.x + direction.x * handleLength, y: start.y + direction.y * handleLength)
    let line = CGMutablePath()
    line.move(to: start)
    line.addLine(to: end)
    return line.copy(strokingWithWidth: handleWidth, lineCap: .round, lineJoin: .round, miterLimit: 10)
}

func ringPath() -> CGPath {
    let path = CGMutablePath()
    path.addPath(circle(radius: ringOuterRadius))
    path.addPath(circle(radius: ringOuterRadius - ringWidth))
    return path
}

// MARK: - Drawing

struct Painter {
    let context: CGContext
    /// Device pixels per grid point; shadows are specified in device space.
    let scale: CGFloat
    /// From 64 pixels up the lens carries the lettering.
    let detailed: Bool

    func shadow(dy: CGFloat, blur: CGFloat, alpha: CGFloat) {
        context.setShadow(offset: CGSize(width: 0, height: -dy * scale), blur: blur * scale, color: rgb(0x000000, alpha))
    }

    func fill(_ path: CGPath, _ color: CGColor, evenOdd: Bool = false) {
        context.saveGState()
        context.addPath(path)
        context.setFillColor(color)
        context.fillPath(using: evenOdd ? .evenOdd : .winding)
        context.restoreGState()
    }

    func clipped(to path: CGPath, evenOdd: Bool = false, _ paint: () -> Void) {
        context.saveGState()
        context.addPath(path)
        if evenOdd { context.clip(using: .evenOdd) } else { context.clip() }
        paint()
        context.restoreGState()
    }

    func paintLinear(_ gradient: CGGradient, from start: CGPoint, to end: CGPoint) {
        context.drawLinearGradient(gradient, start: start, end: end, options: [.drawsBeforeStartLocation, .drawsAfterEndLocation])
    }

    func draw() {
        drawTile()
        context.saveGState()
        context.addPath(tilePath())
        context.clip()
        drawMagnifier()
        context.restoreGState()
    }

    func drawTile() {
        let path = tilePath()
        // The standard shadow under the rounded square.
        context.saveGState()
        shadow(dy: 10, blur: 22, alpha: 0.32)
        fill(path, Palette.tileBottom)
        context.restoreGState()
        clipped(to: path) {
            paintLinear(gradient([Palette.tileTop, Palette.tileBottom], [0, 1]),
                        from: CGPoint(x: 0, y: tile.minY), to: CGPoint(x: 0, y: tile.maxY))
            // Light from above, and a little weight in the lower corners.
            context.drawRadialGradient(gradient([rgb(0xffffff, 0.18), rgb(0xffffff, 0)], [0, 1]),
                                       startCenter: CGPoint(x: 512, y: 110), startRadius: 0,
                                       endCenter: CGPoint(x: 512, y: 110), endRadius: 720, options: [])
            context.drawRadialGradient(gradient([rgb(0x000000, 0), rgb(0x000000, 0.2)], [0.6, 1]),
                                       startCenter: CGPoint(x: 512, y: 512), startRadius: 0,
                                       endCenter: CGPoint(x: 512, y: 512), endRadius: 640, options: [])
        }
    }

    func drawMagnifier() {
        let silhouette = CGMutablePath()
        silhouette.addPath(circle(radius: ringOuterRadius))
        silhouette.addPath(handlePath())
        let everything = CGPath(rect: tile.insetBy(dx: -100, dy: -100), transform: nil)

        // Shadows on the tile: a broad soft one and a tight contact shadow.
        for (dy, blur, alpha) in [(20, 40, 0.38), (4, 8, 0.3)] as [(CGFloat, CGFloat, CGFloat)] {
            context.saveGState()
            shadow(dy: dy, blur: blur, alpha: alpha)
            fill(silhouette, Palette.tileBottom)
            context.restoreGState()
        }

        // The handle, slightly darker towards its end.
        clipped(to: handlePath()) {
            paintLinear(gradient([Palette.metalTop, Palette.metalBottom], [0, 1]),
                        from: CGPoint(x: lensCenter.x + 150, y: lensCenter.y + 150),
                        to: CGPoint(x: lensCenter.x + 440, y: lensCenter.y + 440))
        }

        // The lens: warm glass, lit from the upper left, darker at its rim.
        let lensRadius = ringOuterRadius - ringWidth
        clipped(to: circle(radius: lensRadius)) {
            paintLinear(gradient([Palette.glassLight, Palette.glassDark], [0, 1]),
                        from: CGPoint(x: lensCenter.x - lensRadius, y: lensCenter.y - lensRadius),
                        to: CGPoint(x: lensCenter.x + lensRadius, y: lensCenter.y + lensRadius))
            context.drawRadialGradient(gradient([rgb(0x7a3600, 0), rgb(0x7a3600, 0.16)], [0.72, 1]),
                                       startCenter: lensCenter, startRadius: 0, endCenter: lensCenter, endRadius: lensRadius,
                                       options: [])
            if detailed { drawLettering() }
            // The reflection on the glass.
            let gleam = CGPoint(x: lensCenter.x - lensRadius * 0.42, y: lensCenter.y - lensRadius * 0.46)
            context.drawRadialGradient(gradient([rgb(0xffffff, 0.55), rgb(0xffffff, 0)], [0, 1]),
                                       startCenter: gleam, startRadius: 0, endCenter: gleam, endRadius: lensRadius * 0.62,
                                       options: [])
        }

        // The ring, with a faint line where it meets the glass.
        clipped(to: ringPath(), evenOdd: true) {
            paintLinear(gradient([Palette.metalTop, Palette.metalBottom], [0, 1]),
                        from: CGPoint(x: 0, y: lensCenter.y - ringOuterRadius),
                        to: CGPoint(x: 0, y: lensCenter.y + ringOuterRadius))
        }
        context.saveGState()
        context.addPath(circle(radius: lensRadius + 1))
        context.setStrokeColor(rgb(0x7a3600, 0.14))
        context.setLineWidth(2)
        context.strokePath()
        context.restoreGState()
        _ = everything
    }

    func drawLettering() {
        let base = NSFont.systemFont(ofSize: letteringSize, weight: .heavy)
        let font = NSFont(descriptor: base.fontDescriptor.withDesign(.rounded) ?? base.fontDescriptor,
                          size: letteringSize) ?? base
        let text = NSAttributedString(string: "Aa", attributes: [.font: font, .foregroundColor: NSColor(cgColor: Palette.lettering)!])
        let line = CTLineCreateWithAttributedString(text)
        let bounds = CTLineGetBoundsWithOptions(line, [.useGlyphPathBounds])
        context.saveGState()
        shadow(dy: 3, blur: 6, alpha: 0.18)
        context.translateBy(x: lensCenter.x, y: lensCenter.y)
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
