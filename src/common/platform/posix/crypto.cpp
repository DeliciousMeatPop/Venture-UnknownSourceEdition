// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/crypto.h"
#include <stdexcept>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace venture::crypto {
namespace {
constexpr std::size_t kTag = 16, kNonce = 12, kKey = 32;
struct Context {
  EVP_CIPHER_CTX* cipher = EVP_CIPHER_CTX_new();
  ~Context() { EVP_CIPHER_CTX_free(cipher); }
};
// One AES-256-GCM pass; `input` is the plaintext, or the ciphertext followed by its tag.
Bytes gcm(bool encrypt, std::span<const std::uint8_t> key, std::span<const std::uint8_t> nonce,
          std::span<const std::uint8_t> aad, std::span<const std::uint8_t> input) {
  if (key.size() != kKey || nonce.size() != kNonce) throw std::invalid_argument("bad AES-GCM key or nonce size");
  if (!encrypt && input.size() < kTag) throw std::runtime_error("AES-GCM input is shorter than its tag");
  Context ctx;
  if (!ctx.cipher) throw std::runtime_error("cannot create AES-GCM context");
  const int mode = encrypt ? 1 : 0;
  if (EVP_CipherInit_ex(ctx.cipher, EVP_aes_256_gcm(), nullptr, nullptr, nullptr, mode) != 1 ||
      EVP_CIPHER_CTX_ctrl(ctx.cipher, EVP_CTRL_GCM_SET_IVLEN, int(kNonce), nullptr) != 1 ||
      EVP_CipherInit_ex(ctx.cipher, nullptr, nullptr, key.data(), nonce.data(), mode) != 1)
    throw std::runtime_error("cannot initialize AES-GCM");
  int length = 0;
  if (!aad.empty() && EVP_CipherUpdate(ctx.cipher, nullptr, &length, aad.data(), int(aad.size())) != 1)
    throw std::runtime_error("AES-GCM authenticated data failed");
  const auto body = encrypt ? input : input.first(input.size() - kTag);
  Bytes out(body.size() + (encrypt ? kTag : 0));
  int total = 0;
  if (!body.empty()) {
    if (EVP_CipherUpdate(ctx.cipher, out.data(), &length, body.data(), int(body.size())) != 1)
      throw std::runtime_error("AES-GCM failed");
    total = length;
  }
  if (encrypt) {
    if (EVP_EncryptFinal_ex(ctx.cipher, out.data() + total, &length) != 1)
      throw std::runtime_error("AES-GCM failed");
    total += length;
    if (EVP_CIPHER_CTX_ctrl(ctx.cipher, EVP_CTRL_GCM_GET_TAG, int(kTag), out.data() + total) != 1)
      throw std::runtime_error("AES-GCM tag failed");
    out.resize(std::size_t(total) + kTag);
  } else {
    Bytes tag(input.end() - kTag, input.end());
    if (EVP_CIPHER_CTX_ctrl(ctx.cipher, EVP_CTRL_GCM_SET_TAG, int(kTag), tag.data()) != 1)
      throw std::runtime_error("AES-GCM tag failed");
    if (EVP_DecryptFinal_ex(ctx.cipher, out.data() + total, &length) != 1)
      throw std::runtime_error("AES-GCM authentication failed");
    out.resize(std::size_t(total + length));
  }
  return out;
}
} // namespace
Bytes random_bytes(std::size_t count) {
  Bytes out(count);
  if (count && RAND_bytes(out.data(), int(count)) != 1) throw std::runtime_error("cannot get random bytes");
  return out;
}
Bytes pbkdf2_sha256(std::span<const std::uint8_t> password, std::span<const std::uint8_t> salt,
                    std::uint32_t iterations, std::size_t length) {
  Bytes out(length);
  if (PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(password.data()), int(password.size()), salt.data(),
                        int(salt.size()), int(iterations), EVP_sha256(), int(length), out.data()) != 1)
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
