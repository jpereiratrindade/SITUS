// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#include "situs/spatial/raster_service.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <gdal_alg.h>
#include <gdal_priv.h>
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

} // namespace

nlohmann::json characterize_raster_over_geometry(
    const RasterConfig& cfg,
    const nlohmann::json& geojson_geom_4326,
    double reference_area_m2
) {
    if (!cfg.approved || cfg.geotiff_path.empty() || !std::filesystem::exists(cfg.geotiff_path)) {
        return {
            {"indicator_id", "land_cover_composition"},
            {"value", nullptr},
            {"unit", "m2_ha_and_fraction"},
            {"status", "NO_DATA"},
            {"source_id", cfg.source_id},
            {"dataset_version", cfg.dataset_version},
            {"reference_date", cfg.reference_date},
            {"native_resolution", cfg.native_resolution},
            {"method", "gdal_polygon_mask_categorical_nearest"},
            {"support", "poligono_efetivo_investigacao"},
            {"coverage_fraction", nullptr},
            {"limitations", "Camada ambiental raster pendente de aquisicao administrativa e aprovacao local."},
            {"uncertainty", nullptr}
        };
    }

    if (geojson_geom_4326.is_null() || !geojson_geom_4326.is_object()) {
        return {
            {"indicator_id", "land_cover_composition"},
            {"value", nullptr},
            {"unit", "m2_ha_and_fraction"},
            {"status", "NOT_APPLICABLE"},
            {"source_id", cfg.source_id},
            {"dataset_version", cfg.dataset_version},
            {"native_resolution", cfg.native_resolution},
            {"method", "gdal_polygon_mask_categorical_nearest"},
            {"support", "geometria_ausente"},
            {"coverage_fraction", nullptr},
            {"limitations", "Geometria poligonal necessaria para composicao raster."}
        };
    }

    GDALAllRegister();
    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(cfg.geotiff_path.c_str(), GA_ReadOnly));
    if (!ds) {
        return {
            {"indicator_id", "land_cover_composition"},
            {"value", nullptr},
            {"unit", "m2_ha_and_fraction"},
            {"status", "NO_DATA"},
            {"source_id", cfg.source_id},
            {"dataset_version", cfg.dataset_version},
            {"native_resolution", cfg.native_resolution},
            {"method", "gdal_polygon_mask_categorical_nearest"},
            {"support", "poligono_efetivo_investigacao"},
            {"coverage_fraction", nullptr},
            {"limitations", "Falha ao abrir GeoTIFF local aprovado."}
        };
    }
    struct DsGuard { GDALDataset* p; ~DsGuard() { if (p) GDALClose(p); } } guard{ds};

    double gt[6]{};
    if (ds->GetGeoTransform(gt) != CE_None || gt[1] == 0.0 || gt[5] == 0.0) {
        return {
            {"indicator_id", "land_cover_composition"},
            {"value", nullptr},
            {"unit", "m2_ha_and_fraction"},
            {"status", "NO_DATA"},
            {"source_id", cfg.source_id},
            {"dataset_version", cfg.dataset_version},
            {"native_resolution", cfg.native_resolution},
            {"method", "gdal_polygon_mask_categorical_nearest"},
            {"support", "poligono_efetivo_investigacao"},
            {"coverage_fraction", nullptr},
            {"limitations", "GeoTransform invalido no raster local."}
        };
    }

    const std::string geom_str = geojson_geom_4326.dump();
    OGRGeometryH raw_handle = OGR_G_CreateGeometryFromJson(geom_str.c_str());
    OGRGeometry* raw_geom = OGRGeometry::FromHandle(raw_handle);
    if (!raw_geom) {
        return {
            {"indicator_id", "land_cover_composition"},
            {"value", nullptr},
            {"unit", "m2_ha_and_fraction"},
            {"status", "NO_DATA"},
            {"source_id", cfg.source_id},
            {"dataset_version", cfg.dataset_version},
            {"native_resolution", cfg.native_resolution},
            {"method", "gdal_polygon_mask_categorical_nearest"},
            {"support", "poligono_efetivo_investigacao"},
            {"coverage_fraction", nullptr},
            {"limitations", "Falha ao interpretar geometria GeoJSON para mascara raster."}
        };
    }
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
                return {
                    {"indicator_id", "land_cover_composition"},
                    {"value", nullptr},
                    {"unit", "m2_ha_and_fraction"},
                    {"status", "NO_DATA"},
                    {"source_id", cfg.source_id},
                    {"dataset_version", cfg.dataset_version},
                    {"native_resolution", cfg.native_resolution},
                    {"method", "gdal_polygon_mask_categorical_nearest"},
                    {"support", "poligono_efetivo_investigacao"},
                    {"coverage_fraction", nullptr},
                    {"limitations", "Falha na transformacao de CRS da geometria para o CRS do raster."}
                };
            }
        }
    }

    OGREnvelope env;
    raw_geom->getEnvelope(&env);

    const int x_size = ds->GetRasterXSize();
    const int y_size = ds->GetRasterYSize();

    int x0 = static_cast<int>(std::floor((env.MinX - gt[0]) / gt[1]));
    int x1 = static_cast<int>(std::ceil((env.MaxX - gt[0]) / gt[1]));
    int y0 = static_cast<int>(std::floor((env.MaxY - gt[3]) / gt[5]));
    int y1 = static_cast<int>(std::ceil((env.MinY - gt[3]) / gt[5]));

    x0 = std::clamp(x0, 0, x_size);
    x1 = std::clamp(x1, 0, x_size);
    y0 = std::clamp(y0, 0, y_size);
    y1 = std::clamp(y1, 0, y_size);

    const int win_w = x1 - x0;
    const int win_h = y1 - y0;
    if (win_w <= 0 || win_h <= 0) {
        return {
            {"indicator_id", "land_cover_composition"},
            {"value", nullptr},
            {"unit", "m2_ha_and_fraction"},
            {"status", "NO_DATA"},
            {"source_id", cfg.source_id},
            {"dataset_version", cfg.dataset_version},
            {"native_resolution", cfg.native_resolution},
            {"method", "gdal_polygon_mask_categorical_nearest"},
            {"support", "poligono_efetivo_investigacao"},
            {"coverage_fraction", 0.0},
            {"limitations", "Area solicitada fora da extensao do raster local aprovado."}
        };
    }

    // Limite de segurança para janela raster (evitar alocação excessiva em áreas gigantes)
    if (static_cast<std::size_t>(win_w) * static_cast<std::size_t>(win_h) > 64000000ULL) {
        return {
            {"indicator_id", "land_cover_composition"},
            {"value", nullptr},
            {"unit", "m2_ha_and_fraction"},
            {"status", "NOT_APPLICABLE"},
            {"source_id", cfg.source_id},
            {"dataset_version", cfg.dataset_version},
            {"native_resolution", cfg.native_resolution},
            {"method", "gdal_polygon_mask_categorical_nearest"},
            {"support", "poligono_efetivo_investigacao"},
            {"coverage_fraction", nullptr},
            {"limitations", "Janela raster excede o limite de 64M pixels; selecione um recorte submunicipal menor."}
        };
    }

    // Criar máscara em memória rasterizando o polígono efetivo (sem ALL_TOUCHED: centro do pixel no polígono)
    GDALDriver* mem_drv = GetGDALDriverManager()->GetDriverByName("MEM");
    if (!mem_drv) {
        return {
            {"indicator_id", "land_cover_composition"},
            {"value", nullptr},
            {"unit", "m2_ha_and_fraction"},
            {"status", "NO_DATA"},
            {"source_id", cfg.source_id},
            {"dataset_version", cfg.dataset_version},
            {"limitations", "Driver MEM do GDAL indisponivel."}
        };
    }
    GDALDataset* mask_ds = mem_drv->Create("", win_w, win_h, 1, GDT_Byte, nullptr);
    struct MaskGuard { GDALDataset* p; ~MaskGuard() { if (p) GDALClose(p); } } mask_guard{mask_ds};

    double win_gt[6] = {
        gt[0] + x0 * gt[1],
        gt[1],
        0.0,
        gt[3] + y0 * gt[5],
        0.0,
        gt[5]
    };
    mask_ds->SetGeoTransform(win_gt);

    std::vector<uint8_t> mask_buf(static_cast<std::size_t>(win_w) * static_cast<std::size_t>(win_h), 0);
    GDALRasterBand* mask_band = mask_ds->GetRasterBand(1);
    mask_band->Fill(0.0);

    int band_list[1] = {1};
    OGRGeometryH geom_handles[1] = { static_cast<OGRGeometryH>(raw_geom) };
    double burn_values[1] = {1.0};
    if (GDALRasterizeGeometries(mask_ds, 1, band_list, 1, geom_handles, nullptr, nullptr, burn_values, nullptr, nullptr, nullptr) != CE_None) {
        return {
            {"indicator_id", "land_cover_composition"},
            {"value", nullptr},
            {"unit", "m2_ha_and_fraction"},
            {"status", "NO_DATA"},
            {"source_id", cfg.source_id},
            {"dataset_version", cfg.dataset_version},
            {"limitations", "Falha ao rasterizar mascara poligonal no GDAL."}
        };
    }
    if (mask_band->RasterIO(GF_Read, 0, 0, win_w, win_h, mask_buf.data(), win_w, win_h, GDT_Byte, 0, 0) != CE_None) {
        return {
            {"indicator_id", "land_cover_composition"},
            {"value", nullptr},
            {"unit", "m2_ha_and_fraction"},
            {"status", "NO_DATA"},
            {"source_id", cfg.source_id},
            {"dataset_version", cfg.dataset_version},
            {"limitations", "Falha ao ler mascara rasterizada."}
        };
    }

    GDALRasterBand* band = ds->GetRasterBand(1);
    int has_nodata = 0;
    const double nodata_val = band->GetNoDataValue(&has_nodata);

    std::vector<float> buf(static_cast<std::size_t>(win_w) * static_cast<std::size_t>(win_h));
    if (band->RasterIO(GF_Read, x0, y0, win_w, win_h, buf.data(), win_w, win_h, GDT_Float32, 0, 0) != CE_None) {
        return {
            {"indicator_id", "land_cover_composition"},
            {"value", nullptr},
            {"unit", "m2_ha_and_fraction"},
            {"status", "NO_DATA"},
            {"source_id", cfg.source_id},
            {"dataset_version", cfg.dataset_version},
            {"limitations", "Erro de leitura GDAL RasterIO."}
        };
    }

    const double pixel_area_m2 = is_projected_meters
        ? std::abs(gt[1] * gt[5])
        : (std::abs(gt[1] * 111320.0 * std::cos(-30.0 * 3.14159265358979 / 180.0)) * std::abs(gt[5] * 111320.0));

    std::map<int, std::size_t> counts;
    std::size_t mask_pixels = 0;
    std::size_t valid_pixels = 0;
    std::size_t nodata_pixels = 0;
    for (std::size_t i = 0; i < buf.size(); ++i) {
        if (mask_buf[i] == 0) continue;
        mask_pixels++;
        const double px = static_cast<double>(buf[i]);
        if (is_nodata_value(px, has_nodata, nodata_val)) {
            nodata_pixels++;
            continue;
        }
        const int cls = static_cast<int>(std::llround(px));
        counts[cls]++;
        valid_pixels++;
    }

    const double valid_area_m2 = static_cast<double>(valid_pixels) * pixel_area_m2;
    nlohmann::json classes = nlohmann::json::array();
    for (const auto& [cls, cnt] : counts) {
        const double cls_area_m2 = static_cast<double>(cnt) * pixel_area_m2;
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
            {"pixel_area_m2", pixel_area_m2},
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
        {"limitations", "Resolucao nominal 30m (pixel = 900 m2); pixels de borda avaliados por centro no poligono; nao descreve propriedade individual."}
    };
}

} // namespace situs::spatial
