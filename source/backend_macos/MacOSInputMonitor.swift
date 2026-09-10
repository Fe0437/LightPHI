/**
 * Installs modern AppKit pen capture and forwards framework-free values to C++.
 * UIKit capture is separate because it must attach to an application-owned view.
 */
import AppKit
import LightPHIAppleInterop

private enum CapturedKind {
    static let begin: UInt8 = 1
    static let update: UInt8 = 2
    static let end: UInt8 = 3
}

private enum Capability {
    static let pressure: UInt16 = 1 << 0
    static let timestamp: UInt16 = 1 << 4
    static let tilt: UInt16 = 1 << 5
    static let twist: UInt16 = 1 << 6
    static let eraser: UInt16 = 1 << 7
}

private enum AppKitTabletCapability {
    static let tiltX = 0x0080
    static let tiltY = 0x0100
    static let pressure = 0x0400
    static let rotation = 0x2000
}

public func mapAppKitCapabilities(_ mask: Int, eraser: Bool) -> UInt16 {
    var capabilities = Capability.timestamp
    if mask & AppKitTabletCapability.pressure != 0 {
        capabilities |= Capability.pressure
    }
    if mask & AppKitTabletCapability.tiltX != 0,
       mask & AppKitTabletCapability.tiltY != 0
    {
        capabilities |= Capability.tilt
    }
    if mask & AppKitTabletCapability.rotation != 0 {
        capabilities |= Capability.twist
    }
    if eraser {
        capabilities |= Capability.eraser
    }
    return capabilities
}

private final class MacOSInputMonitor {
    private let sink: UnsafeMutablePointer<lightphi.input.apple_detail.AppleEventSink>
    private let acceptPointerFallback: Bool
    private var eventMonitor: Any?
    private var resignObserver: NSObjectProtocol?
    private var eraser = false
    private var observedTablet = false
    private var observedPointer = false

    init(sink: UnsafeMutableRawPointer, acceptPointerFallback: Bool) {
        self.sink = sink.assumingMemoryBound(to: lightphi.input.apple_detail.AppleEventSink.self)
        self.acceptPointerFallback = acceptPointerFallback
    }

    /// The event position with the origin at the top left of the content view.
    ///
    /// AppKit measures a window from its bottom left and LightPHI reports from the top left, which
    /// is what every other backend and every consumer already uses. Converting here keeps that
    /// difference inside the one backend that has it.
    private func topLeftLocation(_ event: NSEvent) -> CGPoint {
        let location = event.locationInWindow
        guard let view = event.window?.contentView else {
            return location
        }
        let inView = view.convert(location, from: nil)
        return view.isFlipped ? inView : CGPoint(x: inView.x, y: view.bounds.height - inView.y)
    }

    func start() -> Bool {
        guard Thread.isMainThread, NSApp != nil, eventMonitor == nil else {
            return false
        }
        let mask: NSEvent.EventTypeMask = [
            .leftMouseDown,
            .leftMouseDragged,
            .leftMouseUp,
            .mouseCancelled,
            .tabletProximity,
        ]
        eventMonitor = NSEvent.addLocalMonitorForEvents(matching: mask) { [weak self] event in
            self?.capture(event)
            return event
        }
        resignObserver = NotificationCenter.default.addObserver(
            forName: NSApplication.didResignActiveNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            self?.sink.pointee.Cancel()
        }
        return eventMonitor != nil
    }

    func stop() {
        guard Thread.isMainThread else {
            return
        }
        if let eventMonitor {
            NSEvent.removeMonitor(eventMonitor)
            self.eventMonitor = nil
        }
        if let resignObserver {
            NotificationCenter.default.removeObserver(resignObserver)
            self.resignObserver = nil
        }
    }

    private func capture(_ event: NSEvent) {
        if event.type == .tabletProximity {
            eraser = event.pointingDeviceType == .eraser
            observedTablet = true
            let capabilities = mapAppKitCapabilities(event.capabilityMask, eraser: eraser)
            sink.pointee.ObserveDevice(capabilities)
            return
        }
        if event.type == .mouseCancelled {
            sink.pointee.Cancel()
            return
        }

        // A pen reports its pressure, tilt and rotation; an ordinary pointer reports none of them
        // and must not be described as though it did. Delivering it is still the honest answer: the
        // device is real, and refusing it would leave a machine with no tablet unable to draw at
        // all. What it cannot do is simply absent from the capabilities reported for it.
        let tablet = event.subtype == .tabletPoint
        if !tablet {
            guard acceptPointerFallback else {
                return
            }
            if !observedTablet, !observedPointer {
                observedPointer = true
                sink.pointee.ObserveDevice(Capability.timestamp)
            }
        }
        let kind: UInt8
        switch event.type {
        case .leftMouseDown:
            kind = CapturedKind.begin
        case .leftMouseDragged:
            kind = CapturedKind.update
        case .leftMouseUp:
            kind = CapturedKind.end
        default:
            return
        }
        let location = topLeftLocation(event)
        let tilt = tablet ? event.tilt : .zero
        let twist = tablet ? Double(event.rotation) * (.pi / 180.0) : 0.0
        let nanoseconds = UInt64(max(event.timestamp, 0.0) * 1_000_000_000.0)
        sink.pointee.SubmitMeasuredPoint(
            kind,
            nanoseconds,
            location.x,
            location.y,
            Double(event.pressure),
            tilt.x * (.pi / 2.0),
            tilt.y * (.pi / 2.0),
            twist,
            tablet && eraser
        )
    }
}

public func createAppleInputMonitor(
    _ sink: UnsafeMutableRawPointer,
    _ acceptPointerFallback: Bool
) -> UnsafeMutableRawPointer {
    Unmanaged.passRetained(
        MacOSInputMonitor(sink: sink, acceptPointerFallback: acceptPointerFallback)
    ).toOpaque()
}

public func startAppleInputMonitor(_ pointer: UnsafeMutableRawPointer) -> Bool {
    Unmanaged<MacOSInputMonitor>.fromOpaque(pointer).takeUnretainedValue().start()
}

public func stopAppleInputMonitor(_ pointer: UnsafeMutableRawPointer) {
    Unmanaged<MacOSInputMonitor>.fromOpaque(pointer).takeUnretainedValue().stop()
}

public func destroyAppleInputMonitor(_ pointer: UnsafeMutableRawPointer) {
    Unmanaged<MacOSInputMonitor>.fromOpaque(pointer).release()
}
