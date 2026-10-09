# T004 Native Gaussian Source Discovery

**Captured**: 2026-10-08

**Scope**: Read-only review of official ECMWF/Copernicus documentation for possible native N160/N320 input sources. No archive request, credential inspection, data download, S3 write, or gate execution was performed. This identifies an acquisition route; it does not freeze a sample or close T003/T004.

## Official findings

| Source | Finding |
|---|---|
| [Complete ERA5 global atmospheric reanalysis](https://cds.climate.copernicus.eu/datasets/reanalysis-era5-complete?tab=overview) | ERA5 atmosphere/land fields are on N320; ERA5 ensemble-component atmosphere/land fields are on N160. The dataset contains GRIB-1 and GRIB-2 data. |
| [ERA5 data documentation](https://confluence.ecmwf.int/pages/viewpage.action?navigatingVersions=true&pageId=78288944) | HRES is archived on N320 and EDA on N160. `stream=oper` is HRES sub-daily data and `stream=enda` is EDA sub-daily data. Native GRIB requests retain the archived grid geometry. |
| [How to download ERA5](https://confluence.ecmwf.int/pages/viewpage.action?navigatingVersions=true&pageId=355350789) | `reanalysis-era5-complete` exposes raw/native data through the CDS API, including MARS-backed fields. Access requires an ECMWF account and accepted licence; tape retrieval can queue for hours or days. |
| [MARS `grid` keyword](https://confluence.ecmwf.int/spaces/UDOC/pages/123799065/grid%2B-%2Bkeyword%2Bin%2BMARS%2BDissemination%2Brequest) | `grid=av` returns the archived model grid; `N160` and `N320` are recognized original reduced Gaussian grids. |
| [ECMWF Open Data](https://www.ecmwf.int/en/forecasts/datasets/open-data) and [AIFS Machine Learning data](https://www.ecmwf.int/en/forecasts/datasets/aifs-machine-learning-data) | Real-time Open Data is a subset and is disseminated at regular 0.25-degree resolution. AIFS model output is described as N320, but its Open Data representation is the regular latitude/longitude grid. Historical AIFS archive/MARS data requires archive registration. |
| Pinned Open-Meteo source [`EcmwfDomain.swift`](https://github.com/open-meteo/open-meteo/blob/b06f4760fd1f997e5559bb380f64c5e496b4a509/Sources/App/Ecmwf/EcmwfDomain.swift) | The public IFS/AIFS 0.25-degree domains return `RegularGrid(1440, 721)`, not N320. |
| Pinned Open-Meteo source [`Era5Domain.swift`](https://github.com/open-meteo/open-meteo/blob/b06f4760fd1f997e5559bb380f64c5e496b4a509/Sources/App/Era5/Era5Domain.swift) | Public ERA5/EDA domains are regular 0.25/0.5-degree grids; the `ecmwf_ifs` and analysis domains use Gaussian O1280. The pinned producer paths do not expose the required N160/N320 OM v3 objects. |

The live directory audit already recorded in [`open-meteo-s3-live-audit-20261006-1717z.md`](../baseline-local/open-meteo-s3-live-audit-20261006-1717z.md) found only `0p25/` in the sampled AIFS ENS Open Data directory. That observation is consistent with the official Open Data resolution description; it does not show that native AIFS N320 is absent from registered archive/dissemination services.

## Public S3 clarification

The AWS Registry currently documents public ERA5 mirrors such as [Icechunk ERA5](https://registry.opendata.aws/earthmover-era5/), [NSF NCAR ERA5](https://registry.opendata.aws/nsf-ncar-era5/), and [Planette ERA5](https://registry.opendata.aws/planette_era5_reanalysis/) on regular 0.25-degree latitude/longitude grids. These provide ERA5 from S3, but not its native N320/N160 reduced-Gaussian representation. ECMWF's separate real-time `ecmwf-forecasts` S3 mirror provides a subset of forecast data at regular 0.25-degree resolution. The audited Open-Meteo `s3://openmeteo` bucket likewise has O1280 and regular-grid objects, but no matching N160/N320/N320-region OM v3 object.

## Implication for 004

ERA5 Complete is an official candidate source for native Gaussian N160 and N320 GRIB data, so the source search need not be limited to the Open-Meteo public S3 prefixes. The pinned Open-Meteo producer definitions confirm the audited public objects use 0.25/0.5-degree regular grids or O1280; they do not define public N160/N320 OM v3 outputs. The documented acquisition route is to use the CDS API against `reanalysis-era5-complete`, select the intended ERA5 HRES or EDA product from the MARS catalogue, request GRIB on `grid=av`, and freeze the exact request, original GRIB hash, and decoded grid metadata. `grid=av` preserves the archive's original representation; a regridded NetCDF or 0.25-degree Open Data file is not a substitute.

This route has not been exercised here. No registered archive account or accepted licence was supplied for an actual retrieval. A GRIB candidate by itself also does not satisfy T003's real OM v3 object requirement or T004's official OM C full-value decode reference. The acquired source must be paired with a fixed OM v3 object and an independent mapping from object-local positions to the GRIB/producer positions. The 14,747-point N320 regional object and local-to-parent point order remain unacquired and unverified.

## Status

- T003: four-family inventory remains frozen; required Gaussian N160, N320, and N320-region object coverage remains `not-run`.
- T004: remains open. The official native-grid archive path is now documented, but no source bytes, OM v3 object, coordinate/value oracle, region point-order reference, or output hashes were generated.
- No H0/H1/H6 gate or Gaussian producer support claim is promoted by this discovery.
