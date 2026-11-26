// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#include "situs/spatial/raster_service.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <gdal_priv.h>
#include <map>
#include <vector>

namespace situs::spatial {

nlohmann::json characterize_raster_over_bbox(
    const RasterConfig& cfg,
    double min_lon,
    double min_lat,
    double max_lon,
    double max_lat
) {
    if (!cfg.approved || cfg.geotiff_path.empty() || !std::filesystem::exists(cfg.geotiff_path)) {
        return {
            {"indicator_id", "land_cover_composition"},
            {"value", nullptr},
            {"unit", "percent"},
            {"status", "NO_DATA"},
            {"source_id", cfg.source_id},
            {"dataset_version", cfg.dataset_version},
            {"reference_date", nullptr},
            {"native_resolution", cfg.native_resolution},
            {"method", "gdal_categorical_pixel_count_nearest"},
            {"support", "area_solicitada"},
            {"coverage_fraction", nullptr},
            {"limitations", "Camada ambiental raster pendente de aquisicao administrativa e aprovacao local. Nenhum dado sintetico foi substituido."},
            {"uncertainty", nullptr}
        };
    }

    GDALAllRegister();
    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(cfg.geotiff_path.c_str(), GA_ReadOnly));
    if (!ds) {
        return {
            {"indicator_id", "land_cover_composition"},
            {"value", nullptr},
            {"unit", "percent"},
            {"status", "NO_DATA"},
            {"source_id", cfg.source_id},
            {"dataset_version", cfg.dataset_version},
            {"native_resolution", cfg.native_resolution},
            {"method", "gdal_categorical_pixel_count_nearest"},
            {"support", "area_solicitada"},
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
            {"unit", "percent"},
            {"status", "NO_DATA"},
            {"source_id", cfg.source_id},
            {"dataset_version", cfg.dataset_version},
            {"native_resolution", cfg.native_resolution},
            {"method", "gdal_categorical_pixel_count_nearest"},
            {"support", "area_solicitada"},
            {"coverage_fraction", nullptr},
            {"limitations", "GeoTransform invalido no raster local."}
        };
    }

    const int x_size = ds->GetRasterXSize();
    const int y_size = ds->GetRasterYSize();

    int x0 = static_cast<int>(std::floor((min_lon - gt[0]) / gt[1]));
    int x1 = static_cast<int>(std::ceil((max_lon - gt[0]) / gt[1]));
    int y0 = static_cast<int>(std::floor((max_lat - gt[3]) / gt[5]));
    int y1 = static_cast<int>(std::ceil((min_lat - gt[3]) / gt[5]));

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
            {"unit", "percent"},
            {"status", "NO_DATA"},
            {"source_id", cfg.source_id},
            {"dataset_version", cfg.dataset_version},
            {"native_resolution", cfg.native_resolution},
            {"method", "gdal_categorical_pixel_count_nearest"},
            {"support", "area_solicitada"},
            {"coverage_fraction", 0.0},
            {"limitations", "Area solicitada fora da extensao do raster local aprovado."}
        };
    }

    GDALRasterBand* band = ds->GetRasterBand(1);
    int has_nodata = 0;
    const double nodata_val = band->GetNoDataValue(&has_nodata);

    std::vector<int32_t> buf(static_cast<std::size_t>(win_w) * static_cast<std::size_t>(win_h));
    if (band->RasterIO(GF_Read, x0, y0, win_w, win_h, buf.data(), win_w, win_h, GDT_Int32, 0, 0) != CE_None) {
        return {
            {"indicator_id", "land_cover_composition"},
            {"value", nullptr},
            {"unit", "percent"},
            {"status", "NO_DATA"},
            {"source_id", cfg.source_id},
            {"dataset_version", cfg.dataset_version},
            {"native_resolution", cfg.native_resolution},
            {"method", "gdal_categorical_pixel_count_nearest"},
            {"support", "area_solicitada"},
            {"coverage_fraction", nullptr},
            {"limitations", "Erro de leitura GDAL RasterIO."}
        };
    }

    std::map<int32_t, std::size_t> counts;
    std::size_t valid_pixels = 0;
    std::size_t nodata_pixels = 0;
    for (int32_t px : buf) {
        if (has_nodata && static_cast<double>(px) == nodata_val) {
            nodata_pixels++;
            continue;
        }
        counts[px]++;
        valid_pixels++;
    }

    nlohmann::json classes = nlohmann::json::array();
    for (const auto& [cls, cnt] : counts) {
        classes.push_back({
            {"class_id", cls},
            {"pixel_count", cnt},
            {"fraction", valid_pixels > 0 ? static_cast<double>(cnt) / static_cast<double>(valid_pixels) : 0.0}
        });
    }

    const std::size_t total_pixels = valid_pixels + nodata_pixels;
    return {
        {"indicator_id", "land_cover_composition"},
        {"value", {{"classes", classes}, {"valid_pixels", valid_pixels}, {"nodata_pixels", nodata_pixels}}},
        {"unit", "fraction"},
        {"status", valid_pixels > 0 ? "AVAILABLE" : "NO_DATA"},
        {"source_id", cfg.source_id},
        {"dataset_version", cfg.dataset_version},
        {"native_resolution", cfg.native_resolution},
        {"method", "gdal_categorical_pixel_count_nearest"},
        {"support", "bbox_janela_raster"},
        {"coverage_fraction", total_pixels > 0 ? static_cast<double>(valid_pixels) / static_cast<double>(total_pixels) : 0.0},
        {"limitations", "Resolucao nominal 30m nao equivale a exatidao posicional e nao descreve propriedade individual."}
    };
}

} // namespace situs::spatial
