# API guidelines

The public API uses C++23 named modules and is compiled with its consumer.

- Use lowercase dotted module names and lowercase nested namespaces.
- Use `PascalCase` for exported types, functions, methods, enum values, and
  aggregate fields.
- Prefix runtime interfaces with `I`.
- Use fixed-width integers for stable identities and enum storage.
- Use `std::expected` for recoverable failures. Exceptions stay disabled.
- Use `std::span` only for synchronous borrowed sequences.
- Keep allocation and concrete platform objects behind the source interface.
- Bound every externally chosen count before allocation or copying.
- Report absent device features as absent. Do not synthesize them.

Every exported declaration needs a short responsibility comment. A public
signature, enum value, or value layout change requires API and native ABI test
review.

