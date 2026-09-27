// EINGEFROREN: die on_frame-Lambdas von pakete/verdacht.yaml in v29 (Commit 4eb5775, Kopie daneben
// als verdacht_v29.yaml), fast woertlich nach C++ uebertragen: id(wcan)->vd.x -> z.x, Schalter und
// Abstaende -> e.*, ESP_LOGD/ESP_LOGI -> Aktion LOG_D/LOG_I, id(vd_regel)->execute -> Aktion AUSFUEHREN.
// Nicht aendern - das ist die Vergleichsfassung fuer den Aequivalenztest (test_aequivalenz.inc).
// Wird in test_regeln.cpp eingebunden (nach regellogik_v29.h und regelwerk.h).

namespace v29 {

using F = std::vector<uint8_t>;

// eigene Regel im Format bis v29
struct AltRegel {
  std::string name;
  bool an = true;
  uint16_t can_id = 0;
  uint8_t maske[8] = {0}, muster[8] = {0};
  uint8_t lesen = 3;
  uint16_t abstand_min = 10;
};

// Laufzeitzustand der Regeln in v29 (Felder von wc::Verdacht)
struct Z {
  uint32_t r1_a = 0, r1_b = 0, wem_start = 0;
  int kstatus_vorher = -1, w252b_vorher = -1;
  float vl = NAN, at = NAN;
  int r4_key = -1;
  int stby_vorher = -1, rest_vorher = -1;
};

// Schalterstellungen und bekannte Betriebsarten zum Zeitpunkt des Frames
struct Ein {
  bool fest_an[5] = {false, false, false, false, false};
  int fest_ab[5] = {10, 10, 10, 10, 10};
  bool haupt = false;
  uint32_t eigener_befehl_ms = 0;
  int hk = -1, ww = -1;
};

inline void d(std::vector<rw::Aktion> &o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
inline void d(std::vector<rw::Aktion> &o, const char *fmt, ...) {
  char b[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  o.push_back({rw::Aktion::LOG_D, b, "", 0, 0, false});
}
inline void i(std::vector<rw::Aktion> &o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
inline void i(std::vector<rw::Aktion> &o, const char *fmt, ...) {
  char b[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  o.push_back({rw::Aktion::LOG_I, b, "", 0, 0, false});
}
inline void x_(std::vector<rw::Aktion> &o, const char *regel, int ziel, int abstand, bool an) {
  o.push_back({rw::Aktion::AUSFUEHREN, "", regel, ziel, abstand, an});
}

// alle Regel-Handler von verdacht.yaml (v29) fuer einen Frame; j = millis()
inline std::vector<rw::Aktion> frame(Z &z, std::vector<AltRegel> &eigene, const Ein &e, uint32_t can_id, const F &x,
                                     uint32_t j) {
  std::vector<rw::Aktion> o;
  // --- 0x602
  if (can_id == 0x602 && x.size() >= 5) {
    bool eigen = rl29::eigen_ruht(e.eigener_befehl_ms, j);
    int v = 0;
    if (rl29::ist_leseanfrage(x[0])) {
      if (rl29::r1_frame(z.r1_a, z.r1_b, x, j)) {
        bool wem_neu = rl29::wem_neu(z.wem_start, j);
        if (eigen || wem_neu) d(o, "R1-Folge erkannt, ruht (%s)", eigen ? "eigener Befehl" : "WEM-Neustart");
        else x_(o, "R1", 2, e.fest_ab[0], e.fest_an[0]);
      }
    } else if (rl29::w252b_frame(z.w252b_vorher, x, v)) {
      bool r2 = rl29::r2_bei_252b(v, e.ww);
      bool r4 = rl29::r4_bei_252b(v, e.hk);
      if (r2) { if (eigen) d(o, "R2 (0x252B=0F) ruht (eigener Befehl)");
                else x_(o, "R2", 2, e.fest_ab[1], e.fest_an[1]); }
      if (r4) { if (eigen) d(o, "R4 (0x252B=0A) ruht (eigener Befehl)");
                else x_(o, "R4", 1, e.fest_ab[3], e.fest_an[3]); }
    }
  }
  // --- 0x182
  if (can_id == 0x182) {
    int st = 0;
    if (rl29::kstatus_flanke(z.kstatus_vorher, x, st)) {
      bool eigen = rl29::eigen_ruht(e.eigener_befehl_ms, j);
      bool r2 = rl29::r2_bei_kstatus(st, e.ww);
      bool r4 = rl29::r4_bei_kstatus(st, e.hk);
      if (r2) { if (eigen) d(o, "R2 (Kesselstatus 15) ruht (eigener Befehl)");
                else x_(o, "R2", 2, e.fest_ab[1], e.fest_an[1]); }
      if (r4) { if (eigen) d(o, "R4 (Kesselstatus 10) ruht (eigener Befehl)");
                else x_(o, "R4", 1, e.fest_ab[3], e.fest_an[3]); }
    }
  }
  // --- 0x1C1
  if (can_id == 0x1C1) {
    rl29::StatusAenderung s0 = rl29::statusbits(z.stby_vorher, z.rest_vorher, x);
    if (s0.geaendert) {
      bool eigen = rl29::eigen_ruht(e.eigener_befehl_ms, j);
      rl29::StatusEntscheid s = rl29::status_entscheiden(s0, eigen, e.fest_an[2]);
      if (s0.r3 && eigen) d(o, "R3 Standby-Bit %d -> %d ruht (eigener Befehl)", s0.sv, s0.stby);
      if (s.r3) x_(o, "R3", 1, e.fest_ab[2], e.fest_an[2]);
      if (s0.r5) {
        if (eigen) d(o, "R5 Statusbits 0x%04X -> 0x%04X ruht (eigener Befehl)", s0.rv, s0.rest);
        else if (s.r3_liest) d(o, "R5 Statusbits 0x%04X -> 0x%04X: R3 liest schon", s0.rv, s0.rest);
        else if (s.r5) x_(o, "R5", 1, e.fest_ab[4], e.fest_an[4]);
      }
    }
  }
  // --- 0x201
  if (can_id == 0x201 && x.size() >= 3) z.at = reg::pdo_wert(reg::AUSSENTEMP, x);
  // --- 0x241
  if (can_id == 0x241 && x.size() >= 2) {
    float vl = reg::pdo_wert(reg::VORLAUFSOLL_HZ, x);
    z.vl = vl;
    rl29::R4Befund b;
    if (rl29::r4_vorlauf(z.r4_key, vl, z.at, e.hk, b)) {
      if (rl29::eigen_ruht(e.eigener_befehl_ms, j)) {
        d(o, "R4 (Raumsoll-Stufe %d statt %d) ruht (eigener Befehl)", b.stufe, b.soll);
      } else {
        i(o, "R4: Vorlaufsoll %.1f, AT %.1f -> Raumsoll ~%.1f (Stufe %d), erwartet %d", vl, z.at, b.rt, b.stufe, b.soll);
        x_(o, "R4", 1, e.fest_ab[3], e.fest_an[3]);
      }
    }
  }
  // --- 0x701
  if (can_id == 0x701 && x.size() >= 1 && x[0] == 0x00) z.wem_start = j | 1;
  // --- eigene (0x000/0x000); 0x581 hat davor seinen eigenen Handler (Antworten), der hier nicht zaehlt
  if (!eigene.empty() && !rl29::gesperrte_id(can_id)) {
    bool haupt = e.haupt;
    for (auto &r : eigene) {
      if (r.can_id != can_id) continue;
      if (!rl29::maske_passt(r.maske, r.muster, x)) continue;
      if (rl29::eigen_ruht(e.eigener_befehl_ms, j)) { d(o, "%s ruht (eigener Befehl)", r.name.c_str()); continue; }
      x_(o, r.name.c_str(), (int) r.lesen, (int) r.abstand_min, haupt && r.an);
    }
  }
  return o;
}

}  // namespace v29
