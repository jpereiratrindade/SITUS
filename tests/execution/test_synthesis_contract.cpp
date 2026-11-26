// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#include "situs/core/synthesis.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace situs::core;

    // 1. Sintese valida com dois grupos disjuntos e cobertura parcial
    {
        SynthesisRequest req;
        req.reference_area_m2 = 10000.0;
        req.overlap_rule = OverlapRule::DisjointSectors;
        req.dataset_versions = {"2022.1"};
        req.slices = {
            {"431490205000001", "1", "Area urbana", 4000.0, 20000.0},
            {"431490205000002", "1", "Area urbana", 2000.0, 10000.0},
            {"431490205000003", "8", "Area rural", 1500.0, 50000.0}
        };

        auto res = compute_area_synthesis(req);
        assert(res.has_value());
        assert(std::abs(res->total_intersected_area_m2 - 7500.0) < 1e-6);
        assert(res->uncovered_area_m2.has_value());
        assert(std::abs(*res->uncovered_area_m2 - 2500.0) < 1e-6);
        assert(res->coverage_fraction.has_value());
        assert(std::abs(*res->coverage_fraction - 0.75) < 1e-6);
        assert(res->has_partial_coverage);
        assert(res->groups.size() == 2);
        assert(res->groups[0].group_key == "1");
        assert(std::abs(res->groups[0].proportion_of_reference - 0.60) < 1e-6);
        assert(res->groups[1].group_key == "8");
        assert(std::abs(res->groups[1].proportion_of_reference - 0.15) < 1e-6);
    }

    // 2. Sobreposicao excedendo referencia sob DisjointSectors deve ser rejeitada
    {
        SynthesisRequest req;
        req.reference_area_m2 = 1000.0;
        req.overlap_rule = OverlapRule::DisjointSectors;
        req.dataset_versions = {"2022.1"};
        req.slices = {
            {"U1", "1", "G1", 700.0, 1000.0},
            {"U2", "2", "G2", 700.0, 1000.0}
        };
        auto res = compute_area_synthesis(req);
        assert(!res.has_value());
        assert(res.error().code == CoreErrorCode::OverlapExceedsReference);
    }

    // 3. Area de referencia invalida deve falhar
    {
        SynthesisRequest req;
        req.reference_area_m2 = 0.0;
        req.dataset_versions = {"2022.1"};
        auto res = compute_area_synthesis(req);
        assert(!res.has_value());
        assert(res.error().code == CoreErrorCode::InvalidReferenceArea);
    }

    std::cout << "[PASS] test_synthesis_contract\n";
    return 0;
}
