/**
 * The file format for AppKit events and what LightPHI makes of them.
 *
 * The fixture test replays these files and the manual pen check writes them, so a recording from a
 * real device becomes a regression test without being retyped. Only the values the macOS backend
 * reads are kept.
 */
import AppKit

/// One recording or hand-written set of events, replayed in order.
struct AppKitEventFixture: Codable {
    /// "synthesized" for a hand-written file, or the device and machine a recording came from.
    var source: String
    var description: String
    var entries: [Entry]

    struct Entry: Codable {
        var event: Event
        /// For a proximity event: the capability bits it should report.
        var capabilities: UInt16?
        /// For a mouse event: what it should become, or nil when LightPHI ignores it.
        var captured: Captured?
    }

    struct Event: Codable {
        /// "tabletProximity" or an NSEvent mouse type name such as "leftMouseDown".
        var type: String
        /// "tabletPoint" or "mouseEvent"; absent for proximity.
        var subtype: String?
        /// "pen", "eraser" or "cursor"; proximity only.
        var pointerType: String?
        var capabilityMask: Int?
        var x: Double?
        var y: Double?
        var pressure: Double?
        var tiltX: Double?
        var tiltY: Double?
        /// Degrees, as AppKit reports it.
        var rotation: Double?
        /// Seconds since system start, as AppKit reports it.
        var timestamp: Double?
    }

    struct Captured: Codable, Equatable {
        var kind: String
        var x: Double
        var y: Double
        var timeNanoseconds: UInt64
        var pressure: Double
        var tiltXRadians: Double
        var tiltYRadians: Double
        var twistRadians: Double
        var eraser: Bool
    }
}

private let mouseTypes: [String: CGEventType] = [
    "leftMouseDown": .leftMouseDown,
    "leftMouseDragged": .leftMouseDragged,
    "leftMouseUp": .leftMouseUp,
    "rightMouseDown": .rightMouseDown,
]

private let pointerTypes: [String: Int64] = ["pen": 1, "cursor": 2, "eraser": 3]

extension AppKitEventFixture.Event {
    /// Record the values the backend reads from one real event.
    init?(recording event: NSEvent, at location: CGPoint) {
        if event.type == .tabletProximity {
            let names: [NSEvent.PointingDeviceType: String] = [.pen: "pen", .eraser: "eraser", .cursor: "cursor"]
            self.init(type: "tabletProximity", pointerType: names[event.pointingDeviceType] ?? "unknown",
                      capabilityMask: event.capabilityMask)
            return
        }
        guard let name = mouseTypes.first(where: { $0.value.rawValue == UInt32(event.type.rawValue) })?.key else {
            return nil
        }
        let tablet = event.subtype == .tabletPoint
        self.init(type: name, subtype: tablet ? "tabletPoint" : "mouseEvent", x: location.x, y: location.y,
                  pressure: tablet ? Double(event.pressure) : 0, tiltX: tablet ? event.tilt.x : 0,
                  tiltY: tablet ? event.tilt.y : 0, rotation: tablet ? Double(event.rotation) : 0,
                  timestamp: event.timestamp)
    }

    /// Rebuild an AppKit event carrying these values, through the same path the window server uses.
    func synthesize() -> NSEvent? {
        if type == "tabletProximity" {
            guard let event = CGEvent(source: nil) else {
                return nil
            }
            event.type = .tabletProximity
            event.setIntegerValueField(.tabletProximityEventPointerType, value: pointerTypes[pointerType ?? ""] ?? 0)
            event.setIntegerValueField(.tabletProximityEventCapabilityMask, value: Int64(capabilityMask ?? 0))
            event.setIntegerValueField(.tabletProximityEventEnterProximity, value: 1)
            return NSEvent(cgEvent: event)
        }
        guard let mouseType = mouseTypes[type],
              let event = CGEvent(mouseEventSource: nil, mouseType: mouseType,
                                  mouseCursorPosition: CGPoint(x: x ?? 0, y: y ?? 0),
                                  mouseButton: mouseType == .rightMouseDown ? .right : .left)
        else {
            return nil
        }
        if subtype == "tabletPoint" {
            event.setIntegerValueField(.mouseEventSubtype, value: 1)
            event.setDoubleValueField(.mouseEventPressure, value: pressure ?? 0)
            event.setDoubleValueField(.tabletEventTiltX, value: tiltX ?? 0)
            event.setDoubleValueField(.tabletEventTiltY, value: tiltY ?? 0)
            event.setDoubleValueField(.tabletEventRotation, value: rotation ?? 0)
        }
        event.timestamp = CGEventTimestamp((timestamp ?? 0) * 1_000_000_000.0)
        return NSEvent(cgEvent: event)
    }
}
