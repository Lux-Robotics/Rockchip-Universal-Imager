#!/usr/bin/env swift
// Generates packaging/macos/dmg-background.png for the installer DMG window.
//
//   swift packaging/macos/generate-dmg-background.swift [out.png]
//
// Logical size 660×400; rendered @2x for Retina Finder windows.

import AppKit
import Foundation

let width = 660
let height = 400
let size = NSSize(width: width, height: height)
let image = NSImage(size: size)
image.lockFocus()

let rect = NSRect(origin: .zero, size: size)
let gradient = NSGradient(colors: [
    NSColor(calibratedRed: 0.11, green: 0.13, blue: 0.18, alpha: 1),
    NSColor(calibratedRed: 0.16, green: 0.19, blue: 0.26, alpha: 1),
])!
gradient.draw(in: rect, angle: 90)

// Soft panel
let panel = NSBezierPath(
    roundedRect: NSRect(x: 40, y: 70, width: 580, height: 260),
    xRadius: 18,
    yRadius: 18
)
NSColor(calibratedWhite: 1.0, alpha: 0.06).setFill()
panel.fill()

// Title
let title = "Install Rockchip Universal Imager" as NSString
let titleAttrs: [NSAttributedString.Key: Any] = [
    .font: NSFont.systemFont(ofSize: 22, weight: .semibold),
    .foregroundColor: NSColor(calibratedWhite: 0.95, alpha: 1),
]
let titleSize = title.size(withAttributes: titleAttrs)
title.draw(
    at: NSPoint(x: (CGFloat(width) - titleSize.width) / 2, y: 330),
    withAttributes: titleAttrs
)

// Instruction
let instr = "Drag the folder to Applications, then open the app inside" as NSString
let instrAttrs: [NSAttributedString.Key: Any] = [
    .font: NSFont.systemFont(ofSize: 13, weight: .regular),
    .foregroundColor: NSColor(calibratedWhite: 0.75, alpha: 1),
]
let instrSize = instr.size(withAttributes: instrAttrs)
instr.draw(
    at: NSPoint(x: (CGFloat(width) - instrSize.width) / 2, y: 50),
    withAttributes: instrAttrs
)

// Arrow between icon drop zones
let arrowColor = NSColor(calibratedRed: 0.45, green: 0.72, blue: 1.0, alpha: 1)
arrowColor.setFill()
arrowColor.setStroke()

let shaft = NSBezierPath()
shaft.lineWidth = 10
shaft.lineCapStyle = .round
shaft.move(to: NSPoint(x: 250, y: 200))
shaft.line(to: NSPoint(x: 380, y: 200))
shaft.stroke()

let head = NSBezierPath()
head.move(to: NSPoint(x: 375, y: 220))
head.line(to: NSPoint(x: 420, y: 200))
head.line(to: NSPoint(x: 375, y: 180))
head.close()
head.fill()

// Faint drop zones (icons sit on top in Finder)
func drawZone(x: CGFloat) {
    let zone = NSBezierPath(
        roundedRect: NSRect(x: x, y: 145, width: 130, height: 130),
        xRadius: 16,
        yRadius: 16
    )
    NSColor(calibratedWhite: 1.0, alpha: 0.04).setFill()
    zone.fill()
    NSColor(calibratedWhite: 1.0, alpha: 0.08).setStroke()
    zone.lineWidth = 1
    zone.stroke()
}
drawZone(x: 95)
drawZone(x: 435)

image.unlockFocus()

guard let tiff = image.tiffRepresentation,
      let rep = NSBitmapImageRep(data: tiff),
      let png = rep.representation(using: .png, properties: [:])
else {
    fputs("Failed to encode PNG\n", stderr)
    exit(1)
}

let defaultOut = URL(fileURLWithPath: #filePath)
    .deletingLastPathComponent()
    .appendingPathComponent("dmg-background.png")
    .path
let out = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : defaultOut
try png.write(to: URL(fileURLWithPath: out))
print("Wrote \(out)")
