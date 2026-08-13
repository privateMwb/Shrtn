/**
 * @file            main.cpp
 *
 * @date            2026-8-12
 *
 * @version         0.1.0
 *
 * @copyright       Copyright (c) 2026 privateMwb
 *                  All rights reserved.
 *                  https://github.com/privateMwb/Shrtn
 *
 * @attention       This source is released under the MIT license
 *                  SPDX-License-Identifier: MIT
 *                  <http://opensource.org/licenses/MIT>
 */

// Phase 1: server bootstrap only. No shortener logic yet -- just proves
// the middleware chain and /api/metrics observability (reused unchanged
// from FalconEye) work before any real routes are added in Phase 2.

// clang-format off
#include <FalconHTTP/Core/Server.h>
#include <FalconHTTP/HTTP/HttpResponse.h>
#include <FalconHTTP/HTTP/HttpStatus.h>
#include <FalconHTTP/Middleware/Cors.h>
#include <FalconHTTP/Middleware/Logger.h>
#include <FalconHTTP/Middleware/Recovery.h>
#include <FalconHTTP/Routing/Router.h>

#include "Metrics.h"
#include "MetricsJson.h"
#include "ShrtnDb.h"
#include "routes/UrlRoutes.h"

#include <cstdlib>
#include <iostream>
// clang-format on

int main() {
    FalconHTTP::Routing::Router router;

    Metrics metrics(router, /*bufferCapacity=*/200);

    // "./data" must already exist -- StorageEngine does not create it.
    Shrtn::ShrtnDb db("./data");
    if (db.init() != MiniDB::Common::Status::OK) {
        std::cerr << "Failed to initialize database\n";
        return 1;
    }

    router.get("/api/health",
               [](const FalconHTTP::HTTP::HttpRequest&, FalconHTTP::HTTP::HttpResponse& response) {
                   response.setStatus(FalconHTTP::HTTP::HttpStatus::Ok);
                   response.setBody("ok");
               });

    router.get("/api/metrics", [&metrics](const FalconHTTP::HTTP::HttpRequest&,
                                          FalconHTTP::HTTP::HttpResponse& response) {
        response.setJson(metricsToJson(metrics));
    });

    router.post("/shorten", [&db](const FalconHTTP::HTTP::HttpRequest& request,
                                  FalconHTTP::HTTP::HttpResponse& response) {
        Shrtn::Routes::postShorten(db, request, response);
    });

    router.get("/links", [&db](const FalconHTTP::HTTP::HttpRequest& request,
                               FalconHTTP::HTTP::HttpResponse& response) {
        Shrtn::Routes::getLinks(db, request, response);
    });

    router.get("/api/links/:code", [&db](const FalconHTTP::HTTP::HttpRequest& request,
                                         FalconHTTP::HTTP::HttpResponse& response) {
        Shrtn::Routes::getLinkMeta(db, request, response);
    });

    router.get("/:code", [&db](const FalconHTTP::HTTP::HttpRequest& request,
                               FalconHTTP::HTTP::HttpResponse& response) {
        Shrtn::Routes::getRedirect(db, request, response);
    });

    FalconHTTP::Core::Server server(router, /*threadCount=*/4);

    server.use(FalconHTTP::Middleware::Recovery{});
    server.use(FalconHTTP::Middleware::Logger{});
    server.use(FalconHTTP::Middleware::Cors{});
    server.use(metrics);

    // Render provides the PORT environment variable.
    // Fall back to 8080 for local development.
    const char* portEnv = std::getenv("PORT");
    const int port = portEnv ? std::atoi(portEnv) : 8080;

    if (!server.start(port)) {
        std::cerr << "Failed to start server on port " << port << "\n";
        return 1;
    }

    std::cout << "Shrtn listening on :" << port << "\n";

    server.run();

    return 0;
}