// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "situs/domain/types.hpp"
#include <expected>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

namespace situs::policy {

struct SourceMetadata {
    std::string source_id;
    std::string title;
    std::string provider;
    std::string category;
    std::string approval_status;
    std::string license;
    std::string source_url;
    std::string catalog_url;
    std::string scope;
    std::string spatial_level;
    std::string reference_date;
    std::string native_crs;
    std::string native_resolution;
    std::vector<std::string> allowed_fields;
    std::string limitations;
};

[[nodiscard]] std::expected<SourceMetadata, std::string>
validate_category_a_source(const nlohmann::json& source_json);

[[nodiscard]] nlohmann::json filter_allowed_attributes(
    const nlohmann::json& raw_attrs,
    const std::set<std::string>& allowed_fields
);

[[nodiscard]] std::expected<void, std::string> validate_crs(const std::string& crs);
[[nodiscard]] std::expected<void, std::string> validate_wgs84_point(double lon, double lat);
[[nodiscard]] std::expected<void, std::string> validate_municipality_code(const std::string& code);
[[nodiscard]] std::expected<void, std::string> validate_district_code(const std::string& code);
[[nodiscard]] std::expected<void, std::string> validate_cep_syntax(const std::string& cep);
[[nodiscard]] std::expected<void, std::string> validate_grid_cell_size_m(int cell_size_m);

struct GridCellId {
    int cell_size_m = 1000;
    long long ix = 0;
    long long iy = 0;
};

[[nodiscard]] std::expected<GridCellId, std::string> parse_grid_cell_id(const std::string& cell_id);
[[nodiscard]] std::expected<std::size_t, std::string> count_geojson_vertices(
    const nlohmann::json& geom,
    std::size_t max_vertices
);

} // namespace situs::policy
