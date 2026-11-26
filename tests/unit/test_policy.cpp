// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#include "situs/policy/category_a.hpp"
#include <cassert>
#include <iostream>
#include <set>

int main() {
    using namespace situs::policy;

    // 1. Fonte categoria B deve ser rejeitada
    {
        nlohmann::json cat_b = {
            {"source_id", "cnefe_res"},
            {"title", "CNEFE"},
            {"category", "B"},
            {"approval_status", "APPROVED"},
            {"license", "Aberta"},
            {"url", "https://example.org"},
            {"crs", "EPSG:4326"},
            {"allowed_fields", {"ENDERECO"}}
        };
        auto res = validate_category_a_source(cat_b);
        assert(!res.has_value());
    }

    // 2. Fonte categoria A sem licenca deve ser rejeitada
    {
        nlohmann::json no_lic = {
            {"source_id", "sem_lic"},
            {"title", "Sem Licenca"},
            {"category", "A"},
            {"approval_status", "APPROVED"},
            {"license", ""},
            {"url", "https://example.org"},
            {"crs", "EPSG:4326"},
            {"allowed_fields", {"CD_SETOR"}}
        };
        assert(!validate_category_a_source(no_lic).has_value());
    }

    // 3. Fonte categoria A aprovada e filtragem estrita de atributos
    {
        nlohmann::json ok = {
            {"source_id", "ibge_setores_rs_2022"},
            {"title", "Malha RS"},
            {"provider", "IBGE"},
            {"category", "A"},
            {"approval_status", "APPROVED"},
            {"license", "Dados Abertos IBGE"},
            {"url", "https://geoftp.ibge.gov.br/RS_setores_CD2022.gpkg"},
            {"crs", "EPSG:4674"},
            {"allowed_fields", {"CD_SETOR", "CD_MUN"}}
        };
        auto meta = validate_category_a_source(ok);
        assert(meta.has_value());

        nlohmann::json raw = {
            {"CD_SETOR", "431490205000001"},
            {"CD_MUN", "4314902"},
            {"POPULACAO_PROIBIDA", 999},
            {"CPF_HIPOTETICO", "000"}
        };
        auto filtered = filter_allowed_attributes(raw, std::set<std::string>{"CD_SETOR", "CD_MUN"});
        assert(filtered.contains("CD_SETOR"));
        assert(filtered.contains("CD_MUN"));
        assert(!filtered.contains("POPULACAO_PROIBIDA"));
        assert(!filtered.contains("CPF_HIPOTETICO"));
    }

    // 4. Validacao de CRS, coordenadas, municipio e CEP
    assert(validate_crs("EPSG:4326").has_value());
    assert(validate_crs("OGC:CRS84").has_value());
    assert(!validate_crs("EPSG:3857").has_value());
    assert(validate_wgs84_point(-51.23, -30.03).has_value());
    assert(!validate_wgs84_point(-181.0, -30.03).has_value());
    assert(validate_municipality_code("4314902").has_value());
    assert(!validate_municipality_code("43149").has_value());
    assert(validate_cep_syntax("90010-150").has_value());
    assert(!validate_cep_syntax("90010").has_value());

    // 5. Validacao de distrito e grade metrica (SITUS-DIR-002 / EXEC-002)
    assert(validate_district_code("430160217").has_value());
    assert(!validate_district_code("4301602").has_value());
    assert(validate_grid_cell_size_m(500).has_value());
    assert(validate_grid_cell_size_m(1000).has_value());
    assert(validate_grid_cell_size_m(2000).has_value());
    assert(validate_grid_cell_size_m(5000).has_value());
    assert(!validate_grid_cell_size_m(250).has_value());
    {
        auto parsed = parse_grid_cell_id("GRID5880_1000_-384_5556");
        assert(parsed.has_value());
        assert(parsed->cell_size_m == 1000);
        assert(parsed->ix == -384);
        assert(parsed->iy == 5556);
        assert(!parse_grid_cell_id("GRID3857_1000_0_0").has_value());
        assert(!parse_grid_cell_id("GRID5880_250_0_0").has_value());
    }

    std::cout << "[PASS] test_policy\n";
    return 0;
}
