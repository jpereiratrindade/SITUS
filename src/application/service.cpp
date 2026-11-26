// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#include "situs/application/service.hpp"
#include "situs/execution/contracts.hpp"

#include <filesystem>

namespace situs::application {

SitusService::SitusService(AppConfig cfg)
    : cfg_(std::move(cfg)), store_(cfg_.db_conninfo) {}

nlohmann::json SitusService::health() const {
    const bool db_ok = store_.ping();
    const bool wasm_ok = std::filesystem::exists(cfg_.wasm_artifact_path);
    return {
        {"status", db_ok ? "ok" : "degraded"},
        {"service", "situs"},
        {"version", "0.2.0"},
        {"policy", "CATEGORY_A_ONLY"},
        {"dependencies", {
            {"postgis", db_ok ? "up" : "down"},
            {"wasm_artifact", wasm_ok ? "available" : "unavailable"}
        }}
    };
}

std::expected<nlohmann::json, std::string> SitusService::catalog() const {
    return store_.get_catalog();
}

std::expected<nlohmann::json, std::string> SitusService::resolve_reference(const nlohmann::json& req) const {
    return store_.resolve_reference(req, cfg_.limits);
}

std::expected<nlohmann::json, std::string> SitusService::intersect_territories(const nlohmann::json& req) const {
    return store_.intersect_territories(req, cfg_.limits);
}

std::expected<nlohmann::json, std::string> SitusService::characterize_territories(const nlohmann::json& req) const {
    auto inter = store_.intersect_territories(req, cfg_.limits);
    if (!inter) return std::unexpected(inter.error());

    const bool skip_synthesis = req.value("skip_synthesis", false);
    nlohmann::json observations = nlohmann::json::array();

    if (!skip_synthesis) {
        auto synth_inputs = store_.build_synthesis_inputs(req, cfg_.limits);
        nlohmann::json synth_obs;
        if (synth_inputs) {
            nlohmann::json run_req = {
                {"operation_id", "territorial_area_synthesis"},
                {"contract_version", "0.2.0"},
                {"requested_mode", "PREFER_SERVER"},
                {"inputs", (*synth_inputs)["inputs"]}
            };
            auto exec_res = execution::run_native_synthesis(run_req);
            synth_obs = {
                {"indicator_id", "territorial_situation_synthesis"},
                {"value", exec_res["output"]},
                {"unit", "m2_and_proportion"},
                {"status", exec_res["status"] == "COMPLETED" ? "AVAILABLE" : "NO_DATA"},
                {"source_id", "ibge_setores_rs_2022"},
                {"dataset_version", (*inter)["dataset_versions"]},
                {"native_resolution", "Escala cadastral censitaria IBGE 2022 (poligonal)"},
                {"method", "postgis_st_intersection_plus_situs_core_synthesis"},
                {"support", "setores_censitarios_intersectados"},
                {"coverage_fraction", (*inter)["coverage_fraction"]},
                {"limitations", "Sintese territorial exclusivamente geometrica (Categoria A). Nao representa populacao ou domicilios."}
            };
        } else {
            synth_obs = {
                {"indicator_id", "territorial_situation_synthesis"},
                {"value", nullptr},
                {"unit", "m2_and_proportion"},
                {"status", "NO_DATA"},
                {"source_id", "ibge_setores_rs_2022"},
                {"dataset_version", nullptr},
                {"native_resolution", "Escala cadastral censitaria IBGE 2022 (poligonal)"},
                {"method", "postgis_st_intersection_plus_situs_core_synthesis"},
                {"support", "area_solicitada"},
                {"coverage_fraction", 0.0},
                {"limitations", synth_inputs.error()}
            };
        }
        observations.push_back(std::move(synth_obs));
    }

    auto raster_obs = spatial::characterize_raster_over_bbox(cfg_.raster, 0.0, 0.0, 0.0, 0.0);
    observations.push_back(std::move(raster_obs));

    return nlohmann::json{
        {"status", (*inter)["status"]},
        {"intersection", *inter},
        {"observations", observations}
    };
}

std::expected<nlohmann::json, std::string> SitusService::map_features(const nlohmann::json& req) const {
    return store_.get_map_features(req, cfg_.limits);
}

nlohmann::json SitusService::execution_capabilities() const {
    return execution::load_execution_capabilities(cfg_.execution_config_path, cfg_.wasm_artifact_path);
}

std::expected<nlohmann::json, std::string> SitusService::execution_inputs(const nlohmann::json& req) const {
    if (!req.is_object()) {
        return std::unexpected("Payload deve ser objeto JSON");
    }
    const std::string op = req.value("operation_id", "");
    if (op != "territorial_area_synthesis") {
        return std::unexpected("Operacao nao suportada para exportacao de entradas portaveis");
    }
    if (!req.contains("reference")) {
        return std::unexpected("Campo reference obrigatorio");
    }
    return store_.build_synthesis_inputs(req["reference"], cfg_.limits);
}

nlohmann::json SitusService::execution_run(const nlohmann::json& req) const {
    return execution::run_native_synthesis(req);
}

} // namespace situs::application
