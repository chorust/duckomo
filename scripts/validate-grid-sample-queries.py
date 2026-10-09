#!/usr/bin/env python3
"""Bind generated sample views with a matching DuckDB CLI/extension pair."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_QUERIES = ROOT / "test/data/grids/sample-queries.sql"
VIEW_PATTERN = re.compile(r"(?ms)^CREATE TEMP VIEW\s+([A-Za-z_][A-Za-z_0-9]*)\s+AS\b.*?;")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sql_string(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def schema_sql(source: str, extension: Path, report_path: Path,
               identity_path: Path) -> tuple[str, list[str], list[str], list[str]]:
    matches = list(VIEW_PATTERN.finditer(source))
    names: list[str] = []
    defaults: list[str] = []
    default_names: list[str] = []
    info_views: list[str] = []
    for match in matches:
        name = match.group(1)
        statement = match.group(0)
        if "include_source := true" not in statement:
            continue
        names.append(name)
        call = re.search(r"FROM\s+read_om\(\s*(.*?)\s*\)\s*;\s*$", statement, re.DOTALL)
        if call is None:
            raise ValueError(f"cannot extract read_om arguments from generated view {name}")
        arguments = re.sub(r",\s*include_source\s*:=\s*true\s*$", "", call.group(1))
        info_views.append(
            "CREATE TEMP VIEW info_" + name + " AS SELECT grid_id, layout::VARCHAR AS layout "
            "FROM om_grid_info(" + arguments + ");"
        )
        default_name = "default_" + name
        statement = re.sub(r"(?m)^CREATE TEMP VIEW\s+" + re.escape(name) + r"\b",
                           "CREATE TEMP VIEW " + default_name, statement, count=1)
        statement = statement.replace("include_source := true", "include_source := false", 1)
        defaults.append(statement)
        default_names.append(default_name)

    all_names = names + default_names
    rows = [
        "SELECT " + sql_string(name) + " AS view_name, cid, name, type "
        "FROM pragma_table_info(" + sql_string(name) + ")"
        for name in all_names
    ]
    explicit_suffixes = {name[len("explicit_"):] for name in names if name.startswith("explicit_")}
    domain_suffixes = {name[len("domain_"):] for name in names if name.startswith("domain_")}
    paired_suffixes = sorted(explicit_suffixes & domain_suffixes)
    identity_queries = [
        "SELECT " + sql_string(suffix) + " AS definition, "
        "explicit.grid_id = domain.grid_id AS grid_id_matches, "
        "explicit.layout = domain.layout AS layout_matches, "
        "explicit.grid_id AS explicit_grid_id, domain.grid_id AS domain_grid_id, "
        "explicit.layout AS explicit_layout, domain.layout AS domain_layout "
        "FROM info_explicit_" + suffix + " AS explicit CROSS JOIN info_domain_" + suffix + " AS domain"
        for suffix in paired_suffixes
    ]
    sql = "LOAD " + sql_string(str(extension)) + ";\nSET threads = 1;\n"
    sql += "\n".join(match.group(0) for match in matches) + "\n"
    sql += "\n".join(defaults) + "\n"
    sql += "\n".join(info_views) + "\n"
    sql += "COPY (" + " UNION ALL ".join(rows) + " ORDER BY view_name, cid) TO "
    sql += sql_string(str(report_path)) + " (FORMAT CSV, HEADER true);\n"
    if identity_queries:
        sql += "COPY (" + " UNION ALL ".join(identity_queries) + " ORDER BY definition) TO "
        sql += sql_string(str(identity_path)) + " (FORMAT CSV, HEADER true);\n"
    return sql, all_names, names, paired_suffixes


def load_schemas(path: Path, expected_views: list[str]) -> dict[str, list[tuple[str, str]]]:
    schemas: dict[str, list[tuple[str, str]]] = {}
    with path.open("r", encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["view_name", "cid", "name", "type"]:
            raise ValueError(f"unexpected schema report columns: {reader.fieldnames}")
        for row in reader:
            schemas.setdefault(row["view_name"], []).append((row["name"], row["type"]))
    missing = sorted(set(expected_views) - set(schemas))
    if missing:
        raise ValueError("schema report omitted generated views: " + ", ".join(missing))
    for name, columns in schemas.items():
        if not columns:
            raise ValueError(f"generated view {name} has no output columns")
    return schemas


def audit_schemas(schemas: dict[str, list[tuple[str, str]]], names: list[str]) -> list[dict[str, object]]:
    source_type = (
        "STRUCT(object_id VARCHAR, object_version VARCHAR, version_strength VARCHAR, content_verified BOOLEAN, "
        "grid_id VARCHAR, layout_id VARCHAR, logical_index UBIGINT, point_index UBIGINT, "
        "parent_point_index UBIGINT, axis_indices UBIGINT[])"
    )
    reports: list[dict[str, object]] = []
    for name in names:
        regular = schemas[name]
        default = schemas["default_" + name]
        source_valid = (
            all(column != "om_source" for column, _ in default)
            and regular == default + [("om_source", source_type)]
        )
        if not source_valid:
            raise ValueError(f"include_source changed columns beyond one trailing om_source field for {name}")
        reports.append({"view": name, "default_columns": default,
                        "include_source_columns": regular, "source_appended": True})

    explicit_names = {name[len("explicit_"):] for name in names if name.startswith("explicit_")}
    domain_names = {name[len("domain_"):] for name in names if name.startswith("domain_")}
    for suffix in sorted(explicit_names & domain_names):
        explicit = schemas["explicit_" + suffix]
        domain = schemas["domain_" + suffix]
        explicit_default = schemas["default_explicit_" + suffix]
        domain_default = schemas["default_domain_" + suffix]
        if explicit != domain or explicit_default != domain_default:
            raise ValueError(f"explicit and domain output schemas differ for {suffix}")
        reports.append({"pair": suffix, "explicit_domain_default_schema_match": True,
                        "explicit_domain_source_schema_match": True})
    return reports


def load_identity_checks(path: Path, expected: list[str]) -> list[dict[str, object]]:
    checks: list[dict[str, object]] = []
    with path.open("r", encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["definition", "grid_id_matches", "layout_matches", "explicit_grid_id",
                                 "domain_grid_id", "explicit_layout", "domain_layout"]:
            raise ValueError(f"unexpected explicit/domain identity report columns: {reader.fieldnames}")
        for row in reader:
            if row["grid_id_matches"].lower() != "true" or row["layout_matches"].lower() != "true":
                raise ValueError(
                    f"explicit and domain identity differ for {row['definition']}: "
                    f"grid_id={row['grid_id_matches']} ({row['explicit_grid_id']} != {row['domain_grid_id']}), "
                    f"layout={row['layout_matches']} ({row['explicit_layout']} != {row['domain_layout']})"
                )
            checks.append({"definition": row["definition"], "grid_id_matches": True,
                           "layout_matches": True, "grid_id": row["explicit_grid_id"],
                           "layout": row["explicit_layout"]})
    if sorted(row["definition"] for row in checks) != expected:
        raise ValueError("explicit/domain identity report does not cover every generated pair")
    return checks


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duckdb", type=Path, required=True, help="DuckDB CLI from the intended build")
    parser.add_argument("--extension", type=Path, required=True, help="DuckOMO extension from that same build")
    parser.add_argument("--queries", type=Path, default=DEFAULT_QUERIES)
    args = parser.parse_args()

    duckdb = args.duckdb.resolve()
    extension = args.extension.resolve()
    queries = args.queries.resolve()
    for label, path in (("duckdb", duckdb), ("extension", extension), ("queries", queries)):
        if not path.is_file():
            print(f"{label} file does not exist: {path}", file=sys.stderr)
            return 2
    if not duckdb.stat().st_mode & 0o111:
        print(f"DuckDB CLI is not executable: {duckdb}", file=sys.stderr)
        return 2

    source = queries.read_text(encoding="utf-8")
    matches = list(VIEW_PATTERN.finditer(source))
    names = [match.group(1) for match in matches]
    if not names or len(names) != len(set(names)):
        print("sample query file has no views or contains duplicate view names", file=sys.stderr)
        return 2
    name_set = set(names)
    unpaired_domains = [name for name in names if name.startswith("domain_") and
                        "explicit_" + name[len("domain_"):] not in name_set]
    if unpaired_domains:
        print("domain views lack matching explicit views: " + ", ".join(unpaired_domains), file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="duckomo-grid-schema-") as directory:
        temp_root = Path(directory)
        report_path = temp_root / "schema.csv"
        identity_path = temp_root / "identities.csv"
        sql_path = temp_root / "bind-and-schema.sql"
        sql, schema_view_names, audited_names, paired_suffixes = schema_sql(
            source, extension, report_path, identity_path)
        sql_path.write_text(sql, encoding="utf-8")
        result = subprocess.run(
            [str(duckdb), "-unsigned", "-bail", "-batch", "-f", str(sql_path), ":memory:"],
            cwd=ROOT, text=True, capture_output=True)
        schema_reports: list[dict[str, object]] = []
        identity_reports: list[dict[str, object]] = []
        schema_error = None
        if result.returncode == 0:
            try:
                schema_reports = audit_schemas(load_schemas(report_path, schema_view_names), audited_names)
                if paired_suffixes:
                    identity_reports = load_identity_checks(identity_path, paired_suffixes)
            except (OSError, ValueError, csv.Error) as error:
                schema_error = str(error)
    report = {
        "mode": "bind-only; generated and default-schema views are prepared; pragma_table_info inspects schemas without scanning rows",
        "duckdb": {"path": str(duckdb), "sha256": sha256(duckdb)},
        "extension": {"path": str(extension), "sha256": sha256(extension)},
        "queries": {"path": str(queries), "sha256": sha256(queries)},
        "views": names,
        "view_count": len(names),
        "schema_checks": schema_reports,
        "identity_checks": identity_reports,
        "schema_error": schema_error,
        "exit_code": result.returncode if schema_error is None else 1,
        "stdout": result.stdout,
        "stderr": result.stderr,
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return report["exit_code"]


if __name__ == "__main__":
    raise SystemExit(main())
