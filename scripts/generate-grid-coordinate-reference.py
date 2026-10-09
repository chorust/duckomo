#!/usr/bin/env python3
"""Generate coordinate references for frozen producer grid points.

This standalone tool intentionally does not import DuckOMO, its registry
generator, or its coordinate kernels. It emits one row per producer-defined
spatial position using either independent binary64 mathematics or a separate
port of the pinned producer's Float32 operations. Object-axis mapping remains
separate evidence in the sample manifest.
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import csv
import hashlib
import json
import math
import struct
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
TARGETS = {
    "gem_rdps_10km": "rotated_v3",
    "aladin_central_europe_2km": "lambert_v3",
    "gem_regional": "stereographic_v3",
}
PI = math.pi
F32_PI = struct.unpack("!f", struct.pack("!f", PI))[0]


class ReferenceError(ValueError):
    pass


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ReferenceError(f"cannot read JSON object {path}: {error}") from error
    if not isinstance(value, dict):
        raise ReferenceError(f"expected a JSON object in {path}")
    return value


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def normalize_longitude(longitude: float) -> float:
    value = math.fmod(longitude, 360.0)
    if value < -180.0:
        value += 360.0
    if value >= 180.0:
        value -= 360.0
    return value


def source_float32(value: float) -> float:
    """Round a source Float value independently of DuckOMO's coordinate code."""
    return struct.unpack("!f", struct.pack("!f", float(value)))[0]


def source_grid_coordinate(origin: float, step: float, index: int) -> float:
    """Reproduce the producer's Float(index) * step + origin point position."""
    displacement = source_float32(source_float32(index) * source_float32(step))
    return source_float32(source_float32(origin) + displacement)


def rotated_coordinate(parameters: dict[str, Any], x: int, y: int) -> tuple[float, float]:
    # Match the producer's Float32 native point, but keep the projection
    # formula independent and evaluate it in binary64.
    native_lat = math.radians(source_grid_coordinate(parameters["y0"], parameters["dy"], y))
    native_lon = math.radians(source_grid_coordinate(parameters["x0"], parameters["dx"], x) +
                              source_float32(parameters["rotation"]))
    pole_lat = math.radians(source_float32(parameters["north_pole_latitude"]))
    pole_lon = math.radians(source_float32(parameters["north_pole_longitude"]))
    sin_lat, cos_lat = math.sin(native_lat), math.cos(native_lat)
    sin_lon, cos_lon = math.sin(native_lon), math.cos(native_lon)
    sin_pole, cos_pole = math.sin(pole_lat), math.cos(pole_lat)
    sin_pole_lon, cos_pole_lon = math.sin(pole_lon), math.cos(pole_lon)
    vx = cos_lat * cos_lon * sin_pole * cos_pole_lon - cos_lat * sin_lon * sin_pole_lon + sin_lat * cos_pole * cos_pole_lon
    vy = cos_lat * cos_lon * sin_pole * sin_pole_lon + cos_lat * sin_lon * cos_pole_lon + sin_lat * cos_pole * sin_pole_lon
    vz = -cos_lat * cos_lon * cos_pole + sin_lat * sin_pole
    latitude = math.degrees(math.asin(max(-1.0, min(1.0, vz))))
    longitude = normalize_longitude(math.degrees(math.atan2(vy, vx)))
    return longitude, latitude


def lambert_coordinate(parameters: dict[str, Any], x: int, y: int) -> tuple[float, float]:
    earth = parameters["earth"]
    radius = earth["radius_m"]
    phi1 = math.radians(parameters["standard_parallel_1"])
    phi2 = math.radians(parameters["standard_parallel_2"])
    phi0 = math.radians(parameters["latitude_of_false_origin"])
    lambda0 = math.radians(parameters["longitude_of_false_origin"])
    if parameters["standard_parallel_1"] == parameters["standard_parallel_2"]:
        n = math.sin(phi1)
    else:
        n = math.log(math.cos(phi1) / math.cos(phi2)) / math.log(
            math.tan(PI / 4.0 + phi2 / 2.0) / math.tan(PI / 4.0 + phi1 / 2.0)
        )
    if n == 0.0:
        raise ReferenceError("Lambert cone constant is zero")
    f = math.cos(phi1) * math.tan(PI / 4.0 + phi1 / 2.0) ** n / n
    rho0 = radius * f / math.tan(PI / 4.0 + phi0 / 2.0) ** n
    native_x = source_grid_coordinate(parameters["x0"], parameters["dx"], x) - \
        source_float32(parameters.get("false_easting_m", 0.0))
    native_y = source_grid_coordinate(parameters["y0"], parameters["dy"], y) - \
        source_float32(parameters.get("false_northing_m", 0.0))
    delta_y = rho0 - native_y
    theta = math.atan2(native_x, delta_y)
    rho = math.copysign(math.hypot(native_x, delta_y), n)
    latitude = 2.0 * math.atan((radius * f / rho) ** (1.0 / n)) - PI / 2.0
    longitude = lambda0 + theta / n
    return normalize_longitude(math.degrees(longitude)), math.degrees(latitude)


def stereographic_coordinate(parameters: dict[str, Any], x: int, y: int) -> tuple[float, float]:
    projection = parameters["projection"]
    radius = projection["radius_m"]
    phi0 = math.radians(projection["latitude_of_origin"])
    lambda0 = math.radians(projection["longitude_of_origin"])
    native = parameters["native_grid"]
    native_x = source_grid_coordinate(native["x0"], native["dx"], x) - \
        source_float32(projection["false_easting_m"])
    native_y = source_grid_coordinate(native["y0"], native["dy"], y) - \
        source_float32(projection["false_northing_m"])
    rho = math.hypot(native_x, native_y)
    if rho == 0.0:
        return normalize_longitude(projection["longitude_of_origin"]), projection["latitude_of_origin"]
    c = 2.0 * math.atan2(rho, 2.0 * radius * projection["scale_factor"])
    latitude = math.asin(
        math.cos(c) * math.sin(phi0) + native_y * math.sin(c) * math.cos(phi0) / rho
    )
    longitude = lambda0 + math.atan2(
        native_x * math.sin(c),
        rho * math.cos(phi0) * math.cos(c) - native_y * math.sin(phi0) * math.sin(c),
    )
    return normalize_longitude(math.degrees(longitude)), math.degrees(latitude)


TRANSFORMS = {
    "rotated_latlon": rotated_coordinate,
    "lambert_conformal_conic": lambert_coordinate,
    "stereographic": stereographic_coordinate,
}


class LibmFloat32:
    """Call the platform C Float32 math entry points used by the pinned Swift producer."""

    def __init__(self) -> None:
        library_name = ctypes.util.find_library("m")
        try:
            self.library = ctypes.CDLL(library_name or None)
        except OSError as error:
            raise ReferenceError(f"cannot load the platform math library: {error}") from error
        self.unary = {name: self._function(name, 1) for name in ("sinf", "cosf", "tanf", "asinf", "sqrtf", "atanf", "logf")}
        self.binary = {name: self._function(name, 2) for name in ("atan2f", "fmodf", "powf")}

    def _function(self, name: str, arity: int) -> Any:
        try:
            function = getattr(self.library, name)
        except AttributeError as error:
            raise ReferenceError(f"platform math library has no {name} entry point") from error
        function.argtypes = [ctypes.c_float] * arity
        function.restype = ctypes.c_float
        return function

    def call(self, name: str, *args: float) -> float:
        function = self.unary.get(name) or self.binary.get(name)
        if function is None:
            raise ReferenceError(f"unsupported Float32 math operation: {name}")
        return float(function(*(ctypes.c_float(source_float32(arg)) for arg in args)))


_FLOAT32_LIBM: LibmFloat32 | None = None


def _float32_libm() -> LibmFloat32:
    global _FLOAT32_LIBM
    if _FLOAT32_LIBM is None:
        _FLOAT32_LIBM = LibmFloat32()
    return _FLOAT32_LIBM


def _f32_add(left: float, right: float) -> float:
    return source_float32(source_float32(left) + source_float32(right))


def _f32_sub(left: float, right: float) -> float:
    return source_float32(source_float32(left) - source_float32(right))


def _f32_mul(left: float, right: float) -> float:
    return source_float32(source_float32(left) * source_float32(right))


def _f32_div(left: float, right: float) -> float:
    return source_float32(source_float32(left) / source_float32(right))


def _f32_degrees_to_radians(degrees: float) -> float:
    return _f32_div(_f32_mul(degrees, F32_PI), 180.0)


def _f32_radians_to_degrees(radians: float) -> float:
    return _f32_div(_f32_mul(radians, 180.0), F32_PI)


def _f32_wrap_longitude(longitude: float) -> float:
    # This is the pinned Gridable.getCoordinates expression, including its Float32 rounding.
    shifted = _f32_add(longitude, 180.0)
    remainder = _float32_libm().call("fmodf", shifted, 360.0)
    return _f32_sub(remainder, 180.0)


def pinned_rotated_coordinate(parameters: dict[str, Any], x: int, y: int) -> tuple[float, float]:
    """Port the pinned RotatedLatLonProjection.inverse and Gridable longitude wrap."""
    libm = _float32_libm()
    theta_degrees = _f32_add(90.0, parameters["north_pole_latitude"])
    theta = _f32_degrees_to_radians(theta_degrees)
    phi = _f32_degrees_to_radians(parameters["north_pole_longitude"])
    native_lon = _f32_degrees_to_radians(source_grid_coordinate(parameters["x0"], parameters["dx"], x))
    native_lat = _f32_degrees_to_radians(source_grid_coordinate(parameters["y0"], parameters["dy"], y))

    sin_theta, cos_theta = libm.call("sinf", theta), libm.call("cosf", theta)
    sin_lat, cos_lat = libm.call("sinf", native_lat), libm.call("cosf", native_lat)
    sin_lon, cos_lon = libm.call("sinf", native_lon), libm.call("cosf", native_lon)
    latitude_term = _f32_sub(
        _f32_mul(cos_theta, sin_lat),
        _f32_mul(_f32_mul(cos_lon, sin_theta), cos_lat),
    )
    latitude_radians = _f32_mul(-1.0, libm.call("asinf", latitude_term))
    longitude_denominator = _f32_add(
        _f32_mul(libm.call("tanf", native_lat), sin_theta),
        _f32_mul(cos_lon, cos_theta),
    )
    longitude_radians = _f32_mul(
        -1.0,
        _f32_sub(libm.call("atan2f", sin_lon, longitude_denominator), phi),
    )
    return _f32_wrap_longitude(_f32_radians_to_degrees(longitude_radians)), \
        _f32_radians_to_degrees(latitude_radians)


def pinned_lambert_coordinate(parameters: dict[str, Any], x: int, y: int) -> tuple[float, float]:
    """Port the pinned LambertConformalConicProjection.inverse operations."""
    libm = _float32_libm()
    phi1_degrees = source_float32(parameters["standard_parallel_1"])
    phi2_degrees = source_float32(parameters["standard_parallel_2"])
    phi0_degrees = source_float32(parameters["latitude_of_false_origin"])
    lambda0_degrees = source_float32(parameters["longitude_of_false_origin"])
    phi1 = _f32_degrees_to_radians(phi1_degrees)
    phi2 = _f32_degrees_to_radians(phi2_degrees)
    phi0 = _f32_degrees_to_radians(phi0_degrees)
    lambda0 = _f32_degrees_to_radians(_f32_wrap_longitude(lambda0_degrees))
    if phi1 == phi2:
        n = libm.call("sinf", phi1)
    else:
        numerator = libm.call("logf", _f32_div(libm.call("cosf", phi1), libm.call("cosf", phi2)))
        phi2_tan_arg = _f32_add(_f32_div(F32_PI, 4.0), _f32_div(phi2, 2.0))
        phi1_tan_arg = _f32_add(_f32_div(F32_PI, 4.0), _f32_div(phi1, 2.0))
        denominator = libm.call(
            "logf",
            _f32_div(libm.call("tanf", phi2_tan_arg), libm.call("tanf", phi1_tan_arg)),
        )
        n = _f32_div(numerator, denominator)

    phi1_tan_arg = _f32_add(_f32_div(F32_PI, 4.0), _f32_div(phi1, 2.0))
    phi0_tan_arg = _f32_add(_f32_div(F32_PI, 4.0), _f32_div(phi0, 2.0))
    f = _f32_div(
        _f32_mul(libm.call("cosf", phi1), libm.call("powf", libm.call("tanf", phi1_tan_arg), n)),
        n,
    )
    rho0 = _f32_div(f, libm.call("powf", libm.call("tanf", phi0_tan_arg), n))
    radius = source_float32(parameters["earth"]["radius_m"])
    native_x = source_grid_coordinate(parameters["x0"], parameters["dx"], x)
    native_y = source_grid_coordinate(parameters["y0"], parameters["dy"], y)
    x_scaled = _f32_div(native_x, radius)
    y_scaled = _f32_div(native_y, radius)
    y_delta = _f32_sub(rho0, y_scaled)
    if n >= 0.0:
        theta = libm.call("atan2f", x_scaled, y_delta)
    else:
        theta = libm.call("atan2f", _f32_mul(-1.0, x_scaled), _f32_sub(y_scaled, rho0))
    rho_squared = _f32_add(
        libm.call("powf", x_scaled, 2.0),
        libm.call("powf", y_delta, 2.0),
    )
    rho = _f32_mul(1.0 if n > 0.0 else -1.0, libm.call("sqrtf", rho_squared))
    latitude_power = libm.call("powf", _f32_div(f, rho), _f32_div(1.0, n))
    latitude_radians = _f32_sub(
        _f32_mul(2.0, libm.call("atanf", latitude_power)),
        _f32_div(F32_PI, 2.0),
    )
    longitude_radians = _f32_add(lambda0, _f32_div(theta, n))
    longitude = _f32_radians_to_degrees(longitude_radians)
    if longitude > 180.0:
        longitude = _f32_sub(longitude, 360.0)
    return _f32_wrap_longitude(longitude), _f32_radians_to_degrees(latitude_radians)


def pinned_stereographic_coordinate(parameters: dict[str, Any], x: int, y: int) -> tuple[float, float]:
    """Port the pinned StereographicProjection.inverse operations."""
    libm = _float32_libm()
    projection = parameters["projection"]
    native = parameters["native_grid"]
    lambda0 = _f32_degrees_to_radians(projection["longitude_of_origin"])
    phi1 = _f32_degrees_to_radians(projection["latitude_of_origin"])
    sin_phi1, cos_phi1 = libm.call("sinf", phi1), libm.call("cosf", phi1)
    radius = source_float32(projection["radius_m"])
    east = source_grid_coordinate(native["x0"], native["dx"], x)
    north = source_grid_coordinate(native["y0"], native["dy"], y)
    rho_squared = _f32_add(_f32_mul(east, east), _f32_mul(north, north))
    rho = libm.call("sqrtf", rho_squared)
    if rho == 0.0:
        return _f32_wrap_longitude(projection["longitude_of_origin"]), \
            source_float32(projection["latitude_of_origin"])
    c = _f32_mul(2.0, libm.call("atan2f", rho, _f32_mul(2.0, radius)))
    sin_c, cos_c = libm.call("sinf", c), libm.call("cosf", c)
    latitude_arg = _f32_add(
        _f32_mul(cos_c, sin_phi1),
        _f32_div(_f32_mul(_f32_mul(north, sin_c), cos_phi1), rho),
    )
    latitude_radians = libm.call("asinf", latitude_arg)
    longitude_numerator = _f32_mul(east, sin_c)
    longitude_denominator = _f32_sub(
        _f32_mul(_f32_mul(rho, cos_phi1), cos_c),
        _f32_mul(_f32_mul(north, sin_phi1), sin_c),
    )
    longitude_radians = _f32_add(
        lambda0,
        libm.call("atan2f", longitude_numerator, longitude_denominator),
    )
    return _f32_wrap_longitude(_f32_radians_to_degrees(longitude_radians)), \
        _f32_radians_to_degrees(latitude_radians)


PINNED_PRODUCER_TRANSFORMS = {
    "rotated_latlon": pinned_rotated_coordinate,
    "lambert_conformal_conic": pinned_lambert_coordinate,
    "stereographic": pinned_stereographic_coordinate,
}


def generate(args: argparse.Namespace) -> tuple[int, str]:
    definitions = load_json(args.definitions)
    samples = load_json(args.sample_manifest)
    source_manifest = load_json(args.source_manifest)
    sample_rows = samples.get("public_open_meteo_samples", {}).get("objects", [])
    if not isinstance(sample_rows, list):
        raise ReferenceError("sample manifest public_open_meteo_samples.objects must be an array")
    sample = next((row for row in sample_rows if row.get("id") == args.sample_id), None)
    if not isinstance(sample, dict):
        raise ReferenceError(f"sample id is not present in the frozen manifest: {args.sample_id}")
    if sample.get("format_version") != 3 or not str(sample.get("classification", "")).endswith("_om_v3"):
        raise ReferenceError("coordinate generation requires a frozen real OM v3 sample")
    definition_id = sample.get("grid_definition_id")
    target = TARGETS.get(definition_id)
    if target is None:
        raise ReferenceError("only the three pinned projected grid definitions have this independent formula source")
    definition_rows = definitions.get("definitions", [])
    if not isinstance(definition_rows, list):
        raise ReferenceError("definitions manifest definitions must be an array")
    definition = next((row for row in definition_rows if row.get("id") == definition_id), None)
    if not isinstance(definition, dict):
        raise ReferenceError(f"frozen definition is missing: {definition_id}")
    source_path = definition.get("source_path")
    source_files = source_manifest.get("source_files", [])
    source_record = next((row for row in source_files if row.get("path") == source_path), None)
    sample_source = sample.get("grid_source", {})
    if not isinstance(source_record, dict) or not isinstance(sample_source, dict):
        raise ReferenceError("definition source file is not covered by the pinned source manifest")
    if sample_source.get("commit") != source_manifest.get("pinned_commit") or sample_source.get("path") != source_path:
        raise ReferenceError("sample producer source identity does not match the pinned definition source")
    method = getattr(args, "method", "independent_math")
    if method not in ("independent_math", "pinned_producer_coordinates"):
        raise ReferenceError(f"unsupported coordinate-reference method: {method}")
    transforms = PINNED_PRODUCER_TRANSFORMS if method == "pinned_producer_coordinates" else TRANSFORMS
    transform = transforms.get(definition.get("kind"))
    if transform is None:
        raise ReferenceError(f"unsupported independent transform: {definition.get('kind')}")
    parameters = definition.get("parameters")
    if not isinstance(parameters, dict):
        raise ReferenceError("frozen definition parameters are missing")

    nx, ny = parameters.get("nx"), parameters.get("ny")
    shape = sample.get("array", {}).get("shape")
    if not isinstance(nx, int) or not isinstance(ny, int) or not isinstance(shape, list) or shape[:2] != [ny, nx]:
        raise ReferenceError("sample root shape does not match the producer's frozen [ny,nx,ntime] profile")
    if sample.get("array", {}).get("axes_embedded_in_array_metadata") is not False:
        raise ReferenceError("sample axis profile must explicitly record that OM array axes are not embedded")
    if definition.get("parameters", {}).get("numeric_policy") != "openmeteo_f32_v1":
        raise ReferenceError("this generator freezes a 1e-4 degree maximum for the Open-Meteo f32 policy")

    local_object = sample.get("local_copy", {}).get("path")
    if not isinstance(local_object, str) or not local_object:
        raise ReferenceError("frozen sample has no local_copy.path")
    object_path = (args.root / local_object).resolve()
    if not object_path.is_file() or sha256(object_path) != sample.get("sha256"):
        raise ReferenceError("local OM object is missing or does not match its frozen SHA-256")

    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(("spatial_index", "row", "column", "longitude", "latitude"))
        for row in range(ny):
            for column in range(nx):
                longitude, latitude = transform(parameters, column, row)
                if not math.isfinite(longitude) or not math.isfinite(latitude):
                    raise ReferenceError(f"{method} transform produced a non-finite coordinate at {row},{column}")
                spatial_index = row * nx + column
                writer.writerow((spatial_index, row, column, format(longitude, ".17g"), format(latitude, ".17g")))
    return nx * ny, target


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--sample-manifest", type=Path, default=ROOT / "test/data/grids/sample-manifest.json")
    parser.add_argument("--definitions", type=Path, default=ROOT / "test/data/grids/definitions.json")
    parser.add_argument("--source-manifest", type=Path, default=ROOT / "test/data/grids/source-manifest.json")
    parser.add_argument("--sample-id", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--method",
        choices=("independent_math", "pinned_producer_coordinates"),
        default="independent_math",
        help="choose binary64 independent mathematics or the pinned producer's Float32 coordinate operations",
    )
    args = parser.parse_args()
    args.root = args.root.resolve()
    for name in ("sample_manifest", "definitions", "source_manifest"):
        path = getattr(args, name)
        if not path.is_absolute():
            setattr(args, name, args.root / path)
    if not args.output.is_absolute():
        args.output = args.root / args.output
    try:
        rows, target = generate(args)
    except (OSError, ReferenceError, KeyError, TypeError, OverflowError, ZeroDivisionError) as error:
        print(f"grid coordinate generation failed: {error}", file=sys.stderr)
        return 1
    print(f"generated {rows} {args.method} spatial reference rows for {target}: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
