// Zugangsdaten der App auf dem Board (ab v42): drei Stufen.
//   App      Familie, Handy: schalten und ansehen         - Passwort nur als Hash gespeichert
//   Admin    Einstellungsseite                            - Passwort nur als Hash gespeichert
//   Experte  ESPHome-Oberflaeche (/experten), /update,    - Klartext, weil die Digest-Anmeldung
//            Werksreset                                     von ESPHome ihn braucht; leer = gesperrt
// Hash: PBKDF2-HMAC-SHA256 mit 16 Byte Salz und 1000 Runden (eigene Umsetzung, auf dem PC getestet).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>

namespace zg {

// ---------------------------------------------------------------- SHA-256 (FIPS 180-4)
struct Sha256 {
  uint32_t h[8];
  uint8_t puffer[64];
  uint64_t laenge = 0;
  size_t n = 0;

  Sha256() {
    static const uint32_t start[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::memcpy(h, start, sizeof(h));
  }
  static uint32_t r(uint32_t x, int k) { return (x >> k) | (x << (32 - k)); }
  void block(const uint8_t *p) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int t = 0; t < 16; t++)
      w[t] = (uint32_t) p[t * 4] << 24 | (uint32_t) p[t * 4 + 1] << 16 | (uint32_t) p[t * 4 + 2] << 8 | p[t * 4 + 3];
    for (int t = 16; t < 64; t++) {
      uint32_t s0 = r(w[t - 15], 7) ^ r(w[t - 15], 18) ^ (w[t - 15] >> 3);
      uint32_t s1 = r(w[t - 2], 17) ^ r(w[t - 2], 19) ^ (w[t - 2] >> 10);
      w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int t = 0; t < 64; t++) {
      uint32_t t1 = hh + (r(e, 6) ^ r(e, 11) ^ r(e, 25)) + ((e & f) ^ (~e & g)) + K[t] + w[t];
      uint32_t t2 = (r(a, 2) ^ r(a, 13) ^ r(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  void update(const uint8_t *p, size_t len) {
    laenge += len;
    while (len) {
      size_t k = 64 - n < len ? 64 - n : len;
      std::memcpy(puffer + n, p, k);
      n += k; p += k; len -= k;
      if (n == 64) { block(puffer); n = 0; }
    }
  }
  void fertig(uint8_t out[32]) {
    uint64_t bits = laenge * 8;
    uint8_t eins = 0x80, null = 0;
    update(&eins, 1);
    while (n != 56) update(&null, 1);
    uint8_t l[8];
    for (int i = 0; i < 8; i++) l[i] = (uint8_t) (bits >> (56 - 8 * i));
    update(l, 8);
    for (int i = 0; i < 8; i++) {
      out[i * 4] = (uint8_t) (h[i] >> 24); out[i * 4 + 1] = (uint8_t) (h[i] >> 16);
      out[i * 4 + 2] = (uint8_t) (h[i] >> 8); out[i * 4 + 3] = (uint8_t) h[i];
    }
  }
};

inline void hmac(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen, uint8_t out[32]) {
  uint8_t k[64] = {0}, ip[64], op[64];
  if (klen > 64) { Sha256 s; s.update(key, klen); s.fertig(k); } else std::memcpy(k, key, klen);
  for (int i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
  uint8_t inner[32];
  Sha256 a; a.update(ip, 64); a.update(msg, mlen); a.fertig(inner);
  Sha256 b; b.update(op, 64); b.update(inner, 32); b.fertig(out);
}

// PBKDF2-HMAC-SHA256, ein Block (32 Byte Ergebnis)
inline void pbkdf2(const std::string &pw, const uint8_t *salz, size_t slen, uint32_t runden, uint8_t out[32]) {
  uint8_t m[68];
  std::memcpy(m, salz, slen);
  m[slen] = 0; m[slen + 1] = 0; m[slen + 2] = 0; m[slen + 3] = 1;
  uint8_t u[32];
  hmac((const uint8_t *) pw.data(), pw.size(), m, slen + 4, u);
  std::memcpy(out, u, 32);
  for (uint32_t i = 1; i < runden; i++) {
    hmac((const uint8_t *) pw.data(), pw.size(), u, 32, u);
    for (int j = 0; j < 32; j++) out[j] ^= u[j];
  }
}

static const uint32_t RUNDEN = 1000;

struct Geheim {
  uint8_t salz[16] = {0};
  uint8_t hash[32] = {0};
  bool gesetzt() const {
    for (auto b : hash) if (b) return true;
    return false;
  }
};

// salz: 16 Zufallsbytes vom Aufrufer (auf dem Board esp_fill_random)
inline void setzen(Geheim &g, const std::string &pw, const uint8_t salz[16]) {
  std::memcpy(g.salz, salz, 16);
  pbkdf2(pw, g.salz, 16, RUNDEN, g.hash);
}

// Vergleich in konstanter Zeit
inline bool pruefen(const Geheim &g, const std::string &pw) {
  if (!g.gesetzt()) return false;
  uint8_t h[32];
  pbkdf2(pw, g.salz, 16, RUNDEN, h);
  uint8_t x = 0;
  for (int i = 0; i < 32; i++) x |= (uint8_t) (h[i] ^ g.hash[i]);
  return x == 0;
}

// gespeichert im Flash (Schluessel APZ2); version 2
struct Zugang2 {
  uint8_t version = 2;
  char benutzer[33] = {0};
  Geheim app, admin;
  char experte[65] = {0};   // Klartext (Digest), leer = Expertenstufe gesperrt
  char theme[8] = {0};
  uint32_t token_seit = 0;  // Unix-Zeit, seit der das App-Token gilt (0 = unbekannt)
};

inline bool vollstaendig(const Zugang2 &z) { return z.version == 2 && z.benutzer[0] && z.app.gesetzt() && z.admin.gesetzt(); }

}  // namespace zg
