// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "situs/core/synthesis.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace situs::execution {

struct ExecutionEnvelope {
    std::string status; // COMPLETED | UNAVAILABLE | REJECTED | CANCELLED | FAILED
    std::string requested_mode;
    std::string selected_target;
    std::string actual_target;
    std::string decision_reason;
    std::optional<std::string> fallback_reason;
    std::string operation_id;
    std::string algorithm_version;
    std::string contract_version;
    std::vector<std::string> dataset_versions;
    std::string method;
    std::string native_resolution;
    nlohmann::json stages = nlohmann::json::array();
    nlohmann::json timings_ms;
    std::optional<std::size_t> bytes_transferred;
    nlohmann::json output;
};

[[nodiscard]] nlohmann::json synthesis_result_to_json(const situs::core::SynthesisResult& res);
[[nodiscard]] std::expected<situs::core::SynthesisRequest, std::string>
parse_synthesis_request(const nlohmann::json& j);

[[nodiscard]] nlohmann::json run_native_synthesis(const nlohmann::json& request_json);

[[nodiscard]] nlohmann::json load_execution_capabilities(
    const std::string& config_path,
    const std::string& wasm_path
);

} // namespace situs::execution
