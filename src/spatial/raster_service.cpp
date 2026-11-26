// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#include "situs/spatial/raster_service.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <gdal_alg.h>
#include <gdal_priv.h>
#include <limits>
#include <map>
#include <memory>
#include <ogrsf_frmts.h>
#include <vector>

namespace situs::spatial {

namespace {

std::string mapbiomas_class_label(int cls) {
    switch (cls) {
        case 0:  return "Demais coberturas / Nao-campo (classe 0)";
        case 1:  return "Floresta";
        case 3:  return "Formacao Florestal";
        case 4:  return "Formacao Savanica";
        case 5:  return "Mangue";
        case 9:  return "Silvicultura";
        case 10: return "Vegetacao Herbacea e Arbustiva";
        case 11: return "Campo Alagado e Area Pantanosa";
        case 12: return "Formacao Campestre";
        case 15: return "Pastagem";
        case 18: return "Agricultura";
        case 19: return "Lavoura Temporaria";
        case 21: return "Mosaico de Usos";
        case 23: return "Praia, Duna e Areal";
        case 24: return "Area Urbanizada";
        case 25: return "Outras Areas nao Vegetadas";
        case 29: return "Afloramento Rochoso";
        case 33: return "Rio, Lago e Oceano";
        case 39: return "Soja";
        case 40: return "Arroz";
        default: return "Classe " + std::to_string(cls);
    }
}

bool is_nodata_value(double val, int has_nodata, double nodata_val) noexcept {
    if (!std::isfinite(val)) return true;
    if (!has_nodata) return false;
    if (std::isnan(nodata_val)) return std::isnan(val);
    const double scale = std::max(1.0, std::abs(nodata_val));
    return std::abs(val - nodata_val) <= (1e-5 * scale);
}

struct MaskedRasterWindow {
    std::vector<float> values;
    std::vector<uint8_t> mask;
    int has_nodata = 0;
    double nodata_val = 0.0;
    double pixel_area_m2 = 0.0;
};

std::expected<MaskedRasterWindow, nlohmann::json> extract_masked_window(
    const RasterConfig& cfg,
    const nlohmann::json& geojson_geom_4326,
    const std::string& indicator_id,
    const std::string& unit
) {
    auto make_err = [&](const std::string& status, const std::string& msg) {
        return nlohmann::json{
            {"indicator_id", indicator_id},
            {"value", nullptr},
            {"unit", unit},
            {"status", status},
            {"source_id", cfg.source_id},
            {"dataset_version", cfg.dataset_version},
            {"reference_date", cfg.reference_date},
            {"native_resolution", cfg.native_resolution},
            {"method", "gdal_polygon_mask_center_in_polygon"},
            {"support", "poligono_efetivo_investigacao"},
            {"coverage_fraction", nullptr},
            {"limitations", msg}
        };
    };

    if (!cfg.approved || cfg.geotiff_path.empty() || !std::filesystem::exists(cfg.geotiff_path)) {
        return std::unexpected(make_err("NO_DATA", "Camada ambiental raster pendente de aquisicao administrativa e aprovacao local."));
    }
    if (geojson_geom_4326.is_null() || !geojson_geom_4326.is_object()) {
        return std::unexpected(make_err("NOT_APPLICABLE", "Geometria poligonal necessaria para caracterizacao raster."));
    }

    GDALAllRegister();
    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(cfg.geotiff_path.c_str(), GA_ReadOnly));
    if (!ds) return std::unexpected(make_err("NO_DATA", "Falha ao abrir raster local aprovado."));
    struct DsGuard { GDALDataset* p; ~DsGuard() { if (p) GDALClose(p); } } guard{ds};

    double gt[6]{};
    if (ds->GetGeoTransform(gt) != CE_None || gt[1] == 0.0 || gt[5] == 0.0) {
        return std::unexpected(make_err("NO_DATA", "GeoTransform invalido no raster local."));
    }

    const std::string geom_str = geojson_geom_4326.dump();
    OGRGeometry* raw_geom = OGRGeometry::FromHandle(OGR_G_CreateGeometryFromJson(geom_str.c_str()));
    if (!raw_geom) return std::unexpected(make_err("NO_DATA", "Geometria GeoJSON invalida para mascara raster."));
    struct GeomGuard { OGRGeometry* g; ~GeomGuard() { OGRGeometryFactory::destroyGeometry(g); } } geom_guard{raw_geom};

    OGRSpatialReference src_srs;
    src_srs.SetWellKnownGeogCS("WGS84");
    src_srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    const OGRSpatialReference* raster_srs = ds->GetSpatialRef();
    bool is_projected_meters = false;
    if (raster_srs) {
        is_projected_meters = raster_srs->IsProjected() != 0;
        OGRSpatialReference dst_srs(*raster_srs);
        dst_srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        if (!src_srs.IsSame(&dst_srs)) {
            std::unique_ptr<OGRCoordinateTransformation> ct(OGRCreateCoordinateTransformation(&src_srs, &dst_srs));
            if (!ct || raw_geom->transform(ct.get()) != OGRERR_NONE) {
                return std::unexpected(make_err("NO_DATA", "Falha na transformacao de CRS da geometria para o raster."));
            }
        }
    }

    OGREnvelope env;
    raw_geom->getEnvelope(&env);

    const int x_size = ds->GetRasterXSize();
    const int y_size = ds->GetRasterYSize();
    int x0 = std::clamp(static_cast<int>(std::floor((env.MinX - gt[0]) / gt[1])), 0, x_size);
    int x1 = std::clamp(static_cast<int>(std::ceil((env.MaxX - gt[0]) / gt[1])), 0, x_size);
    int y0 = std::clamp(static_cast<int>(std::floor((env.MaxY - gt[3]) / gt[5])), 0, y_size);
    int y1 = std::clamp(static_cast<int>(std::ceil((env.MinY - gt[3]) / gt[5])), 0, y_size);

    const int win_w = x1 - x0;
    const int win_h = y1 - y0;
    if (win_w <= 0 || win_h <= 0) {
        return std::unexpected(make_err("NO_DATA", "Area solicitada fora da extensao do raster local."));
    }
    if (static_cast<std::size_t>(win_w) * static_cast<std::size_t>(win_h) > 64000000ULL) {
        return std::unexpected(make_err("NOT_APPLICABLE", "Janela raster excede 64M pixels; selecione um recorte submunicipal menor."));
    }

    GDALDriver* mem_drv = GetGDALDriverManager()->GetDriverByName("MEM");
    if (!mem_drv) return std::unexpected(make_err("NO_DATA", "Driver MEM do GDAL indisponivel."));
    GDALDataset* mask_ds = mem_drv->Create("", win_w, win_h, 1, GDT_Byte, nullptr);
    struct MaskGuard { GDALDataset* p; ~MaskGuard() { if (p) GDALClose(p); } } mask_guard{mask_ds};

    double win_gt[6] = { gt[0] + x0 * gt[1], gt[1], 0.0, gt[3] + y0 * gt[5], 0.0, gt[5] };
    mask_ds->SetGeoTransform(win_gt);
    GDALRasterBand* mask_band = mask_ds->GetRasterBand(1);
    mask_band->Fill(0.0);

    int band_list[1] = {1};
    OGRGeometryH geom_handles[1] = { static_cast<OGRGeometryH>(raw_geom) };
    double burn_values[1] = {1.0};
    if (GDALRasterizeGeometries(mask_ds, 1, band_list, 1, geom_handles, nullptr, nullptr, burn_values, nullptr, nullptr, nullptr) != CE_None) {
        return std::unexpected(make_err("NO_DATA", "Falha ao rasterizar mascara poligonal no GDAL."));
    }

    MaskedRasterWindow out;
    const std::size_t n_px = static_cast<std::size_t>(win_w) * static_cast<std::size_t>(win_h);
    out.mask.resize(n_px);
    out.values.resize(n_px);

    if (mask_band->RasterIO(GF_Read, 0, 0, win_w, win_h, out.mask.data(), win_w, win_h, GDT_Byte, 0, 0) != CE_None) {
        return std::unexpected(make_err("NO_DATA", "Falha ao ler mascara rasterizada."));
    }

    GDALRasterBand* band = ds->GetRasterBand(1);
    out.nodata_val = band->GetNoDataValue(&out.has_nodata);
    if (band->RasterIO(GF_Read, x0, y0, win_w, win_h, out.values.data(), win_w, win_h, GDT_Float32, 0, 0) != CE_None) {
        return std::unexpected(make_err("NO_DATA", "Erro de leitura GDAL RasterIO."));
    }

    out.pixel_area_m2 = is_projected_meters
        ? std::abs(gt[1] * gt[5])
        : (std::abs(gt[1] * 111320.0 * std::cos(-30.5 * 3.14159265358979 / 180.0)) * std::abs(gt[5] * 111320.0));

    return out;
}

} // namespace

nlohmann::json characterize_raster_over_geometry(
    const RasterConfig& cfg,
    const nlohmann::json& geojson_geom_4326,
    double reference_area_m2
) {
    auto win = extract_masked_window(cfg, geojson_geom_4326, "land_cover_composition", "m2_ha_and_fraction");
    if (!win) return win.error();

    std::map<int, std::size_t> counts;
    std::size_t mask_pixels = 0;
    std::size_t valid_pixels = 0;
    std::size_t nodata_pixels = 0;
    for (std::size_t i = 0; i < win->values.size(); ++i) {
        if (win->mask[i] == 0) continue;
        mask_pixels++;
        const double px = static_cast<double>(win->values[i]);
        if (is_nodata_value(px, win->has_nodata, win->nodata_val)) {
            nodata_pixels++;
            continue;
        }
        counts[static_cast<int>(std::llround(px))]++;
        valid_pixels++;
    }

    const double valid_area_m2 = static_cast<double>(valid_pixels) * win->pixel_area_m2;
    nlohmann::json classes = nlohmann::json::array();
    for (const auto& [cls, cnt] : counts) {
        const double cls_area_m2 = static_cast<double>(cnt) * win->pixel_area_m2;
        classes.push_back({
            {"class_id", cls},
            {"class_label", mapbiomas_class_label(cls)},
            {"pixel_count", cnt},
            {"pixel_fraction", valid_pixels > 0 ? static_cast<double>(cnt) / static_cast<double>(valid_pixels) : 0.0},
            {"area_m2", cls_area_m2},
            {"area_ha", cls_area_m2 / 10000.0},
            {"area_fraction_of_reference", reference_area_m2 > 0.0 ? cls_area_m2 / reference_area_m2 : 0.0}
        });
    }

    return {
        {"indicator_id", "land_cover_composition"},
        {"value", {
            {"classes", classes},
            {"pixel_area_m2", win->pixel_area_m2},
            {"mask_pixels", mask_pixels},
            {"valid_pixels", valid_pixels},
            {"nodata_pixels", nodata_pixels},
            {"valid_area_m2", valid_area_m2},
            {"valid_area_ha", valid_area_m2 / 10000.0},
            {"reference_area_m2", reference_area_m2}
        }},
        {"unit", "m2_ha_and_fraction"},
        {"status", valid_pixels > 0 ? "AVAILABLE" : "NO_DATA"},
        {"source_id", cfg.source_id},
        {"dataset_version", cfg.dataset_version},
        {"reference_date", cfg.reference_date},
        {"native_resolution", cfg.native_resolution},
        {"method", "gdal_polygon_mask_categorical_nearest_center_in_polygon"},
        {"support", "poligono_efetivo_investigacao"},
        {"coverage_fraction", mask_pixels > 0 ? static_cast<double>(valid_pixels) / static_cast<double>(mask_pixels) : 0.0},
        {"limitations", cfg.limitations}
    };
}

nlohmann::json characterize_continuous_raster_over_geometry(
    const RasterConfig& cfg,
    const nlohmann::json& geojson_geom_4326,
    const std::string& indicator_id,
    const std::string& unit
) {
    auto win = extract_masked_window(cfg, geojson_geom_4326, indicator_id, unit);
    if (!win) return win.error();

    std::size_t mask_pixels = 0;
    std::size_t valid_pixels = 0;
    std::size_t nodata_pixels = 0;
    double min_v = std::numeric_limits<double>::infinity();
    double max_v = -std::numeric_limits<double>::infinity();
    double sum = 0.0;
    double sum_sq = 0.0;

    for (std::size_t i = 0; i < win->values.size(); ++i) {
        if (win->mask[i] == 0) continue;
        mask_pixels++;
        const double px = static_cast<double>(win->values[i]);
        if (is_nodata_value(px, win->has_nodata, win->nodata_val)) {
            nodata_pixels++;
            continue;
        }
        valid_pixels++;
        min_v = std::min(min_v, px);
        max_v = std::max(max_v, px);
        sum += px;
        sum_sq += px * px;
    }

    if (valid_pixels == 0) {
        return {
            {"indicator_id", indicator_id},
            {"value", nullptr},
            {"unit", unit},
            {"status", "NO_DATA"},
            {"source_id", cfg.source_id},
            {"dataset_version", cfg.dataset_version},
            {"reference_date", cfg.reference_date},
            {"native_resolution", cfg.native_resolution},
            {"method", "gdal_polygon_mask_continuous_statistics"},
            {"support", "poligono_efetivo_investigacao"},
            {"coverage_fraction", 0.0},
            {"limitations", cfg.limitations}
        };
    }

    const double mean = sum / static_cast<double>(valid_pixels);
    const double var = std::max(0.0, (sum_sq / static_cast<double>(valid_pixels)) - (mean * mean));
    const double stddev = std::sqrt(var);

    return {
        {"indicator_id", indicator_id},
        {"value", {
            {"min", min_v},
            {"max", max_v},
            {"mean", mean},
            {"stddev", stddev},
            {"valid_pixels", valid_pixels},
            {"nodata_pixels", nodata_pixels},
            {"mask_pixels", mask_pixels}
        }},
        {"unit", unit},
        {"status", "AVAILABLE"},
        {"source_id", cfg.source_id},
        {"dataset_version", cfg.dataset_version},
        {"reference_date", cfg.reference_date},
        {"native_resolution", cfg.native_resolution},
        {"method", "gdal_polygon_mask_continuous_statistics"},
        {"support", "poligono_efetivo_investigacao"},
        {"coverage_fraction", mask_pixels > 0 ? static_cast<double>(valid_pixels) / static_cast<double>(mask_pixels) : 0.0},
        {"limitations", cfg.limitations}
    };
}

} // namespace situs::spatial
