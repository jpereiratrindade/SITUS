// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace situs::core {

inline constexpr const char* kSynthesisOperationId = "territorial_area_synthesis";
inline constexpr const char* kSynthesisAlgorithmVersion = "0.2.0";
inline constexpr const char* kSynthesisContractVersion = "0.2.0";

enum class OverlapRule {
    DisjointSectors,
    AllowOverlapExplicit
};

struct AreaSliceInput {
    std::string unit_code;
    std::string group_key;
    std::string group_label;
    double intersected_area_m2 = 0.0;
    double unit_total_area_m2 = 0.0;
};

struct SynthesisRequest {
    double reference_area_m2 = 0.0;
    OverlapRule overlap_rule = OverlapRule::DisjointSectors;
    std::vector<std::string> dataset_versions;
    std::vector<AreaSliceInput> slices;
};

struct GroupSynthesis {
    std::string group_key;
    std::string group_label;
    std::size_t unit_count = 0;
    double intersected_area_m2 = 0.0;
    double proportion_of_reference = 0.0;
};

struct UnitSynthesis {
    std::string unit_code;
    std::string group_key;
    double intersected_area_m2 = 0.0;
    double unit_total_area_m2 = 0.0;
    double share_of_reference = 0.0;
    double share_of_unit = 0.0;
};

struct SynthesisResult {
    double reference_area_m2 = 0.0;
    double total_intersected_area_m2 = 0.0;
    std::optional<double> uncovered_area_m2;
    std::optional<double> coverage_fraction;
    bool has_partial_coverage = false;
    std::vector<GroupSynthesis> groups;
    std::vector<UnitSynthesis> units;
    std::vector<std::string> dataset_versions;
    std::string method;
};

enum class CoreErrorCode {
    InvalidReferenceArea,
    EmptyDatasetVersions,
    InvalidSlice,
    NegativeArea,
    InvalidUnitTotalArea,
    OverlapExceedsReference
};

struct CoreError {
    CoreErrorCode code;
    std::string message;
};

[[nodiscard]] std::expected<SynthesisResult, CoreError>
compute_area_synthesis(const SynthesisRequest& req) noexcept;

} // namespace situs::core
