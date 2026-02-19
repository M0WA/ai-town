# Procedural Generation Test Seeds

- All procedural generators accept an explicit `uint64_t seed` parameter; tests use fixed seeds and log seed on failure
- On RapidCheck failure: print `// Reproduce with seed: 0x<hex>`; a fixed-seed regression test must be added to the standard suite before the nightly finding is closed
