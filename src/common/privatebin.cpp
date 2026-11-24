// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/privatebin.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
#include <miniz.h>
#include <nlohmann/json.hpp>
#include "common/crypto.h"
#include "common/http/client.h"

namespace venture::privatebin {
namespace {
using Json = nlohmann::json;
using crypto::Bytes;
constexpr std::uint32_t kIterations = 100000;
constexpr std::size_t kKeyBytes = 32, kSaltBytes = 8, kNonceBytes = 12;
constexpr char kBase58[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
constexpr char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

Bytes base64_decode(std::string_view text) {
  Bytes out;
  std::uint32_t buffer = 0;
  int bits = 0;
  for (char c : text) {
    if (c == '=') break;
    const char *at = std::strchr(kBase64, c);
    if (!at || !c) throw std::runtime_error("invalid base64");
    buffer = (buffer << 6) | std::uint32_t(at - kBase64);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(std::uint8_t((buffer >> bits) & 0xff));
    }
  }
  return out;
}
Bytes base58_decode(std::string_view text) {
  Bytes out;  // big-endian, built by multiplying the number so far by 58
  std::size_t zeros = 0;
  while (zeros < text.size() && text[zeros] == '1') ++zeros;
  for (char c : text) {
    const char *at = std::strchr(kBase58, c);
    if (!at || !c) throw std::runtime_error("invalid base58");
    std::uint32_t carry = std::uint32_t(at - kBase58);
    for (auto it = out.rbegin(); it != out.rend(); ++it) {
      carry += std::uint32_t(*it) * 58;
      *it = std::uint8_t(carry & 0xff);
      carry >>= 8;
    }
    while (carry) {
      out.insert(out.begin(), std::uint8_t(carry & 0xff));
      carry >>= 8;
    }
  }
  while (!out.empty() && out.front() == 0) out.erase(out.begin());
  out.insert(out.begin(), zeros, std::uint8_t(0));
  return out;
}
// The deflate stream PrivateBin uses: raw, with no zlib header.
Bytes deflate_raw(std::string_view text) {
  size_t size = 0;
  const int flags = int(tdefl_create_comp_flags_from_zip_params(MZ_DEFAULT_LEVEL, -MZ_DEFAULT_WINDOW_BITS,
                                                               MZ_DEFAULT_STRATEGY));
  std::unique_ptr<void, void (*)(void *)> out(tdefl_compress_mem_to_heap(text.data(), text.size(), &size, flags),
                                              mz_free);
  if (!out) throw std::runtime_error("compression failed");
  const auto *begin = static_cast<const std::uint8_t *>(out.get());
  return Bytes(begin, begin + size);
}
std::string inflate_raw(std::span<const std::uint8_t> data) {
  size_t size = 0;
  std::unique_ptr<void, void (*)(void *)> out(tinfl_decompress_mem_to_heap(data.data(), data.size(), &size, 0),
                                              mz_free);
  if (!out) throw std::runtime_error("decompression failed");
  return std::string(static_cast<const char *>(out.get()), size);
}
// The authenticated data: what JSON.stringify gives PrivateBin's page, compact and in this order.
Json adata_for(std::span<const std::uint8_t> nonce, std::span<const std::uint8_t> salt) {
  return Json::array({Json::array({base64(nonce), base64(salt), kIterations, 256, 128, "aes", "gcm", "zlib"}),
                      "plaintext", 0, 0});
}
Bytes derive(std::span<const std::uint8_t> key, std::span<const std::uint8_t> salt) {
  return crypto::pbkdf2_sha256(key, salt, kIterations, kKeyBytes);
}
// A server's short reason, safe to show.
std::string reason(const Json &reply) {
  std::string text = reply.is_object() && reply.contains("message") && reply["message"].is_string()
                         ? reply["message"].get<std::string>() : std::string();
  std::string clean;
  for (unsigned char c : text.substr(0, 120)) clean += (c >= 0x20 && c < 0x7f) ? char(c) : '?';
  return clean;
}
bool valid_id(std::string_view id) {
  return !id.empty() && id.size() <= 64 &&
         std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) != 0; });
}
} // namespace

std::string base64(std::span<const std::uint8_t> bytes) {
  std::string out;
  std::uint32_t buffer = 0;
  int bits = 0;
  for (auto byte : bytes) {
    buffer = (buffer << 8) | byte;
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      out += kBase64[(buffer >> bits) & 63];
    }
  }
  if (bits) out += kBase64[(buffer << (6 - bits)) & 63];
  while (out.size() % 4) out += '=';
  return out;
}
std::string base58(std::span<const std::uint8_t> bytes) {
  std::size_t zeros = 0;
  while (zeros < bytes.size() && bytes[zeros] == 0) ++zeros;
  std::vector<std::uint8_t> digits;  // little-endian base-58 digits
  for (auto byte : bytes) {
    std::uint32_t carry = byte;
    for (auto &digit : digits) {
      carry += std::uint32_t(digit) << 8;
      digit = std::uint8_t(carry % 58);
      carry /= 58;
    }
    while (carry) {
      digits.push_back(std::uint8_t(carry % 58));
      carry /= 58;
    }
  }
  std::string out(zeros, '1');
  for (auto it = digits.rbegin(); it != digits.rend(); ++it) out += kBase58[*it];
  return out;
}

Encrypted encrypt(std::string_view text, std::string_view expire, std::span<const std::uint8_t> key,
                  std::span<const std::uint8_t> salt, std::span<const std::uint8_t> nonce) {
  if (key.size() != kKeyBytes || salt.size() != kSaltBytes || nonce.size() != kNonceBytes)
    throw std::invalid_argument("bad key, salt or nonce size");
  // PrivateBin stores {"paste": text}; invalid UTF-8 in a log becomes U+FFFD instead of failing.
  const std::string payload = Json{{"paste", text}}.dump(-1, ' ', false, Json::error_handler_t::replace);
  const Json adata = adata_for(nonce, salt);
  const std::string aad = adata.dump();
  const Bytes packed = deflate_raw(payload);
  const Bytes sealed = crypto::aes256_gcm_encrypt(derive(key, salt),
                                                  nonce, std::span<const std::uint8_t>(
                                                      reinterpret_cast<const std::uint8_t *>(aad.data()), aad.size()),
                                                  packed);
  Json body{{"v", 2}, {"adata", adata}, {"ct", base64(sealed)}, {"meta", {{"expire", std::string(expire)}}}};
  return {body.dump(), base58(key)};
}
Encrypted encrypt(std::string_view text, std::string_view expire) {
  return encrypt(text, expire, crypto::random_bytes(kKeyBytes), crypto::random_bytes(kSaltBytes),
                 crypto::random_bytes(kNonceBytes));
}
std::string decrypt(std::string_view body, std::string_view fragment) {
  const Json request = Json::parse(body);
  const Json &adata = request.at("adata");
  const Json &cipher = adata.at(0);
  if (request.at("v") != 2 || cipher.at(2).get<std::uint32_t>() != kIterations || cipher.at(3) != 256 ||
      cipher.at(4) != 128 || cipher.at(5) != "aes" || cipher.at(6) != "gcm" || cipher.at(7) != "zlib")
    throw std::runtime_error("unsupported paste format");
  const Bytes key = base58_decode(fragment);
  const Bytes salt = base64_decode(cipher.at(1).get<std::string>());
  const Bytes nonce = base64_decode(cipher.at(0).get<std::string>());
  const std::string aad = adata.dump();
  const Bytes packed = crypto::aes256_gcm_decrypt(
      derive(key, salt), nonce, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(aad.data()), aad.size()),
      base64_decode(request.at("ct").get<std::string>()));
  return Json::parse(inflate_raw(packed)).at("paste").get<std::string>();
}

std::string paste(std::string_view server, std::string_view text, std::string_view expire, std::stop_token stop) {
  if (!server.starts_with("https://") || server.ends_with('/')) throw std::invalid_argument("bad PrivateBin address");
  const Encrypted sealed = encrypt(text, expire);
  const auto reply_http = http::post_body(std::string(server) + "/", sealed.body, "application/json", stop,
                                          {{"X-Requested-With", "JSONHttpRequest"}});
  Json reply = Json::parse(reply_http.body, nullptr, false);
  if (reply_http.status != 200 || !reply.is_object() || reply.value("status", 1) != 0 ||
      !reply.contains("id") || !reply["id"].is_string() || !valid_id(reply["id"].get<std::string>())) {
    std::string why = reason(reply);
    if (!why.empty()) throw std::runtime_error("The upload service refused it: " + why);
    // Not a PrivateBin answer (a proxy or firewall in front of it): show how it began, in one line.
    std::string excerpt;
    for (char c : reply_http.body) {
      if (excerpt.size() >= 120) break;
      if (c == '\r' || c == '\n' || c == '\t') c = ' ';
      if (static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7f && !(c == ' ' && (excerpt.empty() || excerpt.back() == ' ')))
        excerpt += c;
    }
    throw std::runtime_error("The upload service answered " + std::to_string(reply_http.status) +
                             (excerpt.empty() ? "." : ": " + excerpt));
  }
  return std::string(server) + "/?" + reply["id"].get<std::string>() + "#" + sealed.fragment;
}
} // namespace venture::privatebin
