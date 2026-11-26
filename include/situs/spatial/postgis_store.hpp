// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "situs/domain/types.hpp"
#include "situs/policy/category_a.hpp"
#include <expected>
#include <nlohmann/json.hpp>
#include <string>

namespace situs::spatial {

struct IngestReport {
    std::string source_id;
    std::string version_id;
    std::string dataset_version;
    std::string sha256;
    std::size_t read_features = 0;
    std::size_t valid_features = 0;
    std::size_t inserted_features = 0;
    std::size_t merged_multipart_features = 0;
    std::size_t skipped_invalid = 0;
    bool already_ingested = false;
};

class PostgisStore {
public:
    explicit PostgisStore(std::string conninfo);
    ~PostgisStore() = default;

    [[nodiscard]] bool ping() const;
    [[nodiscard]] std::expected<nlohmann::json, std::string> get_catalog() const;

    [[nodiscard]] std::expected<nlohmann::json, std::string>
    resolve_reference(const nlohmann::json& req, const domain::SecurityLimits& limits) const;

    [[nodiscard]] std::expected<nlohmann::json, std::string>
    intersect_territories(const nlohmann::json& req, const domain::SecurityLimits& limits) const;

    [[nodiscard]] std::expected<nlohmann::json, std::string>
    get_map_features(const nlohmann::json& req, const domain::SecurityLimits& limits) const;

    [[nodiscard]] std::expected<nlohmann::json, std::string>
    build_synthesis_inputs(const nlohmann::json& req, const domain::SecurityLimits& limits) const;

    [[nodiscard]] std::expected<IngestReport, std::string>
    ingest_gpkg(
        const std::string& gpkg_path,
        const policy::SourceMetadata& source_meta,
        const std::string& dataset_version,
        const std::string& sha256_hex
    ) const;

private:
    std::string conninfo_;
};

} // namespace situs::spatial
