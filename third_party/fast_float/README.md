# fast_float (C++ header-only)

Vendored from https://github.com/fastfloat/fast_float, release `v8.0.2`: the
single-header amalgamation `fast_float.h` attached to that release,
SHA-256 `0883786faf4d98a2ffe97f29d839e2651ac29464a84c24f075a1d4fac8151711`.

Licensed Apache-2.0 OR MIT OR BSL-1.0 (see `LICENSE-APACHE`, `LICENSE-MIT`,
`LICENSE-BOOST`); we use it under MIT. Used by `lib/cellui/src/NumberParse.cpp`
where the standard library has no usable floating-point `std::from_chars`
(libc++ before a macOS 26.0 deployment target, whose implementation lives in
the system dylib).

To update: fetch `fast_float.h` from a newer release of the same repository,
and update the version and checksum here and in `THIRD_PARTY_LICENSES.md`.
