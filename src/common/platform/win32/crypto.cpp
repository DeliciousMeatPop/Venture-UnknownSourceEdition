// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/crypto.h"
#include <limits>
#include <stdexcept>
#include <windows.h>
#include <bcrypt.h>

namespace venture::crypto {
namespace {
constexpr std::size_t kTag = 16, kNonce = 12, kKey = 32;
constexpr NTSTATUS kAuthTagMismatch = static_cast<NTSTATUS>(0xC000A002L);

ULONG checked(std::size_t size) {
  if (size > std::numeric_limits<ULONG>::max()) throw std::runtime_error("crypto input is too large");
  return static_cast<ULONG>(size);
}
// A pointer a CNG call accepts for an empty span.
PUCHAR bytes(std::span<const std::uint8_t> data) {
  static UCHAR none = 0;
  return data.empty() ? &none : const_cast<PUCHAR>(data.data());
}
struct Algorithm {
  BCRYPT_ALG_HANDLE handle = nullptr;
  Algorithm(LPCWSTR name, ULONG flags) {
    if (BCryptOpenAlgorithmProvider(&handle, name, nullptr, flags) < 0)
      throw std::runtime_error("cannot open crypto provider");
  }
  ~Algorithm() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
  Algorithm(const Algorithm&) = delete;
  Algorithm& operator=(const Algorithm&) = delete;
};
struct Key {
  BCRYPT_KEY_HANDLE handle = nullptr;
  ~Key() { if (handle) BCryptDestroyKey(handle); }
};
Bytes gcm(bool encrypt, std::span<const std::uint8_t> key, std::span<const std::uint8_t> nonce,
          std::span<const std::uint8_t> aad, std::span<const std::uint8_t> input) {
  if (key.size() != kKey || nonce.size() != kNonce) throw std::invalid_argument("bad AES-GCM key or nonce size");
  if (!encrypt && input.size() < kTag) throw std::runtime_error("AES-GCM input is shorter than its tag");
  Algorithm algorithm(BCRYPT_AES_ALGORITHM, 0);
  if (BCryptSetProperty(algorithm.handle, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<PWSTR>(BCRYPT_CHAIN_MODE_GCM)),
                        sizeof(BCRYPT_CHAIN_MODE_GCM), 0) < 0)
    throw std::runtime_error("cannot select AES-GCM");
  Key aes;
  if (BCryptGenerateSymmetricKey(algorithm.handle, &aes.handle, nullptr, 0, bytes(key), checked(key.size()), 0) < 0)
    throw std::runtime_error("cannot create AES key");
  const auto body = encrypt ? input : input.first(input.size() - kTag);
  Bytes out(body.size() + (encrypt ? kTag : 0));
  Bytes tag(kTag);
  if (!encrypt) tag.assign(input.end() - kTag, input.end());
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
  BCRYPT_INIT_AUTH_MODE_INFO(info);
  info.pbNonce = bytes(nonce);
  info.cbNonce = checked(nonce.size());
  info.pbAuthData = aad.empty() ? nullptr : bytes(aad);
  info.cbAuthData = checked(aad.size());
  info.pbTag = tag.data();
  info.cbTag = static_cast<ULONG>(kTag);
  ULONG written = 0;
  if (encrypt) {
    UCHAR empty = 0;
    NTSTATUS status = BCryptEncrypt(aes.handle, body.empty() ? &empty : bytes(body), checked(body.size()), &info,
                                    nullptr, 0, out.data(), checked(body.size()), &written, 0);
    if (status < 0) throw std::runtime_error("AES-GCM failed");
    out.resize(written);
    out.insert(out.end(), tag.begin(), tag.end());
  } else {
    UCHAR empty = 0;
    NTSTATUS status = BCryptDecrypt(aes.handle, body.empty() ? &empty : bytes(body), checked(body.size()), &info,
                                    nullptr, 0, out.data(), checked(out.size()), &written, 0);
    if (status == kAuthTagMismatch) throw std::runtime_error("AES-GCM authentication failed");
    if (status < 0) throw std::runtime_error("AES-GCM failed");
    out.resize(written);
  }
  return out;
}
} // namespace
Bytes random_bytes(std::size_t count) {
  Bytes out(count);
  if (count && BCryptGenRandom(nullptr, out.data(), checked(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
    throw std::runtime_error("cannot get random bytes");
  return out;
}
Bytes pbkdf2_sha256(std::span<const std::uint8_t> password, std::span<const std::uint8_t> salt,
                    std::uint32_t iterations, std::size_t length) {
  Algorithm hmac(BCRYPT_SHA256_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG);
  Bytes out(length);
  if (BCryptDeriveKeyPBKDF2(hmac.handle, bytes(password), checked(password.size()), bytes(salt), checked(salt.size()),
                            iterations, out.data(), checked(length), 0) < 0)
    throw std::runtime_error("PBKDF2 failed");
  return out;
}
Bytes aes256_gcm_encrypt(std::span<const std::uint8_t> key, std::span<const std::uint8_t> nonce,
                         std::span<const std::uint8_t> aad, std::span<const std::uint8_t> plaintext) {
  return gcm(true, key, nonce, aad, plaintext);
}
Bytes aes256_gcm_decrypt(std::span<const std::uint8_t> key, std::span<const std::uint8_t> nonce,
                         std::span<const std::uint8_t> aad, std::span<const std::uint8_t> ciphertext_and_tag) {
  return gcm(false, key, nonce, aad, ciphertext_and_tag);
}
} // namespace venture::crypto
