// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace venture::crypto {
using Bytes = std::vector<std::uint8_t>;
// The operating system's providers (Windows CNG, OpenSSL elsewhere). Failures throw std::runtime_error.
Bytes random_bytes(std::size_t count);
Bytes pbkdf2_sha256(std::span<const std::uint8_t> password, std::span<const std::uint8_t> salt,
                    std::uint32_t iterations, std::size_t length);
// AES-256-GCM with a 12-byte nonce and a 16-byte tag. Encryption returns the ciphertext followed by
// the tag; decryption takes that form and throws when the tag does not verify.
Bytes aes256_gcm_encrypt(std::span<const std::uint8_t> key, std::span<const std::uint8_t> nonce,
                         std::span<const std::uint8_t> aad, std::span<const std::uint8_t> plaintext);
Bytes aes256_gcm_decrypt(std::span<const std::uint8_t> key, std::span<const std::uint8_t> nonce,
                         std::span<const std::uint8_t> aad, std::span<const std::uint8_t> ciphertext_and_tag);
} // namespace venture::crypto
