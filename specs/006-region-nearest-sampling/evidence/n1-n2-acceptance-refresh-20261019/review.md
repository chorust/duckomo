# N1/N2 scoped acceptance refresh — 2026-10-19

Status: **pass**

DuckDB runtime: v1.5.4 build; tests executed against v1.5.4, v1.5.5, v1.5.6.

## Disposition

- **N1: pass** for synthetic regular spherical grids and NCEP GFS025. The 377-case capability matrix contains 201 accepted and 176 rejected cases across 17 reachable rejection classes. Ten independent scientific distance vectors passed. Twelve fixed GFS025 targets were each compared with all 1,038,240 source coordinates. The nominal non-periodic full-span fixture is correctly classified at the earlier multiple-wrap check; the later extent guard is unreachable. The contract explicitly defines a finite representative matrix and does not claim arbitrary binary64 or target-space exhaustion.
- **N2: pass** for the same declared profile scope. SQL, native sampling, legacy points compatibility, inherited reader and semantic-axis checks, geometry dependency checks, and shared RegionCollection parser checks passed on all three runtimes. The aggregate WKB test isolates the total-byte guard and asserts its diagnostic. Four binder-precedence edges are checked. Parser permutations and precedence cross-products remain finite-sample boundaries, as permitted by the contract.
- Overall 006 remains incomplete because N5 real/remote inputs, N6 full audit, and N7 independent reproduction are unfinished.

## Locked evidence identity

- Capability CSV SHA-256: `41a82525730b52ea8d1c7fc98bd1435cab8e33a02aa233e93667d3d311154e39`
- Reference manifest SHA-256: `08798b8365a1b03591865477273049807f2a50b4b50556ff804a62dffd5e8cfd`
- Native N1 comparator SHA-256: `f76f9904fd9cc3b17b81b595e8d2168b26204d428744c322f6e11142ad2af4b9`
- Native region input test SHA-256: `d519ab7c368f297d3bb4d6a24da4bcbaaa5c63431b312688988d993d2829934b`
- Nearest SQL profile SHA-256: `2acb08e3d5cf9f320554dfaab336ca111ce8990e1acfa2ca558af67fb024aa57`
- Runtime matrix SHA-256: `159c00b3aff2c90d8ff10d0522e9488e998ee4f71375be3f49fc4ff43d786753`
- Commands SHA-256: `2c485aaa96eba4107013275ef90622ca2596bdeeb5adbec2796872878b33ff78`
- Inputs SHA-256: `2e10cd945a7449967af3ac5f7a780cb04fc754c3e4769c538169b5abd2efbcf7`
- Manifest SHA-256: `489276b632d14d6d3bc92df275e5238147a689c41cbe8932a4112602213db56b`

## Recorded commands

### N1: pass

- exit 0: `nearest_sampling_test`; output `N1-01.stdout.txt` SHA-256 `deb0986da499622355951d2b0da97b47176351efc2768fbd59aad0f75d84f3d2`
- exit 0: `region_geometry_test`; output `N1-02.stdout.txt` SHA-256 `12c11916f00549cc40bc2a7509cb5a7fb302724cb823cd67ae2e5a82a896c4be`
- exit 0: `nearest_sampling_test`; output `N1-03.stdout.txt` SHA-256 `deb0986da499622355951d2b0da97b47176351efc2768fbd59aad0f75d84f3d2`
- exit 0: `region_geometry_test`; output `N1-04.stdout.txt` SHA-256 `ac79df989598e53e753c059e3eb34cbea29958af836dc64310847f9616ca9816`
- exit 0: `nearest_sampling_test`; output `N1-05.stdout.txt` SHA-256 `deb0986da499622355951d2b0da97b47176351efc2768fbd59aad0f75d84f3d2`
- exit 0: `region_geometry_test`; output `N1-06.stdout.txt` SHA-256 `b967308c3952a59102dd151b71af3066065bf503fecb641a4f7ead41d1f0481d`
- exit 0: `spatial_layout_test`; output `N1-07.stdout.txt` SHA-256 `ec79a5034a866bb06266adde78ccca97cf411016922e919fe2d92771b3241008`

### N2: pass

- exit 0: `unittest`; output `N2-08.stdout.txt` SHA-256 `f2014af769aa200d65e74cdbbbda9cc030cf8793bc6445de856e91ecfa2e1fe2`
- exit 0: `unittest`; output `N2-09.stdout.txt` SHA-256 `9f1dab7fdacd2356c974428fbd4bf657fc37f1d376ae573584790cd14c98f7e3`
- exit 0: `unittest`; output `N2-10.stdout.txt` SHA-256 `33c4b57ffd1502daa495f2c8ea250e182abbb86ad912105f93344f540b2d66f6`
- exit 0: `unittest`; output `N2-11.stdout.txt` SHA-256 `4f66af5a2f31edf2e0ef6ade726e8a9cd8c49a7f9887cbc07ca51f57b291f18b`
- exit 0: `unittest`; output `N2-12.stdout.txt` SHA-256 `4419997760beec3b6044ce48980cf163e3c86fa99b5439ae3110836b6883b93d`
- exit 0: `unittest`; output `N2-13.stdout.txt` SHA-256 `d5a7fe3abf1c110c5c4d002bf449e150ea237eb6f9affcf70fa83901ec4201f4`
- exit 0: `region_sampling_test`; output `N2-14.stdout.txt` SHA-256 `821a126233c0086fe469f911427c3de3a22d3b0de3681b819cddb393633922e6`
- exit 0: `region_input_test`; output `N2-15.stdout.txt` SHA-256 `f14746be3b28b7da7de4d044f8b95cce04f56457fc7112404273971f6da10672`
- exit 0: `unittest`; output `N2-16.stdout.txt` SHA-256 `f2014af769aa200d65e74cdbbbda9cc030cf8793bc6445de856e91ecfa2e1fe2`
- exit 0: `unittest`; output `N2-17.stdout.txt` SHA-256 `9f1dab7fdacd2356c974428fbd4bf657fc37f1d376ae573584790cd14c98f7e3`
- exit 0: `unittest`; output `N2-18.stdout.txt` SHA-256 `33c4b57ffd1502daa495f2c8ea250e182abbb86ad912105f93344f540b2d66f6`
- exit 0: `unittest`; output `N2-19.stdout.txt` SHA-256 `4f66af5a2f31edf2e0ef6ade726e8a9cd8c49a7f9887cbc07ca51f57b291f18b`
- exit 0: `unittest`; output `N2-20.stdout.txt` SHA-256 `4419997760beec3b6044ce48980cf163e3c86fa99b5439ae3110836b6883b93d`
- exit 0: `unittest`; output `N2-21.stdout.txt` SHA-256 `d5a7fe3abf1c110c5c4d002bf449e150ea237eb6f9affcf70fa83901ec4201f4`
- exit 0: `region_sampling_test`; output `N2-22.stdout.txt` SHA-256 `d482e63d5361cfe41f0b4d7cac5753ad703daf8d4889dc3a5b3d76d92af92673`
- exit 0: `region_input_test`; output `N2-23.stdout.txt` SHA-256 `f14746be3b28b7da7de4d044f8b95cce04f56457fc7112404273971f6da10672`
- exit 0: `unittest`; output `N2-24.stdout.txt` SHA-256 `f2014af769aa200d65e74cdbbbda9cc030cf8793bc6445de856e91ecfa2e1fe2`
- exit 0: `unittest`; output `N2-25.stdout.txt` SHA-256 `9f1dab7fdacd2356c974428fbd4bf657fc37f1d376ae573584790cd14c98f7e3`
- exit 0: `unittest`; output `N2-26.stdout.txt` SHA-256 `33c4b57ffd1502daa495f2c8ea250e182abbb86ad912105f93344f540b2d66f6`
- exit 0: `unittest`; output `N2-27.stdout.txt` SHA-256 `4f66af5a2f31edf2e0ef6ade726e8a9cd8c49a7f9887cbc07ca51f57b291f18b`
- exit 0: `unittest`; output `N2-28.stdout.txt` SHA-256 `4419997760beec3b6044ce48980cf163e3c86fa99b5439ae3110836b6883b93d`
- exit 0: `unittest`; output `N2-29.stdout.txt` SHA-256 `d5a7fe3abf1c110c5c4d002bf449e150ea237eb6f9affcf70fa83901ec4201f4`
- exit 0: `region_sampling_test`; output `N2-30.stdout.txt` SHA-256 `bedaeb127a9d37c664c9476d38bfba7a8b0ed68680d47c8158adc4dd6a096b5c`
- exit 0: `region_input_test`; output `N2-31.stdout.txt` SHA-256 `f14746be3b28b7da7de4d044f8b95cce04f56457fc7112404273971f6da10672`
