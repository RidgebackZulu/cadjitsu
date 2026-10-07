#pragma once

#include <string>

namespace cad {

// Standard base64 (RFC 4648, with padding), for binary data in JSON files.
std::string base64Encode(const std::string &bytes);
// False if `text` is not valid base64 (whitespace is ignored).
bool base64Decode(const std::string &text, std::string &bytes);

} // namespace cad
