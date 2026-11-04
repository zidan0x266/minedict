// minedict - BSD 3-Clause License; see README.
//
// Draws the app icon on the macOS icon grid: a 1024-point canvas holding an
// 824-point rounded square with continuous corners and a soft shadow below.
// The subject is an open dictionary seen from the front and a little above.
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
    static let tileTop = rgb(0x6f63d8)
    static let tileBottom = rgb(0x2f2868)
    static let coverTop = rgb(0x35304f)
    static let coverBottom = rgb(0x1a172b)
    static let coverRim = rgb(0xffffff, 0.22)
    static let pageEdgeDark = rgb(0xcfc6ad)
    static let pageEdgeLight = rgb(0xf3eddc)
    static let paperOuter = rgb(0xfdfaf0)
    static let paperInner = rgb(0xe9e2cd)
    static let gutter = rgb(0x2a2340, 0.38)
    static let ink = rgb(0xb9b09b)
    static let inkStrong = rgb(0x8c846f)
    static let headword = rgb(0x6b40e3)
    static let marker = rgb(0xf1fa8c, 0.9)
    static let tabs = [0xff79c6, 0xffb86c, 0x50fa7b, 0x8be9fd, 0xff5555].map { rgb($0) }
    static let ribbon = rgb(0xff5577)
    static let ribbonFold = rgb(0xc93a5a)
}

let colorSpace = CGColorSpace(name: CGColorSpace.sRGB)!

func gradient(_ colors: [CGColor], _ locations: [CGFloat]) -> CGGradient {
    CGGradient(colorsSpace: colorSpace, colors: colors as CFArray, locations: locations)!
}

// MARK: - Geometry (1024-point grid, y grows downwards)

let canvas: CGFloat = 1024
let tile = CGRect(x: 100, y: 100, width: 824, height: 824)
let tileRadius: CGFloat = 184

/// The book lies open. The near (lower) edge is wider than the far one, and
/// the pages bow a little along both edges.
let gutterX: CGFloat = 512
let farY: CGFloat = 300
let nearY: CGFloat = 716
let farHalfWidth: CGFloat = 280
let nearHalfWidth: CGFloat = 312
let gutterFarY: CGFloat = 332
let gutterNearY: CGFloat = 746
let coverRim: CGFloat = 22
/// The binding and the page block sit this much lower than the open pages,
/// so the near edge shows their thickness and the far edge only a sliver.
let depth: CGFloat = 14

/// Apple's continuous-curvature rounded square.
func tilePath() -> CGPath {
    RoundedRectangle(cornerRadius: tileRadius, style: .continuous).path(in: tile).cgPath
}

/// One page; `side` is -1 for the left page and +1 for the right.
func pagePath(side: CGFloat) -> CGPath {
    let path = CGMutablePath()
    let outerFar = CGPoint(x: gutterX + side * farHalfWidth, y: farY)
    let outerNear = CGPoint(x: gutterX + side * nearHalfWidth, y: nearY)
    path.move(to: CGPoint(x: gutterX, y: gutterFarY))
    path.addQuadCurve(to: outerFar, control: CGPoint(x: gutterX + side * farHalfWidth * 0.5, y: farY - 14))
    path.addLine(to: outerNear)
    path.addQuadCurve(to: CGPoint(x: gutterX, y: gutterNearY),
                      control: CGPoint(x: gutterX + side * nearHalfWidth * 0.5, y: nearY + 16))
    path.closeSubpath()
    return path
}

func bookPath() -> CGPath {
    let path = CGMutablePath()
    path.addPath(pagePath(side: -1))
    path.addPath(pagePath(side: 1))
    return path
}

/// The x of a page's outer edge at height `y`.
func outerEdgeX(side: CGFloat, y: CGFloat) -> CGFloat {
    let t = (y - farY) / (nearY - farY)
    return gutterX + side * (farHalfWidth + (nearHalfWidth - farHalfWidth) * t)
}

func roundedRect(_ rect: CGRect, radius: CGFloat) -> CGPath {
    CGPath(roundedRect: rect, cornerWidth: radius, cornerHeight: radius, transform: nil)
}

func shifted(_ path: CGPath, dy: CGFloat) -> CGPath {
    var transform = CGAffineTransform(translationX: 0, y: dy)
    return path.copy(using: &transform)!
}

// MARK: - Drawing

struct Painter {
    let context: CGContext
    /// Device pixels per grid point; shadows are specified in device space.
    let scale: CGFloat
    /// From 64 pixels up the icon shows the headword, the ribbon and the tabs.
    let detailed: Bool
    /// From 128 pixels up it also shows the lines of text.
    let fine: Bool

    func shadow(dy: CGFloat, blur: CGFloat, alpha: CGFloat) {
        context.setShadow(offset: CGSize(width: 0, height: -dy * scale), blur: blur * scale, color: rgb(0x000000, alpha))
    }

    func fill(_ path: CGPath, _ color: CGColor) {
        context.saveGState()
        context.addPath(path)
        context.setFillColor(color)
        context.fillPath()
        context.restoreGState()
    }

    /// Clips to `path` grown outwards by `amount` with rounded corners, then paints.
    func clipDilated(_ path: CGPath, by amount: CGFloat, _ paint: () -> Void) {
        context.saveGState()
        if amount > 0 {
            context.addPath(path)
            context.setLineWidth(amount * 2)
            context.setLineJoin(.round)
            context.setLineCap(.round)
            context.replacePathWithStrokedPath()
        }
        context.addPath(path)
        context.clip()
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
        drawBook()
        context.restoreGState()
    }

    func drawTile() {
        let path = tilePath()
        // The standard shadow under the rounded square.
        context.saveGState()
        shadow(dy: 10, blur: 22, alpha: 0.32)
        fill(path, Palette.tileBottom)
        context.restoreGState()
        context.saveGState()
        context.addPath(path)
        context.clip()
        paintLinear(gradient([Palette.tileTop, Palette.tileBottom], [0, 1]),
                    from: CGPoint(x: 0, y: tile.minY), to: CGPoint(x: 0, y: tile.maxY))
        // Light from above, and a little weight in the lower corners.
        context.drawRadialGradient(gradient([rgb(0xffffff, 0.16), rgb(0xffffff, 0)], [0, 1]),
                                   startCenter: CGPoint(x: 512, y: 120), startRadius: 0,
                                   endCenter: CGPoint(x: 512, y: 120), endRadius: 700, options: [])
        context.drawRadialGradient(gradient([rgb(0x000000, 0), rgb(0x000000, 0.22)], [0.6, 1]),
                                   startCenter: CGPoint(x: 512, y: 512), startRadius: 0,
                                   endCenter: CGPoint(x: 512, y: 512), endRadius: 640, options: [])
        context.restoreGState()
    }

    func drawBook() {
        let book = bookPath()
        let binding = shifted(book, dy: depth)
        let block = shifted(book, dy: depth - 2)
        let everything = CGPath(rect: tile.insetBy(dx: -100, dy: -100), transform: nil)

        // Shadows on the tile: a broad soft one and a tight contact shadow.
        for (dy, blur, alpha) in [(22, 44, 0.45), (5, 9, 0.35)] as [(CGFloat, CGFloat, CGFloat)] {
            context.saveGState()
            shadow(dy: dy, blur: blur, alpha: alpha)
            clipDilated(binding, by: coverRim) { fill(everything, Palette.coverBottom) }
            context.restoreGState()
        }

        // The binding, with a lit rim.
        clipDilated(binding, by: coverRim) { fill(everything, Palette.coverRim) }
        clipDilated(binding, by: coverRim - 2) {
            paintLinear(gradient([Palette.coverTop, Palette.coverBottom], [0, 1]),
                        from: CGPoint(x: 0, y: farY - coverRim + depth), to: CGPoint(x: 0, y: nearY + coverRim + depth))
        }

        // The block of pages, its edges stacked along the near side.
        clipDilated(block, by: 4) {
            fill(everything, Palette.pageEdgeLight)
            var y = farY
            while y < nearY + 80 {
                fill(CGPath(rect: CGRect(x: 0, y: y, width: canvas, height: 2.2), transform: nil), Palette.pageEdgeDark)
                y += 5
            }
        }

        // The pages: lighter at the outer edges, darker towards the gutter.
        for side in [CGFloat(-1), 1] {
            context.saveGState()
            context.addPath(pagePath(side: side))
            context.clip()
            paintLinear(gradient([Palette.paperOuter, Palette.paperInner], [0, 1]),
                        from: CGPoint(x: gutterX + side * nearHalfWidth, y: 0), to: CGPoint(x: gutterX, y: 0))
            context.restoreGState()
        }
        context.saveGState()
        context.addPath(book)
        context.clip()
        // The valley of the gutter, and light from above.
        paintLinear(gradient([rgb(0x2a2340, 0), Palette.gutter, Palette.gutter, rgb(0x2a2340, 0)], [0, 0.46, 0.54, 1]),
                    from: CGPoint(x: gutterX - 110, y: 0), to: CGPoint(x: gutterX + 110, y: 0))
        paintLinear(gradient([rgb(0xffffff, 0.2), rgb(0xffffff, 0)], [0, 1]),
                    from: CGPoint(x: 0, y: farY), to: CGPoint(x: 0, y: farY + 220))
        // The light comes from the upper left, so the right page is a shade darker.
        paintLinear(gradient([rgb(0xffffff, 0.07), rgb(0xffffff, 0), rgb(0x2a2340, 0), rgb(0x2a2340, 0.09)], [0, 0.4, 0.6, 1]),
                    from: CGPoint(x: gutterX - nearHalfWidth, y: 0), to: CGPoint(x: gutterX + nearHalfWidth, y: 0))
        context.restoreGState()

        if fine { drawText() }
        if detailed {
            drawThumbIndex()
            drawRibbon()
            drawHeadword()
        }
    }

    /// A line of greeked text.
    func line(y: CGFloat, from x0: CGFloat, to x1: CGFloat, color: CGColor, height: CGFloat = 12) {
        fill(roundedRect(CGRect(x: x0, y: y, width: x1 - x0, height: height), radius: height / 2), color)
    }

    func drawText() {
        let margin: CGFloat = 36
        let pitch: CGFloat = 30
        // Left page: the entry body under the headword.
        let leftLengths: [CGFloat] = [0.88, 1, 0.7, 0.95, 1, 0.58, 0.92, 0.8, 0.45]
        for (index, length) in leftLengths.enumerated() {
            let y = 410 + CGFloat(index) * pitch
            let x0 = outerEdgeX(side: -1, y: y) + margin
            let x1 = gutterX - margin
            line(y: y, from: x0, to: x0 + (x1 - x0) * length, color: Palette.ink)
        }
        // Right page: the next entries, one line marked.
        let rightLengths: [CGFloat] = [0.6, 1, 0.9, 0.75, 1, 0.5, 0.85, 1, 0.66, 0.9, 0.4]
        for (index, length) in rightLengths.enumerated() {
            let y = 344 + CGFloat(index) * pitch
            let x0 = gutterX + margin
            let x1 = outerEdgeX(side: 1, y: y) - margin
            let end = x0 + (x1 - x0) * length
            if index == 4 {
                fill(roundedRect(CGRect(x: x0 - 10, y: y - 7, width: end - x0 + 20, height: 26), radius: 8), Palette.marker)
                line(y: y, from: x0, to: end, color: Palette.inkStrong)
            } else {
                line(y: y, from: x0, to: end, color: index == 0 || index == 7 ? Palette.inkStrong : Palette.ink)
            }
        }
    }

    func drawHeadword() {
        let base = NSFont.systemFont(ofSize: 94, weight: .heavy)
        let font = NSFont(descriptor: base.fontDescriptor.withDesign(.rounded) ?? base.fontDescriptor, size: 94) ?? base
        let text = NSAttributedString(string: "Aa", attributes: [.font: font, .foregroundColor: NSColor(cgColor: Palette.headword)!])
        let textLine = CTLineCreateWithAttributedString(text)
        let bounds = CTLineGetBoundsWithOptions(textLine, [.useGlyphPathBounds])
        let x0 = outerEdgeX(side: -1, y: 370) + 36
        context.saveGState()
        context.translateBy(x: x0, y: 378)
        context.scaleBy(x: 1, y: -1)  // Core Text draws with y upwards
        context.textPosition = CGPoint(x: -bounds.minX, y: -bounds.maxY)
        CTLineDraw(textLine, context)
        context.restoreGState()
    }

    func drawThumbIndex() {
        let count = Palette.tabs.count
        for (index, color) in Palette.tabs.enumerated() {
            let t = (CGFloat(index) + 0.5) / CGFloat(count)
            let y = farY + 40 + (nearY - farY - 80) * t
            let x = outerEdgeX(side: 1, y: y - depth) + coverRim - 8
            let tab = CGRect(x: x, y: y - 24, width: 30, height: 48)
            context.saveGState()
            shadow(dy: 3, blur: 6, alpha: 0.3)
            fill(roundedRect(tab, radius: 9), color)
            context.restoreGState()
            fill(CGPath(rect: CGRect(x: x, y: y - 24, width: 8, height: 48), transform: nil), rgb(0x000000, 0.12))
        }
    }

    func drawRibbon() {
        // From the far end of the gutter, across the right page, over the near edge.
        let top = CGPoint(x: gutterX - 2, y: gutterFarY - 6)
        let width: CGFloat = 26
        let bottomY = nearY + depth + coverRim + 48
        let drift: CGFloat = 46
        let path = CGMutablePath()
        path.move(to: top)
        path.addLine(to: CGPoint(x: top.x + width, y: top.y))
        path.addLine(to: CGPoint(x: top.x + width + drift, y: bottomY))
        path.addLine(to: CGPoint(x: top.x + width / 2 + drift, y: bottomY - 18))
        path.addLine(to: CGPoint(x: top.x + drift, y: bottomY))
        path.closeSubpath()
        context.saveGState()
        shadow(dy: 6, blur: 10, alpha: 0.35)
        fill(path, Palette.ribbon)
        context.restoreGState()
        // A fold line along the edge, and the light the paper throws back on it.
        context.saveGState()
        context.addPath(path)
        context.clip()
        paintLinear(gradient([Palette.ribbonFold, Palette.ribbon, Palette.ribbon], [0, 0.35, 1]),
                    from: CGPoint(x: top.x, y: 0), to: CGPoint(x: top.x + width + drift, y: 0))
        paintLinear(gradient([rgb(0xffffff, 0.18), rgb(0xffffff, 0)], [0, 1]),
                    from: CGPoint(x: 0, y: top.y), to: CGPoint(x: 0, y: top.y + 200))
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
    Painter(context: context, scale: scale, detailed: pixels >= 64, fine: pixels >= 128).draw()
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
