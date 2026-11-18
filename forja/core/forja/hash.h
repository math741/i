// forja-core: SHA-256 (digest de receitas), SHA-1 e base64 (handshake WebSocket do servidor de protótipo).
#pragma once
#include <cstdint>
#include <string>

namespace forja {

std::string sha256_hex(const std::string& data);
std::string sha1_raw(const std::string& data);   // 20 bytes crus
std::string base64(const std::string& raw);
std::string hex_bytes(const std::string& raw);

}  // namespace forja
