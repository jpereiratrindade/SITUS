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

struct DistrictMatch {
    std::string district_code;
    std::string district_name;
    std::string municipality_code;
    std::string municipality_name;
    int sector_count = 0;
    double area_m2 = 0.0;
};

std::expected<std::vector<DistrictMatch>, std::string> lookup_districts(
    PGconn* conn,
    const nlohmann::json& req
) {
    if (!req.contains("value") || !req["value"].is_string()) {
        return std::unexpected("value para DISTRICT deve ser string (nome do distrito ou codigo IBGE de 9 digitos)");
    }
    const std::string raw = req["value"].get<std::string>();
    if (raw.empty()) {
        return std::unexpected("Informe o nome do distrito ou codigo IBGE de 9 digitos");
    }
    const std::string mun_code = req.value("municipality_code", "");
    if (!mun_code.empty()) {
        if (auto v = policy::validate_municipality_code(mun_code); !v) {
            return std::unexpected(v.error());
        }
    }
    const bool rural_only = req.value("rural_only", false);
    const std::string rural_filter = rural_only ? " AND tu.situation_label = 'Rural'" : "";

    auto res = [&]() {
        if (policy::validate_district_code(raw).has_value()) {
            const std::string sql =
                "SELECT tu.attributes->>'CD_DIST', tu.district_name, tu.municipality_code, tu.municipality_name, "
                "COUNT(*)::int, COALESCE(SUM(tu.area_m2), 0.0) "
                "FROM territorial_unit tu "
                "JOIN dataset_version dv ON dv.version_id = tu.version_id "
                "WHERE dv.is_active = TRUE AND tu.attributes->>'CD_DIST' = $1" + rural_filter + " "
                "GROUP BY tu.attributes->>'CD_DIST', tu.district_name, tu.municipality_code, tu.municipality_name";
            return exec_params(conn, sql.c_str(), {raw});
        }
        if (!mun_code.empty()) {
            const std::string sql =
                "SELECT tu.attributes->>'CD_DIST', tu.district_name, tu.municipality_code, tu.municipality_name, "
                "COUNT(*)::int, COALESCE(SUM(tu.area_m2), 0.0) "
                "FROM territorial_unit tu "
                "JOIN dataset_version dv ON dv.version_id = tu.version_id "
                "WHERE dv.is_active = TRUE AND tu.municipality_code = $1 "
                "AND LOWER(tu.district_name) = LOWER($2)" + rural_filter + " "
                "GROUP BY tu.attributes->>'CD_DIST', tu.district_name, tu.municipality_code, tu.municipality_name "
                "ORDER BY tu.district_name ASC";
            return exec_params(conn, sql.c_str(), {mun_code, raw});
        }
        const std::string sql =
            "SELECT tu.attributes->>'CD_DIST', tu.district_name, tu.municipality_code, tu.municipality_name, "
            "COUNT(*)::int, COALESCE(SUM(tu.area_m2), 0.0) "
            "FROM territorial_unit tu "
            "JOIN dataset_version dv ON dv.version_id = tu.version_id "
            "WHERE dv.is_active = TRUE AND LOWER(tu.district_name) = LOWER($1)" + rural_filter + " "
            "GROUP BY tu.attributes->>'CD_DIST', tu.district_name, tu.municipality_code, tu.municipality_name "
            "ORDER BY tu.municipality_name ASC, tu.district_name ASC "
            "LIMIT 20";
        return exec_params(conn, sql.c_str(), {raw});
    }();
    if (!res) return std::unexpected(res.error());

    std::vector<DistrictMatch> out;
    const int n = PQntuples(res->get());
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        out.push_back(DistrictMatch{
            PQgetvalue(res->get(), i, 0),
            PQgetvalue(res->get(), i, 1),
            PQgetvalue(res->get(), i, 2),
            PQgetvalue(res->get(), i, 3),
            std::atoi(PQgetvalue(res->get(), i, 4)),
            std::strtod(PQgetvalue(res->get(), i, 5), nullptr)
        });
    }
    return out;
}

struct GridSpecResolved {
    int cell_size_m = 1000;
    long long ix = 0;
    long long iy = 0;
    std::string cell_id;
    bool from_point = false;
    double lon = 0.0;
    double lat = 0.0;
};

std::expected<GridSpecResolved, std::string> resolve_grid_spec(
    PGconn* conn,
    const nlohmann::json& req
) {
    if (!req.contains("value")) {
        return std::unexpected("Campo value obrigatorio para GRID_CELL ([lon, lat] ou GRID5880_<S>_<ix>_<iy>)");
    }
    if (req["value"].is_string()) {
        auto parsed = policy::parse_grid_cell_id(req["value"].get<std::string>());
        if (!parsed) return std::unexpected(parsed.error());
        return GridSpecResolved{
            parsed->cell_size_m,
            parsed->ix,
            parsed->iy,
            req["value"].get<std::string>(),
            false, 0.0, 0.0
        };
    }
    if (req["value"].is_array() && req["value"].size() >= 2 &&
        req["value"][0].is_number() && req["value"][1].is_number()) {
        const double lon = req["value"][0].get<double>();
        const double lat = req["value"][1].get<double>();
        if (auto v = policy::validate_wgs84_point(lon, lat); !v) {
            return std::unexpected(v.error());
        }
        const int cell_size_m = req.value("cell_size_m", 1000);
        if (auto v = policy::validate_grid_cell_size_m(cell_size_m); !v) {
            return std::unexpected(v.error());
        }
        auto res = exec_params(
            conn,
            "SELECT FLOOR(ST_X(p) / $3::double precision)::bigint, "
            "FLOOR(ST_Y(p) / $3::double precision)::bigint "
            "FROM (SELECT ST_Transform(ST_SetSRID(ST_MakePoint($1::double precision, $2::double precision), 4326), 5880) AS p) s",
            {std::to_string(lon), std::to_string(lat), std::to_string(cell_size_m)}
        );
        if (!res) return std::unexpected(res.error());
        if (PQntuples(res->get()) == 0) {
            return std::unexpected("Falha ao projetar coordenada para EPSG:5880");
        }
        const long long ix = std::stoll(PQgetvalue(res->get(), 0, 0));
        const long long iy = std::stoll(PQgetvalue(res->get(), 0, 1));
        const std::string cid = "GRID5880_" + std::to_string(cell_size_m) + "_" + std::to_string(ix) + "_" + std::to_string(iy);
        return GridSpecResolved{cell_size_m, ix, iy, cid, true, lon, lat};
    }
    return std::unexpected("value para GRID_CELL deve ser [lon, lat] ou identificador GRID5880_<S>_<ix>_<iy>");
}

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
            return std::unexpected("value para MUNICIPALITY deve ser string (codigo IBGE 7 digitos ou nome do municipio)");
        }
        const std::string raw = req["value"].get<std::string>();
        if (raw.empty()) {
            return std::unexpected("Informe o codigo IBGE ou nome do municipio");
        }
        std::string code = raw;
        if (!policy::validate_municipality_code(raw)) {
            auto mres = exec_params(
                conn,
                "SELECT DISTINCT municipality_code FROM territorial_unit tu "
                "JOIN dataset_version dv ON dv.version_id = tu.version_id "
                "WHERE dv.is_active = TRUE AND LOWER(tu.municipality_name) = LOWER($1) "
                "LIMIT 2",
                {raw}
            );
            if (!mres || PQntuples(mres->get()) == 0) {
                return std::unexpected("Municipio nao encontrado na malha oficial do RS (informe o nome exato ou codigo IBGE de 7 digitos)");
            }
            if (PQntuples(mres->get()) > 1) {
                return std::unexpected("Nome de municipio ambiguo; informe o codigo IBGE de 7 digitos");
            }
            code = PQgetvalue(mres->get(), 0, 0);
        }
        return GeomQuery{
            "(SELECT ST_Union(geom) FROM territorial_unit tu JOIN dataset_version dv ON dv.version_id = tu.version_id WHERE dv.is_active = TRUE AND tu.municipality_code = $1)",
            {code},
            "Uniao dos setores censitarios ativos do municipio IBGE " + code
        };
    }

    if (kind == "SECTOR") {
        if (!req["value"].is_string()) {
            return std::unexpected("value para SECTOR deve ser string (codigo do setor IBGE)");
        }
        const std::string unit_code = req["value"].get<std::string>();
        if (unit_code.empty() || unit_code.size() > 32) {
            return std::unexpected("Codigo de setor invalido");
        }
        return GeomQuery{
            "(SELECT geom FROM territorial_unit tu JOIN dataset_version dv ON dv.version_id = tu.version_id WHERE dv.is_active = TRUE AND tu.unit_code = $1 LIMIT 1)",
            {unit_code},
            "Geometria exata do setor censitario IBGE 2022 " + unit_code
        };
    }

    if (kind == "DISTRICT") {
        auto matches = lookup_districts(conn, req);
        if (!matches) return std::unexpected(matches.error());
        if (matches->empty()) {
            return std::unexpected("Distrito nao encontrado na malha oficial do RS");
        }
        if (matches->size() > 1) {
            return std::unexpected("Nome de distrito ambiguo entre municipios; informe municipality_code ou codigo IBGE de distrito (9 digitos)");
        }
        const auto& m = matches->front();
        const bool rural_only = req.value("rural_only", false);
        const std::string rural_filter = rural_only ? " AND tu.situation_label = 'Rural'" : "";
        return GeomQuery{
            "(SELECT ST_Union(geom) FROM territorial_unit tu JOIN dataset_version dv ON dv.version_id = tu.version_id WHERE dv.is_active = TRUE AND tu.attributes->>'CD_DIST' = $1" + rural_filter + ")",
            {m.district_code},
            "Uniao dos setores censitarios IBGE 2022 do distrito " + m.district_name + " (" + m.district_code + ")"
        };
    }

    if (kind == "GRID_CELL") {
        auto gs = resolve_grid_spec(conn, req);
        if (!gs) return std::unexpected(gs.error());
        const double x0 = static_cast<double>(gs->ix) * static_cast<double>(gs->cell_size_m);
        const double y0 = static_cast<double>(gs->iy) * static_cast<double>(gs->cell_size_m);
        const double x1 = x0 + static_cast<double>(gs->cell_size_m);
        const double y1 = y0 + static_cast<double>(gs->cell_size_m);
        return GeomQuery{
            "ST_Transform(ST_MakeEnvelope($1::double precision, $2::double precision, $3::double precision, $4::double precision, 5880), 4326)",
            {std::to_string(x0), std::to_string(y0), std::to_string(x1), std::to_string(y1)},
            "Celula metrica regular " + gs->cell_id + " (" + std::to_string(gs->cell_size_m) + " m x " + std::to_string(gs->cell_size_m) + " m em EPSG:5880)"
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

    if (kind == "CEP") {
        if (!req["value"].is_string()) {
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
        const double radius_m = req.value("radius_m", 500.0);
        if (radius_m <= 0.0 || radius_m > limits.max_radius_m) {
            return std::unexpected("radius_m para entorno de CEP deve estar entre 1 e " + std::to_string(limits.max_radius_m) + " m");
        }

        auto chk = exec_params(
            conn,
            "SELECT pr.resolution_level, pr.municipality_code, COUNT(*) OVER() "
            "FROM postal_reference pr "
            "JOIN dataset_version dv ON dv.version_id = pr.version_id "
            "JOIN source s ON s.source_id = dv.source_id "
            "WHERE dv.is_active = TRUE AND s.approval_status = 'APPROVED' AND pr.cep_code = $1 "
            "ORDER BY CASE pr.resolution_level "
            "  WHEN 'R5' THEN 1 WHEN 'R4' THEN 2 WHEN 'R3' THEN 3 WHEN 'R2' THEN 4 WHEN 'POSTAL_REGION' THEN 5 WHEN 'R1' THEN 6 ELSE 9 END "
            "LIMIT 1",
            {cep_digits}
        );
        if (!chk || PQntuples(chk->get()) == 0) {
            return std::unexpected("CEP nao disponivel para caracterizacao de entorno");
        }
        const std::string lvl = PQgetvalue(chk->get(), 0, 0);
        if (lvl == "R1") {
            return std::unexpected("CEP possui apenas resolucao municipal (R1); selecione o municipio ou desenhe uma area no mapa");
        }
        return GeomQuery{
            "(SELECT ST_Buffer(ST_Union(pr.support_geom)::geography, $2::double precision)::geometry "
            "FROM postal_reference pr JOIN dataset_version dv ON dv.version_id = pr.version_id "
            "WHERE dv.is_active = TRUE AND pr.cep_code = $1 AND pr.resolution_level IN ('R2','R3','R4','R5','POSTAL_REGION') AND pr.support_geom IS NOT NULL)",
            {cep_digits, std::to_string(radius_m)},
            "Entorno geodesico de " + std::to_string(static_cast<int>(radius_m)) + " m ao redor do suporte territorial publico do CEP"
        };
    }

    if (kind == "STREET") {
        if (!req["value"].is_string()) {
            return std::unexpected("value para STREET deve ser string");
        }
        const std::string street_raw = req["value"].get<std::string>();
        if (street_raw.size() < 3) {
            return std::unexpected("Informe pelo menos 3 caracteres para o logradouro");
        }
        const std::string mun_code = req.value("municipality_code", std::string{});
        const double radius_m = req.value("radius_m", 500.0);
        if (radius_m <= 0.0 || radius_m > limits.max_radius_m) {
            return std::unexpected("radius_m para entorno de logradouro deve estar entre 1 e " + std::to_string(limits.max_radius_m) + " m");
        }

        auto chk = exec_params(
            conn,
            "WITH q AS ("
            "  SELECT UPPER(translate(TRIM($1), 'ÁÀÂÃÄáàâãäÉÈÊËéèêëÍÌÎÏíìîïÓÒÔÕÖóòôõöÚÙÛÜúùûüÇçÑñ', 'AAAAAaaaaaEEEEeeeeIIIIiiiiOOOOOoooooUUUUuuuuCcNn')) AS norm"
            ") "
            "SELECT COUNT(DISTINCT sf.municipality_code), COUNT(*) "
            "FROM street_face sf "
            "JOIN dataset_version dv ON dv.version_id = sf.version_id "
            "JOIN source s ON s.source_id = dv.source_id "
            "CROSS JOIN q "
            "WHERE dv.is_active = TRUE AND s.approval_status = 'APPROVED' "
            "  AND ($2 = '' OR sf.municipality_code = $2) "
            "  AND (sf.normalized_name = q.norm OR sf.normalized_name LIKE '%' || q.norm || '%')",
            {street_raw, mun_code}
        );
        if (!chk || PQntuples(chk->get()) == 0 || std::atoi(PQgetvalue(chk->get(), 0, 1)) == 0) {
            return std::unexpected("Logradouro nao encontrado na Base de Faces do IBGE 2022");
        }
        if (std::atoi(PQgetvalue(chk->get(), 0, 0)) > 1) {
            return std::unexpected("Logradouro encontrado em multiplos municipios; especifique o municipio");
        }
        return GeomQuery{
            "(SELECT ST_Buffer(ST_Union(sf.geom)::geography, $3::double precision)::geometry "
            "FROM street_face sf "
            "JOIN dataset_version dv ON dv.version_id = sf.version_id "
            "JOIN source s ON s.source_id = dv.source_id "
            "WHERE dv.is_active = TRUE AND s.approval_status = 'APPROVED' "
            "  AND ($2 = '' OR sf.municipality_code = $2) "
            "  AND (sf.normalized_name = UPPER(translate(TRIM($1), 'ÁÀÂÃÄáàâãäÉÈÊËéèêëÍÌÎÏíìîïÓÒÔÕÖóòôõöÚÙÛÜúùûüÇçÑñ', 'AAAAAaaaaaEEEEeeeeIIIIiiiiOOOOOoooooUUUUuuuuCcNn')) "
            "       OR sf.normalized_name LIKE '%' || UPPER(translate(TRIM($1), 'ÁÀÂÃÄáàâãäÉÈÊËéèêëÍÌÎÏíìîïÓÒÔÕÖóòôõöÚÙÛÜúùûüÇçÑñ', 'AAAAAaaaaaEEEEeeeeIIIIiiiiOOOOOoooooUUUUuuuuCcNn')) || '%'))",
            {street_raw, mun_code, std::to_string(radius_m)},
            "Entorno geodesico de " + std::to_string(static_cast<int>(radius_m)) + " m ao redor do logradouro (IBGE Faces 2022)"
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

    if (kind == "SECTOR") {
        if (!req.contains("value") || !req["value"].is_string()) {
            return std::unexpected("value para SECTOR deve ser string (codigo do setor IBGE)");
        }
        const std::string unit_code = req["value"].get<std::string>();
        auto conn = connect_pg(conninfo_);
        if (!conn) return std::unexpected(conn.error());
        auto res = exec_params(
            conn->get(),
            "SELECT tu.unit_code, tu.municipality_code, tu.municipality_name, "
            "COALESCE(tu.district_name, ''), COALESCE(tu.situation_label, ''), "
            "tu.area_m2, ST_AsGeoJSON(ST_SimplifyPreserveTopology(tu.geom, 0.00025)), "
            "ST_AsGeoJSON(tu.geom) "
            "FROM territorial_unit tu "
            "JOIN dataset_version dv ON dv.version_id = tu.version_id "
            "WHERE dv.is_active = TRUE AND tu.unit_code = $1 LIMIT 1",
            {unit_code}
        );
        if (!res) return std::unexpected(res.error());
        if (PQntuples(res->get()) == 0) {
            return nlohmann::json{
                {"status", "OUTSIDE_COVERAGE"},
                {"kind", "SECTOR"},
                {"spatial_level", "SETOR_CENSITARIO"},
                {"support_geometry", nullptr},
                {"analysis_geometry", nullptr},
                {"reasons", nlohmann::json::array({"Setor censitario nao encontrado na cobertura ativa"})}
            };
        }
        return nlohmann::json{
            {"status", "RESOLVED"},
            {"kind", "SECTOR"},
            {"spatial_level", "SETOR_CENSITARIO"},
            {"method", "ibge_2022_exact_sector_geometry"},
            {"unit_code", PQgetvalue(res->get(), 0, 0)},
            {"municipality_code", PQgetvalue(res->get(), 0, 1)},
            {"municipality_name", PQgetvalue(res->get(), 0, 2)},
            {"district_name", PQgetvalue(res->get(), 0, 3)},
            {"situation_label", PQgetvalue(res->get(), 0, 4)},
            {"area_m2", std::strtod(PQgetvalue(res->get(), 0, 5), nullptr)},
            {"support_geometry", nlohmann::json::parse(PQgetvalue(res->get(), 0, 6))},
            {"analysis_geometry", nlohmann::json::parse(PQgetvalue(res->get(), 0, 7))},
            {"reasons", nlohmann::json::array({"Geometria oficial exata do setor censitario IBGE 2022 (sem simplificacao de borda)."})}
        };
    }

    if (kind == "DISTRICT") {
        auto conn = connect_pg(conninfo_);
        if (!conn) return std::unexpected(conn.error());
        auto matches = lookup_districts(conn->get(), req);
        if (!matches) return std::unexpected(matches.error());
        if (matches->empty()) {
            return nlohmann::json{
                {"status", "OUTSIDE_COVERAGE"},
                {"kind", "DISTRICT"},
                {"spatial_level", "DISTRITO"},
                {"support_geometry", nullptr},
                {"analysis_geometry", nullptr},
                {"reasons", nlohmann::json::array({"Distrito nao encontrado na cobertura territorial ativa"})}
            };
        }
        if (matches->size() > 1) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& m : *matches) {
                arr.push_back({
                    {"district_code", m.district_code},
                    {"district_name", m.district_name},
                    {"municipality_code", m.municipality_code},
                    {"municipality_name", m.municipality_name},
                    {"sector_count", m.sector_count},
                    {"area_m2", m.area_m2}
                });
            }
            return nlohmann::json{
                {"status", "AMBIGUOUS"},
                {"kind", "DISTRICT"},
                {"spatial_level", "DISTRITO"},
                {"matches", arr},
                {"support_geometry", nullptr},
                {"analysis_geometry", nullptr},
                {"reasons", nlohmann::json::array({"Nome de distrito presente em multiplos municipios; selecione o distrito desejado."})}
            };
        }
        const auto& m = matches->front();
        const bool rural_only = req.value("rural_only", false);
        const std::string rural_filter = rural_only ? " AND tu.situation_label = 'Rural'" : "";
        const std::string sql =
            "WITH ref AS ("
            "  SELECT ST_Union(geom) AS g FROM territorial_unit tu "
            "  JOIN dataset_version dv ON dv.version_id = tu.version_id "
            "  WHERE dv.is_active = TRUE AND tu.attributes->>'CD_DIST' = $1" + rural_filter +
            ") "
            "SELECT COALESCE(ST_Area(g::geography), 0.0), "
            "COALESCE(ST_AsGeoJSON(ST_SimplifyPreserveTopology(g, 0.0005)), 'null'), "
            "COALESCE(ST_AsGeoJSON(g), 'null'), "
            "ST_X(ST_PointOnSurface(g)), ST_Y(ST_PointOnSurface(g)) "
            "FROM ref";
        auto res = exec_params(conn->get(), sql.c_str(), {m.district_code});
        if (!res) return std::unexpected(res.error());
        const double area_m2 = std::strtod(PQgetvalue(res->get(), 0, 0), nullptr);
        const double pt_lon = std::strtod(PQgetvalue(res->get(), 0, 3), nullptr);
        const double pt_lat = std::strtod(PQgetvalue(res->get(), 0, 4), nullptr);
        return nlohmann::json{
            {"status", "RESOLVED"},
            {"kind", "DISTRICT"},
            {"spatial_level", "DISTRITO"},
            {"method", "ibge_2022_sector_union_by_cd_dist"},
            {"district_code", m.district_code},
            {"district_name", m.district_name},
            {"municipality_code", m.municipality_code},
            {"municipality_name", m.municipality_name},
            {"rural_only", rural_only},
            {"sector_count", m.sector_count},
            {"area_m2", area_m2},
            {"anchor_point", nlohmann::json::array({pt_lon, pt_lat})},
            {"support_geometry", nlohmann::json::parse(PQgetvalue(res->get(), 0, 1))},
            {"analysis_geometry", nlohmann::json::parse(PQgetvalue(res->get(), 0, 2))},
            {"reasons", nlohmann::json::array({
                "Geometria distrital construida pela uniao dos setores censitarios IBGE 2022 (" +
                std::to_string(m.sector_count) + " setores); confrontacao com limite distrital oficial pendente de fonte dedicada."
            })}
        };
    }

    if (kind == "GRID_CELL") {
        auto conn = connect_pg(conninfo_);
        if (!conn) return std::unexpected(conn.error());
        auto gs = resolve_grid_spec(conn->get(), req);
        if (!gs) return std::unexpected(gs.error());

        const double x0 = static_cast<double>(gs->ix) * static_cast<double>(gs->cell_size_m);
        const double y0 = static_cast<double>(gs->iy) * static_cast<double>(gs->cell_size_m);
        const double x1 = x0 + static_cast<double>(gs->cell_size_m);
        const double y1 = y0 + static_cast<double>(gs->cell_size_m);

        auto res = exec_params(
            conn->get(),
            "WITH cell AS ("
            "  SELECT ST_Transform(ST_MakeEnvelope($1::double precision, $2::double precision, $3::double precision, $4::double precision, 5880), 4326) AS g"
            ") "
            "SELECT ST_Area(g::geography), ST_AsGeoJSON(g), ST_X(ST_Centroid(g)), ST_Y(ST_Centroid(g)) FROM cell",
            {std::to_string(x0), std::to_string(y0), std::to_string(x1), std::to_string(y1)}
        );
        if (!res) return std::unexpected(res.error());
        const double area_m2 = std::strtod(PQgetvalue(res->get(), 0, 0), nullptr);
        auto cell_geom = nlohmann::json::parse(PQgetvalue(res->get(), 0, 1));
        const double c_lon = std::strtod(PQgetvalue(res->get(), 0, 2), nullptr);
        const double c_lat = std::strtod(PQgetvalue(res->get(), 0, 3), nullptr);
        nlohmann::json support_geom = cell_geom;
        if (gs->from_point) {
            support_geom = {
                {"type", "Point"},
                {"coordinates", nlohmann::json::array({gs->lon, gs->lat})}
            };
        }
        return nlohmann::json{
            {"status", "RESOLVED"},
            {"kind", "GRID_CELL"},
            {"spatial_level", "GRADE_METRICA"},
            {"method", "on_demand_metric_grid_epsg5880"},
            {"area_m2", area_m2},
            {"grid_spec", {
                {"crs", "EPSG:5880"},
                {"crs_name", "SIRGAS 2000 / Brazil Polyconic"},
                {"origin_m", nlohmann::json::array({0.0, 0.0})},
                {"cell_size_m", gs->cell_size_m},
                {"ix", gs->ix},
                {"iy", gs->iy},
                {"cell_id", gs->cell_id},
                {"bounds_5880_m", nlohmann::json::array({x0, y0, x1, y1})},
                {"nominal_area_ha", (static_cast<double>(gs->cell_size_m) * static_cast<double>(gs->cell_size_m)) / 10000.0},
                {"centroid_wgs84", nlohmann::json::array({c_lon, c_lat})}
            }},
            {"support_geometry", support_geom},
            {"analysis_geometry", cell_geom},
            {"reasons", nlohmann::json::array({
                "Unidade espacial regular gerada sob demanda em EPSG:5880 (origem 0,0), independente da estrutura fundiaria."
            })}
        };
    }

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
            "SELECT pr.cep_code, pr.resolution_level, pr.reference_kind, pr.reference_name, "
            "pr.municipality_code, pr.municipality_name, pr.source_id, pr.license, "
            "pr.association_method, pr.limitations, pr.uncertainty, dv.dataset_version, "
            "COALESCE(ST_AsGeoJSON(pr.support_geom), 'null') "
            "FROM postal_reference pr "
            "JOIN dataset_version dv ON dv.version_id = pr.version_id "
            "JOIN source s ON s.source_id = dv.source_id "
            "WHERE dv.is_active = TRUE AND s.approval_status = 'APPROVED' AND pr.cep_code = $1 "
            "ORDER BY CASE pr.resolution_level "
            "  WHEN 'R5' THEN 1 WHEN 'R4' THEN 2 WHEN 'R3' THEN 3 WHEN 'R2' THEN 4 WHEN 'POSTAL_REGION' THEN 5 WHEN 'R1' THEN 6 ELSE 9 END, "
            "pr.source_id ASC",
            {cep_digits}
        );
        if (!res) return std::unexpected(res.error());

        const int nrows = PQntuples(res->get());
        if (nrows == 0) {
            return nlohmann::json{
                {"status", "UNAVAILABLE"},
                {"kind", "CEP"},
                {"resolution_level", "R0"},
                {"spatial_level", "R0 (UNAVAILABLE)"},
                {"method", "local_approved_postal_reference_lookup"},
                {"support_geometry", nullptr},
                {"analysis_geometry", nullptr},
                {"provenance", {
                    {"policy", "CATEGORY_A_ONLY"}
                }},
                {"reasons", nlohmann::json::array({
                    "CEP nao encontrado nas fontes publicas aprovadas (GeoNames / OSM Categoria A).",
                    "O SITUS nao envia CEP a servicos externos e nao infere localizacao pela numeracao."
                })}
            };
        }

        nlohmann::json matches = nlohmann::json::array();
        std::set<std::string> distinct_muns;
        std::set<std::string> distinct_sources;
        for (int i = 0; i < nrows; ++i) {
            const std::string cd_mun = PQgetvalue(res->get(), i, 4);
            const std::string src_id = PQgetvalue(res->get(), i, 6);
            distinct_muns.insert(cd_mun);
            distinct_sources.insert(src_id);

            const std::string sg_str = PQgetvalue(res->get(), i, 12);
            matches.push_back({
                {"resolution_level", PQgetvalue(res->get(), i, 1)},
                {"reference_kind", PQgetvalue(res->get(), i, 2)},
                {"reference_name", PQgetvalue(res->get(), i, 3)},
                {"municipality_code", cd_mun},
                {"municipality_name", PQgetvalue(res->get(), i, 5)},
                {"source_id", src_id},
                {"license", PQgetvalue(res->get(), i, 7)},
                {"association_method", PQgetvalue(res->get(), i, 8)},
                {"limitations", PQgetvalue(res->get(), i, 9)},
                {"uncertainty", PQgetvalue(res->get(), i, 10)},
                {"dataset_version", PQgetvalue(res->get(), i, 11)},
                {"support_geometry", (sg_str.empty() || sg_str == "null") ? nlohmann::json(nullptr) : nlohmann::json::parse(sg_str)}
            });
        }

        const bool is_ambiguous = distinct_muns.size() > 1;
        const auto& top = matches[0];
        const std::string top_level = top["resolution_level"].get<std::string>();
        const std::string top_mun = top["municipality_code"].get<std::string>();

        nlohmann::json support_geom = top["support_geometry"];
        nlohmann::json analysis_geom = nullptr;

        // Quando nao e ambiguo e tem municipio unico:
        // Se radius_m > 0 e suporte submunicipal (R2..R5/POSTAL_REGION), gera entorno como analysis_geometry;
        // caso contrario, disponibiliza o poligono do municipio como contexto.
        const double req_radius_m = req.value("radius_m", 0.0);
        if (!is_ambiguous && req_radius_m > 0.0 && top_level != "R1") {
            auto bgeom = exec_params(
                conn->get(),
                "SELECT ST_AsGeoJSON(ST_Buffer(ST_Union(pr.support_geom)::geography, $2::double precision)::geometry) "
                "FROM postal_reference pr JOIN dataset_version dv ON dv.version_id = pr.version_id "
                "WHERE dv.is_active = TRUE AND pr.cep_code = $1 AND pr.resolution_level IN ('R2','R3','R4','R5','POSTAL_REGION') AND pr.support_geom IS NOT NULL",
                {cep_digits, std::to_string(req_radius_m)}
            );
            if (bgeom && PQntuples(bgeom->get()) > 0) {
                const std::string bg = PQgetvalue(bgeom->get(), 0, 0);
                if (!bg.empty() && bg != "null") {
                    analysis_geom = nlohmann::json::parse(bg);
                }
            }
        } else if (!is_ambiguous && !top_mun.empty()) {
            auto mgeom = exec_params(
                conn->get(),
                "SELECT ST_AsGeoJSON(ST_SimplifyPreserveTopology(ST_Union(geom), 0.0005)) "
                "FROM territorial_unit tu "
                "JOIN dataset_version dv ON dv.version_id = tu.version_id "
                "WHERE dv.is_active = TRUE AND tu.municipality_code = $1",
                {top_mun}
            );
            if (mgeom && PQntuples(mgeom->get()) > 0) {
                const std::string mg = PQgetvalue(mgeom->get(), 0, 0);
                if (!mg.empty() && mg != "null") {
                    analysis_geom = nlohmann::json::parse(mg);
                    if (top_level == "R1") {
                        support_geom = analysis_geom;
                    }
                }
            }
        }

        std::vector<std::string> reasons;
        if (is_ambiguous) {
            reasons.push_back("CEP associado a multiplos municipios nas fontes aprovadas; nenhuma referencia foi priorizada automaticamente.");
        } else if (top_level == "R1") {
            reasons.push_back("CEP resolvido apenas em nivel municipal (R1). Nao representa localizacao submunicipal.");
        } else {
            reasons.push_back("CEP resolvido em suporte territorial publico (" + top_level + ") com contexto municipal.");
        }

        return nlohmann::json{
            {"status", is_ambiguous ? "AMBIGUOUS" : "RESOLVED"},
            {"kind", "CEP"},
            {"cep_code", cep_digits},
            {"resolution_level", is_ambiguous ? "AMBIGUOUS" : top_level},
            {"spatial_level", is_ambiguous ? "AMBIGUOUS" : (top_level + " (" + top["reference_kind"].get<std::string>() + ")")},
            {"municipality_code", is_ambiguous ? nlohmann::json(nullptr) : nlohmann::json(top_mun)},
            {"municipality_name", is_ambiguous ? nlohmann::json(nullptr) : top["municipality_name"]},
            {"method", top["association_method"]},
            {"support_geometry", is_ambiguous ? nlohmann::json(nullptr) : support_geom},
            {"analysis_geometry", analysis_geom},
            {"matches", matches},
            {"reasons", reasons}
        };
    }

    if (kind == "STREET") {
        if (!req.contains("value") || !req["value"].is_string()) {
            return std::unexpected("value para STREET deve ser string");
        }
        const std::string street_raw = req["value"].get<std::string>();
        if (street_raw.size() < 3) {
            return std::unexpected("Informe pelo menos 3 caracteres para o logradouro");
        }
        const std::string mun_filter = req.value("municipality_code", std::string{});

        auto conn = connect_pg(conninfo_);
        if (!conn) return std::unexpected(conn.error());

        auto res = exec_params(
            conn->get(),
            "WITH q AS ("
            "  SELECT UPPER(translate(TRIM($1), 'ÁÀÂÃÄáàâãäÉÈÊËéèêëÍÌÎÏíìîïÓÒÔÕÖóòôõöÚÙÛÜúùûüÇçÑñ', 'AAAAAaaaaaEEEEeeeeIIIIiiiiOOOOOoooooUUUUuuuuCcNn')) AS norm, "
            "         UPPER(translate(TRIM($2), 'ÁÀÂÃÄáàâãäÉÈÊËéèêëÍÌÎÏíìîïÓÒÔÕÖóòôõöÚÙÛÜúùûüÇçÑñ', 'AAAAAaaaaaEEEEeeeeIIIIiiiiOOOOOoooooUUUUuuuuCcNn')) AS mun_norm"
            "), mun_names AS ("
            "  SELECT DISTINCT tu.municipality_code, tu.municipality_name "
            "  FROM territorial_unit tu JOIN dataset_version dv ON dv.version_id = tu.version_id "
            "  WHERE dv.is_active = TRUE"
            ") "
            "SELECT sf.municipality_code, COALESCE(mn.municipality_name, sf.municipality_code), "
            "       sf.full_name, COUNT(*)::int AS face_count, "
            "       STRING_AGG(DISTINCT sf.sector_code, ', ' ORDER BY sf.sector_code) AS sectors, "
            "       ST_AsGeoJSON(ST_Union(sf.geom)), dv.dataset_version, "
            "       MAX(CASE WHEN sf.normalized_name = q.norm THEN 1 ELSE 0 END) AS is_exact "
            "FROM street_face sf "
            "JOIN dataset_version dv ON dv.version_id = sf.version_id "
            "JOIN source s ON s.source_id = dv.source_id "
            "LEFT JOIN mun_names mn ON mn.municipality_code = sf.municipality_code "
            "CROSS JOIN q "
            "WHERE dv.is_active = TRUE AND s.approval_status = 'APPROVED' "
            "  AND sf.full_name <> '' "
            "  AND (q.mun_norm = '' OR sf.municipality_code = $2 OR UPPER(translate(COALESCE(mn.municipality_name, ''), 'ÁÀÂÃÄáàâãäÉÈÊËéèêëÍÌÎÏíìîïÓÒÔÕÖóòôõöÚÙÛÜúùûüÇçÑñ', 'AAAAAaaaaaEEEEeeeeIIIIiiiiOOOOOoooooUUUUuuuuCcNn')) = q.mun_norm) "
            "  AND (sf.normalized_name = q.norm OR sf.normalized_name LIKE '%' || q.norm || '%') "
            "GROUP BY sf.municipality_code, mn.municipality_name, sf.full_name, dv.dataset_version "
            "ORDER BY is_exact DESC, COUNT(*) DESC, sf.full_name ASC "
            "LIMIT 15",
            {street_raw, mun_filter}
        );
        if (!res) return std::unexpected(res.error());

        const int nrows = PQntuples(res->get());
        if (nrows == 0) {
            return nlohmann::json{
                {"status", "UNAVAILABLE"},
                {"kind", "STREET"},
                {"resolution_level", "R0"},
                {"spatial_level", "R0 (UNAVAILABLE)"},
                {"method", "ibge_faces_logradouros_2022_lookup"},
                {"support_geometry", nullptr},
                {"analysis_geometry", nullptr},
                {"provenance", {{"policy", "CATEGORY_A_ONLY"}}},
                {"reasons", nlohmann::json::array({
                    "Logradouro nao encontrado na Base de Faces de Logradouros (IBGE Censo 2022)."
                })}
            };
        }

        nlohmann::json matches = nlohmann::json::array();
        for (int i = 0; i < nrows; ++i) {
            matches.push_back({
                {"resolution_level", "R4"},
                {"reference_kind", "PUBLIC_WAY"},
                {"municipality_code", PQgetvalue(res->get(), i, 0)},
                {"municipality_name", PQgetvalue(res->get(), i, 1)},
                {"reference_name", PQgetvalue(res->get(), i, 2)},
                {"face_count", std::atoi(PQgetvalue(res->get(), i, 3))},
                {"sectors", PQgetvalue(res->get(), i, 4)},
                {"source_id", "ibge_faces_rs_2022"},
                {"license", "Dados Abertos IBGE"},
                {"dataset_version", PQgetvalue(res->get(), i, 6)},
                {"limitations", "Faces publicas de logradouros (IBGE Censo 2022); sem numeracao predial ou CEP."},
                {"support_geometry", nlohmann::json::parse(PQgetvalue(res->get(), i, 5))}
            });
        }

        const bool is_ambiguous = nrows > 1;
        const auto& top = matches[0];
        const std::string top_mun = top["municipality_code"].get<std::string>();

        nlohmann::json support_geom = is_ambiguous ? nlohmann::json(nullptr) : top["support_geometry"];
        nlohmann::json analysis_geom = nullptr;
        const double req_radius_m = req.value("radius_m", 0.0);
        if (!is_ambiguous && req_radius_m > 0.0) {
            auto bgeom = exec_params(
                conn->get(),
                "SELECT ST_AsGeoJSON(ST_Buffer(ST_GeomFromGeoJSON($1)::geography, $2::double precision)::geometry)",
                {support_geom.dump(), std::to_string(req_radius_m)}
            );
            if (bgeom && PQntuples(bgeom->get()) > 0) {
                const std::string bg = PQgetvalue(bgeom->get(), 0, 0);
                if (!bg.empty() && bg != "null") {
                    analysis_geom = nlohmann::json::parse(bg);
                }
            }
        }

        return nlohmann::json{
            {"status", is_ambiguous ? "AMBIGUOUS" : "RESOLVED"},
            {"kind", "STREET"},
            {"street_query", street_raw},
            {"resolution_level", is_ambiguous ? "AMBIGUOUS" : "R4"},
            {"spatial_level", is_ambiguous ? "AMBIGUOUS" : "R4 (PUBLIC_WAY)"},
            {"municipality_code", is_ambiguous ? nlohmann::json(nullptr) : nlohmann::json(top_mun)},
            {"municipality_name", is_ambiguous ? nlohmann::json(nullptr) : top["municipality_name"]},
            {"reference_name", is_ambiguous ? nlohmann::json(nullptr) : top["reference_name"]},
            {"method", "ibge_faces_logradouros_2022_lookup"},
            {"support_geometry", support_geom},
            {"analysis_geometry", analysis_geom},
            {"matches", matches},
            {"reasons", nlohmann::json::array({
                is_ambiguous
                    ? "Multiplos logradouros ou municipios correspondentes encontrados; selecione o logradouro desejado."
                    : "Logradouro localizado na Base de Faces de Logradouros do IBGE (Censo 2022, R4)."
            })}
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
PostgisStore::search_municipalities(const std::string& query) const {
    auto conn = connect_pg(conninfo_);
    if (!conn) return std::unexpected(conn.error());

    auto res = exec_params(
        conn->get(),
        "SELECT DISTINCT tu.municipality_code, tu.municipality_name "
        "FROM territorial_unit tu "
        "JOIN dataset_version dv ON dv.version_id = tu.version_id "
        "WHERE dv.is_active = TRUE AND tu.municipality_code <> '' "
        "AND ($1 = '' OR tu.municipality_name ILIKE '%' || $1 || '%' OR tu.municipality_code LIKE $1 || '%') "
        "ORDER BY tu.municipality_name ASC "
        "LIMIT 25",
        {query}
    );
    if (!res) return std::unexpected(res.error());

    nlohmann::json arr = nlohmann::json::array();
    for (int i = 0; i < PQntuples(res->get()); ++i) {
        arr.push_back({
            {"municipality_code", PQgetvalue(res->get(), i, 0)},
            {"municipality_name", PQgetvalue(res->get(), i, 1)}
        });
    }
    return nlohmann::json{{"municipalities", arr}};
}

std::expected<nlohmann::json, std::string>
PostgisStore::search_districts(const std::string& query, const std::string& municipality_code) const {
    if (!municipality_code.empty()) {
        if (auto v = policy::validate_municipality_code(municipality_code); !v) {
            return std::unexpected(v.error());
        }
    }
    auto conn = connect_pg(conninfo_);
    if (!conn) return std::unexpected(conn.error());

    auto res = exec_params(
        conn->get(),
        "SELECT tu.attributes->>'CD_DIST' AS cd_dist, tu.district_name, "
        "tu.municipality_code, tu.municipality_name, "
        "COUNT(*) FILTER (WHERE tu.situation_label = 'Rural')::int AS rural_sectors, "
        "COUNT(*)::int AS total_sectors "
        "FROM territorial_unit tu "
        "JOIN dataset_version dv ON dv.version_id = tu.version_id "
        "WHERE dv.is_active = TRUE AND COALESCE(tu.district_name, '') <> '' "
        "AND ($1 = '' OR tu.municipality_code = $1) "
        "AND ($2 = '' OR tu.district_name ILIKE '%' || $2 || '%' OR tu.municipality_name ILIKE '%' || $2 || '%' OR tu.attributes->>'CD_DIST' LIKE $2 || '%') "
        "GROUP BY tu.attributes->>'CD_DIST', tu.district_name, tu.municipality_code, tu.municipality_name "
        "ORDER BY rural_sectors DESC, tu.municipality_name ASC, tu.district_name ASC "
        "LIMIT 30",
        {municipality_code, query}
    );
    if (!res) return std::unexpected(res.error());

    nlohmann::json arr = nlohmann::json::array();
    for (int i = 0; i < PQntuples(res->get()); ++i) {
        arr.push_back({
            {"district_code", PQgetvalue(res->get(), i, 0)},
            {"district_name", PQgetvalue(res->get(), i, 1)},
            {"municipality_code", PQgetvalue(res->get(), i, 2)},
            {"municipality_name", PQgetvalue(res->get(), i, 3)},
            {"rural_sectors", std::atoi(PQgetvalue(res->get(), i, 4))},
            {"total_sectors", std::atoi(PQgetvalue(res->get(), i, 5))}
        });
    }
    return nlohmann::json{{"districts", arr}};
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
        "AND ST_Area(ST_Intersection(tu.geom, ref.g)::geography) > 1.0 "
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
        const std::string sit_lbl_base = sit_code_raw.empty()
            ? "Sem classificacao de situacao (ex.: massa d'agua)"
            : situation_label_from_code(sit_code, PQgetvalue(res->get(), i, 4));
        const std::string sit_lbl = sit_code_raw.empty()
            ? sit_lbl_base
            : (sit_lbl_base + " (sit. " + sit_code + ")");

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
PostgisStore::characterize_environmental_vectors(const nlohmann::json& req, const domain::SecurityLimits& limits) const {
    auto conn = connect_pg(conninfo_);
    if (!conn) return std::unexpected(conn.error());

    auto gq = build_reference_geom_query(conn->get(), req, limits);
    if (!gq) return std::unexpected(gq.error());

    nlohmann::json obs = nlohmann::json::array();

    // 1. Biomas (IBGE 1:250.000)
    {
        const std::string sql =
            "WITH ref AS (SELECT " + gq->geom_expr + " AS g) "
            "SELECT b.biome_code, b.biome_name, "
            "SUM(ST_Area(ST_Intersection(b.geom, ref.g)::geography)) AS area_m2, "
            "ST_Area(ref.g::geography) AS ref_area_m2, "
            "dv.dataset_version "
            "FROM ref "
            "JOIN biome_unit b ON ST_Intersects(b.geom, ref.g) "
            "JOIN dataset_version dv ON dv.version_id = b.version_id "
            "WHERE dv.is_active = TRUE AND ref.g IS NOT NULL "
            "GROUP BY b.biome_code, b.biome_name, ref.g, dv.dataset_version "
            "ORDER BY area_m2 DESC";
        auto res = exec_params(conn->get(), sql.c_str(), gq->params);
        if (res && PQntuples(res->get()) > 0) {
            nlohmann::json items = nlohmann::json::array();
            double sum_area = 0.0, ref_area = 0.0;
            std::string dv;
            for (int i = 0; i < PQntuples(res->get()); ++i) {
                const double a = std::strtod(PQgetvalue(res->get(), i, 2), nullptr);
                ref_area = std::strtod(PQgetvalue(res->get(), i, 3), nullptr);
                dv = PQgetvalue(res->get(), i, 4);
                sum_area += a;
                items.push_back({
                    {"biome_code", PQgetvalue(res->get(), i, 0)},
                    {"biome_name", PQgetvalue(res->get(), i, 1)},
                    {"area_ha", a / 10000.0},
                    {"fraction", ref_area > 0.0 ? a / ref_area : 0.0}
                });
            }
            obs.push_back({
                {"indicator_id", "biome_composition"},
                {"dimension", "Geografia territorial"},
                {"value", {{"biomes", items}, {"reference_area_ha", ref_area / 10000.0}}},
                {"unit", "ha_and_fraction"},
                {"status", "AVAILABLE"},
                {"source_id", "ibge_biomas_250k"},
                {"dataset_version", dv},
                {"reference_date", "2019-01-01"},
                {"native_resolution", "1:250.000"},
                {"method", "postgis_st_intersection_geography_wgs84"},
                {"support", "poligono_efetivo_investigacao"},
                {"coverage_fraction", ref_area > 0.0 ? std::min(1.0, sum_area / ref_area) : 0.0},
                {"limitations", "Escala regional 1:250.000; transicoes locais exigem verificacao em campo."}
            });
        } else {
            obs.push_back({
                {"indicator_id", "biome_composition"},
                {"dimension", "Geografia territorial"},
                {"value", nullptr},
                {"unit", "ha_and_fraction"},
                {"status", "NO_DATA"},
                {"source_id", "ibge_biomas_250k"},
                {"dataset_version", "2019"},
                {"native_resolution", "1:250.000"},
                {"method", "postgis_st_intersection_geography_wgs84"},
                {"support", "poligono_efetivo_investigacao"},
                {"coverage_fraction", 0.0},
                {"limitations", "Nenhum bioma carregado ou area fora da cobertura."}
            });
        }
    }

    // 2. Hidrografia (IBGE BC250)
    {
        const std::string sql =
            "WITH ref AS (SELECT " + gq->geom_expr + " AS g) "
            "SELECT h.feature_kind, "
            "COUNT(*) AS cnt, "
            "COALESCE(SUM(CASE WHEN h.feature_kind = 'WATERCOURSE' THEN ST_Length(ST_Intersection(h.geom, ref.g)::geography) / 1000.0 ELSE 0 END), 0.0) AS len_km, "
            "COALESCE(SUM(CASE WHEN h.feature_kind = 'WATER_BODY' THEN ST_Area(ST_Intersection(h.geom, ref.g)::geography) / 10000.0 ELSE 0 END), 0.0) AS body_ha, "
            "STRING_AGG(DISTINCT NULLIF(h.feature_name, ''), ', ') AS names, "
            "MAX(dv.dataset_version) AS dv "
            "FROM ref "
            "JOIN hydro_feature h ON ST_Intersects(h.geom, ref.g) "
            "JOIN dataset_version dv ON dv.version_id = h.version_id "
            "WHERE dv.is_active = TRUE AND ref.g IS NOT NULL "
            "GROUP BY h.feature_kind";
        auto res = exec_params(conn->get(), sql.c_str(), gq->params);
        if (res) {
            std::size_t wc_cnt = 0, wb_cnt = 0;
            double len_km = 0.0, body_ha = 0.0;
            std::string names;
            std::string dv = "2026-03-03";
            for (int i = 0; i < PQntuples(res->get()); ++i) {
                const std::string fk = PQgetvalue(res->get(), i, 0);
                const std::size_t cnt = static_cast<std::size_t>(std::strtoll(PQgetvalue(res->get(), i, 1), nullptr, 10));
                if (fk == "WATERCOURSE") {
                    wc_cnt = cnt;
                    len_km = std::strtod(PQgetvalue(res->get(), i, 2), nullptr);
                } else if (fk == "WATER_BODY") {
                    wb_cnt = cnt;
                    body_ha = std::strtod(PQgetvalue(res->get(), i, 3), nullptr);
                }
                const std::string nm = PQgetvalue(res->get(), i, 4);
                if (!nm.empty()) {
                    if (!names.empty()) names += ", ";
                    names += nm;
                }
                const std::string d = PQgetvalue(res->get(), i, 5);
                if (!d.empty()) dv = d;
            }
            obs.push_back({
                {"indicator_id", "hydrography_summary"},
                {"dimension", "Hidrografia"},
                {"value", {
                    {"watercourse_segments", wc_cnt},
                    {"watercourse_length_km", len_km},
                    {"water_bodies", wb_cnt},
                    {"water_body_area_ha", body_ha},
                    {"named_features", names}
                }},
                {"unit", "km_and_ha"},
                {"status", "AVAILABLE"},
                {"source_id", "ibge_bc250_hidrografia"},
                {"dataset_version", dv},
                {"reference_date", "2026-03-03"},
                {"native_resolution", "1:250.000"},
                {"method", "postgis_st_intersection_length_and_area_geography"},
                {"support", "poligono_efetivo_investigacao"},
                {"coverage_fraction", 1.0},
                {"limitations", "Escala cartografica 1:250.000; nascentes e arroios de pequena ordem podem nao constar."}
            });
        }
    }

    // 3. Pedologia (IBGE BDiA 1:250.000)
    {
        const std::string sql =
            "WITH ref AS (SELECT " + gq->geom_expr + " AS g) "
            "SELECT s.soil_symbol, s.soil_class, s.soil_order, "
            "SUM(ST_Area(ST_Intersection(s.geom, ref.g)::geography)) AS area_m2, "
            "ST_Area(ref.g::geography) AS ref_area_m2, "
            "MAX(dv.dataset_version) AS dv "
            "FROM ref "
            "JOIN soil_unit s ON ST_Intersects(s.geom, ref.g) "
            "JOIN dataset_version dv ON dv.version_id = s.version_id "
            "WHERE dv.is_active = TRUE AND ref.g IS NOT NULL "
            "GROUP BY s.soil_symbol, s.soil_class, s.soil_order, ref.g "
            "ORDER BY area_m2 DESC "
            "LIMIT 10";
        auto res = exec_params(conn->get(), sql.c_str(), gq->params);
        if (res && PQntuples(res->get()) > 0) {
            nlohmann::json soils = nlohmann::json::array();
            double sum_area = 0.0, ref_area = 0.0;
            std::string dv;
            for (int i = 0; i < PQntuples(res->get()); ++i) {
                const double a = std::strtod(PQgetvalue(res->get(), i, 3), nullptr);
                ref_area = std::strtod(PQgetvalue(res->get(), i, 4), nullptr);
                dv = PQgetvalue(res->get(), i, 5);
                sum_area += a;
                soils.push_back({
                    {"soil_symbol", PQgetvalue(res->get(), i, 0)},
                    {"soil_class", PQgetvalue(res->get(), i, 1)},
                    {"soil_order", PQgetvalue(res->get(), i, 2)},
                    {"area_ha", a / 10000.0},
                    {"fraction", ref_area > 0.0 ? a / ref_area : 0.0}
                });
            }
            obs.push_back({
                {"indicator_id", "soil_composition"},
                {"dimension", "Solos"},
                {"value", {{"soils", soils}}},
                {"unit", "ha_and_fraction"},
                {"status", "AVAILABLE"},
                {"source_id", "ibge_bdia_pedologia_250k"},
                {"dataset_version", dv},
                {"reference_date", "2023-08-04"},
                {"native_resolution", "1:250.000"},
                {"method", "postgis_st_intersection_geography_wgs84"},
                {"support", "poligono_efetivo_investigacao"},
                {"coverage_fraction", ref_area > 0.0 ? std::min(1.0, sum_area / ref_area) : 0.0},
                {"limitations", "Escala exploratoria 1:250.000; nao substitui levantamento pedologico local."}
            });
        }
    }

    return obs;
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

    nlohmann::json hydro_features = nlohmann::json::array();
    {
        const std::string hsql =
            "WITH ref AS (SELECT " + gq->geom_expr + " AS g), "
            "vis_env AS ("
            "  SELECT COALESCE(ST_Envelope(ST_Collect(tu.geom)), ST_Envelope(MAX(ref.g))) AS env "
            "  FROM ref "
            "  LEFT JOIN territorial_unit tu ON ST_Intersects(tu.geom, ref.g) "
            "  LEFT JOIN dataset_version dv ON dv.version_id = tu.version_id AND dv.is_active = TRUE "
            "  WHERE ref.g IS NOT NULL"
            ") "
            "SELECT h.feature_kind, COALESCE(h.feature_name, ''), "
            "ST_AsGeoJSON(ST_SimplifyPreserveTopology(h.geom, 0.0003)) "
            "FROM vis_env "
            "JOIN hydro_feature h ON ST_Intersects(h.geom, vis_env.env) "
            "JOIN dataset_version dv ON dv.version_id = h.version_id "
            "WHERE dv.is_active = TRUE AND vis_env.env IS NOT NULL "
            "LIMIT 4000";
        auto hres = exec_params(conn->get(), hsql.c_str(), gq->params);
        if (hres) {
            for (int i = 0; i < PQntuples(hres->get()); ++i) {
                const std::string gstr = PQgetvalue(hres->get(), i, 2);
                if (gstr.empty() || gstr == "null") continue;
                hydro_features.push_back({
                    {"type", "Feature"},
                    {"geometry", nlohmann::json::parse(gstr)},
                    {"properties", {
                        {"feature_kind", PQgetvalue(hres->get(), i, 0)},
                        {"feature_name", PQgetvalue(hres->get(), i, 1)}
                    }}
                });
            }
        }
    }

    return nlohmann::json{
        {"type", "FeatureCollection"},
        {"features", features},
        {"hydro_features", hydro_features},
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

    // Preparar statement para inserção rápida com união explícita de setores multiparte (mesmo CD_SETOR)
    PGresult* prep = PQprepare(
        conn->get(),
        "ins_tu",
        "INSERT INTO territorial_unit "
        "(version_id, unit_code, unit_level, municipality_code, municipality_name, uf_code, "
        "situation_code, situation_label, district_name, neighborhood_name, attributes, geom, area_m2) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11::jsonb, "
        "ST_Multi(ST_SetSRID(ST_GeomFromWKB(decode($12, 'hex')), 4326)), "
        "ST_Area(ST_SetSRID(ST_GeomFromWKB(decode($12, 'hex')), 4326)::geography)) "
        "ON CONFLICT (version_id, unit_code) DO UPDATE SET "
        "geom = ST_Multi(ST_Union(territorial_unit.geom, EXCLUDED.geom)), "
        "area_m2 = ST_Area(ST_Union(territorial_unit.geom, EXCLUDED.geom)::geography) "
        "RETURNING (xmax = 0) AS inserted_new",
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
        if (!ins || PQresultStatus(ins.get()) != PGRES_TUPLES_OK) {
            return rollback_with(std::string("Erro ao inserir setor ") + unit_code_clean + ": " + PQerrorMessage(conn->get()));
        }
        if (PQntuples(ins.get()) > 0 && std::string(PQgetvalue(ins.get(), 0, 0)) == "t") {
            report.inserted_features++;
        } else {
            report.merged_multipart_features++;
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
