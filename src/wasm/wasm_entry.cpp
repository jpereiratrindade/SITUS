// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#include "situs/core/synthesis.hpp"
#include <nlohmann/json.hpp>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define SITUS_WASM_KEEP EMSCRIPTEN_KEEPALIVE
#else
#define SITUS_WASM_KEEP
#endif

namespace {

thread_local std::string g_last_response;

nlohmann::json make_error(const std::string& status, const std::string& reason, const std::string& requested_mode = "PREFER_LOCAL") {
    return {
        {"status", status},
        {"requested_mode", requested_mode},
        {"selected_target", "BROWSER_WASM"},
        {"actual_target", nullptr},
        {"decision_reason", reason},
        {"fallback_reason", nullptr},
        {"operation_id", situs::core::kSynthesisOperationId},
        {"algorithm_version", situs::core::kSynthesisAlgorithmVersion},
        {"contract_version", situs::core::kSynthesisContractVersion},
        {"dataset_versions", nlohmann::json::array()},
        {"method", "none"},
        {"native_resolution", "Escala cadastral censitaria IBGE 2022 (poligonal)"},
        {"output", nullptr}
    };
}

} // namespace

extern "C" {

SITUS_WASM_KEEP
const char* situs_wasm_version() {
    return situs::core::kSynthesisAlgorithmVersion;
}

SITUS_WASM_KEEP
const char* situs_wasm_run_synthesis(const char* request_cstr) {
    if (!request_cstr) {
        g_last_response = make_error("REJECTED", "EMPTY_INPUT: Ponteiro nulo recebido pelo modulo Wasm").dump();
        return g_last_response.c_str();
    }

    nlohmann::json req_json;
    try {
        req_json = nlohmann::json::parse(request_cstr);
    } catch (const std::exception& e) {
        g_last_response = make_error("REJECTED", std::string("JSON_PARSE_ERROR: ") + e.what()).dump();
        return g_last_response.c_str();
    }

    const std::string requested_mode = req_json.value("requested_mode", "PREFER_LOCAL");
    const std::string op_id = req_json.value("operation_id", "");
    if (op_id != situs::core::kSynthesisOperationId) {
        g_last_response = make_error("REJECTED", "OPERATION_NOT_SUPPORTED_IN_WASM", requested_mode).dump();
        return g_last_response.c_str();
    }

    if (!req_json.contains("inputs") || !req_json["inputs"].is_object()) {
        g_last_response = make_error("REJECTED", "MISSING_INPUTS_OBJECT", requested_mode).dump();
        return g_last_response.c_str();
    }

    const auto& inp = req_json["inputs"];
    if (!inp.contains("reference_area_m2") || !inp["reference_area_m2"].is_number() ||
        !inp.contains("dataset_versions") || !inp["dataset_versions"].is_array() ||
        !inp.contains("slices") || !inp["slices"].is_array()) {
        g_last_response = make_error("REJECTED", "INVALID_SYNTHESIS_SCHEMA", requested_mode).dump();
        return g_last_response.c_str();
    }

    situs::core::SynthesisRequest req;
    req.reference_area_m2 = inp["reference_area_m2"].get<double>();
    const std::string rule = inp.value("overlap_rule", "DISJOINT_SECTORS");
    if (rule == "DISJOINT_SECTORS") {
        req.overlap_rule = situs::core::OverlapRule::DisjointSectors;
    } else if (rule == "ALLOW_OVERLAP_EXPLICIT") {
        req.overlap_rule = situs::core::OverlapRule::AllowOverlapExplicit;
    } else {
        g_last_response = make_error("REJECTED", "INVALID_OVERLAP_RULE", requested_mode).dump();
        return g_last_response.c_str();
    }

    for (const auto& dv : inp["dataset_versions"]) {
        if (dv.is_string()) req.dataset_versions.push_back(dv.get<std::string>());
    }
    for (const auto& s : inp["slices"]) {
        if (!s.is_object() || !s.contains("unit_code") || !s.contains("group_key") ||
            !s.contains("intersected_area_m2") || !s.contains("unit_total_area_m2")) {
            g_last_response = make_error("REJECTED", "INVALID_SLICE_ENTRY", requested_mode).dump();
            return g_last_response.c_str();
        }
        situs::core::AreaSliceInput slice;
        slice.unit_code = s["unit_code"].get<std::string>();
        slice.group_key = s["group_key"].get<std::string>();
        slice.group_label = s.value("group_label", slice.group_key);
        slice.intersected_area_m2 = s["intersected_area_m2"].get<double>();
        slice.unit_total_area_m2 = s["unit_total_area_m2"].get<double>();
        req.slices.push_back(std::move(slice));
    }

    auto res = situs::core::compute_area_synthesis(req);
    if (!res) {
        g_last_response = make_error("REJECTED", std::string("CORE_ERROR: ") + res.error().message, requested_mode).dump();
        return g_last_response.c_str();
    }

    nlohmann::json groups = nlohmann::json::array();
    for (const auto& g : res->groups) {
        groups.push_back({
            {"group_key", g.group_key},
            {"group_label", g.group_label},
            {"unit_count", g.unit_count},
            {"intersected_area_m2", g.intersected_area_m2},
            {"proportion_of_reference", g.proportion_of_reference}
        });
    }
    nlohmann::json units = nlohmann::json::array();
    for (const auto& u : res->units) {
        units.push_back({
            {"unit_code", u.unit_code},
            {"group_key", u.group_key},
            {"intersected_area_m2", u.intersected_area_m2},
            {"unit_total_area_m2", u.unit_total_area_m2},
            {"share_of_reference", u.share_of_reference},
            {"share_of_unit", u.share_of_unit}
        });
    }

    nlohmann::json out = {
        {"status", "COMPLETED"},
        {"requested_mode", requested_mode},
        {"selected_target", "BROWSER_WASM"},
        {"actual_target", "BROWSER_WASM"},
        {"decision_reason", "WASM_WORKER_EXECUTION: Nucleo C++26 executado localmente em Web Worker"},
        {"fallback_reason", nullptr},
        {"operation_id", situs::core::kSynthesisOperationId},
        {"algorithm_version", situs::core::kSynthesisAlgorithmVersion},
        {"contract_version", situs::core::kSynthesisContractVersion},
        {"dataset_versions", res->dataset_versions},
        {"method", res->method},
        {"native_resolution", "Escala cadastral censitaria IBGE 2022 (poligonal)"},
        {"output", {
            {"reference_area_m2", res->reference_area_m2},
            {"total_intersected_area_m2", res->total_intersected_area_m2},
            {"uncovered_area_m2", res->uncovered_area_m2.has_value() ? nlohmann::json(*res->uncovered_area_m2) : nlohmann::json(nullptr)},
            {"coverage_fraction", res->coverage_fraction.has_value() ? nlohmann::json(*res->coverage_fraction) : nlohmann::json(nullptr)},
            {"has_partial_coverage", res->has_partial_coverage},
            {"groups", groups},
            {"units", units},
            {"dataset_versions", res->dataset_versions},
            {"method", res->method}
        }}
    };

    g_last_response = out.dump();
    return g_last_response.c_str();
}

} // extern "C"
