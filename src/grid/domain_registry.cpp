#include "duckomo/domain_registry.hpp"
#include "duckomo/generated_grid_registry.hpp"
#include "duckomo/grid_identity.hpp"
#include "duckomo/om_reader.hpp"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace duckdb {
namespace duckomo {

namespace {

// Names are Open-Meteo S3 prefixes. The pinned Swift sources declare Float
// grids; decimal definitions are evaluated in DOUBLE here and checked against
// each file's published Float WKT bounds when present.
struct Definition {
	const char *name;
	std::uint32_t nx, ny;
	double lat0, lon0, dlat, dlon;
	const char *source;
	const char *sample_sha256;
	bool allow_out_of_range_latitude = false;
};

constexpr char UPSTREAM_COMMIT[] = "b06f4760fd1f997e5559bb380f64c5e496b4a509";

const std::vector<VerifiedDomain> &Domains() {
	static const std::vector<VerifiedDomain> domains = [] {
		static const Definition definitions[] = {
		    {"bom_access_global", 2048, 1536, -89.941406, -179.912109, 180.0/1536.0, 360.0/2048.0,
		     "Bom/BomDomain.swift", ""},
		    {"bom_access_global_ensemble", 800, 600, -89.85, -179.775, 180.0/600.0, 360.0/800.0,
		     "Bom/BomDomain.swift", ""},
		    {"cams_europe", 700, 420, 71.95, -24.95, -0.1, 0.1, "Cams/CamsDomain.swift", ""},
		    {"cams_global", 900, 451, -90.0, -180.0, 0.4, 0.4, "Cams/CamsDomain.swift", ""},
		    {"cams_global_greenhouse_gases", 3600, 1801, -90.0, -180.0, 0.1, 0.1, "Cams/CamsDomain.swift", ""},
		    {"chmi_aladin_cz_1km", 501, 290, 48.5, 12.0, (51.098-48.5)/289.0, (18.995-12.0)/500.0,
		     "Chmi/ChmiDomain.swift", "96151beb40fb73374b7a9014008be3252f572d49b09ffd470e82c4cbdf53e51a"},
		    {"cma_grapes_global", 2880, 1440, -89.9375, -180.0, 0.125, 0.125, "CMA/CmaDomain.swift", ""},
		    {"cmc_gem_gdps_15km", 2400, 1201, -90.0, -180.0, 0.15, 0.15, "Gem/GemDomain.swift", ""},
		    {"cmc_gem_gdps", 2400, 1201, -90.0, -180.0, 0.15, 0.15, "Gem/GemDomain.swift", ""},
		    {"cmc_gem_gdps_15km_upper_level", 2400, 1201, -90.0, -180.0, 0.15, 0.15, "Gem/GemDomain.swift", ""},
		    {"cmc_gem_geps", 720, 361, -90.0, -180.0, 0.5, 0.5, "Gem/GemDomain.swift", ""},
		    {"cmc_gem_geps_ensemble_mean", 720, 361, -90.0, -180.0, 0.5, 0.5, "Gem/GemDomain.swift", ""},
		    {"copernicus_era5", 1440, 721, -90.0, -180.0, 0.25, 0.25, "Era5/Era5Domain.swift", ""},
		    {"copernicus_era5_ensemble", 720, 361, -90.0, -180.0, 0.5, 0.5, "Era5/Era5Domain.swift", ""},
		    {"copernicus_era5_land", 3600, 1801, -90.0, -180.0, 0.1, 0.1, "Era5/Era5Domain.swift", ""},
		    {"copernicus_era5_ocean", 720, 361, -90.0, -180.0, 0.5, 0.5, "Era5/Era5Domain.swift", ""},
		    {"dwd_ewam", 526, 721, 30.0, -10.5, 0.05, 0.1, "IconWave/IconWaveDomain.swift", ""},
		    {"dwd_gwam", 1440, 699, -85.25, -180.0, 0.25, 0.25, "IconWave/IconWaveDomain.swift", ""},
		    {"dwd_icon", 2879, 1441, -90.0, -180.0, 0.125, 0.125, "Icon/Icon.swift", ""},
		    {"dwd_icon_d2", 1215, 746, 43.18, -3.94, 0.02, 0.02, "Icon/Icon.swift", ""},
		    {"dwd_icon_d2_15min", 1215, 746, 43.18, -3.94, 0.02, 0.02, "Icon/Icon.swift", ""},
		    {"dwd_icon_d2_eps", 1214, 745, 43.18, -3.94, 0.02, 0.02, "Icon/Icon.swift", ""},
		    {"dwd_icon_d2_eps_ensemble_mean", 1214, 745, 43.18, -3.94, 0.02, 0.02, "Icon/Icon.swift", ""},
		    {"dwd_icon_eps", 1439, 721, -90.0, -180.0, 0.25, 0.25, "Icon/Icon.swift", ""},
		    {"dwd_icon_eps_ensemble_mean", 1439, 721, -90.0, -180.0, 0.25, 0.25, "Icon/Icon.swift", ""},
		    {"dwd_icon_eu", 1377, 657, 29.5, -23.5, 0.0625, 0.0625, "Icon/Icon.swift", ""},
		    {"dwd_icon_eu_eps", 689, 329, 29.5, -23.5, 0.125, 0.125, "Icon/Icon.swift", ""},
		    {"dwd_icon_eu_eps_ensemble_mean", 689, 329, 29.5, -23.5, 0.125, 0.125, "Icon/Icon.swift", ""},
		    {"ecmwf_aifs025_ensemble", 1440, 721, -90.0, -180.0, 0.25, 0.25, "Ecmwf/EcmwfDomain.swift", ""},
		    {"ecmwf_aifs025_ensemble_mean", 1440, 721, -90.0, -180.0, 0.25, 0.25, "Ecmwf/EcmwfDomain.swift", ""},
		    {"ecmwf_aifs025_single", 1440, 721, -90.0, -180.0, 0.25, 0.25, "Ecmwf/EcmwfDomain.swift", ""},
		    {"ecmwf_ifs025", 1440, 721, -90.0, -180.0, 0.25, 0.25, "Ecmwf/EcmwfDomain.swift", ""},
		    {"ecmwf_ifs025_ensemble", 1440, 721, -90.0, -180.0, 0.25, 0.25, "Ecmwf/EcmwfDomain.swift", ""},
		    {"ecmwf_ifs025_ensemble_mean", 1440, 721, -90.0, -180.0, 0.25, 0.25, "Ecmwf/EcmwfDomain.swift", ""},
		    {"ecmwf_wam025", 1440, 721, -90.0, -180.0, 0.25, 0.25, "Ecmwf/EcmwfDomain.swift", ""},
		    {"geosphere_arome_austria", 594, 492, 42.981, 5.498, 0.018, 0.028,
		     "GeoSphere/GeoSphereDomain.swift", "3086b9844a3b9eaf2170e1a745c7882a4d41b9546f5b90d259f17f33d1bd90ea"},
		    {"italia_meteo_arpae_icon_2i", 761, 761, 33.7, 3.0, 0.02, 0.025,
		     "ItaliaMeteoArpae/ItaliaMeteoArpaeDomain.swift", ""},
		    {"jma_gsm", 720, 361, -90.0, -180.0, 0.5, 0.5, "JMA/JmaDownloader.swift", ""},
		    {"jma_msm", 481, 505, 22.4, 120.0, 0.05, 0.0625, "JMA/JmaDownloader.swift", ""},
		    {"jma_msm_upper_level", 241, 253, 22.4, 120.0, 0.1, 0.125, "JMA/JmaDownloader.swift", ""},
		    {"kma_gdps", 2560, 1920, -90.0+180.0/1920.0/2.0, -180.0+360.0/2560.0/2.0,
		     180.0/1920.0, 360.0/2560.0, "Kma/KmaDomain.swift", ""},
		    {"knmi_harmonie_arome_netherlands", 390, 390, 49.0, 0.0, 0.018, 0.029, "Knmi/KnmiDomain.swift", ""},
		    {"meteofrance_arome_france0025", 1121, 717, 37.5, -12.0, 0.025, 0.025,
		     "MeteoFrance/MeteoFranceDomain.swift", ""},
		    {"meteofrance_arome_france0025_15min", 1121, 717, 37.5, -12.0, 0.025, 0.025,
		     "MeteoFrance/MeteoFranceDomain.swift", ""},
		    {"meteofrance_arome_france_hd", 2801, 1791, 37.5, -12.0, 0.01, 0.01,
		     "MeteoFrance/MeteoFranceDomain.swift", ""},
		    {"meteofrance_arome_france_hd_15min", 2801, 1791, 37.5, -12.0, 0.01, 0.01,
		     "MeteoFrance/MeteoFranceDomain.swift", ""},
		    {"meteofrance_arpege_europe", 741, 521, 20.0, -32.0, 0.1, 0.1,
		     "MeteoFrance/MeteoFranceDomain.swift", ""},
		    {"meteofrance_arpege_europe_probabilities", 741, 521, 20.0, -32.0, 0.1, 0.1,
		     "MeteoFrance/MeteoFranceDomain.swift", ""},
		    {"meteofrance_arpege_world025", 1440, 721, -90.0, -180.0, 0.25, 0.25,
		     "MeteoFrance/MeteoFranceDomain.swift", ""},
		    {"meteofrance_currents", 4320, 2041, -80.0+1.0/24.0, -180.0+1.0/24.0, 1.0/12.0, 1.0/12.0,
		     "MfWave/MfWaveDomain.swift", "", true},
		    {"meteofrance_sea_surface_temperature", 4320, 2041, -80.0+1.0/24.0, -180.0+1.0/24.0,
		     1.0/12.0, 1.0/12.0, "MfWave/MfWaveDomain.swift", "", true},
		    {"meteofrance_wave", 4320, 2041, -80.0+1.0/24.0, -180.0+1.0/24.0, 1.0/12.0, 1.0/12.0,
		     "MfWave/MfWaveDomain.swift", "", true},
		    {"ncep_aigefs025", 1440, 721, -90.0, -180.0, 0.25, 0.25, "GfsGraphCast/GfsGraphCastDomain.swift", ""},
		    {"ncep_aigefs025_ensemble_mean", 1440, 721, -90.0, -180.0, 0.25, 0.25,
		     "GfsGraphCast/GfsGraphCastDomain.swift", ""},
		    {"ncep_aigfs025", 1440, 721, -90.0, -180.0, 0.25, 0.25, "GfsGraphCast/GfsGraphCastDomain.swift", ""},
		    {"ncep_gefs025", 1440, 721, -90.0, -180.0, 0.25, 0.25, "Gfs/GfsDomain.swift", ""},
		    {"ncep_gefs025_ensemble_mean", 1440, 721, -90.0, -180.0, 0.25, 0.25, "Gfs/GfsDomain.swift", ""},
		    {"ncep_gefs05", 720, 361, -90.0, -180.0, 0.5, 0.5, "Gfs/GfsDomain.swift", ""},
		    {"ncep_gefs05_ensemble_mean", 720, 361, -90.0, -180.0, 0.5, 0.5, "Gfs/GfsDomain.swift", ""},
		    {"ncep_gfs013", 3072, 1536, (-0.11714935*1535.0)/2.0, -180.0, 0.11714935,
		     360.0/3072.0, "Gfs/GfsDomain.swift", ""},
		    {"ncep_gfs025", 1440, 721, -90.0, -180.0, 0.25, 0.25, "Gfs/GfsDomain.swift", ""},
		    {"ncep_gfs_graphcast025", 1440, 721, -90.0, -180.0, 0.25, 0.25,
		     "GfsGraphCast/GfsGraphCastDomain.swift", ""},
		    {"ncep_gfswave016", 2160, 406, -15.0, -180.0, 67.5/405.0, 360.0/2160.0, "Gfs/GfsDomain.swift", ""},
		    {"ncep_gfswave025", 1440, 721, -90.0, -180.0, 0.25, 0.25, "Gfs/GfsDomain.swift",
		     "0b44b22cde2f59a230423d54766826af7d28750f99f8052522844fe0b993dbfd"},
		    {"ncep_hgefs025_ensemble_mean", 1440, 721, -90.0, -180.0, 0.25, 0.25,
		     "GfsGraphCast/GfsGraphCastDomain.swift", ""},
		    {"ukmo_global_deterministic_10km", 2560, 1920, -90.0, -180.0, 180.0/1920.0, 360.0/2560.0,
		     "UKMO/UkmoDomain.swift", ""},
		    {"ukmo_global_ensemble_20km", 1280, 960, -90.0, -180.0, 180.0/960.0, 360.0/1280.0,
		     "UKMO/UkmoDomain.swift", ""},
		    {"ukmo_global_ensemble_mean_20km", 1280, 960, -90.0, -180.0, 180.0/960.0, 360.0/1280.0,
		     "UKMO/UkmoDomain.swift", ""},
		};
		std::vector<VerifiedDomain> result;
		result.reserve(sizeof(definitions) / sizeof(definitions[0]));
		for (const auto &definition : definitions) {
			try {
				result.push_back({definition.name,
				                  RegularGrid(definition.nx, definition.ny, definition.lat0, definition.lon0,
				                              definition.dlat, definition.dlon, GridStorageOrder::Separate,
				                              definition.allow_out_of_range_latitude),
				                  UPSTREAM_COMMIT,
				                  definition.sample_sha256, definition.source});
			} catch (const ReaderError &error) {
				throw ReaderError(error.Code(), std::string("invalid registered domain '") + definition.name +
				                                    "': " + error.what());
			}
		}
		return result;
	}();
	return domains;
}

const std::vector<RegisteredGridDefinition> &GridDefinitions() {
	static const std::vector<RegisteredGridDefinition> definitions = [] {
		auto generated_definitions = generated::BuildGridRegistryDefinitions();
		std::vector<RegisteredGridDefinition> result;
		result.reserve(generated_definitions.size());
		for (auto &record : generated_definitions) {
			// Constructing the runtime identity here validates that the checked-in
			// typed definition remains canonical and does not depend on provenance.
			const auto runtime_grid_id = GridId(record.definition);
			if (runtime_grid_id != record.grid_id) {
				throw ReaderError(ReaderErrorCode::InvalidShape,
				                  "generated grid registry runtime identity differs from its checked-in identity");
			}
			const auto runtime_parent_id = ParentGridId(record.definition).value_or("");
			if (runtime_parent_id != record.parent_grid_id) {
				throw ReaderError(ReaderErrorCode::InvalidShape,
				                  "generated grid registry parent identity differs from its checked-in identity");
			}
			std::vector<std::string> axis_order;
			axis_order.reserve(record.axis_count);
			for (std::size_t index = 0; index < record.axis_count; index++) {
				axis_order.emplace_back(record.axis_order[index]);
			}
			result.push_back({std::string(record.id), std::string(record.kind), std::move(record.definition),
			                  std::string(generated::GRID_REGISTRY_UPSTREAM_COMMIT), std::string(record.source_path),
			                  std::string(record.grid_id), std::string(record.parent_grid_id),
			                  std::string(record.expected_layout), std::move(axis_order),
			                  std::string(record.object_profile_status), std::string(record.evidence_level),
			                  std::string(record.evidence_sample_id), std::string(record.evidence_source_uri),
			                  std::string(record.evidence_build_pair), std::string(record.evidence_claims),
			                  std::string(record.parent_definition),
			                  record.domain_bindable});
		}
		for (const auto &definition : result) {
			if (definition.parent_definition.empty()) continue;
			const auto parent = std::find_if(result.begin(), result.end(), [&](const auto &candidate) {
				return candidate.name == definition.parent_definition;
			});
			if (parent == result.end() || ParentGridId(definition.definition) != GridId(parent->definition)) {
				throw ReaderError(ReaderErrorCode::InvalidShape,
				                  "generated grid registry parent definition identity is inconsistent");
			}
		}
		return result;
	}();
	return definitions;
}

} // namespace

const VerifiedDomain *FindVerifiedDomain(const std::string &name) {
	const auto &domains = Domains();
	for (const auto &domain : domains) {
		if (domain.name == name) {
			return &domain;
		}
	}
	return nullptr;
}

const RegisteredGridDefinition *FindRegisteredGridDefinition(const std::string &name) {
	const auto &definitions = GridDefinitions();
	for (const auto &definition : definitions) {
		if (definition.name == name) return &definition;
	}
	return nullptr;
}

} // namespace duckomo
} // namespace duckdb
