// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace situs::spatial {

struct RasterConfig {
    std::string geotiff_path;
    std::string source_id = "mapbiomas_cobertura_30m_rs";
    std::string dataset_version = "PENDING";
    std::string native_resolution = "30 m";
    std::string reference_date = "2023-01-01";
    bool approved = false;
};

[[nodiscard]] nlohmann::json characterize_raster_over_geometry(
    const RasterConfig& cfg,
    const nlohmann::json& geojson_geom_4326,
    double reference_area_m2
);

} // namespace situs::spatial
