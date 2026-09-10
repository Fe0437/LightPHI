/**
 * The window, hardware description and event recording of the manual pen check.
 *
 * pen_check.cpp drives LightPHI and judges what it delivers. This file only gives the pen somewhere
 * to draw, says which machine and tablet the numbers came from, and records the raw AppKit events so
 * a real session can be replayed later as a fixture.
 */
import AppKit
import LightPHIMacOSCapture

private final class PenCheckSession: NSObject, NSWindowDelegate {
    let window: NSWindow
    let recordPath: String?
    var monitor: Any?
    var entries: [AppKitEventFixture.Entry] = []
    var tablets: Set<String> = []
    var eraser = false

    init(recordPath: String?) {
        self.recordPath = recordPath
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 900, height: 600),
                          styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
        super.init()
        window.title = "LightPHI pen check: follow docs/MANUAL_CHECKS.md, then close this window"
        window.delegate = self
        window.isReleasedWhenClosed = false
        window.center()
    }

    func start() {
        monitor = NSEvent.addLocalMonitorForEvents(
            matching: [.leftMouseDown, .leftMouseDragged, .leftMouseUp, .tabletProximity]
        ) { [weak self] event in
            self?.record(event)
            return event
        }
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    private func record(_ event: NSEvent) {
        if event.type == .tabletProximity {
            eraser = event.pointingDeviceType == .eraser
            tablets.insert(String(format: "vendor 0x%04x tablet 0x%04x", event.vendorID, event.tabletID))
        }
        let location = topLeftLocation(event)
        guard let recorded = AppKitEventFixture.Event(recording: event, at: location) else {
            return
        }
        let expected = event.type == .tabletProximity ? nil : capturedEvent(from: event, at: location, eraser: eraser)
        entries.append(AppKitEventFixture.Entry(
            event: recorded,
            capabilities: event.type == .tabletProximity
                ? mapAppKitCapabilities(event.capabilityMask, eraser: eraser) : nil,
            captured: expected.map {
                AppKitEventFixture.Captured(kind: kindName($0.Kind), x: $0.X, y: $0.Y,
                                            timeNanoseconds: $0.TimeNanoseconds, pressure: $0.Pressure,
                                            tiltXRadians: $0.TiltXRadians, tiltYRadians: $0.TiltYRadians,
                                            twistRadians: $0.TwistRadians, eraser: $0.Eraser)
            }
        ))
    }

    private func topLeftLocation(_ event: NSEvent) -> CGPoint {
        guard let view = event.window?.contentView else {
            return event.locationInWindow
        }
        let inView = view.convert(event.locationInWindow, from: nil)
        return view.isFlipped ? inView : CGPoint(x: inView.x, y: view.bounds.height - inView.y)
    }

    private func kindName(_ kind: CapturedEventKind) -> String {
        switch kind {
        case .Begin: return "Begin"
        case .Update: return "Update"
        case .End: return "End"
        default: return "Other"
        }
    }

    var hardware: String {
        var size = 0
        sysctlbyname("hw.model", nil, &size, nil, 0)
        var model = [CChar](repeating: 0, count: max(size, 1))
        sysctlbyname("hw.model", &model, &size, nil, 0)
        let tablet = tablets.isEmpty ? "no tablet seen" : tablets.sorted().joined(separator: ", ")
        return "\(String(cString: model)), \(ProcessInfo.processInfo.operatingSystemVersionString), \(tablet)"
    }

    func windowWillClose(_: Notification) {
        if let monitor {
            NSEvent.removeMonitor(monitor)
        }
        if let recordPath {
            let fixture = AppKitEventFixture(source: hardware, description: "Recorded by lightphi_pen_check.",
                                             entries: entries)
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            if let data = try? encoder.encode(fixture) {
                FileManager.default.createFile(atPath: recordPath, contents: data)
            }
        }
        NSApp.stop(nil)
        // stop takes effect after the current event; post one so run returns now.
        NSApp.postEvent(NSEvent.otherEvent(with: .applicationDefined, location: .zero, modifierFlags: [],
                                           timestamp: 0, windowNumber: 0, context: nil, subtype: 0,
                                           data1: 0, data2: 0)!, atStart: true)
    }
}

private var session: PenCheckSession?

/// Create the application and its window, so LightPHI can be started on the main thread.
@_cdecl("lightphi_pen_check_open")
public func penCheckOpen(_ recordPath: UnsafePointer<CChar>?) {
    _ = NSApplication.shared
    NSApp.setActivationPolicy(.regular)
    session = PenCheckSession(recordPath: recordPath.map { String(cString: $0) })
    session?.start()
}

/// Run until the person closes the window.
@_cdecl("lightphi_pen_check_run")
public func penCheckRun() {
    NSApp.run()
}

/// The machine, operating system and tablets seen; the caller frees the returned string.
@_cdecl("lightphi_pen_check_hardware")
public func penCheckHardware() -> UnsafeMutablePointer<CChar>? {
    strdup(session?.hardware ?? "unknown")
}
