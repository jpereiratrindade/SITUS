// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#include "situs/application/service.hpp"
#include "situs/http/server.hpp"
#include "situs/policy/category_a.hpp"
#include "situs/spatial/postgis_store.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <sstream>
#include <string>

namespace {

std::string env_or(const char* key, const char* fallback) {
    const char* v = std::getenv(key);
    return (v && *v) ? std::string(v) : std::string(fallback);
}

std::string compute_file_sha256(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return "";
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return "";
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    char buf[8192];
    while (ifs.read(buf, sizeof(buf)) || ifs.gcount() > 0) {
        EVP_DigestUpdate(ctx, buf, static_cast<size_t>(ifs.gcount()));
    }
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_DigestFinal_ex(ctx, hash, &len);
    EVP_MD_CTX_free(ctx);
    std::ostringstream oss;
    for (unsigned int i = 0; i < len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

situs::application::AppConfig load_config(bool admin_mode) {
    situs::application::AppConfig cfg;
    cfg.http_host = env_or("SITUS_HTTP_HOST", "127.0.0.1");
    cfg.http_port = static_cast<unsigned short>(std::stoi(env_or("SITUS_HTTP_PORT", "8096")));
    cfg.web_dir = env_or("SITUS_WEB_DIR", "web");
    cfg.execution_config_path = env_or("SITUS_EXECUTION_CONFIG", "config/execution.example.json");
    cfg.wasm_artifact_path = cfg.web_dir + "/wasm/situs_core.wasm";

    const std::string db_host = env_or("SITUS_DB_HOST", "127.0.0.1");
    const std::string db_port = env_or("SITUS_DB_PORT", "55436");
    const std::string db_name = env_or("SITUS_DB_NAME", "situs");
    const std::string db_user = admin_mode
        ? env_or("SITUS_DB_ADMIN_USER", "situs_migrator")
        : env_or("SITUS_DB_QUERY_USER", "situs_reader");
    const std::string db_pass = admin_mode
        ? env_or("SITUS_DB_ADMIN_PASSWORD", "change_me_migrator_local")
        : env_or("SITUS_DB_QUERY_PASSWORD", "change_me_reader_local");

    cfg.db_conninfo = "host=" + db_host + " port=" + db_port + " dbname=" + db_name +
                      " user=" + db_user + " password=" + db_pass;
    return cfg;
}

void print_usage() {
    std::cout << "Uso: situs <comando> [opcoes]\n"
              << "Comandos:\n"
              << "  serve                             Iniciar servidor web/API local (127.0.0.1:8096)\n"
              << "  health                            Verificar saude do PostGIS e artefato Wasm\n"
              << "  catalog                           Listar fontes e camadas cadastradas\n"
              << "  ingest --file <gpkg> --source <sources.json> --source-id <id> --version <ver>\n"
              << "                                    Ingerir GeoPackage Categoria A aprovado (idempotente)\n"
              << "  intersect --municipality <ibge7>  Testar intersecao territorial por municipio\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const std::string cmd = argv[1];

    if (cmd == "serve") {
        situs::application::SitusService service(load_config(false));
        return situs::http::run_http_server(service);
    }

    if (cmd == "health") {
        situs::application::SitusService service(load_config(false));
        std::cout << service.health().dump(2) << "\n";
        return 0;
    }

    if (cmd == "catalog") {
        situs::application::SitusService service(load_config(false));
        auto res = service.catalog();
        if (!res) {
            std::cerr << "Erro: " << res.error() << "\n";
            return 1;
        }
        std::cout << res->dump(2) << "\n";
        return 0;
    }

    if (cmd == "intersect") {
        if (argc < 4 || std::string(argv[2]) != "--municipality") {
            std::cerr << "Uso: situs intersect --municipality <ibge7>\n";
            return 1;
        }
        situs::application::SitusService service(load_config(false));
        nlohmann::json req = {
            {"kind", "MUNICIPALITY"},
            {"value", std::string(argv[3])},
            {"crs", "EPSG:4326"}
        };
        auto res = service.intersect_territories(req);
        if (!res) {
            std::cerr << "Erro: " << res.error() << "\n";
            return 1;
        }
        std::cout << res->dump(2) << "\n";
        return 0;
    }

    if (cmd == "ingest") {
        std::string file_path;
        std::string sources_path = "config/sources.example.json";
        std::string source_id = "ibge_setores_rs_2022";
        std::string dataset_version = "2022.1";

        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--file" && i + 1 < argc) file_path = argv[++i];
            else if (arg == "--source" && i + 1 < argc) sources_path = argv[++i];
            else if (arg == "--source-id" && i + 1 < argc) source_id = argv[++i];
            else if (arg == "--version" && i + 1 < argc) dataset_version = argv[++i];
        }

        if (file_path.empty()) {
            std::cerr << "Erro: --file <caminho.gpkg> e obrigatorio\n";
            return 1;
        }

        std::ifstream ifs(sources_path);
        if (!ifs) {
            std::cerr << "Erro: nao foi possivel abrir " << sources_path << "\n";
            return 1;
        }
        nlohmann::json cat;
        ifs >> cat;

        nlohmann::json target_src;
        for (const auto& s : cat.value("sources", nlohmann::json::array())) {
            if (s.value("source_id", "") == source_id) {
                target_src = s;
                break;
            }
        }
        auto meta = situs::policy::validate_category_a_source(target_src);
        if (!meta) {
            std::cerr << "Rejeitado pela politica Categoria A: " << meta.error() << "\n";
            return 2;
        }

        const std::string sha = compute_file_sha256(file_path);
        if (sha.empty()) {
            std::cerr << "Erro ao calcular SHA-256 de " << file_path << "\n";
            return 1;
        }

        auto cfg = load_config(true);
        situs::spatial::PostgisStore admin_store(cfg.db_conninfo);
        auto rep = admin_store.ingest_gpkg(file_path, *meta, dataset_version, sha);
        if (!rep) {
            std::cerr << "Erro na ingestao: " << rep.error() << "\n";
            return 1;
        }

        nlohmann::json out = {
            {"source_id", rep->source_id},
            {"version_id", rep->version_id},
            {"dataset_version", rep->dataset_version},
            {"sha256", rep->sha256},
            {"read_features", rep->read_features},
            {"valid_features", rep->valid_features},
            {"inserted_features", rep->inserted_features},
            {"skipped_invalid", rep->skipped_invalid},
            {"already_ingested", rep->already_ingested}
        };
        std::cout << out.dump(2) << "\n";
        return 0;
    }

    print_usage();
    return 1;
}
