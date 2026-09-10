/**
 * Proves the macOS backend turns AppKit events into the values its delivery core expects.
 *
 * Each fixture file is replayed through real NSEvent objects rebuilt from CGEvent, so the test runs
 * without a window, a running application or a tablet. The delivery core that receives these values
 * is tested on its own in apple_delivery_contract.cpp.
 */
import AppKit
import LightPHIMacOSCapture

/// AppKit stores pressure in 1/255 steps and tilt in 1/32767 steps, so a value rebuilt from a
/// fixture is only as exact as those.
private let pressureTolerance = 1.0 / 255.0
private let angleTolerance = (Double.pi / 2.0) / 32767.0 * 2.0
private let timeToleranceNanoseconds: UInt64 = 1_000

private func close(_ left: Double, _ right: Double, _ tolerance: Double) -> Bool {
    abs(left - right) <= tolerance
}

private func matches(_ actual: CapturedEvent, _ expected: AppKitEventFixture.Captured) -> Bool {
    let kinds: [String: CapturedEventKind] = ["Begin": .Begin, "Update": .Update, "End": .End]
    let time = actual.TimeNanoseconds > expected.timeNanoseconds
        ? actual.TimeNanoseconds - expected.timeNanoseconds : expected.timeNanoseconds - actual.TimeNanoseconds
    return actual.Kind == kinds[expected.kind] && actual.X == expected.x && actual.Y == expected.y
        && time <= timeToleranceNanoseconds && close(actual.Pressure, expected.pressure, pressureTolerance)
        && close(actual.TiltXRadians, expected.tiltXRadians, angleTolerance)
        && close(actual.TiltYRadians, expected.tiltYRadians, angleTolerance)
        && close(actual.TwistRadians, expected.twistRadians, angleTolerance) && actual.Eraser == expected.eraser
}

/// Replay one fixture file and return a description of every entry that did not translate as recorded.
private func replay(_ fixture: AppKitEventFixture) -> [String] {
    var failures: [String] = []
    var eraser = false
    for (index, entry) in fixture.entries.enumerated() {
        guard let event = entry.event.synthesize() else {
            failures.append("entry \(index): cannot rebuild a \(entry.event.type) event")
            continue
        }
        if event.type == .tabletProximity {
            eraser = event.pointingDeviceType == .eraser
            let reported = mapAppKitCapabilities(event.capabilityMask, eraser: eraser)
            if reported != entry.capabilities {
                failures.append("entry \(index): capabilities \(reported), expected \(entry.capabilities ?? 0)")
            }
            continue
        }
        let location = CGPoint(x: entry.event.x ?? 0, y: entry.event.y ?? 0)
        let captured = capturedEvent(from: event, at: location, eraser: eraser)
        switch (captured, entry.captured) {
        case (nil, nil):
            break
        case let (actual?, expected?) where matches(actual, expected):
            break
        default:
            failures.append("entry \(index): \(entry.event.type) translated to \(String(describing: captured))")
        }
    }
    return failures
}

@main
struct MacOSCaptureFixtures {
    static func main() {
        let paths = CommandLine.arguments.dropFirst()
        guard !paths.isEmpty else {
            FileHandle.standardError.write("usage: lightphi_macos_capture_fixtures FIXTURE.json...\n".data(using: .utf8)!)
            exit(2)
        }
        var failed = false
        for path in paths {
            guard let data = FileManager.default.contents(atPath: path),
                  let fixture = try? JSONDecoder().decode(AppKitEventFixture.self, from: data)
            else {
                print("\(path): cannot read the fixture")
                failed = true
                continue
            }
            for failure in replay(fixture) {
                print("\(path): \(failure)")
                failed = true
            }
        }
        exit(failed ? 1 : 0)
    }
}
