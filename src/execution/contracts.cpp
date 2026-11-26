// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#include "situs/execution/contracts.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <openssl/evp.h>
#include <sstream>

namespace situs::execution {

namespace {

std::optional<std::string> sha256_file(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return std::nullopt;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return std::nullopt;

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        return std::nullopt;
    }

    char buf[8192];
    while (ifs.read(buf, sizeof(buf)) || ifs.gcount() > 0) {
        EVP_DigestUpdate(ctx, buf, static_cast<size_t>(ifs.gcount()));
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    for (unsigned int i = 0; i < hash_len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

} // namespace

nlohmann::json synthesis_result_to_json(const situs::core::SynthesisResult& res) {
    nlohmann::json groups = nlohmann::json::array();
    for (const auto& g : res.groups) {
        groups.push_back({
            {"group_key", g.group_key},
            {"group_label", g.group_label},
            {"unit_count", g.unit_count},
            {"intersected_area_m2", g.intersected_area_m2},
            {"proportion_of_reference", g.proportion_of_reference}
        });
    }

    nlohmann::json units = nlohmann::json::array();
    for (const auto& u : res.units) {
        units.push_back({
            {"unit_code", u.unit_code},
            {"group_key", u.group_key},
            {"intersected_area_m2", u.intersected_area_m2},
            {"unit_total_area_m2", u.unit_total_area_m2},
            {"share_of_reference", u.share_of_reference},
            {"share_of_unit", u.share_of_unit}
        });
    }

    nlohmann::json j = {
        {"reference_area_m2", res.reference_area_m2},
        {"total_intersected_area_m2", res.total_intersected_area_m2},
        {"uncovered_area_m2", res.uncovered_area_m2.has_value() ? nlohmann::json(*res.uncovered_area_m2) : nlohmann::json(nullptr)},
        {"coverage_fraction", res.coverage_fraction.has_value() ? nlohmann::json(*res.coverage_fraction) : nlohmann::json(nullptr)},
        {"has_partial_coverage", res.has_partial_coverage},
        {"groups", groups},
        {"units", units},
        {"dataset_versions", res.dataset_versions},
        {"method", res.method}
    };
    return j;
}

std::expected<situs::core::SynthesisRequest, std::string>
parse_synthesis_request(const nlohmann::json& j) {
    if (!j.is_object()) {
        return std::unexpected("Payload de entrada deve ser um objeto JSON");
    }
    if (!j.contains("reference_area_m2") || !j["reference_area_m2"].is_number()) {
        return std::unexpected("Campo reference_area_m2 obrigatorio e numerico");
    }
    if (!j.contains("dataset_versions") || !j["dataset_versions"].is_array() || j["dataset_versions"].empty()) {
        return std::unexpected("Campo dataset_versions obrigatorio e nao vazio");
    }
    if (!j.contains("slices") || !j["slices"].is_array()) {
        return std::unexpected("Campo slices obrigatorio e deve ser um array");
    }

    situs::core::SynthesisRequest req;
    req.reference_area_m2 = j["reference_area_m2"].get<double>();

    const std::string rule = j.value("overlap_rule", "DISJOINT_SECTORS");
    if (rule == "DISJOINT_SECTORS") {
        req.overlap_rule = situs::core::OverlapRule::DisjointSectors;
    } else if (rule == "ALLOW_OVERLAP_EXPLICIT") {
        req.overlap_rule = situs::core::OverlapRule::AllowOverlapExplicit;
    } else {
        return std::unexpected("overlap_rule desconhecido");
    }

    for (const auto& dv : j["dataset_versions"]) {
        if (!dv.is_string() || dv.get<std::string>().empty()) {
            return std::unexpected("dataset_versions deve conter strings nao vazias");
        }
        req.dataset_versions.push_back(dv.get<std::string>());
    }

    for (const auto& s : j["slices"]) {
        if (!s.is_object()) {
            return std::unexpected("Cada elemento de slices deve ser um objeto");
        }
        if (!s.contains("unit_code") || !s["unit_code"].is_string() ||
            !s.contains("group_key") || !s["group_key"].is_string() ||
            !s.contains("intersected_area_m2") || !s["intersected_area_m2"].is_number() ||
            !s.contains("unit_total_area_m2") || !s["unit_total_area_m2"].is_number()) {
            return std::unexpected("Fatia invalida: unit_code, group_key, intersected_area_m2 e unit_total_area_m2 sao obrigatorios");
        }
        situs::core::AreaSliceInput slice;
        slice.unit_code = s["unit_code"].get<std::string>();
        slice.group_key = s["group_key"].get<std::string>();
        slice.group_label = s.value("group_label", slice.group_key);
        slice.intersected_area_m2 = s["intersected_area_m2"].get<double>();
        slice.unit_total_area_m2 = s["unit_total_area_m2"].get<double>();
        req.slices.push_back(std::move(slice));
    }

    return req;
}

nlohmann::json run_native_synthesis(const nlohmann::json& request_json) {
    const auto t0 = std::chrono::steady_clock::now();

    const std::string op_id = request_json.value("operation_id", "");
    const std::string requested_mode = request_json.value("requested_mode", "AUTO");
    const std::string contract_ver = request_json.value("contract_version", situs::core::kSynthesisContractVersion);

    if (op_id != situs::core::kSynthesisOperationId) {
        return {
            {"status", "REJECTED"},
            {"requested_mode", requested_mode},
            {"selected_target", nullptr},
            {"actual_target", nullptr},
            {"decision_reason", "OPERATION_NOT_ALLOWLISTED: Operacao nao permitida na execucao portavel"},
            {"fallback_reason", nullptr},
            {"operation_id", op_id},
            {"algorithm_version", situs::core::kSynthesisAlgorithmVersion},
            {"contract_version", contract_ver},
            {"dataset_versions", nlohmann::json::array()},
            {"method", "none"},
            {"native_resolution", "Escala cadastral censitaria IBGE 2022"},
            {"output", nullptr}
        };
    }

    if (!request_json.contains("inputs")) {
        return {
            {"status", "REJECTED"},
            {"requested_mode", requested_mode},
            {"selected_target", "NATIVE_SERVER"},
            {"actual_target", nullptr},
            {"decision_reason", "INVALID_INPUTS: Campo inputs ausente"},
            {"fallback_reason", nullptr},
            {"operation_id", op_id},
            {"algorithm_version", situs::core::kSynthesisAlgorithmVersion},
            {"contract_version", contract_ver},
            {"dataset_versions", nlohmann::json::array()},
            {"method", "none"},
            {"native_resolution", "Escala cadastral censitaria IBGE 2022"},
            {"output", nullptr}
        };
    }

    auto parsed = parse_synthesis_request(request_json["inputs"]);
    if (!parsed) {
        return {
            {"status", "REJECTED"},
            {"requested_mode", requested_mode},
            {"selected_target", "NATIVE_SERVER"},
            {"actual_target", nullptr},
            {"decision_reason", std::string("VALIDATION_ERROR: ") + parsed.error()},
            {"fallback_reason", nullptr},
            {"operation_id", op_id},
            {"algorithm_version", situs::core::kSynthesisAlgorithmVersion},
            {"contract_version", contract_ver},
            {"dataset_versions", nlohmann::json::array()},
            {"method", "none"},
            {"native_resolution", "Escala cadastral censitaria IBGE 2022"},
            {"output", nullptr}
        };
    }

    auto res = situs::core::compute_area_synthesis(*parsed);
    const auto t1 = std::chrono::steady_clock::now();
    const double server_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (!res) {
        return {
            {"status", "REJECTED"},
            {"requested_mode", requested_mode},
            {"selected_target", "NATIVE_SERVER"},
            {"actual_target", nullptr},
            {"decision_reason", std::string("CORE_ERROR: ") + res.error().message},
            {"fallback_reason", nullptr},
            {"operation_id", op_id},
            {"algorithm_version", situs::core::kSynthesisAlgorithmVersion},
            {"contract_version", situs::core::kSynthesisContractVersion},
            {"dataset_versions", parsed->dataset_versions},
            {"method", "none"},
            {"native_resolution", "Escala cadastral censitaria IBGE 2022"},
            {"timings_ms", {{"server_ms", server_ms}, {"worker_ms", nullptr}, {"transfer_ms", nullptr}, {"total_ms", server_ms}}},
            {"output", nullptr}
        };
    }

    return {
        {"status", "COMPLETED"},
        {"requested_mode", requested_mode},
        {"selected_target", "NATIVE_SERVER"},
        {"actual_target", "NATIVE_SERVER"},
        {"decision_reason", requested_mode == "PREFER_SERVER"
            ? "USER_PREFERENCE_SERVER: Execucao nativa no servidor conforme preferencia"
            : "NATIVE_SERVER_REFERENCE: Execucao nativa de referencia no servidor C++26"},
        {"fallback_reason", nullptr},
        {"operation_id", situs::core::kSynthesisOperationId},
        {"algorithm_version", situs::core::kSynthesisAlgorithmVersion},
        {"contract_version", situs::core::kSynthesisContractVersion},
        {"dataset_versions", res->dataset_versions},
        {"method", res->method},
        {"native_resolution", "Escala cadastral censitaria IBGE 2022 (poligonal)"},
        {"stages", nlohmann::json::array({{{"stage", "area_synthesis"}, {"target", "NATIVE_SERVER"}}})},
        {"timings_ms", {
            {"server_ms", server_ms},
            {"worker_ms", nullptr},
            {"transfer_ms", nullptr},
            {"total_ms", server_ms}
        }},
        {"bytes_transferred", nullptr},
        {"output", synthesis_result_to_json(*res)}
    };
}

nlohmann::json load_execution_capabilities(
    const std::string& config_path,
    const std::string& wasm_path
) {
    nlohmann::json caps;
    std::ifstream ifs(config_path);
    if (ifs) {
        try {
            ifs >> caps;
        } catch (...) {
            caps = nlohmann::json::object();
        }
    }
    if (!caps.is_object()) {
        caps = {
            {"contract_version", situs::core::kSynthesisContractVersion},
            {"default_mode", "AUTO"},
            {"default_profile", "BALANCED"},
            {"allow_fallback", true},
            {"show_execution_details", true},
            {"operations", nlohmann::json::array()}
        };
    }

    const bool wasm_exists = std::filesystem::exists(wasm_path) && std::filesystem::is_regular_file(wasm_path);
    std::optional<std::string> wasm_hash;
    if (wasm_exists) {
        wasm_hash = sha256_file(wasm_path);
    }

    caps["wasm_artifact"] = {
        {"available", wasm_exists && wasm_hash.has_value()},
        {"path", "/wasm/situs_core.wasm"},
        {"sha256", wasm_hash.has_value() ? nlohmann::json(*wasm_hash) : nlohmann::json(nullptr)},
        {"version", wasm_exists ? nlohmann::json(situs::core::kSynthesisAlgorithmVersion) : nlohmann::json(nullptr)}
    };

    return caps;
}

} // namespace situs::execution
