import AppKit

// Refined A: thinner strokes, larger glyph, clean hollow keyhole.
// Preview at 512.

func makeRep(pixels size: Int) -> (NSBitmapImageRep, CGFloat) {
    let rep = NSBitmapImageRep(
        bitmapDataPlanes: nil,
        pixelsWide: size,
        pixelsHigh: size,
        bitsPerSample: 8,
        samplesPerPixel: 4,
        hasAlpha: true,
        isPlanar: false,
        colorSpaceName: .deviceRGB,
        bytesPerRow: 0,
        bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    return (rep, CGFloat(size))
}

func bg(s: CGFloat) {
    NSGradient(colors: [
        NSColor(calibratedRed: 0.03, green: 0.08, blue: 0.18, alpha: 1),
        NSColor(calibratedRed: 0.08, green: 0.24, blue: 0.50, alpha: 1),
    ], atLocations: [0, 1], colorSpace: .deviceRGB)!
        .draw(in: NSRect(x: 0, y: 0, width: s, height: s), angle: 90)
}

func drawGlyph(s: CGFloat) {
    let strokeW = s * 0.032
    let w = s * 0.60, h = w * 0.78
    let rect = NSRect(x: (s - w) / 2 - s * 0.02, y: s * 0.22, width: w, height: h)

    // shackle
    let sr = w * 0.24
    let shackle = NSBezierPath()
    shackle.appendArc(withCenter: NSPoint(x: rect.midX, y: rect.maxY + sr * 0.1),
                      radius: sr, startAngle: -25, endAngle: 205, clockwise: false)
    shackle.lineWidth = strokeW
    NSColor.white.setStroke()
    shackle.stroke()

    // body outline
    let r = h * 0.18
    let body = NSBezierPath(roundedRect: rect.insetBy(dx: strokeW / 2, dy: strokeW / 2),
                            xRadius: r, yRadius: r)
    body.lineWidth = strokeW
    body.lineJoinStyle = .round
    body.stroke()

    // tail
    let tx = rect.minX + w * 0.25
    let tailLen = w * 0.09
    let tail = NSBezierPath()
    tail.move(to: NSPoint(x: tx, y: rect.minY + strokeW * 0.4))
    tail.line(to: NSPoint(x: tx - tailLen, y: rect.minY - tailLen * 0.9))
    tail.line(to: NSPoint(x: tx + tailLen * 1.7, y: rect.minY + strokeW * 0.4))
    tail.lineWidth = strokeW
    tail.lineCapStyle = .round
    tail.lineJoinStyle = .round
    tail.stroke()

    // hollow keyhole: stroked circle + short stem line
    let kr = w * 0.09
    let cx = rect.midX, cy = rect.minY + h * 0.45
    let circle = NSBezierPath(ovalIn: NSRect(x: cx - kr, y: cy - kr * 0.2, width: kr * 2, height: kr * 2))
    circle.lineWidth = strokeW
    circle.stroke()
    let stem = NSBezierPath()
    stem.move(to: NSPoint(x: cx, y: cy - kr * 0.5))
    stem.line(to: NSPoint(x: cx, y: cy - kr * 1.5))
    stem.lineWidth = strokeW
    stem.lineCapStyle = .round
    stem.stroke()
}

let (rep, s) = makeRep(pixels: 512)
bg(s: s)
drawGlyph(s: s)
try? rep.representation(using: .png, properties: [:])!
    .write(to: URL(fileURLWithPath: "/tmp/oim_icons_v7/A_refined.png"))
NSGraphicsContext.restoreGraphicsState()
print("wrote /tmp/oim_icons_v7/A_refined.png")
