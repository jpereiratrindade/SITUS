// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>

namespace situs::domain {

enum class ReferenceKind {
    Municipality,
    Point,
    Polygon,
    Cep
};

enum class ResolutionStatus {
    Resolved,
    Ambiguous,
    Unavailable,
    OutsideCoverage,
    Rejected
};

struct SecurityLimits {
    std::size_t max_body_bytes = 1048576;
    std::size_t max_polygon_vertices = 5000;
    double max_query_area_km2 = 5000.0;
    double max_radius_m = 50000.0;
    std::size_t max_map_features = 1500;
};

inline std::string to_string(ResolutionStatus s) {
    switch (s) {
        case ResolutionStatus::Resolved: return "RESOLVED";
        case ResolutionStatus::Ambiguous: return "AMBIGUOUS";
        case ResolutionStatus::Unavailable: return "UNAVAILABLE";
        case ResolutionStatus::OutsideCoverage: return "OUTSIDE_COVERAGE";
        case ResolutionStatus::Rejected: return "REJECTED";
    }
    return "REJECTED";
}

} // namespace situs::domain
