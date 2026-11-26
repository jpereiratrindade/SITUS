// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "situs/domain/types.hpp"
#include "situs/spatial/postgis_store.hpp"
#include "situs/spatial/raster_service.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace situs::application {

struct AppConfig {
    std::string http_host = "127.0.0.1";
    unsigned short http_port = 8096;
    std::string db_conninfo;
    std::string web_dir = "web";
    std::string execution_config_path = "config/execution.example.json";
    std::string wasm_artifact_path = "web/wasm/situs_core.wasm";
    domain::SecurityLimits limits;
    spatial::RasterConfig raster;
};

class SitusService {
public:
    explicit SitusService(AppConfig cfg);

    [[nodiscard]] nlohmann::json health() const;
    [[nodiscard]] std::expected<nlohmann::json, std::string> catalog() const;
    [[nodiscard]] std::expected<nlohmann::json, std::string> search_municipalities(const std::string& query) const;
    [[nodiscard]] std::expected<nlohmann::json, std::string> resolve_reference(const nlohmann::json& req) const;
    [[nodiscard]] std::expected<nlohmann::json, std::string> intersect_territories(const nlohmann::json& req) const;
    [[nodiscard]] std::expected<nlohmann::json, std::string> characterize_territories(const nlohmann::json& req) const;
    [[nodiscard]] std::expected<nlohmann::json, std::string> map_features(const nlohmann::json& req) const;
    [[nodiscard]] nlohmann::json execution_capabilities() const;
    [[nodiscard]] std::expected<nlohmann::json, std::string> execution_inputs(const nlohmann::json& req) const;
    [[nodiscard]] nlohmann::json execution_run(const nlohmann::json& req) const;

    [[nodiscard]] const AppConfig& config() const noexcept { return cfg_; }

private:
    AppConfig cfg_;
    spatial::PostgisStore store_;
};

} // namespace situs::application
