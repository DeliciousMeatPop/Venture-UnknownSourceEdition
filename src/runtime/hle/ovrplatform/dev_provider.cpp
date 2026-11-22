#include "hle/ovrplatform/dev_provider.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <condition_variable>
#include <mutex>

#include "common/environment.h"
#include "common/log.h"
#include "hle/ovrplatform/identity_iap.h"

namespace venture::ovrplatform {
namespace {
constexpr const char* kDevAlias = "Developer";

bool printable_name(std::string_view s) {
  if (s.empty() || s.size() > 64) return false;
  for (unsigned char c : s)
    if (c < 0x20 || c > 0x7e) return false;
  return true;
}

class DevProvider final : public Provider {
  AppIdentity app_;
  DevSettings settings_;
  // Waits the configured latency; true if the request was cancelled meanwhile.
  bool wait(std::stop_token stop) const {
    if (settings_.latency.count() > 0) {
      std::mutex mutex;
      std::condition_variable_any wake;
      std::unique_lock lock(mutex);
      wake.wait_for(lock, stop, settings_.latency, [] { return false; });
    }
    return stop.stop_requested();
  }
  User user() const { return {settings_.user_id, settings_.alias, settings_.display_name}; }

 public:
  explicit DevProvider(AppIdentity app)
      : app_(std::move(app)),
        settings_(dev_settings([](const char* name) { return environment::getenv(name); })) {}
  Error prepare() override {
    log::warn("accounts",
              "DEV IDENTITY active for {}: local developer user, no Meta account, "
              "entitlement not checked. Enabled by the per-app dev setting.",
              app_.package);
    log::info("accounts", "Dev user id={} alias={} display_name={} answer delay={} ms", settings_.user_id,
              settings_.alias, settings_.display_name, settings_.latency.count());
    for (const auto& problem : settings_.problems) log::warn("accounts", "Dev setting ignored: {}", problem);
    return {};
  }
  void declare_app_id(std::string_view id) override { app_.app_id = std::string(id); }
  Result entitlement(std::stop_token stop) override {
    if (wait(stop)) return {failure(ErrorCode::cancelled)};
    return {};
  }
  Result logged_in_user(std::stop_token stop) override {
    if (wait(stop)) return {failure(ErrorCode::cancelled)};
    return {{}, user()};
  }
  Result access_token(std::stop_token stop) override {
    if (wait(stop)) return {failure(ErrorCode::cancelled)};
    return {{}, user(), "venture-dev-token"};
  }
  Result user_proof(std::stop_token stop) override {
    if (wait(stop)) return {failure(ErrorCode::cancelled)};
    return {{}, {}, "venture-dev-nonce"};
  }
  Result heartbeat(std::string_view, std::stop_token stop) override {
    if (stop.stop_requested()) return {failure(ErrorCode::cancelled)};
    return {{}, {}, "60"};
  }
  HttpResponse graph_request(HttpRequest request, std::stop_token stop) override {
    if (wait(stop)) return {0, {}, failure(ErrorCode::cancelled)};
    return dev_graph_response(request);
  }
  Result service(std::string_view name, const RequestArgs& args,
                 std::stop_token stop) override {
    if (name == "ovr_IAP_LaunchCheckoutFlow") return {unsupported_operation(name)};
    if (args.error) return {args.error};
    // No local purchase history: the durable cache is simply empty.
    if (name == "ovr_IAP_GetViewerPurchasesDurableCache")
      return {{}, {}, {}, Model{"PurchaseArray", {{"NextUrl", ""}}}};
    try {
      auto request = identity_iap_request(name, args);
      auto response = graph_request(request, stop);
      if (response.error) return {response.error};
      auto body = nlohmann::json::parse(response.body);
      return {{}, {}, {}, identity_iap_model(name, body)};
    } catch (const std::invalid_argument&) {
      return {failure(ErrorCode::invalid_argument)};
    } catch (...) {
      return {failure(ErrorCode::invalid_response)};
    }
  }
};
}  // namespace

DevSettings dev_settings(const std::function<const char*(const char*)>& get) {
  DevSettings out;
  auto value = [&](const char* name) -> std::string_view {
    const char* text = get(name);
    return text ? std::string_view(text) : std::string_view();
  };
  if (auto id = value("VENTURE_DEV_USER_ID"); !id.empty()) {
    Id parsed = 0;
    auto [end, error] = std::from_chars(id.data(), id.data() + id.size(), parsed);
    if (error == std::errc{} && end == id.data() + id.size() && parsed && valid_id(id))
      out.user_id = parsed;
    else
      out.problems.push_back("VENTURE_DEV_USER_ID must be a nonzero number of digits");
  }
  if (auto alias = value("VENTURE_DEV_USER_ALIAS"); !alias.empty()) {
    if (printable_name(alias)) out.alias = std::string(alias);
    else out.problems.push_back("VENTURE_DEV_USER_ALIAS must be 1 to 64 printable ASCII characters");
  }
  if (auto name = value("VENTURE_DEV_USER_NAME"); !name.empty()) {
    if (printable_name(name)) out.display_name = std::string(name);
    else out.problems.push_back("VENTURE_DEV_USER_NAME must be 1 to 64 printable ASCII characters");
  }
  if (auto latency = value("VENTURE_DEV_LATENCY_MS"); !latency.empty()) {
    int parsed = -1;
    auto [end, error] = std::from_chars(latency.data(), latency.data() + latency.size(), parsed);
    if (error == std::errc{} && end == latency.data() + latency.size() && parsed >= 0 && parsed <= 10000)
      out.latency = std::chrono::milliseconds(parsed);
    else
      out.problems.push_back("VENTURE_DEV_LATENCY_MS must be a whole number from 0 to 10000");
  }
  return out;
}

HttpResponse dev_graph_response(const HttpRequest& request) {
  const auto& endpoint = request.endpoint;
  if (endpoint == "identity-age")
    return {200, R"({"age_category":"UNKNOWN"})", {}};
  if (endpoint == "identity-user") {
    nlohmann::json body{{"id", request.id},
                        {"alias", kDevAlias},
                        {"presence_status", "UNKNOWN"},
                        {"org_scoped_id", request.id}};
    return {200, body.dump(), {}};
  }
  if (endpoint == "leaderboard-entries")
    return {200, R"({"data":[],"summary":{"total_count":0}})", {}};
  if (endpoint == "leaderboard-write")
    return {200, R"({"did_update":true,"updated_challenge_ids":[]})", {}};
  if (endpoint == "achievement-write") {
    nlohmann::json body{{"api_name", ""}, {"just_unlocked", false}};
    for (const auto& [key, value] : request.parameters)
      if (key == "api_name") {
        body["api_name"] = value;
        break;
      }
    return {200, body.dump(), {}};
  }
  if (endpoint == "social-send-invites") return {200, R"({"invites":[]})", {}};
  if (endpoint == "iap-consume") return {200, R"({})", {}};
  return {200, R"({"data":[]})", {}};
}

bool package_suggests_dev(std::string_view package) {
  size_t start = 0;
  while (start <= package.size()) {
    auto end = package.find('.', start);
    if (end == std::string_view::npos) end = package.size();
    // The last segment is the app's own name, not a namespace.
    if (end != package.size() && package.substr(start, end - start) == "dev") return true;
    start = end + 1;
  }
  return false;
}

std::shared_ptr<Provider> dev_provider(AppIdentity app) {
  return std::make_shared<DevProvider>(std::move(app));
}
}  // namespace venture::ovrplatform
