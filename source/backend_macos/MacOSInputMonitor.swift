/**
 * Installs modern AppKit pen capture and forwards framework-free values to C++.
 * UIKit capture is separate because it must attach to an application-owned view.
 */
import AppKit
import LightPHIAppleInterop

// Only the functions marked @_expose(Cxx) reach the generated C++ header; the C++ side needs nothing
// else, and the rest of this file names AppKit types C++ cannot spell.
public typealias CapturedEvent = lightphi.input.apple_detail.CapturedEvent
// Only the functions marked @_expose(Cxx) reach the generated C++ header; the C++ side needs nothing
// else, and the rest of this file names AppKit types C++ cannot spell.
public typealias CapturedEventKind = lightphi.input.apple_detail.CapturedEventKind
private typealias CapturedCapability = lightphi.input.apple_detail.CapturedCapability

private enum AppKitTabletCapability {
    static let tiltX = 0x0080
    static let tiltY = 0x0100
    static let pressure = 0x0400
    static let rotation = 0x2000
}

/// How long a delivery a busy receiver refused waits before it is offered again.
///
/// Retrying on the very next run-loop turn would spin the main thread for as long as the receiver
/// stays busy. A millisecond is well under one display frame, so the wait cannot be seen.
private let busyRetryDelay = DispatchTimeInterval.milliseconds(1)

/// The LightPHI capabilities an AppKit tablet reports in its proximity capability mask.
public func mapAppKitCapabilities(_ mask: Int, eraser: Bool) -> UInt16 {
    var capabilities = CapturedCapability.Timestamp.rawValue
    if mask & AppKitTabletCapability.pressure != 0 {
        capabilities |= CapturedCapability.Pressure.rawValue
    }
    if mask & AppKitTabletCapability.tiltX != 0,
       mask & AppKitTabletCapability.tiltY != 0
    {
        capabilities |= CapturedCapability.Tilt.rawValue
    }
    if mask & AppKitTabletCapability.rotation != 0 {
        capabilities |= CapturedCapability.Twist.rawValue
    }
    if eraser {
        capabilities |= CapturedCapability.Eraser.rawValue
    }
    return capabilities
}

/// The contact event one AppKit mouse event stands for, or nil for an event LightPHI ignores.
///
/// Only a tablet point carries pen measurements. AppKit reports a pressed mouse at full pressure,
/// which nothing measured, so an ordinary pointer contributes its position and time and nothing
/// else.
///
/// - Parameters:
///   - location: The event position with the origin at the top left of the drawing surface.
///   - eraser: Whether the last proximity event named the eraser end.
public func capturedEvent(from event: NSEvent, at location: CGPoint, eraser: Bool) -> CapturedEvent? {
    var captured = CapturedEvent()
    switch event.type {
    case .leftMouseDown:
        captured.Kind = .Begin
    case .leftMouseDragged:
        captured.Kind = .Update
    case .leftMouseUp:
        captured.Kind = .End
    default:
        return nil
    }
    captured.TimeNanoseconds = UInt64(max(event.timestamp, 0.0) * 1_000_000_000.0)
    captured.X = location.x
    captured.Y = location.y
    guard event.subtype == .tabletPoint else {
        return captured
    }
    captured.Pressure = Double(event.pressure)
    captured.TiltXRadians = event.tilt.x * (.pi / 2.0)
    captured.TiltYRadians = event.tilt.y * (.pi / 2.0)
    captured.TwistRadians = Double(event.rotation) * (.pi / 180.0)
    captured.Eraser = eraser
    return captured
}

private final class MacOSInputMonitor {
    private let sink: UnsafeMutablePointer<lightphi.input.apple_detail.AppleEventSink>
    private let acceptPointerFallback: Bool
    private var eventMonitor: Any?
    private var resignObserver: NSObjectProtocol?
    private var eraser = false
    private var observedTablet = false
    private var observedPointer = false
    private var retryScheduled = false

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
            guard let self else {
                return
            }
            self.retryIfPending(self.sink.pointee.Cancel())
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
            sink.pointee.ObserveDevice(mapAppKitCapabilities(event.capabilityMask, eraser: eraser))
            return
        }
        if event.type == .mouseCancelled {
            retryIfPending(sink.pointee.Cancel())
            return
        }

        // An ordinary pointer is still a real device, and refusing it would leave a machine with no
        // tablet unable to draw at all. What it cannot measure is simply absent from what it reports.
        if event.subtype != .tabletPoint {
            guard acceptPointerFallback else {
                return
            }
            if !observedTablet, !observedPointer {
                observedPointer = true
                sink.pointee.ObserveDevice(CapturedCapability.Timestamp.rawValue)
            }
        }
        guard let captured = capturedEvent(from: event, at: topLeftLocation(event), eraser: eraser) else {
            return
        }
        retryIfPending(sink.pointee.Submit(captured))
    }

    /// Offer refused deliveries again shortly, because no further event may ever arrive to do it.
    private func retryIfPending(_ pending: Bool) {
        guard pending, !retryScheduled else {
            return
        }
        retryScheduled = true
        DispatchQueue.main.asyncAfter(deadline: .now() + busyRetryDelay) { [weak self] in
            guard let self else {
                return
            }
            self.retryScheduled = false
            self.retryIfPending(self.sink.pointee.RetryPending())
        }
    }
}

@_expose(Cxx)
public func createAppleInputMonitor(
    _ sink: UnsafeMutableRawPointer,
    _ acceptPointerFallback: Bool
) -> UnsafeMutableRawPointer {
    Unmanaged.passRetained(
        MacOSInputMonitor(sink: sink, acceptPointerFallback: acceptPointerFallback)
    ).toOpaque()
}

@_expose(Cxx)
public func startAppleInputMonitor(_ pointer: UnsafeMutableRawPointer) -> Bool {
    Unmanaged<MacOSInputMonitor>.fromOpaque(pointer).takeUnretainedValue().start()
}

@_expose(Cxx)
public func stopAppleInputMonitor(_ pointer: UnsafeMutableRawPointer) {
    Unmanaged<MacOSInputMonitor>.fromOpaque(pointer).takeUnretainedValue().stop()
}

@_expose(Cxx)
public func destroyAppleInputMonitor(_ pointer: UnsafeMutableRawPointer) {
    Unmanaged<MacOSInputMonitor>.fromOpaque(pointer).release()
}
