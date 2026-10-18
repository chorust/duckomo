# N1/N2 independent review refresh — 2026-10-19

Status: **blocked**

DuckDB: v1.5.4

## Findings

- N1: blocked. The regenerated 377-case capability oracle has 201 accepted and 176 rejected cases over 17 reachable rejection classes. The renamed full-span fixture records its actual earlier `multiple-longitude-wraps` rejection. The later non-periodic full-extent guard is unreachable after periodic and wrap checks. All seven selected N1 commands exited zero across v1.5.4–v1.5.6. The reference Python independently models that nominal case as rejected by the wrap check.
- N2: blocked. The aggregate-WKB fixture exceeds the 16 MiB collection cap by 77 bytes, while each WKB is below the per-region 1 MiB cap, aggregate vertex count is 1,048,567 (<1,048,576), and estimated owned storage is below 64 MiB. It asserts the specific total-WKB diagnostic. All 24 selected N2 commands exited zero across v1.5.4–v1.5.6.
- Remaining boundaries: no exhaustive binary64/target-space proof for N1 and no exhaustive parser permutation or combined-invalid precedence cross-product for N2. These limits are recorded; neither gate is promoted.

## Locked identities

- Capability reference SHA-256: `41a82525730b52ea8d1c7fc98bd1435cab8e33a02aa233e93667d3d311154e39`
- Reference manifest SHA-256: `08798b8365a1b03591865477273049807f2a50b4b50556ff804a62dffd5e8cfd`
- N1 comparator source SHA-256: `f76f9904fd9cc3b17b81b595e8d2168b26204d428744c322f6e11142ad2af4b9`
- N2 region input test SHA-256: `d519ab7c368f297d3bb4d6a24da4bcbaaa5c63431b312688988d993d2829934b`
- N2 SQL profile SHA-256: `2acb08e3d5cf9f320554dfaab336ca111ce8990e1acfa2ca558af67fb024aa57`
- Commands SHA-256: `3b87caf8966eb8e360fe9879e9d4117eabb91ec97022a9635e93252bba66966c`
- Inputs SHA-256: `5e64f5993a8d5927e4c6246ed145804149c4291c7d91c04afb63f1869b2b8608`
- Manifest SHA-256: `65cf33db877ea7cd2f3002e68a0c9cc899dcf6e5f7950518eafb5ecd3438759c`

## Command record

### N1: blocked

- exit 0: `nearest_sampling_test`; output `N1-01.stdout.txt` SHA-256 `deb0986da499622355951d2b0da97b47176351efc2768fbd59aad0f75d84f3d2`
- exit 0: `region_geometry_test`; output `N1-02.stdout.txt` SHA-256 `12c11916f00549cc40bc2a7509cb5a7fb302724cb823cd67ae2e5a82a896c4be`
- exit 0: `nearest_sampling_test`; output `N1-03.stdout.txt` SHA-256 `deb0986da499622355951d2b0da97b47176351efc2768fbd59aad0f75d84f3d2`
- exit 0: `region_geometry_test`; output `N1-04.stdout.txt` SHA-256 `ac79df989598e53e753c059e3eb34cbea29958af836dc64310847f9616ca9816`
- exit 0: `nearest_sampling_test`; output `N1-05.stdout.txt` SHA-256 `deb0986da499622355951d2b0da97b47176351efc2768fbd59aad0f75d84f3d2`
- exit 0: `region_geometry_test`; output `N1-06.stdout.txt` SHA-256 `b967308c3952a59102dd151b71af3066065bf503fecb641a4f7ead41d1f0481d`
- exit 0: `spatial_layout_test`; output `N1-07.stdout.txt` SHA-256 `ec79a5034a866bb06266adde78ccca97cf411016922e919fe2d92771b3241008`

### N2: blocked

- exit 0: `unittest`; output `N2-08.stdout.txt` SHA-256 `f2014af769aa200d65e74cdbbbda9cc030cf8793bc6445de856e91ecfa2e1fe2`
- exit 0: `unittest`; output `N2-09.stdout.txt` SHA-256 `9f1dab7fdacd2356c974428fbd4bf657fc37f1d376ae573584790cd14c98f7e3`
- exit 0: `unittest`; output `N2-10.stdout.txt` SHA-256 `33c4b57ffd1502daa495f2c8ea250e182abbb86ad912105f93344f540b2d66f6`
- exit 0: `unittest`; output `N2-11.stdout.txt` SHA-256 `4f66af5a2f31edf2e0ef6ade726e8a9cd8c49a7f9887cbc07ca51f57b291f18b`
- exit 0: `unittest`; output `N2-12.stdout.txt` SHA-256 `4419997760beec3b6044ce48980cf163e3c86fa99b5439ae3110836b6883b93d`
- exit 0: `unittest`; output `N2-13.stdout.txt` SHA-256 `d5a7fe3abf1c110c5c4d002bf449e150ea237eb6f9affcf70fa83901ec4201f4`
- exit 0: `region_sampling_test`; output `N2-14.stdout.txt` SHA-256 `fd2ac47a245963395bda1b288e04d99f7b7cd1def554743808c322f3e9455934`
- exit 0: `region_input_test`; output `N2-15.stdout.txt` SHA-256 `f14746be3b28b7da7de4d044f8b95cce04f56457fc7112404273971f6da10672`
- exit 0: `unittest`; output `N2-16.stdout.txt` SHA-256 `f2014af769aa200d65e74cdbbbda9cc030cf8793bc6445de856e91ecfa2e1fe2`
- exit 0: `unittest`; output `N2-17.stdout.txt` SHA-256 `9f1dab7fdacd2356c974428fbd4bf657fc37f1d376ae573584790cd14c98f7e3`
- exit 0: `unittest`; output `N2-18.stdout.txt` SHA-256 `33c4b57ffd1502daa495f2c8ea250e182abbb86ad912105f93344f540b2d66f6`
- exit 0: `unittest`; output `N2-19.stdout.txt` SHA-256 `4f66af5a2f31edf2e0ef6ade726e8a9cd8c49a7f9887cbc07ca51f57b291f18b`
- exit 0: `unittest`; output `N2-20.stdout.txt` SHA-256 `4419997760beec3b6044ce48980cf163e3c86fa99b5439ae3110836b6883b93d`
- exit 0: `unittest`; output `N2-21.stdout.txt` SHA-256 `d5a7fe3abf1c110c5c4d002bf449e150ea237eb6f9affcf70fa83901ec4201f4`
- exit 0: `region_sampling_test`; output `N2-22.stdout.txt` SHA-256 `8dc6b19e11fbe83d35936bdc01c3734d43f5d26455abda4e0c55a3613bcfe312`
- exit 0: `region_input_test`; output `N2-23.stdout.txt` SHA-256 `f14746be3b28b7da7de4d044f8b95cce04f56457fc7112404273971f6da10672`
- exit 0: `unittest`; output `N2-24.stdout.txt` SHA-256 `f2014af769aa200d65e74cdbbbda9cc030cf8793bc6445de856e91ecfa2e1fe2`
- exit 0: `unittest`; output `N2-25.stdout.txt` SHA-256 `9f1dab7fdacd2356c974428fbd4bf657fc37f1d376ae573584790cd14c98f7e3`
- exit 0: `unittest`; output `N2-26.stdout.txt` SHA-256 `33c4b57ffd1502daa495f2c8ea250e182abbb86ad912105f93344f540b2d66f6`
- exit 0: `unittest`; output `N2-27.stdout.txt` SHA-256 `4f66af5a2f31edf2e0ef6ade726e8a9cd8c49a7f9887cbc07ca51f57b291f18b`
- exit 0: `unittest`; output `N2-28.stdout.txt` SHA-256 `4419997760beec3b6044ce48980cf163e3c86fa99b5439ae3110836b6883b93d`
- exit 0: `unittest`; output `N2-29.stdout.txt` SHA-256 `d5a7fe3abf1c110c5c4d002bf449e150ea237eb6f9affcf70fa83901ec4201f4`
- exit 0: `region_sampling_test`; output `N2-30.stdout.txt` SHA-256 `f630f0cc6ac3ea8791ca9bf3fed7b86a1a1cdaa676c140abd8fc04dce26c8705`
- exit 0: `region_input_test`; output `N2-31.stdout.txt` SHA-256 `f14746be3b28b7da7de4d044f8b95cce04f56457fc7112404273971f6da10672`
