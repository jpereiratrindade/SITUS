// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#include "situs/spatial/postgis_store.hpp"

#include <gdal_priv.h>
#include <libpq-fe.h>
#include <ogrsf_frmts.h>

#include <array>
#include <cstdlib>
#include <memory>
#include <set>
#include <vector>

namespace situs::spatial {

namespace {

struct PgConnDeleter {
    void operator()(PGconn* c) const noexcept {
        if (c) PQfinish(c);
    }
};

struct PgResDeleter {
    void operator()(PGresult* r) const noexcept {
        if (r) PQclear(r);
    }
};

using PgConnPtr = std::unique_ptr<PGconn, PgConnDeleter>;
using PgResPtr = std::unique_ptr<PGresult, PgResDeleter>;

std::expected<PgConnPtr, std::string> connect_pg(const std::string& conninfo) {
    PgConnPtr conn(PQconnectdb(conninfo.c_str()));
    if (!conn || PQstatus(conn.get()) != CONNECTION_OK) {
        std::string msg = conn ? PQerrorMessage(conn.get()) : "Falha ao alocar PGconn";
        return std::unexpected("Erro de conexao PostgreSQL: " + msg);
    }
    return conn;
}

std::expected<PgResPtr, std::string> exec_params(
    PGconn* conn,
    const char* sql,
    const std::vector<std::string>& params
) {
    std::vector<const char*> ptrs;
    ptrs.reserve(params.size());
    for (const auto& p : params) ptrs.push_back(p.c_str());

    PgResPtr res(PQexecParams(
        conn,
        sql,
        static_cast<int>(ptrs.size()),
        nullptr,
        ptrs.empty() ? nullptr : ptrs.data(),
        nullptr,
        nullptr,
        0
    ));
    if (!res) {
        return std::unexpected(std::string("PQexecParams retornou nulo: ") + PQerrorMessage(conn));
    }
    const auto st = PQresultStatus(res.get());
    if (st != PGRES_TUPLES_OK && st != PGRES_COMMAND_OK) {
        return std::unexpected(std::string("Erro SQL: ") + PQresultErrorMessage(res.get()));
    }
    return res;
}

// Constrói geometria SQL parametrizada ($1..$N) a partir da referência territorial validada.
// Retorna par (expressão_sql_geom_4326, params) ou erro.
struct GeomQuery {
    std::string geom_expr;
    std::vector<std::string> params;
    std::string support_note;
};

std::expected<GeomQuery, std::string> build_reference_geom_query(
    PGconn* conn,
    const nlohmann::json& req,
    const domain::SecurityLimits& limits
) {
    if (!req.is_object()) {
        return std::unexpected("Requisicao deve ser objeto JSON");
    }
    const std::string kind = req.value("kind", "");
    const std::string crs = req.value("crs", "EPSG:4326");
    if (auto c = policy::validate_crs(crs); !c) {
        return std::unexpected(c.error());
    }
    if (!req.contains("value")) {
        return std::unexpected("Campo value obrigatorio");
    }

    if (kind == "MUNICIPALITY") {
        if (!req["value"].is_string()) {
            return std::unexpected("value para MUNICIPALITY deve ser string (codigo IBGE 7 digitos)");
        }
        const std::string code = req["value"].get<std::string>();
        if (auto v = policy::validate_municipality_code(code); !v) {
            return std::unexpected(v.error());
        }
        return GeomQuery{
            "(SELECT ST_Union(geom) FROM territorial_unit tu JOIN dataset_version dv ON dv.version_id = tu.version_id WHERE dv.is_active = TRUE AND tu.municipality_code = $1)",
            {code},
            "Uniao dos setores censitarios ativos do municipio IBGE " + code
        };
    }

    if (kind == "POINT") {
        if (!req["value"].is_array() || req["value"].size() < 2 ||
            !req["value"][0].is_number() || !req["value"][1].is_number()) {
            return std::unexpected("value para POINT deve ser [lon, lat]");
        }
        const double lon = req["value"][0].get<double>();
        const double lat = req["value"][1].get<double>();
        if (auto v = policy::validate_wgs84_point(lon, lat); !v) {
            return std::unexpected(v.error());
        }
        const double radius_m = req.value("radius_m", 0.0);
        if (radius_m < 0.0 || radius_m > limits.max_radius_m) {
            return std::unexpected("radius_m fora dos limites permitidos");
        }
        if (radius_m > 0.0) {
            return GeomQuery{
                "ST_Buffer(ST_SetSRID(ST_MakePoint($1::double precision, $2::double precision), 4326)::geography, $3::double precision)::geometry",
                {std::to_string(lon), std::to_string(lat), std::to_string(radius_m)},
                "Buffer geodesico em torno de coordenada efemera de referencia"
            };
        }
        return GeomQuery{
            "ST_SetSRID(ST_MakePoint($1::double precision, $2::double precision), 4326)",
            {std::to_string(lon), std::to_string(lat)},
            "Coordenada efemera de referencia territorial"
        };
    }

    if (kind == "POLYGON") {
        const auto& geom = req["value"];
        if (auto cnt = policy::count_geojson_vertices(geom, limits.max_polygon_vertices); !cnt) {
            return std::unexpected(cnt.error());
        }
        const std::string geom_str = geom.dump();
        auto chk = exec_params(
            conn,
            "SELECT ST_IsValid(g), ST_IsValidReason(g), ST_Area(g::geography) / 1000000.0 "
            "FROM (SELECT ST_SetSRID(ST_GeomFromGeoJSON($1), 4326) AS g) s",
            {geom_str}
        );
        if (!chk) return std::unexpected(chk.error());
        if (PQntuples(chk->get()) == 0) {
            return std::unexpected("Falha ao validar geometria GeoJSON");
        }
        const std::string is_valid = PQgetvalue(chk->get(), 0, 0);
        if (is_valid != "t") {
            const std::string reason = PQgetvalue(chk->get(), 0, 1);
            return std::unexpected("Geometria invalida (nao reparada silenciosamente): " + reason);
        }
        const double area_km2 = std::strtod(PQgetvalue(chk->get(), 0, 2), nullptr);
        if (area_km2 <= 0.0 || area_km2 > limits.max_query_area_km2) {
            return std::unexpected("Area do poligono fora dos limites permitidos");
        }
        return GeomQuery{
            "ST_SetSRID(ST_GeomFromGeoJSON($1), 4326)",
            {geom_str},
            "Poligono explicito fornecido na consulta efemera"
        };
    }

    return std::unexpected("kind nao suportado para operacao espacial: " + kind);
}

std::string situation_label_from_code(const std::string& code, const std::string& fallback) {
    if (!fallback.empty()) return fallback;
    if (code == "1") return "Area urbana de alta densidade";
    if (code == "2") return "Area urbana de baixa densidade";
    if (code == "3") return "Nucleo urbano";
    if (code == "5") return "Aglomerado rural";
    if (code == "6") return "Zona rural";
    if (code == "7") return "Aglomerado subnormal / nucleo";
    if (code == "8") return "Area rural (exclusive aglomerados)";
    return "Situacao " + code;
}

} // namespace

PostgisStore::PostgisStore(std::string conninfo) : conninfo_(std::move(conninfo)) {}

bool PostgisStore::ping() const {
    auto conn = connect_pg(conninfo_);
    if (!conn) return false;
    auto res = exec_params(conn->get(), "SELECT 1", {});
    return res.has_value();
}

std::expected<nlohmann::json, std::string> PostgisStore::get_catalog() const {
    auto conn = connect_pg(conninfo_);
    if (!conn) return std::unexpected(conn.error());

    auto res = exec_params(
        conn->get(),
        "SELECT s.source_id, s.title, s.provider, s.category, s.approval_status, s.license, "
        "s.scope, s.spatial_level, COALESCE(to_char(s.reference_date, 'YYYY-MM-DD'), ''), "
        "s.native_crs, s.native_resolution, s.limitations, "
        "COALESCE(dv.version_id, ''), COALESCE(dv.dataset_version, ''), COALESCE(dv.artifact_sha256, ''), "
        "COALESCE(to_char(dv.ingested_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'), ''), "
        "COALESCE(dv.feature_count, 0), COALESCE(dv.is_active, FALSE) "
        "FROM source s "
        "LEFT JOIN dataset_version dv ON dv.source_id = s.source_id "
        "ORDER BY s.source_id, dv.acquired_at DESC",
        {}
    );
    if (!res) return std::unexpected(res.error());

    nlohmann::json sources = nlohmann::json::array();
    const int n = PQntuples(res->get());
    for (int i = 0; i < n; ++i) {
        sources.push_back({
            {"source_id", PQgetvalue(res->get(), i, 0)},
            {"title", PQgetvalue(res->get(), i, 1)},
            {"provider", PQgetvalue(res->get(), i, 2)},
            {"category", PQgetvalue(res->get(), i, 3)},
            {"approval_status", PQgetvalue(res->get(), i, 4)},
            {"license", PQgetvalue(res->get(), i, 5)},
            {"scope", PQgetvalue(res->get(), i, 6)},
            {"spatial_level", PQgetvalue(res->get(), i, 7)},
            {"reference_date", std::string(PQgetvalue(res->get(), i, 8)).empty() ? nlohmann::json(nullptr) : nlohmann::json(PQgetvalue(res->get(), i, 8))},
            {"native_crs", PQgetvalue(res->get(), i, 9)},
            {"native_resolution", PQgetvalue(res->get(), i, 10)},
            {"limitations", PQgetvalue(res->get(), i, 11)},
            {"version_id", std::string(PQgetvalue(res->get(), i, 12)).empty() ? nlohmann::json(nullptr) : nlohmann::json(PQgetvalue(res->get(), i, 12))},
            {"dataset_version", std::string(PQgetvalue(res->get(), i, 13)).empty() ? nlohmann::json(nullptr) : nlohmann::json(PQgetvalue(res->get(), i, 13))},
            {"artifact_sha256", std::string(PQgetvalue(res->get(), i, 14)).empty() ? nlohmann::json(nullptr) : nlohmann::json(PQgetvalue(res->get(), i, 14))},
            {"ingested_at", std::string(PQgetvalue(res->get(), i, 15)).empty() ? nlohmann::json(nullptr) : nlohmann::json(PQgetvalue(res->get(), i, 15))},
            {"feature_count", std::strtoll(PQgetvalue(res->get(), i, 16), nullptr, 10)},
            {"is_active", std::string(PQgetvalue(res->get(), i, 17)) == "t"}
        });
    }

    return nlohmann::json{
        {"policy", "CATEGORY_A_ONLY"},
        {"sources", sources}
    };
}

std::expected<nlohmann::json, std::string>
PostgisStore::resolve_reference(const nlohmann::json& req, const domain::SecurityLimits& limits) const {
    if (!req.is_object()) {
        return std::unexpected("Payload deve ser objeto JSON");
    }
    const std::string kind = req.value("kind", "");

    if (kind == "CEP") {
        if (!req.contains("value") || !req["value"].is_string()) {
            return std::unexpected("value para CEP deve ser string");
        }
        const std::string cep_raw = req["value"].get<std::string>();
        if (auto v = policy::validate_cep_syntax(cep_raw); !v) {
            return std::unexpected(v.error());
        }
        std::string cep_digits;
        for (char c : cep_raw) {
            if (std::isdigit(static_cast<unsigned char>(c))) cep_digits.push_back(c);
        }

        auto conn = connect_pg(conninfo_);
        if (!conn) return std::unexpected(conn.error());

        auto res = exec_params(
            conn->get(),
            "SELECT pr.cep_code, pr.resolution_level, pr.unit_code, pr.municipality_code, pr.support_note, "
            "dv.dataset_version, s.source_id "
            "FROM postal_reference pr "
            "JOIN dataset_version dv ON dv.version_id = pr.version_id "
            "JOIN source s ON s.source_id = dv.source_id "
            "WHERE dv.is_active = TRUE AND s.approval_status = 'APPROVED' AND pr.cep_code = $1",
            {cep_digits}
        );
        if (!res) return std::unexpected(res.error());

        if (PQntuples(res->get()) == 0) {
            return nlohmann::json{
                {"status", "UNAVAILABLE"},
                {"kind", "CEP"},
                {"spatial_level", "UNAVAILABLE"},
                {"method", "local_approved_postal_reference_lookup"},
                {"support_geometry", nullptr},
                {"analysis_geometry", nullptr},
                {"provenance", {
                    {"source_id", "cep_referencia_territorial"},
                    {"policy", "CATEGORY_A_ONLY"}
                }},
                {"reasons", nlohmann::json::array({
                    "Nenhuma fonte CEP Categoria A aprovada e licenciada encontrada para este codigo.",
                    "O SITUS nao envia CEP a servicos externos e nao converte prefixo postal em coordenada ficticia."
                })}
            };
        }

        nlohmann::json matches = nlohmann::json::array();
        for (int i = 0; i < PQntuples(res->get()); ++i) {
            matches.push_back({
                {"resolution_level", PQgetvalue(res->get(), i, 1)},
                {"unit_code", PQgetvalue(res->get(), i, 2)},
                {"municipality_code", PQgetvalue(res->get(), i, 3)},
                {"support_note", PQgetvalue(res->get(), i, 4)},
                {"dataset_version", PQgetvalue(res->get(), i, 5)},
                {"source_id", PQgetvalue(res->get(), i, 6)}
            });
        }
        return nlohmann::json{
            {"status", PQntuples(res->get()) > 1 ? "AMBIGUOUS" : "RESOLVED"},
            {"kind", "CEP"},
            {"matches", matches},
            {"reasons", nlohmann::json::array({"Resolvido via tabela local aprovada de referencia postal"})}
        };
    }

    auto conn = connect_pg(conninfo_);
    if (!conn) return std::unexpected(conn.error());

    auto gq = build_reference_geom_query(conn->get(), req, limits);
    if (!gq) return std::unexpected(gq.error());

    if (kind == "POINT" && req.value("radius_m", 0.0) == 0.0) {
        const std::string sql =
            "SELECT tu.unit_code, tu.municipality_code, tu.municipality_name, tu.situation_code, "
            "tu.area_m2, dv.dataset_version, ST_AsGeoJSON(tu.geom) "
            "FROM territorial_unit tu "
            "JOIN dataset_version dv ON dv.version_id = tu.version_id "
            "WHERE dv.is_active = TRUE AND ST_Covers(tu.geom, " + gq->geom_expr + ")";

        auto res = exec_params(conn->get(), sql.c_str(), gq->params);
        if (!res) return std::unexpected(res.error());
        const int n = PQntuples(res->get());
        if (n == 0) {
            return nlohmann::json{
                {"status", "OUTSIDE_COVERAGE"},
                {"kind", "POINT"},
                {"spatial_level", "SETOR_CENSITARIO"},
                {"method", "ST_Covers_WGS84"},
                {"support_geometry", nullptr},
                {"analysis_geometry", nullptr},
                {"reasons", nlohmann::json::array({"Coordenada fora da cobertura dos setores censitarios carregados"})}
            };
        }

        nlohmann::json units = nlohmann::json::array();
        for (int i = 0; i < n; ++i) {
            units.push_back({
                {"unit_code", PQgetvalue(res->get(), i, 0)},
                {"municipality_code", PQgetvalue(res->get(), i, 1)},
                {"municipality_name", PQgetvalue(res->get(), i, 2)},
                {"situation_code", PQgetvalue(res->get(), i, 3)},
                {"area_m2", std::strtod(PQgetvalue(res->get(), i, 4), nullptr)},
                {"dataset_version", PQgetvalue(res->get(), i, 5)}
            });
        }
        return nlohmann::json{
            {"status", n > 1 ? "AMBIGUOUS" : "RESOLVED"},
            {"kind", "POINT"},
            {"spatial_level", "SETOR_CENSITARIO"},
            {"method", "ST_Covers_WGS84"},
            {"units", units},
            {"support_geometry", nlohmann::json::parse(PQgetvalue(res->get(), 0, 6))},
            {"analysis_geometry", nullptr},
            {"reasons", nlohmann::json::array({n > 1 ? "Ponto sobre fronteira de setores (todas as unidades retornadas)" : gq->support_note})}
        };
    }

    const std::string sql =
        "WITH ref AS (SELECT " + gq->geom_expr + " AS g) "
        "SELECT CASE WHEN g IS NULL THEN 0 ELSE 1 END, "
        "COALESCE(ST_Area(g::geography), 0.0), "
        "COALESCE(ST_AsGeoJSON(ST_SimplifyPreserveTopology(g, 0.0005)), 'null'), "
        "COALESCE(ST_AsGeoJSON(g), 'null') "
        "FROM ref";

    auto res = exec_params(conn->get(), sql.c_str(), gq->params);
    if (!res) return std::unexpected(res.error());

    const bool has_geom = std::string(PQgetvalue(res->get(), 0, 0)) == "1";
    if (!has_geom) {
        return nlohmann::json{
            {"status", "OUTSIDE_COVERAGE"},
            {"kind", kind},
            {"spatial_level", kind == "MUNICIPALITY" ? "MUNICIPIO" : "AREA"},
            {"support_geometry", nullptr},
            {"analysis_geometry", nullptr},
            {"reasons", nlohmann::json::array({"Referencia nao encontrada na cobertura territorial ativa"})}
        };
    }

    const double area_m2 = std::strtod(PQgetvalue(res->get(), 0, 1), nullptr);
    return nlohmann::json{
        {"status", "RESOLVED"},
        {"kind", kind},
        {"spatial_level", kind == "MUNICIPALITY" ? "MUNICIPIO" : "AREA_POLIGONAL"},
        {"method", "postgis_geography_wgs84"},
        {"area_m2", area_m2},
        {"support_geometry", nlohmann::json::parse(PQgetvalue(res->get(), 0, 2))},
        {"analysis_geometry", nlohmann::json::parse(PQgetvalue(res->get(), 0, 3))},
        {"reasons", nlohmann::json::array({gq->support_note})}
    };
}

std::expected<nlohmann::json, std::string>
PostgisStore::intersect_territories(const nlohmann::json& req, const domain::SecurityLimits& limits) const {
    auto conn = connect_pg(conninfo_);
    if (!conn) return std::unexpected(conn.error());

    auto gq = build_reference_geom_query(conn->get(), req, limits);
    if (!gq) return std::unexpected(gq.error());

    const std::string sql =
        "WITH ref AS (SELECT " + gq->geom_expr + " AS g) "
        "SELECT tu.unit_code, tu.municipality_code, tu.municipality_name, "
        "COALESCE(tu.situation_code, 'N/A'), COALESCE(tu.situation_label, ''), "
        "COALESCE(tu.district_name, ''), COALESCE(tu.neighborhood_name, ''), "
        "tu.area_m2 AS unit_total_area_m2, "
        "ST_Area(ST_Intersection(tu.geom, ref.g)::geography) AS intersected_area_m2, "
        "ST_Area(ref.g::geography) AS reference_area_m2, "
        "dv.dataset_version "
        "FROM ref "
        "JOIN territorial_unit tu ON ST_Intersects(tu.geom, ref.g) "
        "JOIN dataset_version dv ON dv.version_id = tu.version_id "
        "WHERE dv.is_active = TRUE AND ref.g IS NOT NULL "
        "ORDER BY intersected_area_m2 DESC, tu.unit_code ASC";

    auto res = exec_params(conn->get(), sql.c_str(), gq->params);
    if (!res) return std::unexpected(res.error());

    const int n = PQntuples(res->get());
    nlohmann::json units = nlohmann::json::array();
    double reference_area_m2 = 0.0;
    double total_intersected_m2 = 0.0;
    std::set<std::string> versions;

    for (int i = 0; i < n; ++i) {
        const double unit_area = std::strtod(PQgetvalue(res->get(), i, 7), nullptr);
        const double inter_area = std::strtod(PQgetvalue(res->get(), i, 8), nullptr);
        reference_area_m2 = std::strtod(PQgetvalue(res->get(), i, 9), nullptr);
        const std::string dv = PQgetvalue(res->get(), i, 10);
        versions.insert(dv);
        total_intersected_m2 += inter_area;

        const std::string sit_code_raw = PQgetvalue(res->get(), i, 3);
        const std::string sit_code = sit_code_raw.empty() ? "N/A" : sit_code_raw;
        const std::string sit_lbl = sit_code_raw.empty()
            ? "Sem classificacao de situacao (ex.: massa d'agua)"
            : situation_label_from_code(sit_code, PQgetvalue(res->get(), i, 4));

        units.push_back({
            {"unit_code", PQgetvalue(res->get(), i, 0)},
            {"municipality_code", PQgetvalue(res->get(), i, 1)},
            {"municipality_name", PQgetvalue(res->get(), i, 2)},
            {"situation_code", sit_code},
            {"situation_label", sit_lbl},
            {"district_name", PQgetvalue(res->get(), i, 5)},
            {"neighborhood_name", PQgetvalue(res->get(), i, 6)},
            {"unit_total_area_m2", unit_area},
            {"intersected_area_m2", inter_area},
            {"share_of_unit", unit_area > 0.0 ? std::min(1.0, inter_area / unit_area) : 0.0},
            {"dataset_version", dv}
        });
    }

    nlohmann::json dv_arr = nlohmann::json::array();
    for (const auto& v : versions) dv_arr.push_back(v);

    return nlohmann::json{
        {"status", n > 0 ? "AVAILABLE" : "NO_DATA"},
        {"method", "postgis_st_intersection_geography_wgs84"},
        {"reference_area_m2", reference_area_m2},
        {"total_intersected_area_m2", total_intersected_m2},
        {"coverage_fraction", reference_area_m2 > 0.0 ? std::min(1.0, total_intersected_m2 / reference_area_m2) : 0.0},
        {"unit_count", n},
        {"dataset_versions", dv_arr},
        {"units", units}
    };
}

std::expected<nlohmann::json, std::string>
PostgisStore::build_synthesis_inputs(const nlohmann::json& req, const domain::SecurityLimits& limits) const {
    auto inter = intersect_territories(req, limits);
    if (!inter) return std::unexpected(inter.error());

    if (inter->value("unit_count", 0) == 0) {
        return std::unexpected("Nenhuma unidade territorial intersectada para sintetizar");
    }

    nlohmann::json slices = nlohmann::json::array();
    for (const auto& u : (*inter)["units"]) {
        slices.push_back({
            {"unit_code", u["unit_code"]},
            {"group_key", u["situation_code"]},
            {"group_label", u["situation_label"]},
            {"intersected_area_m2", u["intersected_area_m2"]},
            {"unit_total_area_m2", u["unit_total_area_m2"]}
        });
    }

    return nlohmann::json{
        {"operation_id", "territorial_area_synthesis"},
        {"contract_version", "0.2.0"},
        {"completeness", "COMPLETE"},
        {"inputs", {
            {"reference_area_m2", (*inter)["reference_area_m2"]},
            {"overlap_rule", "DISJOINT_SECTORS"},
            {"dataset_versions", (*inter)["dataset_versions"]},
            {"slices", slices}
        }}
    };
}

std::expected<nlohmann::json, std::string>
PostgisStore::get_map_features(const nlohmann::json& req, const domain::SecurityLimits& limits) const {
    auto conn = connect_pg(conninfo_);
    if (!conn) return std::unexpected(conn.error());

    auto gq = build_reference_geom_query(conn->get(), req, limits);
    if (!gq) return std::unexpected(gq.error());

    const std::string sql =
        "WITH ref AS (SELECT " + gq->geom_expr + " AS g) "
        "SELECT tu.unit_code, tu.municipality_code, tu.municipality_name, "
        "COALESCE(tu.situation_code, ''), COALESCE(tu.situation_label, ''), "
        "tu.area_m2, ST_AsGeoJSON(ST_SimplifyPreserveTopology(tu.geom, 0.00025)) "
        "FROM ref "
        "JOIN territorial_unit tu ON ST_Intersects(tu.geom, ref.g) "
        "JOIN dataset_version dv ON dv.version_id = tu.version_id "
        "WHERE dv.is_active = TRUE AND ref.g IS NOT NULL "
        "ORDER BY tu.unit_code ASC "
        "LIMIT " + std::to_string(limits.max_map_features);

    auto res = exec_params(conn->get(), sql.c_str(), gq->params);
    if (!res) return std::unexpected(res.error());

    nlohmann::json features = nlohmann::json::array();
    const int n = PQntuples(res->get());
    for (int i = 0; i < n; ++i) {
        const std::string geom_str = PQgetvalue(res->get(), i, 6);
        if (geom_str.empty() || geom_str == "null") continue;
        const std::string sit_code = PQgetvalue(res->get(), i, 3);
        features.push_back({
            {"type", "Feature"},
            {"geometry", nlohmann::json::parse(geom_str)},
            {"properties", {
                {"unit_code", PQgetvalue(res->get(), i, 0)},
                {"municipality_code", PQgetvalue(res->get(), i, 1)},
                {"municipality_name", PQgetvalue(res->get(), i, 2)},
                {"situation_code", sit_code},
                {"situation_label", situation_label_from_code(sit_code, PQgetvalue(res->get(), i, 4))},
                {"area_m2", std::strtod(PQgetvalue(res->get(), i, 5), nullptr)},
                {"visualization_only", true}
            }}
        });
    }

    return nlohmann::json{
        {"type", "FeatureCollection"},
        {"features", features},
        {"truncated", static_cast<std::size_t>(n) >= limits.max_map_features}
    };
}

std::expected<IngestReport, std::string>
PostgisStore::ingest_gpkg(
    const std::string& gpkg_path,
    const policy::SourceMetadata& source_meta,
    const std::string& dataset_version,
    const std::string& sha256_hex
) const {
    GDALAllRegister();

    GDALDataset* ds = static_cast<GDALDataset*>(
        GDALOpenEx(gpkg_path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr)
    );
    if (!ds) {
        return std::unexpected("Falha ao abrir GeoPackage via GDAL: " + gpkg_path);
    }
    struct DsGuard {
        GDALDataset* ptr;
        ~DsGuard() { if (ptr) GDALClose(ptr); }
    } ds_guard{ds};

    OGRLayer* layer = ds->GetLayer(0);
    if (!layer) {
        return std::unexpected("GeoPackage sem camada vetorial");
    }

    OGRSpatialReference dst_srs;
    dst_srs.SetWellKnownGeogCS("WGS84");
    dst_srs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    const OGRSpatialReference* src_srs = layer->GetSpatialRef();
    std::unique_ptr<OGRCoordinateTransformation> ct;
    if (src_srs) {
        OGRSpatialReference src_copy(*src_srs);
        src_copy.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        if (!src_copy.IsSame(&dst_srs)) {
            ct.reset(OGRCreateCoordinateTransformation(&src_copy, &dst_srs));
        }
    }

    auto conn = connect_pg(conninfo_);
    if (!conn) return std::unexpected(conn.error());

    const std::string version_id = source_meta.source_id + ":" + dataset_version;

    // Verificar idempotência por sha256 e version_id
    {
        auto chk = exec_params(
            conn->get(),
            "SELECT artifact_sha256, feature_count FROM dataset_version WHERE version_id = $1",
            {version_id}
        );
        if (!chk) return std::unexpected(chk.error());
        if (PQntuples(chk->get()) > 0) {
            const std::string existing_sha = PQgetvalue(chk->get(), 0, 0);
            const std::size_t existing_cnt = static_cast<std::size_t>(std::strtoll(PQgetvalue(chk->get(), 0, 1), nullptr, 10));
            if (existing_sha == sha256_hex && existing_cnt > 0) {
                IngestReport rep;
                rep.source_id = source_meta.source_id;
                rep.version_id = version_id;
                rep.dataset_version = dataset_version;
                rep.sha256 = sha256_hex;
                rep.inserted_features = existing_cnt;
                rep.valid_features = existing_cnt;
                rep.read_features = existing_cnt;
                rep.already_ingested = true;
                return rep;
            }
        }
    }

    if (auto b = exec_params(conn->get(), "BEGIN", {}); !b) return std::unexpected(b.error());

    auto rollback_with = [&](const std::string& err) -> std::unexpected<std::string> {
        static_cast<void>(exec_params(conn->get(), "ROLLBACK", {}));
        return std::unexpected(err);
    };

    // Registrar fonte Categoria A
    nlohmann::json allowed_json = source_meta.allowed_fields;
    auto up_src = exec_params(
        conn->get(),
        "INSERT INTO source (source_id, title, provider, category, approval_status, license, source_url, catalog_url, "
        "scope, spatial_level, reference_date, native_crs, native_resolution, allowed_fields, limitations) "
        "VALUES ($1, $2, $3, 'A', 'APPROVED', $4, $5, $6, $7, $8, NULLIF($9, '')::date, $10, $11, $12::jsonb, $13) "
        "ON CONFLICT (source_id) DO UPDATE SET "
        "title = EXCLUDED.title, approval_status = 'APPROVED', allowed_fields = EXCLUDED.allowed_fields",
        {
            source_meta.source_id, source_meta.title, source_meta.provider,
            source_meta.license, source_meta.source_url, source_meta.catalog_url,
            source_meta.scope, source_meta.spatial_level, source_meta.reference_date,
            source_meta.native_crs, source_meta.native_resolution, allowed_json.dump(),
            source_meta.limitations
        }
    );
    if (!up_src) return rollback_with(up_src.error());

    // Desativar versões anteriores da mesma fonte sem apagá-las
    auto deact = exec_params(
        conn->get(),
        "UPDATE dataset_version SET is_active = FALSE WHERE source_id = $1",
        {source_meta.source_id}
    );
    if (!deact) return rollback_with(deact.error());

    auto up_ver = exec_params(
        conn->get(),
        "INSERT INTO dataset_version (version_id, source_id, dataset_version, artifact_sha256, is_active) "
        "VALUES ($1, $2, $3, $4, TRUE) "
        "ON CONFLICT (version_id) DO UPDATE SET artifact_sha256 = EXCLUDED.artifact_sha256, is_active = TRUE",
        {version_id, source_meta.source_id, dataset_version, sha256_hex}
    );
    if (!up_ver) return rollback_with(up_ver.error());

    const std::set<std::string> allowed_set(source_meta.allowed_fields.begin(), source_meta.allowed_fields.end());

    // Preparar statement para inserção rápida
    PGresult* prep = PQprepare(
        conn->get(),
        "ins_tu",
        "INSERT INTO territorial_unit "
        "(version_id, unit_code, unit_level, municipality_code, municipality_name, uf_code, "
        "situation_code, situation_label, district_name, neighborhood_name, attributes, geom, area_m2) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11::jsonb, "
        "ST_Multi(ST_SetSRID(ST_GeomFromWKB(decode($12, 'hex')), 4326)), "
        "ST_Area(ST_SetSRID(ST_GeomFromWKB(decode($12, 'hex')), 4326)::geography)) "
        "ON CONFLICT (version_id, unit_code) DO NOTHING",
        12,
        nullptr
    );
    PgResPtr prep_guard(prep);
    if (!prep || PQresultStatus(prep) != PGRES_COMMAND_OK) {
        return rollback_with(std::string("Erro PQprepare: ") + PQerrorMessage(conn->get()));
    }

    IngestReport report;
    report.source_id = source_meta.source_id;
    report.version_id = version_id;
    report.dataset_version = dataset_version;
    report.sha256 = sha256_hex;

    layer->ResetReading();
    OGRFeature* feat_raw = nullptr;
    while ((feat_raw = layer->GetNextFeature()) != nullptr) {
        struct FeatGuard {
            OGRFeature* f;
            ~FeatGuard() { OGRFeature::DestroyFeature(f); }
        } fg{feat_raw};

        report.read_features++;
        OGRGeometry* geom = feat_raw->GetGeometryRef();
        if (!geom || geom->IsEmpty()) {
            report.skipped_invalid++;
            continue;
        }

        std::unique_ptr<OGRGeometry> gclone(geom->clone());
        if (ct && gclone->transform(ct.get()) != OGRERR_NONE) {
            report.skipped_invalid++;
            continue;
        }
        if (!gclone->IsValid()) {
            report.skipped_invalid++;
            continue;
        }
        report.valid_features++;

        // Extrair somente campos permitidos
        nlohmann::json raw_attrs = nlohmann::json::object();
        const int fcount = feat_raw->GetFieldCount();
        for (int i = 0; i < fcount; ++i) {
            const char* fname = feat_raw->GetFieldDefnRef(i)->GetNameRef();
            if (!fname || !allowed_set.contains(fname)) continue;
            if (!feat_raw->IsFieldSetAndNotNull(i)) continue;
            const auto ftype = feat_raw->GetFieldDefnRef(i)->GetType();
            if (ftype == OFTInteger || ftype == OFTInteger64) {
                raw_attrs[fname] = feat_raw->GetFieldAsInteger64(i);
            } else if (ftype == OFTReal) {
                raw_attrs[fname] = feat_raw->GetFieldAsDouble(i);
            } else {
                raw_attrs[fname] = feat_raw->GetFieldAsString(i);
            }
        }

        const std::string unit_code = raw_attrs.contains("CD_SETOR") ? raw_attrs["CD_SETOR"].dump() : "";
        std::string unit_code_clean;
        if (raw_attrs.contains("CD_SETOR") && raw_attrs["CD_SETOR"].is_string()) {
            unit_code_clean = raw_attrs["CD_SETOR"].get<std::string>();
        } else if (!unit_code.empty()) {
            unit_code_clean = unit_code;
        } else {
            unit_code_clean = std::to_string(feat_raw->GetFID());
        }

        const std::string mun_code = raw_attrs.value("CD_MUN", "");
        const std::string mun_name = raw_attrs.value("NM_MUN", "");
        const std::string uf_code = raw_attrs.value("CD_UF", "");
        const std::string sit_code = raw_attrs.value("CD_SIT", "");
        const std::string sit_label = raw_attrs.value("SITUACAO", "");
        const std::string dist_name = raw_attrs.value("NM_DIST", "");
        const std::string neigh_name = raw_attrs.value("NM_BAIRRO", "");

        // Serializar WKB em hex
        const int wkb_size = static_cast<int>(gclone->WkbSize());
        std::vector<unsigned char> wkb_buf(static_cast<std::size_t>(wkb_size));
        gclone->exportToWkb(wkbNDR, wkb_buf.data(), wkbVariantIso);

        static constexpr char kHex[] = "0123456789abcdef";
        std::string wkb_hex;
        wkb_hex.reserve(wkb_buf.size() * 2);
        for (unsigned char b : wkb_buf) {
            wkb_hex.push_back(kHex[(b >> 4) & 0xF]);
            wkb_hex.push_back(kHex[b & 0xF]);
        }

        const std::string attrs_str = raw_attrs.dump();
        const std::array<const char*, 12> pvals = {
            version_id.c_str(),
            unit_code_clean.c_str(),
            source_meta.spatial_level.c_str(),
            mun_code.c_str(),
            mun_name.c_str(),
            uf_code.c_str(),
            sit_code.c_str(),
            sit_label.c_str(),
            dist_name.c_str(),
            neigh_name.c_str(),
            attrs_str.c_str(),
            wkb_hex.c_str()
        };

        PgResPtr ins(PQexecPrepared(conn->get(), "ins_tu", 12, pvals.data(), nullptr, nullptr, 0));
        if (!ins || PQresultStatus(ins.get()) != PGRES_COMMAND_OK) {
            return rollback_with(std::string("Erro ao inserir setor ") + unit_code_clean + ": " + PQerrorMessage(conn->get()));
        }
        const char* cmd_tuples = PQcmdTuples(ins.get());
        if (cmd_tuples && std::strtoll(cmd_tuples, nullptr, 10) > 0) {
            report.inserted_features++;
        }
    }

    auto upd_ver = exec_params(
        conn->get(),
        "UPDATE dataset_version SET ingested_at = now(), feature_count = "
        "(SELECT COUNT(*) FROM territorial_unit WHERE version_id = $1) "
        "WHERE version_id = $1",
        {version_id}
    );
    if (!upd_ver) return rollback_with(upd_ver.error());

    auto up_layer = exec_params(
        conn->get(),
        "INSERT INTO layer (layer_id, source_id, version_id, title, geometry_kind, storage_srid, is_available) "
        "VALUES ($1, $2, $3, $4, 'MULTIPOLYGON', 4326, TRUE) "
        "ON CONFLICT (layer_id) DO UPDATE SET version_id = EXCLUDED.version_id, is_available = TRUE",
        {source_meta.source_id + "_layer", source_meta.source_id, version_id, source_meta.title}
    );
    if (!up_layer) return rollback_with(up_layer.error());

    if (auto c = exec_params(conn->get(), "COMMIT", {}); !c) {
        return rollback_with(c.error());
    }

    return report;
}

} // namespace situs::spatial
