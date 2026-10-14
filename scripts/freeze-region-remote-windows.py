#!/usr/bin/env python3
"""Freeze bounded remote validation queries without scanning each entire object."""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'test/data/regions/real-targets.json'
OUTPUT = ROOT / 'test/data/regions/remote-windows.json'


def build():
    targets = json.loads(SOURCE.read_text())['targets']
    records = []
    for target in targets:
        west, south, east, north = target['region_bounds_lonlat']
        # Fixed geometric expansion, independent of observed query costs/results.
        margin = 1.0 if target['grid_id'] == 'ecmwf_ifs' else 0.25
        dx = max(east - west, margin)
        dy = max(north - south, margin)
        bounds = [max(-180.0, west - dx), max(-90.0, south - dy),
                  min(180.0, east + dx), min(90.0, north + dy)]
        window = f"lon BETWEEN {bounds[0]!r} AND {bounds[2]!r} AND lat BETWEEN {bounds[1]!r} AND {bounds[3]!r}"
        columns = ','.join('"' + name.replace('"', '""') + '"'
                           for name in target['queries']['projection'] if name != 'om_source')
        identifier = "'" + target['grid_id'].replace("'", "''") + "'"
        geometry = "ST_GeomFromWKB(from_hex('" + target['region_wkb']['wkb_hex'] + "'))"
        baseline = (
            'WITH window_rows AS MATERIALIZED (' + target['queries']['full_local_sql'] + ' WHERE ' + window + ') '
            'SELECT ' + columns + ',' + identifier + ' AS region_id,om_source.logical_index AS source_index '
            'FROM window_rows WHERE ST_Covers(' + geometry + ',ST_Point(lon,lat)) ORDER BY ALL')
        region = ('SELECT ' + columns + ',region_id,om_source.logical_index AS source_index FROM (' +
                  target['queries']['region_local_sql'] + ' WHERE ' + window + ') AS region_rows ORDER BY ALL')
        records.append({'grid_id': target['grid_id'], 'window_bounds_lonlat': bounds,
                        'baseline_local_sql': baseline, 'region_local_sql': region,
                        'expected_rows': target['expected_region_rows']})
    document = {'schema_version': 1, 'scope': 'bounded_window',
                'decision': 'User authorized dropping full-domain scans; bounded-window consistency and costs only.',
                'real_targets_sha256': hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
                'window_policy': 'Expand each frozen polygon envelope by max(span,0.25 degrees) on each side (O1280 uses a fixed 1-degree minimum for its dense polar rows); clamp to geographic limits; keep all original dimensions and values. No LIMIT or time sampling.',
                'comparison': 'Materialized read_om window filtered by ST_Covers versus read_om_regions; exact ordered rows and source positions must agree across all four sources.',
                'savings_policy': 'Require strictly fewer value bytes, decoded blocks and HTTP/S3 service-sent bytes for polygon than the fixed larger window; make no full-domain savings claim.',
                'targets': records}
    OUTPUT.write_text(json.dumps(document, sort_keys=True, indent=2) + '\n')
    print('Frozen bounded-window remote validation:', OUTPUT)


if __name__ == '__main__':
    build()
