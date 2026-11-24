// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/crypto.h"
#include "common/privatebin.h"
#include <doctest/doctest.h>
#include <numeric>
#include <string>
using namespace venture;
namespace {
std::string hex(const crypto::Bytes &b) {
  static const char *d = "0123456789abcdef";
  std::string s;
  for (auto c : b) { s += d[c >> 4]; s += d[c & 15]; }
  return s;
}
crypto::Bytes seq(int from, int n) { crypto::Bytes b(n); std::iota(b.begin(), b.end(), from); return b; }
std::span<const std::uint8_t> view(const std::string &s) { return {reinterpret_cast<const std::uint8_t *>(s.data()), s.size()}; }
} // namespace
// Expected values were produced by Python's hashlib and the cryptography package.
TEST_CASE("PBKDF2-SHA256 and AES-256-GCM match known answers") {
  auto pw = seq(0, 32), salt = seq(100, 8), nonce = seq(200, 12), key = seq(7, 32);
  CHECK(hex(crypto::pbkdf2_sha256(pw, salt, 100000, 32)) == "30b1106c7a8413471fcc0d56191f9ca3c062b5486830bebb758d321e9ad689d2");
  std::string aad = R"([["x","y",100000,256,128,"aes","gcm","zlib"],"plaintext",0,0])", plain = "hello privatebin";
  auto sealed = crypto::aes256_gcm_encrypt(key, nonce, view(aad), view(plain));
  CHECK(hex(sealed) == "b4fd1597c0c93c7983e89141b792fe9fdcbce5fc9ac63d8e16aee1137716fb9f");
  CHECK(crypto::aes256_gcm_decrypt(key, nonce, view(aad), sealed) == crypto::Bytes(plain.begin(), plain.end()));
  auto bad = sealed; bad.back() ^= 1;
  CHECK_THROWS(crypto::aes256_gcm_decrypt(key, nonce, view(aad), bad));
  CHECK_THROWS(crypto::aes256_gcm_decrypt(key, nonce, view(plain), sealed));
  CHECK(crypto::random_bytes(16) != crypto::random_bytes(16));
}
TEST_CASE("PrivateBin encodings and paste round trip") {
  CHECK(privatebin::base64(crypto::Bytes{}) == "");
  CHECK(privatebin::base64(crypto::Bytes{'f'}) == "Zg==");
  CHECK(privatebin::base64(crypto::Bytes{'f', 'o'}) == "Zm8=");
  CHECK(privatebin::base64(crypto::Bytes{'f', 'o', 'o'}) == "Zm9v");
  CHECK(privatebin::base58(crypto::Bytes{0, 0, 1}) == "112");
  CHECK(privatebin::base58(crypto::Bytes{'h', 'e', 'l', 'l', 'o'}) == "Cn8eVZg");
  std::string text = "report cafÃ© "quoted" \ tab	
";
  for (int i = 0; i < 2000; ++i) text += "12345.678 I [launch] repeated log line " + std::to_string(i) + "
";
  auto enc = privatebin::encrypt(text, "1month");
  CHECK(privatebin::decrypt(enc.body, enc.fragment) == text);
  auto other = privatebin::encrypt(text, "1month");
  CHECK_THROWS(privatebin::decrypt(enc.body, other.fragment));
  auto odd = privatebin::encrypt("bad byte ÿþ here", "1day");
  CHECK(privatebin::decrypt(odd.body, odd.fragment).starts_with("bad byte"));
}
