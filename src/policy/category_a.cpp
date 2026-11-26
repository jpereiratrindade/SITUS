// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#include "situs/policy/category_a.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace situs::policy {

std::expected<SourceMetadata, std::string>
validate_category_a_source(const nlohmann::json& j) {
    if (!j.is_object()) {
        return std::unexpected("Metadado de fonte deve ser objeto JSON");
    }

    const std::string category = j.value("category", "");
    if (category != "A") {
        return std::unexpected("Politica Categoria A: fonte rejeitada (category != 'A')");
    }

    const std::string approval = j.value("approval_status", "");
    if (approval != "APPROVED") {
        return std::unexpected("Fonte nao aprovada para ingestao (approval_status=" + approval + ")");
    }

    const std::string license = j.value("license", "");
    if (license.empty()) {
        return std::unexpected("Fonte rejeitada: licenca ausente");
    }

    if (!j.contains("allowed_fields") || !j["allowed_fields"].is_array() || j["allowed_fields"].empty()) {
        return std::unexpected("Fonte rejeitada: lista positiva allowed_fields obrigatoria");
    }

    SourceMetadata meta;
    meta.source_id = j.value("source_id", "");
    meta.title = j.value("title", "");
    meta.provider = j.value("provider", "");
    meta.category = category;
    meta.approval_status = approval;
    meta.license = license;
    meta.source_url = j.value("url", "");
    meta.catalog_url = j.value("catalog_url", "");
    meta.scope = j.value("scope", "");
    meta.spatial_level = j.value("spatial_level", "");
    meta.reference_date = j.contains("reference_date") && j["reference_date"].is_string()
        ? j["reference_date"].get<std::string>()
        : "";
    meta.native_crs = j.value("crs", "");
    meta.native_resolution = j.value("native_resolution", "");
    meta.limitations = j.value("limitations", "");

    if (meta.source_id.empty() || meta.title.empty() || meta.source_url.empty() || meta.native_crs.empty()) {
        return std::unexpected("Fonte rejeitada: source_id, title, url e crs sao obrigatorios");
    }

    for (const auto& f : j["allowed_fields"]) {
        if (f.is_string() && !f.get<std::string>().empty()) {
            meta.allowed_fields.push_back(f.get<std::string>());
        }
    }
    if (meta.allowed_fields.empty()) {
        return std::unexpected("Fonte rejeitada: nenhum campo valido em allowed_fields");
    }

    return meta;
}

nlohmann::json filter_allowed_attributes(
    const nlohmann::json& raw_attrs,
    const std::set<std::string>& allowed_fields
) {
    nlohmann::json filtered = nlohmann::json::object();
    if (!raw_attrs.is_object()) return filtered;
    for (auto it = raw_attrs.begin(); it != raw_attrs.end(); ++it) {
        if (allowed_fields.contains(it.key())) {
            filtered[it.key()] = it.value();
        }
    }
    return filtered;
}

std::expected<void, std::string> validate_crs(const std::string& crs) {
    if (crs == "EPSG:4326" || crs == "OGC:CRS84") {
        return {};
    }
    return std::unexpected("CRS nao suportado na fronteira (exigido EPSG:4326 ou OGC:CRS84)");
}

std::expected<void, std::string> validate_wgs84_point(double lon, double lat) {
    if (!std::isfinite(lon) || !std::isfinite(lat)) {
        return std::unexpected("Coordenadas devem ser numeros finitos");
    }
    if (lon < -180.0 || lon > 180.0 || lat < -90.0 || lat > 90.0) {
        return std::unexpected("Coordenadas fora dos limites WGS84");
    }
    return {};
}

std::expected<void, std::string> validate_municipality_code(const std::string& code) {
    if (code.size() != 7 || !std::ranges::all_of(code, [](unsigned char c) { return std::isdigit(c); })) {
        return std::unexpected("Codigo IBGE de municipio deve ter 7 digitos numericos");
    }
    return {};
}

std::expected<void, std::string> validate_cep_syntax(const std::string& cep) {
    std::string digits;
    digits.reserve(cep.size());
    for (unsigned char c : cep) {
        if (std::isdigit(c)) digits.push_back(static_cast<char>(c));
        else if (c != '-') {
            return std::unexpected("CEP contem caracteres invalidos");
        }
    }
    if (digits.size() != 8) {
        return std::unexpected("CEP deve conter exatamente 8 digitos");
    }
    return {};
}

namespace {

std::expected<std::size_t, std::string> count_ring_array(const nlohmann::json& rings, std::size_t max_vertices) {
    if (!rings.is_array() || rings.empty()) {
        return std::unexpected("Anel de poligono vazio ou invalido");
    }
    std::size_t total = 0;
    for (const auto& ring : rings) {
        if (!ring.is_array() || ring.size() < 4) {
            return std::unexpected("Anel de poligono deve ter ao menos 4 coordenadas");
        }
        total += ring.size();
        if (total > max_vertices) {
            return std::unexpected("Limite de vertices excedido");
        }
        for (const auto& pt : ring) {
            if (!pt.is_array() || pt.size() < 2 || !pt[0].is_number() || !pt[1].is_number()) {
                return std::unexpected("Coordenada invalida no poligono");
            }
            auto v = validate_wgs84_point(pt[0].get<double>(), pt[1].get<double>());
            if (!v) return std::unexpected(v.error());
        }
    }
    return total;
}

} // namespace

std::expected<std::size_t, std::string> count_geojson_vertices(
    const nlohmann::json& geom,
    std::size_t max_vertices
) {
    if (!geom.is_object()) {
        return std::unexpected("Geometria GeoJSON deve ser objeto");
    }
    const std::string type = geom.value("type", "");
    if (!geom.contains("coordinates")) {
        return std::unexpected("Geometria GeoJSON sem campo coordinates");
    }
    if (type == "Polygon") {
        return count_ring_array(geom["coordinates"], max_vertices);
    }
    if (type == "MultiPolygon") {
        const auto& polys = geom["coordinates"];
        if (!polys.is_array() || polys.empty()) {
            return std::unexpected("MultiPolygon vazio");
        }
        std::size_t total = 0;
        for (const auto& poly : polys) {
            auto c = count_ring_array(poly, max_vertices - total);
            if (!c) return c;
            total += *c;
        }
        return total;
    }
    return std::unexpected("Tipo de geometria nao suportado (esperado Polygon ou MultiPolygon)");
}

} // namespace situs::policy
