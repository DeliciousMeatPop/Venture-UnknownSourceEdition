#pragma once
#include <chrono>
#include <functional>
#include <vector>

#include "hle/ovrplatform/provider.h"

namespace venture::ovrplatform {
// Local identity for development builds and apps from other stores.
//
// Selected only by an explicit per-app opt-in (--dev-identity). It never
// contacts Meta, never reads the account vault, and is never reached for an
// app the user did not opt in. Everything it answers is local: a stable
// developer user, a dummy token, empty purchases and canned Graph replies.
// Entitlement is reported only because the user marked this app as a dev
// build; apps that are not opted in keep the real Meta path unchanged.
std::shared_ptr<Provider> dev_provider(AppIdentity);
// What the local developer user looks like to the game, and how quickly it answers. Defaults mimic a real
// Meta user (a long numeric id, a display name) and answer at once. For experiments, the environment may
// override them: VENTURE_DEV_USER_ID (digits), VENTURE_DEV_USER_ALIAS, VENTURE_DEV_USER_NAME (1 to 64
// printable ASCII characters) and VENTURE_DEV_LATENCY_MS (0 to 10000, a delay before every answer, as a
// network round trip would add). Invalid values are ignored and reported in `problems`.
struct DevSettings {
  Id user_id = 5000000000000001;
  std::string alias = "Developer", display_name = "Developer";
  std::chrono::milliseconds latency{0};
  std::vector<std::string> problems;
};
DevSettings dev_settings(const std::function<const char*(const char*)>& get);
// The canned Graph replies, exposed for tests.
HttpResponse dev_graph_response(const HttpRequest&);
// True if the package looks like a dev build (a "dev" dot-segment, e.g.
// com.dev.studio.game or dev.studio.game). Only a suggestion for the launcher
// to offer; it never enables dev identity by itself.
bool package_suggests_dev(std::string_view package);
}  // namespace venture::ovrplatform
