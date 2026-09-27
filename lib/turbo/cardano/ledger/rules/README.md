# Ledger rules used in Conway

This directory owns ledger predicates and effects, including inherited rules and
batched implementations. Start with the [audit map](../../../../../doc/conway-agda-rule-map.md)
for rule ownership, state projections and known spec differences.

## Editing rules

- Shared rules retain their state classes, including `shelley::state` methods
  used by Conway. Keep one implementation.
- `.ipp` fragments compile inside their existing translation unit or class,
  retaining private types and inlining opportunities. Do not compile them separately.
- Rule families are ownership boundaries. Preserve fused preprocessing,
  partitioned joins and ordered state updates; avoid extra scans or barriers.
- Put predicates and substantive effects here. Keep codecs, storage, generic
  cryptography and the Plutus interpreter in their own modules.
- Era-entry translation lives in `../transition/`; historical version gates live
  beside their rules and in `compatibility/`.

## Coverage

After building Coverage, run from the repository root:

```sh
bash test/gen-coverage.sh --ledger-rules '*' tmp/coverage-ledger build-cov
```

The `coverage-ledger` CMake target uses the same generator and `COVERAGE_ARGS`.
Reports include HTML, `rules.json`, `rules.lcov` and `rules-summary.txt`,
covering production `.cpp`, `.hpp` and `.ipp` files under this directory.

Use both the full test suite and a separate Conway workload: source filtering
cannot identify which era exercised inherited code. Coverage measures execution,
not whether every premise or resulting state was asserted, or whether C++ agrees
with Agda.
