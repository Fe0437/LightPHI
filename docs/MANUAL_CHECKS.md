# Manual checks

The automated suite proves the contract, the delivery core and the AppKit
translation without a device. It cannot prove that a real tablet, on a real
machine, reaches a receiver the way the contract says. These checks do, and they
need a person, so they are never part of `just debug-test` or `just debug-smoke`.

Run them when the macOS backend changes how it reads the device, when a new kind
of pen value is read (tilt, twist, hover), and once for each new tablet or Mac
that matters.

Results stay private. They name the machine and the tablet, so they are kept in
`.private/manual-checks/`, which git ignores, and never in this repository.

## Pen check

```sh
just pen-check --record .private/manual-checks/session.json
```

A window opens. With the tablet pen:

1. Draw five slow strokes, pressing lightly and then hard.
2. Draw five fast strokes.
3. Flip the pen and erase across the window.
4. Start a stroke and switch to another application before lifting the pen.
5. Close the window.

The report is printed when the window closes and appended, under the date, to
`.private/manual-checks/pen-check-results.md`. Check that:

- **Delivery rules** says every batch is valid. Anything else is a bug in the
  backend; the report names the rule and the batch.
- **Capabilities seen** lists what the tablet can do. A pen with pressure must
  list `Pressure`; a pen held reversed must add `Eraser`.
- **Highest pressure** is close to 1 after pressing hard.
- **Contacts / cancelled** counts at least one cancellation, from step 4.
- No **Source failure** row appears.

Then run `just pen-check --pointer` with an ordinary mouse or trackpad and no pen
nearby, and draw one stroke. The report must list `Timestamp` alone and a highest
pressure of 0: a pointer measures no pen values, and LightPHI never invents them.

### Keeping the recording

`session.json` holds the raw AppKit events and what the backend made of each one.
To turn it into a regression test, copy it to `tests/fixtures/macos/` with a name
that says the device, for example `wacom-intuos-pro-m.json`, and the fixture test
replays it on every run. A later change to the translation that alters any of
those values then fails the suite. The copy becomes public, so replace its
`source` field, which names the machine and the tablet, with the device name
alone.
