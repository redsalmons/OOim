import AppKit

// 零海 OceanTalk icon: ocean gradient + envelope + padlock + waves.
// Draws into an NSBitmapImageRep with explicit pixel dimensions so the
// output is exact regardless of the display backing scale factor.

func drawIcon(pixels size: Int) -> NSBitmapImageRep {
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

    let s = CGFloat(size)
    let rect = NSRect(x: 0, y: 0, width: s, height: s)

    // Background: deep ocean -> teal gradient, full-bleed square
    // (macOS applies the squircle mask itself).
    let top = NSColor(calibratedRed: 0.05, green: 0.32, blue: 0.62, alpha: 1) // #0D52A1
    let bottom = NSColor(calibratedRed: 0.02, green: 0.62, blue: 0.72, alpha: 1) // #059EB8
    let gradient = NSGradient(colors: [bottom, top], atLocations: [0, 1], colorSpace: .deviceRGB)!
    gradient.draw(in: rect, angle: 90)

    // Waves: two translucent arcs near the bottom
    func wave(baseY: CGFloat, amp: CGFloat, alpha: CGFloat) {
        let path = NSBezierPath()
        path.move(to: NSPoint(x: 0, y: baseY))
        var x: CGFloat = 0
        while x <= s {
            path.curve(
                to: NSPoint(x: x + s * 0.25, y: baseY),
                controlPoint1: NSPoint(x: x + s * 0.08, y: baseY + amp),
                controlPoint2: NSPoint(x: x + s * 0.17, y: baseY - amp))
            x += s * 0.25
        }
        path.line(to: NSPoint(x: s, y: 0))
        path.line(to: NSPoint(x: 0, y: 0))
        path.close()
        NSColor.white.withAlphaComponent(alpha).setFill()
        path.fill()
    }
    wave(baseY: s * 0.16, amp: s * 0.03, alpha: 0.10)
    wave(baseY: s * 0.10, amp: s * 0.025, alpha: 0.16)

    // Envelope: centered, slight upward bias
    let envW = s * 0.56
    let envH = envW * 0.66
    let envRect = NSRect(
        x: (s - envW) / 2,
        y: s * 0.36,
        width: envW,
        height: envH)

    // Soft drop shadow under the envelope
    let shadow = NSShadow()
    shadow.shadowColor = NSColor.black.withAlphaComponent(0.28)
    shadow.shadowOffset = NSSize(width: 0, height: -s * 0.012)
    shadow.shadowBlurRadius = s * 0.02

    // Envelope body
    let bodyPath = NSBezierPath(roundedRect: envRect, xRadius: envH * 0.10, yRadius: envH * 0.10)
    NSGraphicsContext.saveGraphicsState()
    shadow.set()
    NSColor.white.setFill()
    bodyPath.fill()
    NSGraphicsContext.restoreGraphicsState()

    // Envelope flap: V chevron from top corners to center
    let flap = NSBezierPath()
    flap.move(to: NSPoint(x: envRect.minX + envW * 0.08, y: envRect.maxY - envH * 0.10))
    flap.line(to: NSPoint(x: envRect.midX, y: envRect.minY + envH * 0.34))
    flap.line(to: NSPoint(x: envRect.maxX - envW * 0.08, y: envRect.maxY - envH * 0.10))
    flap.lineWidth = envH * 0.085
    flap.lineCapStyle = .round
    flap.lineJoinStyle = .round
    top.withAlphaComponent(0.9).setStroke()
    flap.stroke()

    // Padlock badge at bottom-right of the envelope
    let lockW = s * 0.15
    let lockH = lockW * 0.82
    let lockX = envRect.maxX - lockW * 0.55
    let lockY = envRect.minY - lockH * 0.18

    // Shackle
    let shackle = NSBezierPath()
    let shackleR = lockW * 0.30
    let shackleCY = lockY + lockH + shackleR * 0.55
    shackle.appendArc(
        withCenter: NSPoint(x: lockX + lockW / 2, y: shackleCY),
        radius: shackleR,
        startAngle: -20, endAngle: 200, clockwise: false)
    shackle.lineWidth = lockW * 0.13
    NSColor(calibratedRed: 0.05, green: 0.30, blue: 0.58, alpha: 1).setStroke()
    shackle.stroke()

    // Lock body
    let lockBody = NSBezierPath(
        roundedRect: NSRect(x: lockX, y: lockY, width: lockW, height: lockH),
        xRadius: lockW * 0.16, yRadius: lockW * 0.16)
    NSColor(calibratedRed: 0.05, green: 0.30, blue: 0.58, alpha: 1).setFill()
    lockBody.fill()

    // Keyhole
    let keyholeR = lockW * 0.11
    let keyhole = NSBezierPath(
        ovalIn: NSRect(
            x: lockX + lockW / 2 - keyholeR,
            y: lockY + lockH * 0.48,
            width: keyholeR * 2, height: keyholeR * 2))
    NSColor.white.setFill()
    keyhole.fill()
    let stem = NSBezierPath(
        roundedRect: NSRect(
            x: lockX + lockW / 2 - keyholeR * 0.38,
            y: lockY + lockH * 0.22,
            width: keyholeR * 0.76, height: lockH * 0.30),
        xRadius: keyholeR * 0.3, yRadius: keyholeR * 0.3)
    stem.fill()

    NSGraphicsContext.restoreGraphicsState()
    return rep
}

let outDir = CommandLine.arguments.count > 1
    ? CommandLine.arguments[1]
    : "macos/Runner/Assets.xcassets/AppIcon.appiconset"

for side in [16, 32, 64, 128, 256, 512, 1024] {
    let rep = drawIcon(pixels: side)
    let path = "\(outDir)/app_icon_\(side).png"
    do {
        try rep.representation(using: .png, properties: [:])!
            .write(to: URL(fileURLWithPath: path))
        print("wrote \(path)")
    } catch {
        fputs("failed \(path): \(error)\n", stderr)
    }
}
