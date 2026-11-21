#pragma once
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace venture {
struct DeviceIdentity {
  std::string_view manifest_name, legacy_name, model, device, system_name;
  std::int32_t vrapi_type;
  // Guest renderer identity; API versions, extensions and limits remain capability queries.
  std::string_view gpu_renderer;
  // XR_META_headset_id XrUuidEXT bytes; OVRPlugin classifies the headset by them
  // and falls back to a systemName table that predates Quest 3.
  std::array<std::uint8_t, 16> headset_id;
  // A supportedDevices value Meta's early documentation used for the model.
  std::string_view early_name{};
};
const DeviceIdentity& native_device_identity();
const DeviceIdentity& select_device_identity(std::string_view supported_devices);
// A built-in model by its manifest name ("quest", "quest2", "questpro", "quest3"); null if unknown.
const DeviceIdentity* find_device_identity(std::string_view manifest_name);
// A user-supplied guest system property override (experimental device presentation). Only the product
// identity strings may change (ro.product.* except the cpu ABI, ro.hardware, ro.build.fingerprint,
// ro.build.product); SDK level, security and ABI properties stay Venture's. Values are 1-128 printable
// ASCII characters without ';'. Header-only so the launcher can validate without linking the runtime.
inline bool valid_device_property(std::string_view name, std::string_view value) {
  if (value.empty() || value.size() > 128) return false;
  for (unsigned char c : value)
    if (c < 0x20 || c > 0x7e || c == ';') return false;
  if (name.empty() || name.size() > 80) return false;
  for (unsigned char c : name)
    if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-')) return false;
  if (name == "ro.hardware" || name == "ro.build.fingerprint" || name == "ro.build.product") return true;
  return name.starts_with("ro.product.") && name.find(".cpu.") == name.npos;
}
// Original APK binary XML, not Store build targeting or translated metadata.
std::string manifest_supported_devices(std::span<const std::byte> xml);
} // namespace venture
