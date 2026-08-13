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
#include <FalconHTTP/Core/Server.h>        // Server
#include <FalconHTTP/HTTP/HttpResponse.h>  // HttpResponse
#include <FalconHTTP/HTTP/HttpStatus.h>    // HttpStatus
#include <FalconHTTP/Middleware/Cors.h>     // Cors
#include <FalconHTTP/Middleware/Logger.h>   // Logger
#include <FalconHTTP/Middleware/Recovery.h> // Recovery
#include <FalconHTTP/Routing/Router.h>      // Router

#include "Metrics.h"     // Metrics
#include "MetricsJson.h" // metricsToJson
#include "ShrtnDb.h"     // Shrtn::ShrtnDb
#include "routes/UrlRoutes.h" // Shrtn::Routes::postShorten, getRedirect, getLinks, getLinkMeta

#include <iostream> // std::cout, std::cerr
// clang-format on

int main() {
    FalconHTTP::Routing::Router router;

    // Metrics constructed here (not just registered) since /api/health
    // and /api/metrics below both need to reference it.
    // Default capacity (50) is FalconEye's todos-example sizing.
    // Shrtn's /shorten + /:code traffic is expected to run higher
    // volume, so recentRequests would churn out of a 50-slot window
    // within seconds under any real load -- bumped to keep a more
    // useful trailing window. This override lives here, not in
    // Metrics.h, so the copied-unchanged file stays untouched.
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

    // Registered last: /:code is a single-segment catch-all pattern, so
    // it's kept after the more specific literal routes above even though
    // PathMatcher likely also distinguishes by segment count.
    router.get("/:code", [&db](const FalconHTTP::HTTP::HttpRequest& request,
                               FalconHTTP::HTTP::HttpResponse& response) {
        Shrtn::Routes::getRedirect(db, request, response);
    });

    FalconHTTP::Core::Server server(router, /*threadCount=*/4);

    // Onion-model order matters: Recovery outermost so it can catch
    // exceptions from everything inside it; Metrics last so its timing
    // window wraps the full request, including the route handler.
    server.use(FalconHTTP::Middleware::Recovery{});
    server.use(FalconHTTP::Middleware::Logger{});
    server.use(FalconHTTP::Middleware::Cors{});
    server.use(metrics);

    if (!server.start(8080)) {
        std::cerr << "Failed to start server on port 8080\n";
        return 1;
    }

    std::cout << "Shrtn listening on :8080\n";
    server.run();

    return 0;
}
