# LightPHI documentation

LightPHI is a portable C++23 pen and tablet input interface. Start with the
[README](../README.md) for what the library does and how to build it.

Read the [glossary](GLOSSARY.md) first. It fixes the meaning of *contact*, *batch*,
*sample*, *capability*, *prediction* and *correction*, which every other document
uses without redefining them.

## Design

- [Architecture](ARCHITECTURE.md) — the core/backend split, delivery rules, what may
  depend on what, and the known limitations. Read before moving a responsibility or
  adding a dependency.
- [API guidelines](API_GUIDELINES.md) — the rules an exported module surface follows.
  Read before changing a contract.
- [Package map](PACKAGE_MAP.md) — which directory and build target owns what.

## Working on LightPHI

- [Developer commands](DEVELOPER_COMMANDS.md) — every command, what it builds, and
  where the output goes.
- [Test strategy](TEST_STRATEGY.md) — what each test owns, and how to tell whether a
  new test can actually fail.
- [Sanitizer support](SANITIZER_SUPPORT.md) — the ASan, UBSan and TSan presets and
  when each is worth running.

`CONTRIBUTING.md` covers how to propose a change. `AGENTS.md` is the same policy
written for automated contributors.

```{toctree}
:hidden:
:caption: Reference

GLOSSARY
```

```{toctree}
:hidden:
:caption: Design

ARCHITECTURE
API_GUIDELINES
PACKAGE_MAP
```

```{toctree}
:hidden:
:caption: Working on LightPHI

DEVELOPER_COMMANDS
TEST_STRATEGY
SANITIZER_SUPPORT
```

```{toctree}
:hidden:
:caption: API reference

api/index
```
