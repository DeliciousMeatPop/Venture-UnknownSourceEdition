// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>

namespace venture::privatebin {
// A PrivateBin (https://privatebin.info, format version 2) client. The text is compressed and
// encrypted here with a random key that never leaves this computer: the server stores only
// ciphertext, and the key travels in the link's fragment (after the '#'), which browsers do not send.

// What to POST and the link's key part.
struct Encrypted {
  std::string body;      // the JSON request body
  std::string fragment;  // base58 of the key, the part of the link after '#'
};
// Compresses and encrypts `text`; `expire` is one of PrivateBin's lifetimes ("5min", "1hour", "1day",
// "1week", "1month", "1year", "never"). The key, salt and nonce are random; the overload taking them
// exists for tests (key 32 bytes, salt 8, nonce 12).
Encrypted encrypt(std::string_view text, std::string_view expire);
Encrypted encrypt(std::string_view text, std::string_view expire, std::span<const std::uint8_t> key,
                  std::span<const std::uint8_t> salt, std::span<const std::uint8_t> nonce);
// The inverse of encrypt, as PrivateBin's page does it: the text of a request body or paste, given
// the link's fragment. Throws when it does not verify. Exposed for tests.
std::string decrypt(std::string_view body, std::string_view fragment);

// Uploads `text` to the PrivateBin at `server` (https, no trailing slash) and returns the link,
// key included. Throws with a short reason when the server refuses or cannot be reached.
std::string paste(std::string_view server, std::string_view text, std::string_view expire,
                  std::stop_token stop = {});

// Exposed for tests.
std::string base58(std::span<const std::uint8_t> bytes);
std::string base64(std::span<const std::uint8_t> bytes);
} // namespace venture::privatebin
