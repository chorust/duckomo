#!/usr/bin/env python3
"""Read-only inventory of Open-Meteo S3 catalog metadata (no weather chunks)."""

from __future__ import annotations

import argparse
import concurrent.futures
import datetime as dt
import hashlib
import json
import re
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path


ENDPOINT = "https://openmeteo.s3.us-west-2.amazonaws.com/"
XML_NS = "http://s3.amazonaws.com/doc/2006-03-01/"
GRID_LABEL = re.compile(r"(?<![A-Za-z0-9])([NO])\s*[-_=]?\s*(160|320|1280)(?!\d)", re.I)
N_LABEL = re.compile(r"(?<![A-Za-z0-9])N\s*[-_=]?\s*(160|320)(?!\d)", re.I)
REGION_CANDIDATES = (
    "data/ecmwf_aifs_europe_ensemble/",
    "data_spatial/ecmwf_aifs_europe_ensemble/",
    "data/ecmwf_aifs_europe_ensemble_mean/",
    "data_spatial/ecmwf_aifs_europe_ensemble_mean/",
)


def normalized_proxy_map() -> tuple[dict[str, str], list[str]]:
    """Strip a root trailing slash from proxy URLs without exposing their values."""
    proxies = urllib.request.getproxies()
    changed = []
    for scheme, value in list(proxies.items()):
        parsed = urllib.parse.urlsplit(value)
        if parsed.path == "/" and not parsed.query and not parsed.fragment:
            proxies[scheme] = urllib.parse.urlunsplit(
                (parsed.scheme, parsed.netloc, "", "", "")
            )
            changed.append(scheme)
    return proxies, sorted(changed)


PROXIES, NORMALIZED_PROXY_SCHEMES = normalized_proxy_map()
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler(PROXIES))


def request_bytes(url: str, timeout: int = 25) -> tuple[int, bytes]:
    request = urllib.request.Request(
        url, headers={"User-Agent": "duckomo-readonly-catalog-audit/1.0"}
    )
    with OPENER.open(request, timeout=timeout) as response:
        return response.status, response.read()


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def list_prefix(prefix: str) -> dict:
    token = None
    pages = []
    child_prefixes = []
    object_keys = []
    while True:
        params = {
            "list-type": "2",
            "delimiter": "/",
            "max-keys": "1000",
            "prefix": prefix,
        }
        if token:
            params["continuation-token"] = token
        url = ENDPOINT + "?" + urllib.parse.urlencode(params)
        status, body = request_bytes(url)
        root = ET.fromstring(body)
        page_prefixes = []
        page_objects = []
        for node in root.iter():
            name = local_name(node.tag)
            if name == "CommonPrefixes":
                value = next(
                    (child.text for child in node if local_name(child.tag) == "Prefix"),
                    None,
                )
                if value:
                    page_prefixes.append(value)
            elif name == "Contents":
                value = next(
                    (child.text for child in node if local_name(child.tag) == "Key"),
                    None,
                )
                if value:
                    page_objects.append(value)
        child_prefixes.extend(page_prefixes)
        object_keys.extend(page_objects)
        truncated = root.findtext(f"{{{XML_NS}}}IsTruncated") == "true"
        pages.append(
            {
                "status": status,
                "body_bytes": len(body),
                "prefixes_on_page": len(page_prefixes),
                "objects_on_page": len(page_objects),
                "truncated": truncated,
                "sha256": hashlib.sha256(body).hexdigest(),
            }
        )
        if not truncated:
            break
        token = root.findtext(f"{{{XML_NS}}}NextContinuationToken")
        if not token:
            raise RuntimeError(f"truncated S3 listing has no continuation token: {prefix}")
    return {
        "prefix": prefix,
        "pages": pages,
        "prefix_count": len(child_prefixes),
        "object_count": len(object_keys),
        "child_prefixes": sorted(child_prefixes),
        "object_keys": sorted(object_keys),
        "truncated": pages[-1]["truncated"],
    }


def collect_mentions(value, path="$", mentions=None):
    if mentions is None:
        mentions = []
    if isinstance(value, dict):
        for key, child in value.items():
            collect_mentions(child, f"{path}.{key}", mentions)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            collect_mentions(child, f"{path}[{index}]", mentions)
    elif isinstance(value, str):
        for match in GRID_LABEL.finditer(value):
            mentions.append(
                {
                    "json_path": path,
                    "label": f"{match.group(1).upper()}{match.group(2)}",
                    "text": value[:1200],
                }
            )
    return mentions


def fetch_metadata(key: str) -> dict:
    url = ENDPOINT + urllib.parse.quote(key, safe="/")
    try:
        status, body = request_bytes(url)
        record = {
            "key": key,
            "status": status,
            "bytes": len(body),
            "sha256": hashlib.sha256(body).hexdigest(),
        }
        if status == 200:
            try:
                parsed = json.loads(body)
            except (UnicodeDecodeError, json.JSONDecodeError):
                record["json_parse"] = "failed"
                return record
            mentions = collect_mentions(parsed)
            record["grid_mentions"] = mentions
            record["n_grid_mentions"] = [m for m in mentions if N_LABEL.search(m["label"])]
        return record
    except urllib.error.HTTPError as error:
        return {"key": key, "status": error.code, "bytes": 0}
    except (urllib.error.URLError, TimeoutError, OSError) as error:
        reason = getattr(error, "reason", error)
        return {
            "key": key,
            "status": None,
            "bytes": 0,
            "error": type(error).__name__,
            "reason_type": type(reason).__name__,
            "reason_errno": getattr(reason, "errno", None),
        }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--workers", type=int, default=4)
    args = parser.parse_args()
    if args.workers < 1 or args.workers > 12:
        parser.error("--workers must be between 1 and 12")

    captured = dt.datetime.now(dt.timezone.utc).isoformat()
    root_listings = {}
    root_errors = []
    for root_prefix in ("data/", "data_spatial/"):
        try:
            root_listings[root_prefix.rstrip("/")] = list_prefix(root_prefix)
        except Exception as error:  # preserve a partial audit instead of losing root evidence
            root_errors.append(
                {"prefix": root_prefix, "error": type(error).__name__}
            )

    data_listing = root_listings.get("data", {})
    spatial_listing = root_listings.get("data_spatial", {})
    metadata_keys = [
        prefix + "static/meta.json"
        for prefix in data_listing.get("child_prefixes", [])
    ]
    metadata_keys.extend(
        prefix + suffix
        for prefix in spatial_listing.get("child_prefixes", [])
        for suffix in ("latest.json", "in-progress.json")
    )
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        objects = list(pool.map(fetch_metadata, metadata_keys))

    direct_prefixes = []
    for prefix in REGION_CANDIDATES:
        try:
            direct_prefixes.append(list_prefix(prefix))
        except Exception as error:
            direct_prefixes.append(
                {"prefix": prefix, "error": type(error).__name__}
            )

    status_counts = {}
    for item in objects:
        label = str(item.get("status"))
        status_counts[label] = status_counts.get(label, 0) + 1
    n_hits = [
        {"key": item["key"], "matches": item["n_grid_mentions"]}
        for item in objects
        if item.get("n_grid_mentions")
    ]
    direct_listing_errors = [item for item in direct_prefixes if "error" in item]
    metadata_errors = [
        item
        for item in objects
        if item.get("status") not in (200, 404) or item.get("json_parse") == "failed"
    ]
    root_incomplete = any(item.get("truncated") for item in root_listings.values())
    partial = bool(root_errors or direct_listing_errors or metadata_errors or root_incomplete)
    audit = {
        "schema_version": 1,
        "captured_at_utc": captured,
        "bucket_endpoint": ENDPOINT,
        "scope": "anonymous read-only S3 root listings and catalog JSON; no weather OM chunks downloaded",
        "proxy_normalization": {
            "root_trailing_slash_removed_for_schemes": NORMALIZED_PROXY_SCHEMES,
            "proxy_values_recorded": False,
        },
        "root_listings": root_listings,
        "root_errors": root_errors,
        "metadata_requests": len(metadata_keys),
        "metadata_status_counts": status_counts,
        "metadata_errors": metadata_errors,
        "n160_n320_metadata_hits": n_hits,
        "regional_candidate_prefixes": direct_prefixes,
        "objects": objects,
        "audit_status": "partial" if partial else "complete_catalog_metadata_scan",
    }
    args.output_dir.mkdir(parents=True, exist_ok=False)
    audit_path = args.output_dir / "audit.json"
    audit_path.write_text(json.dumps(audit, indent=2, sort_keys=True) + "\n")
    digest = hashlib.sha256(audit_path.read_bytes()).hexdigest()
    summary = {
        "audit_status": audit["audit_status"],
        "captured_at_utc": captured,
        "root_prefix_counts": {
            key: value.get("prefix_count") for key, value in root_listings.items()
        },
        "metadata_requests": len(metadata_keys),
        "metadata_status_counts": status_counts,
        "metadata_errors": len(metadata_errors),
        "n160_n320_metadata_hits": len(n_hits),
        "regional_prefix_object_counts": {
            item.get("prefix"): item.get("object_count") for item in direct_prefixes
        },
        "audit_sha256": digest,
    }
    print(json.dumps(summary, sort_keys=True))
    return 2 if partial else 0


if __name__ == "__main__":
    raise SystemExit(main())
