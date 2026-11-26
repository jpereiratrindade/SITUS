// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#include "situs/http/server.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

namespace situs::http {

namespace beast = boost::beast;
namespace bhttp = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

namespace {

volatile std::sig_atomic_t g_stop = 0;

void handle_signal(int) {
    g_stop = 1;
}

std::string mime_type_for(const std::filesystem::path& p) {
    const auto ext = p.extension().string();
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js" || ext == ".mjs") return "application/javascript; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".wasm") return "application/wasm";
    if (ext == ".png") return "image/png";
    return "application/octet-stream";
}

void apply_security_headers(bhttp::response<bhttp::string_body>& res) {
    // CSP restritiva sem CDN, sem fontes externas e sem 'unsafe-eval'
    res.set(bhttp::field::content_security_policy,
        "default-src 'self'; "
        "script-src 'self' 'wasm-unsafe-eval'; "
        "worker-src 'self'; "
        "style-src 'self'; "
        "img-src 'self' data:; "
        "connect-src 'self'; "
        "object-src 'none'; "
        "base-uri 'self'; "
        "frame-ancestors 'none'");
    res.set("X-Content-Type-Options", "nosniff");
    res.set("Referrer-Policy", "no-referrer");
    res.set(bhttp::field::cache_control, "no-store");
}

bhttp::response<bhttp::string_body> make_json_response(
    bhttp::status status,
    const nlohmann::json& body,
    unsigned version,
    bool keep_alive
) {
    bhttp::response<bhttp::string_body> res{status, version};
    res.set(bhttp::field::content_type, "application/json; charset=utf-8");
    res.keep_alive(keep_alive);
    apply_security_headers(res);
    res.body() = body.dump();
    res.prepare_payload();
    return res;
}

bhttp::response<bhttp::string_body> make_error_response(
    bhttp::status status,
    const std::string& code,
    const std::string& message,
    unsigned version,
    bool keep_alive
) {
    return make_json_response(
        status,
        {{"error", {{"code", code}, {"message", message}}}},
        version,
        keep_alive
    );
}

std::optional<std::filesystem::path> resolve_safe_static_path(
    const std::filesystem::path& web_root,
    const std::string& target
) {
    std::string clean = target;
    auto qpos = clean.find('?');
    if (qpos != std::string::npos) clean.resize(qpos);
    if (clean == "/" || clean.empty()) clean = "/index.html";
    if (clean.find("..") != std::string::npos) return std::nullopt;
    while (!clean.empty() && clean.front() == '/') clean.erase(clean.begin());

    std::error_code ec;
    auto root_canon = std::filesystem::weakly_canonical(web_root, ec);
    if (ec) return std::nullopt;
    auto full_canon = std::filesystem::weakly_canonical(root_canon / clean, ec);
    if (ec) return std::nullopt;

    auto [it_r, it_f] = std::mismatch(root_canon.begin(), root_canon.end(), full_canon.begin(), full_canon.end());
    if (it_r != root_canon.end()) return std::nullopt;
    if (!std::filesystem::exists(full_canon, ec) || !std::filesystem::is_regular_file(full_canon, ec)) {
        return std::nullopt;
    }
    return full_canon;
}

bhttp::response<bhttp::string_body> handle_request(
    const application::SitusService& service,
    const bhttp::request<bhttp::string_body>& req
) {
    const unsigned ver = req.version();
    const bool ka = req.keep_alive();
    std::string target(req.target());
    if (auto q = target.find('?'); q != std::string::npos) target.resize(q);

    // Log mínimo sem payload, sem CEP, sem coordenadas e sem query string
    std::cerr << "[situs-http] " << req.method_string() << " " << target << "\n";

    // Bloqueio estrito de origem externa quando header Origin está presente
    if (auto origin_it = req.find(bhttp::field::origin); origin_it != req.end()) {
        const std::string origin(origin_it->value());
        const std::string expected_http = "http://" + service.config().http_host + ":" + std::to_string(service.config().http_port);
        const std::string expected_localhost = "http://localhost:" + std::to_string(service.config().http_port);
        if (origin != expected_http && origin != expected_localhost) {
            return make_error_response(bhttp::status::forbidden, "ORIGIN_FORBIDDEN", "Origem nao autorizada", ver, ka);
        }
    }

    if (req.method() == bhttp::verb::get && target == "/health") {
        return make_json_response(bhttp::status::ok, service.health(), ver, ka);
    }

    if (req.method() == bhttp::verb::get && target == "/api/v1/catalog") {
        auto res = service.catalog();
        if (!res) return make_error_response(bhttp::status::service_unavailable, "CATALOG_UNAVAILABLE", res.error(), ver, ka);
        return make_json_response(bhttp::status::ok, *res, ver, ka);
    }

    if (req.method() == bhttp::verb::get && target == "/api/v1/execution/capabilities") {
        return make_json_response(bhttp::status::ok, service.execution_capabilities(), ver, ka);
    }

    if (req.method() == bhttp::verb::post && target.starts_with("/api/v1/")) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body());
        } catch (...) {
            return make_error_response(bhttp::status::bad_request, "INVALID_JSON", "Corpo JSON malformado", ver, ka);
        }

        if (target == "/api/v1/references/resolve") {
            auto res = service.resolve_reference(body);
            if (!res) return make_error_response(bhttp::status::unprocessable_entity, "REFERENCE_REJECTED", res.error(), ver, ka);
            return make_json_response(bhttp::status::ok, *res, ver, ka);
        }
        if (target == "/api/v1/territories/intersect") {
            auto res = service.intersect_territories(body);
            if (!res) return make_error_response(bhttp::status::unprocessable_entity, "INTERSECT_REJECTED", res.error(), ver, ka);
            return make_json_response(bhttp::status::ok, *res, ver, ka);
        }
        if (target == "/api/v1/territories/characterize") {
            auto res = service.characterize_territories(body);
            if (!res) return make_error_response(bhttp::status::unprocessable_entity, "CHARACTERIZE_REJECTED", res.error(), ver, ka);
            return make_json_response(bhttp::status::ok, *res, ver, ka);
        }
        if (target == "/api/v1/map/features") {
            auto res = service.map_features(body);
            if (!res) return make_error_response(bhttp::status::unprocessable_entity, "MAP_FEATURES_REJECTED", res.error(), ver, ka);
            return make_json_response(bhttp::status::ok, *res, ver, ka);
        }
        if (target == "/api/v1/execution/inputs") {
            auto res = service.execution_inputs(body);
            if (!res) return make_error_response(bhttp::status::unprocessable_entity, "EXECUTION_INPUTS_REJECTED", res.error(), ver, ka);
            return make_json_response(bhttp::status::ok, *res, ver, ka);
        }
        if (target == "/api/v1/execution/run") {
            auto res = service.execution_run(body);
            const auto st = res.value("status", "") == "COMPLETED" ? bhttp::status::ok : bhttp::status::unprocessable_entity;
            return make_json_response(st, res, ver, ka);
        }
        return make_error_response(bhttp::status::not_found, "ROUTE_NOT_FOUND", "Rota API desconhecida", ver, ka);
    }

    if (req.method() == bhttp::verb::get || req.method() == bhttp::verb::head) {
        auto file_path = resolve_safe_static_path(service.config().web_dir, target);
        if (!file_path) {
            return make_error_response(bhttp::status::not_found, "NOT_FOUND", "Recurso nao encontrado", ver, ka);
        }
        std::ifstream ifs(*file_path, std::ios::binary);
        if (!ifs) {
            return make_error_response(bhttp::status::internal_server_error, "READ_ERROR", "Falha ao ler arquivo estatico", ver, ka);
        }
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        bhttp::response<bhttp::string_body> res{bhttp::status::ok, ver};
        res.set(bhttp::field::content_type, mime_type_for(*file_path));
        res.keep_alive(ka);
        apply_security_headers(res);
        if (req.method() == bhttp::verb::get) {
            res.body() = std::move(content);
        }
        res.prepare_payload();
        return res;
    }

    return make_error_response(bhttp::status::method_not_allowed, "METHOD_NOT_ALLOWED", "Metodo nao permitido", ver, ka);
}

void do_session(tcp::socket socket, const application::SitusService& service) {
    beast::error_code ec;
    beast::tcp_stream stream(std::move(socket));
    beast::flat_buffer buffer;

    for (;;) {
        stream.expires_after(std::chrono::seconds(30));
        bhttp::request_parser<bhttp::string_body> parser;
        parser.header_limit(16 * 1024);
        parser.body_limit(service.config().limits.max_body_bytes);

        bhttp::read(stream, buffer, parser, ec);
        if (ec == bhttp::error::end_of_stream) break;
        if (ec) {
            if (ec == bhttp::error::body_limit) {
                auto err_res = make_error_response(bhttp::status::payload_too_large, "BODY_LIMIT_EXCEEDED", "Corpo excede limite configurado", 11, false);
                bhttp::write(stream, err_res, ec);
            }
            break;
        }

        auto req = parser.release();
        auto res = handle_request(service, req);
        const bool keep = res.keep_alive();
        bhttp::write(stream, res, ec);
        if (ec || !keep) break;
    }
    stream.socket().shutdown(tcp::socket::shutdown_send, ec);
}

} // namespace

int run_http_server(const application::SitusService& service) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    try {
        const auto addr = net::ip::make_address(service.config().http_host);
        if (!addr.is_loopback()) {
            std::cerr << "[situs] ERRO: Bind permitido apenas em endereco loopback (127.0.0.1)\n";
            return 1;
        }
        net::io_context ioc{1};
        tcp::acceptor acceptor{ioc, {addr, service.config().http_port}};
        acceptor.non_blocking(true);

        std::cerr << "[situs] Servidor ouvindo em http://"
                  << service.config().http_host << ":" << service.config().http_port << "\n";

        while (!g_stop) {
            beast::error_code ec;
            tcp::socket socket{ioc};
            acceptor.accept(socket, ec);
            if (ec == net::error::would_block) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            if (ec) {
                std::cerr << "[situs] Erro accept: " << ec.message() << "\n";
                continue;
            }
            std::thread(&do_session, std::move(socket), std::cref(service)).detach();
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[situs] Excecao fatal HTTP: " << e.what() << "\n";
        return 1;
    }
}

} // namespace situs::http
