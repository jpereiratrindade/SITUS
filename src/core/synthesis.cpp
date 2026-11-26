// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#include "situs/core/synthesis.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace situs::core {

namespace {

bool is_finite_positive(double v) noexcept {
    return std::isfinite(v) && v > 0.0;
}

bool is_finite_non_negative(double v) noexcept {
    return std::isfinite(v) && v >= 0.0;
}

} // namespace

std::expected<SynthesisResult, CoreError>
compute_area_synthesis(const SynthesisRequest& req) noexcept {
    if (!is_finite_positive(req.reference_area_m2)) {
        return std::unexpected(CoreError{
            CoreErrorCode::InvalidReferenceArea,
            "reference_area_m2 deve ser finito e estritamente positivo"
        });
    }
    if (req.dataset_versions.empty()) {
        return std::unexpected(CoreError{
            CoreErrorCode::EmptyDatasetVersions,
            "dataset_versions nao pode ser vazio"
        });
    }

    SynthesisResult out;
    out.reference_area_m2 = req.reference_area_m2;
    out.dataset_versions = req.dataset_versions;
    out.method = req.overlap_rule == OverlapRule::DisjointSectors
        ? "disjoint_sector_area_synthesis_v0_2"
        : "explicit_overlap_area_synthesis_v0_2";

    std::map<std::string, GroupSynthesis> group_acc;
    double total_intersected = 0.0;

    out.units.reserve(req.slices.size());
    for (const auto& s : req.slices) {
        if (s.unit_code.empty() || s.group_key.empty()) {
            return std::unexpected(CoreError{
                CoreErrorCode::InvalidSlice,
                "unit_code e group_key sao obrigatorios em cada fatia"
            });
        }
        if (!is_finite_non_negative(s.intersected_area_m2)) {
            return std::unexpected(CoreError{
                CoreErrorCode::NegativeArea,
                "intersected_area_m2 deve ser finito e >= 0"
            });
        }
        if (!is_finite_positive(s.unit_total_area_m2)) {
            return std::unexpected(CoreError{
                CoreErrorCode::InvalidUnitTotalArea,
                "unit_total_area_m2 deve ser finito e > 0"
            });
        }

        total_intersected += s.intersected_area_m2;

        UnitSynthesis u;
        u.unit_code = s.unit_code;
        u.group_key = s.group_key;
        u.intersected_area_m2 = s.intersected_area_m2;
        u.unit_total_area_m2 = s.unit_total_area_m2;
        u.share_of_reference = s.intersected_area_m2 / req.reference_area_m2;
        u.share_of_unit = std::clamp(s.intersected_area_m2 / s.unit_total_area_m2, 0.0, 1.0);
        out.units.push_back(std::move(u));

        auto& g = group_acc[s.group_key];
        if (g.group_key.empty()) {
            g.group_key = s.group_key;
            g.group_label = s.group_label.empty() ? s.group_key : s.group_label;
        }
        g.unit_count += 1;
        g.intersected_area_m2 += s.intersected_area_m2;
    }

    if (req.overlap_rule == OverlapRule::DisjointSectors) {
        // Tolerância física de 0.1% + 1 m² para diferenças de projeção/arredondamento
        const double max_allowed = req.reference_area_m2 * 1.001 + 1.0;
        if (total_intersected > max_allowed) {
            return std::unexpected(CoreError{
                CoreErrorCode::OverlapExceedsReference,
                "Soma das areas intersectadas excede a area de referencia sob regra DisjointSectors"
            });
        }
        const double clamped_total = std::min(total_intersected, req.reference_area_m2);
        out.total_intersected_area_m2 = total_intersected;
        out.uncovered_area_m2 = std::max(0.0, req.reference_area_m2 - clamped_total);
        out.coverage_fraction = clamped_total / req.reference_area_m2;
        out.has_partial_coverage = (*out.coverage_fraction) < 0.999;
    } else {
        out.total_intersected_area_m2 = total_intersected;
        out.uncovered_area_m2 = std::nullopt;
        out.coverage_fraction = std::nullopt;
        out.has_partial_coverage = false;
    }

    out.groups.reserve(group_acc.size());
    for (auto& [_, g] : group_acc) {
        g.proportion_of_reference = g.intersected_area_m2 / req.reference_area_m2;
        out.groups.push_back(std::move(g));
    }

    std::ranges::sort(out.groups, [](const GroupSynthesis& a, const GroupSynthesis& b) {
        if (a.intersected_area_m2 != b.intersected_area_m2) {
            return a.intersected_area_m2 > b.intersected_area_m2;
        }
        return a.group_key < b.group_key;
    });

    std::ranges::sort(out.units, [](const UnitSynthesis& a, const UnitSynthesis& b) {
        return a.unit_code < b.unit_code;
    });

    return out;
}

} // namespace situs::core
