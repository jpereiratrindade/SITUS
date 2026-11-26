// SITUS — Sistema de Conhecimento Territorial
// Copyright (C) 2026 José Pedro Trindade
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "situs/application/service.hpp"

namespace situs::http {

// Inicia o servidor HTTP sincronizado/loopback até receber SIGINT/SIGTERM.
int run_http_server(const application::SitusService& service);

} // namespace situs::http
