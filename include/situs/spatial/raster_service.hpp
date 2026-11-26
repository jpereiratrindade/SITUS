// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <expected>
#include <nlohmann/json.hpp>
#include <string>

namespace situs::spatial {

struct RasterConfig {
    std::string geotiff_path;
    std::string source_id = "mapbiomas_cobertura_30m_rs";
    std::string dataset_version = "PENDING";
    std::string native_resolution = "30 m";
    bool approved = false;
};

[[nodiscard]] nlohmann::json characterize_raster_over_bbox(
    const RasterConfig& cfg,
    double min_lon,
    double min_lat,
    double max_lon,
    double max_lat
);

} // namespace situs::spatial
