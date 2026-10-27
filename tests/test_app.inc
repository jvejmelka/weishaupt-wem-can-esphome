// Tests des Pakets APP (app_logik.h, app_dateien.h): /api/status wie die Docker-App,
// Zusatzwerte, Login-Sperre, Befehlsverlauf, eingebettete Dateien.

static ap::Messwert mw(double v, uint32_t alter_ms = 1000) {
  ap::Messwert m;
  m.da = true; m.v = v; m.alter_ms = alter_ms;
  return m;
}

// ArduinoJson legt Kommazahlen, wo es reicht, als float ab - daher 1e-4
static bool nah(double a, double b) { return std::fabs(a - b) < 1e-4; }

static JsonDocument app_status(const ap::Lage &l) {
  JsonDocument d;
  deserializeJson(d, ap::status_antwort(l));
  return d;
}

TEST(app_status_messwerte_und_altersgrenze) {
  ap::Lage l;
  l.zahlen["aussentemperatur"] = mw(7.5, 3600000);      // ohne Altersgrenze
  l.zahlen["kesseltemperatur"] = mw(48.2);
  l.zahlen["abgastemperatur"] = mw(40.0, 700000);       // veraltet (> 10 min)
  l.zahlen["vorlauf_heizkreis"] = mw(39.0);             // Ersatz fuer vorlauf_vpt
  l.zahlen["warmwasser"] = mw(32.1);
  l.zahlen["vorlauf_soll"] = mw(NAN);                   // "nan" = unbekannt
  auto d = app_status(l);
  PRUEFE(nah(d["outsideTempC"].as<double>(), 7.5));
  PRUEFE(nah(d["kesselTempC"].as<double>(), 48.2));
  PRUEFE(d["abgasTempC"].isNull());
  PRUEFE(nah(d["vorlaufIstC"].as<double>(), 39.0));
  PRUEFE(d["vorlaufSollC"].isNull());
  PRUEFE(nah(d["warmwasserC"].as<double>(), 32.1));
  PRUEFE(d["modeCode"].isNull());
  PRUEFE(d["heizkreis"].isNull() && d["schalten"].isNull());
  PRUEFE(std::string(d["source"].as<const char *>()) == "board");
}

TEST(app_status_aus_status_json) {
  ap::Lage l;
  bf::Status st;
  st.hk_vorgabe = 2; st.hk_bits = 0x0040; st.ww_vorgabe = 1; st.kesselstatus = 15; st.weg_can = true;
  st.bus_lebt = true;
  bf::Schaltung sch;
  l.status_json = bf::status_json(st, sch, "v38", 1761580800, 3);
  l.firmware = "v38";
  l.zahlen["kesselstatus_code"] = mw(15);
  l.zahlen["heizkreis_betriebsart_code"] = mw(5);        // status/json hat Vorrang
  l.zahlen["brenner"] = mw(1);
  l.zahlen["brennerphase"] = mw(1);
  l.texte["warmwasser_aktiv"] = "Ein";
  auto d = app_status(l);
  PRUEFE(d["modeCode"].as<int>() == 2);
  PRUEFE(std::string(d["warmwasser"].as<const char *>()) == "Ein");
  PRUEFE(d["ladung"]["gas"].as<bool>() == true);
  PRUEFE(std::string(d["kesselZweck"].as<const char *>()) == "Warmwasser");
  PRUEFE(d["heizungWartet"].as<bool>() == true);        // fordert (Bit 0x40), Kessel laedt Warmwasser
  PRUEFE(d["kesselZweckWarnung"].as<bool>() == false);
  PRUEFE(d["brennerAn"].as<bool>() == true);
  PRUEFE(std::string(d["brennerText"].as<const char *>()) == "Vorlüften");
  PRUEFE(d["heizkreisBits"].as<int>() == 0x40);
  PRUEFE(std::string(d["schalten"]["weg"].as<const char *>()) == "can");
  PRUEFE(d["bus"]["lebt"].as<bool>() == true);
  PRUEFE(std::string(d["boardFirmware"].as<const char *>()) == "v38");
  PRUEFE(d["statusJsonAlterS"].as<int>() == 0);
}

TEST(app_status_ohne_status_json_aus_sensoren) {
  ap::Lage l;
  l.zahlen["heizkreis_betriebsart_code"] = mw(1, 5000000);  // Einstellung: ohne Altersgrenze
  l.zahlen["heizkreis_status_code"] = mw(0x1000, 9000000);  // nur bei Aenderung gesendet
  l.zahlen["kesselstatus_code"] = mw(104);
  l.texte["warmwasser_betriebsart"] = "Aus";
  auto d = app_status(l);
  PRUEFE(d["modeCode"].as<int>() == 1);
  PRUEFE(d["heizkreisBits"].as<int>() == 0x1000);
  PRUEFE(std::string(d["warmwasser"].as<const char *>()) == "Aus");
  PRUEFE(std::string(d["kesselZweck"].as<const char *>()) == "Wartung");
  PRUEFE(d["kesselZweckWarnung"].as<bool>() == true);
  PRUEFE(d["heizungWartet"].as<bool>() == false);
  PRUEFE(d["brennerAn"].isNull() && d["brennerText"].isNull());
}

TEST(app_zusatz_zahl) {
  double v = 0;
  PRUEFE(ap::zusatz_zahl("21.5", "", v) && nah(v, 21.5));
  PRUEFE(ap::zusatz_zahl(" 7 ", "", v) && v == 7);
  PRUEFE(!ap::zusatz_zahl("{\"a\":1}", "", v));
  PRUEFE(!ap::zusatz_zahl("nan", "", v));
  PRUEFE(!ap::zusatz_zahl("abc", "", v));
  PRUEFE(ap::zusatz_zahl("true", "", v) && v == 1);
  PRUEFE(ap::zusatz_zahl("{\"apower\":512.3}", "apower", v) && nah(v, 512.3));
  PRUEFE(ap::zusatz_zahl("{\"tmp\":{\"tC\":19.4}}", "tmp.tC", v) && nah(v, 19.4));
  PRUEFE(ap::zusatz_zahl("{\"output\":false}", "output", v) && v == 0);
  PRUEFE(ap::zusatz_zahl("{\"t\":\"18.2\"}", "t", v) && nah(v, 18.2));
  PRUEFE(!ap::zusatz_zahl("{\"t\":null}", "t", v));
  PRUEFE(!ap::zusatz_zahl("{\"a\":1}", "a.b", v));
  PRUEFE(!ap::zusatz_zahl("kaputt", "a", v));
}

TEST(app_status_zusatz) {
  ap::Lage l;
  ap::Zusatzwert w;
  w.e.name = "Wohnzimmer"; w.e.topic = "t/wz"; w.e.feld = "tC"; w.e.einheit = "°C";
  w.da = true; w.nutzlast = "{\"tC\":21.3}"; w.alter_ms = 65000;
  l.zusatz.push_back(w);
  ap::Zusatzwert p;
  p.e.name = "WP"; p.e.topic = "t/wp"; p.e.feld = "apower"; p.e.einheit = "W"; p.e.laeuft = true; p.e.schwelle = 50;
  p.da = true; p.nutzlast = "{\"apower\":480}"; p.alter_ms = 1000;
  l.zusatz.push_back(p);
  ap::Zusatzwert alt = w;
  alt.e.name = "Alt"; alt.alter_ms = 3UL * 60 * 60 * 1000;    // aelter als 2 h -> ausgeblendet
  l.zusatz.push_back(alt);
  ap::Zusatzwert leer;
  leer.e.name = "Leer"; leer.e.topic = "t/x";
  l.zusatz.push_back(leer);
  auto d = app_status(l);
  JsonArray z = d["zusatz"];
  PRUEFE(z.size() == 4);
  PRUEFE(nah(z[0]["wert"].as<double>(), 21.3) && z[0]["alter_s"].as<int>() == 65 && z[0]["laeuft"].isNull());
  PRUEFE(std::string(z[0]["art"].as<const char *>()) == "wert");
  PRUEFE(std::string(z[1]["art"].as<const char *>()) == "laeuft" && z[1]["laeuft"].as<bool>() == true);
  PRUEFE(z[2]["wert"].isNull() && z[2]["alter_s"].as<int>() == 10800);
  PRUEFE(z[3]["wert"].isNull() && z[3]["alter_s"].isNull());
}

TEST(app_befehlsverlauf) {
  ap::verlauf().clear();
  bf::Befehl b;
  b.id = "app-1"; b.ziel = 1; b.wert = 2; b.phase = "angenommen";
  ap::befehl_merken(b);
  b.phase = "bestaetigt";
  ap::befehl_merken(b);
  PRUEFE(ap::verlauf().size() == 1 && ap::verlauf().back().phase == "bestaetigt");
  for (int i = 0; i < 15; i++) { b.id = "x" + std::to_string(i); ap::befehl_merken(b); }
  PRUEFE(ap::verlauf().size() == 10 && ap::verlauf().front().id == "x5");
  ap::Lage l;
  auto d = app_status(l);
  PRUEFE(d["befehle"].size() == 10);
  PRUEFE(std::string(d["befehle"][9]["id"].as<const char *>()) == "x14");
  PRUEFE(std::string(d["befehle"][9]["phase"].as<const char *>()) == "bestaetigt");
  ap::verlauf().clear();
}

TEST(app_login_sperre) {
  ap::Anmeldung a;
  uint32_t t = 4000000000UL;           // ueber den millis()-Ueberlauf hinweg
  for (int i = 0; i < 4; i++) PRUEFE(!a.fehlversuch(7, t + i * 1000));
  PRUEFE(!a.gesperrt(7, t + 5000));
  PRUEFE(a.fehlversuch(7, t + 5000));  // 5. Versuch in 15 min -> gesperrt
  PRUEFE(a.gesperrt(7, t + 6000));
  PRUEFE(!a.gesperrt(8, t + 6000));    // andere Adresse frei
  PRUEFE(a.gesperrt(7, t + 5000 + ap::SPERRE_MS - 1));
  PRUEFE(!a.gesperrt(7, t + 5000 + ap::SPERRE_MS));
  // Fehlversuche ausserhalb des Fensters zaehlen nicht
  ap::Anmeldung b;
  for (int i = 0; i < 8; i++) PRUEFE(!b.fehlversuch(9, t + i * (ap::FEHL_FENSTER_MS / 4 + 1)));
  PRUEFE(b.heute == 8);
  PRUEFE(ap::gleich("geheim", "geheim") && !ap::gleich("geheim", "geheiM") && !ap::gleich("geh", "geheim"));
  PRUEFE(!ap::gleich("", "x") && ap::gleich("", ""));
}

TEST(app_meta_und_energie) {
  JsonDocument d;
  deserializeJson(d, ap::meta_antwort("/"));
  PRUEFE(std::string(d["boardUrl"].as<const char *>()) == "/");
  PRUEFE(d["modes"].size() == 8 && d["modes"][0]["value"].as<int>() == 1);
  PRUEFE(std::string(d["modes"][7]["label"].as<const char *>()) == bf::hk_text(8));
  ap::Lage l;
  l.zahlen["w__rmemenge_vortag_heizung"] = mw(38.9, 20UL * 60 * 60 * 1000);   // Tageswert: veraltet nach 10 min
  l.zahlen["w__rmemenge_vortag_ww"] = mw(10.8);
  JsonDocument e;
  deserializeJson(e, ap::energie_antwort(l));
  PRUEFE(e["heizungKwh"].isNull() && nah(e["warmwasserKwh"].as<double>(), 10.8) && e["gesamtKwh"].isNull());
}

TEST(app_eingebettete_dateien) {
  const char *pfade[] = {"/app", "/app.js", "/style.css", "/manifest.json", "/icon.svg", "/sw.js", "/einstellungen", "/einstellungen.js"};
  for (auto *p : pfade) {
    auto *f = ap::datei(p, std::strlen(p));
    PRUEFE(f != nullptr);
    if (f) PRUEFE(f->laenge > 20 && f->daten[0] == 0x1f && f->daten[1] == 0x8b);   // gzip
  }
  PRUEFE(ap::datei("/", 1) == nullptr);
  PRUEFE(ap::datei("/app.j", 6) == nullptr);
  PRUEFE(std::strlen(ap::APP_VERSION) == 10);
}

TEST(app_zugang_einrichtung) {
  ap::Einrichtung e;
  std::string f;
  PRUEFE(!ap::einrichtung_lesen("kaputt", e, f));
  PRUEFE(!ap::einrichtung_lesen("{\"benutzer\":\"jo\",\"app_passwort\":\"kurz\",\"admin_passwort\":\"lang-genug-1\"}", e, f));
  PRUEFE(f.find("8 bis 64") != std::string::npos);
  PRUEFE(!ap::einrichtung_lesen("{\"benutzer\":\"j o\",\"app_passwort\":\"geheim-123\",\"admin_passwort\":\"admin-4567\"}", e, f));
  PRUEFE(!ap::einrichtung_lesen("{\"benutzer\":\"jo\",\"app_passwort\":\"gleich-123\",\"admin_passwort\":\"gleich-123\"}", e, f));
  PRUEFE(f.find("verschieden") != std::string::npos);
  PRUEFE(!ap::einrichtung_lesen("{\"benutzer\":\"jo\",\"app_passwort\":\"geheim-123\",\"admin_passwort\":\"admin-4567\",\"experte_passwort\":\"admin-4567\"}", e, f));
  PRUEFE(ap::einrichtung_lesen("{\"benutzer\":\"jo\",\"app_passwort\":\"geheim-123\",\"admin_passwort\":\"admin-4567\"}", e, f));
  PRUEFE(e.benutzer == "jo" && e.app == "geheim-123" && e.admin == "admin-4567" && e.experte.empty());
  PRUEFE(ap::einrichtung_lesen("{\"benutzer\":\"jo\",\"app_passwort\":\"geheim-123\",\"admin_passwort\":\"admin-4567\",\"experte_passwort\":\"experte-89\"}", e, f));
  PRUEFE(e.experte == "experte-89");
  char t[8];
  PRUEFE(ap::theme("") == "auto" && ap::kopieren(t, sizeof(t), "hell") && ap::theme(t) == "hell");
  PRUEFE(!ap::kopieren(t, sizeof(t), "viel-zu-lang"));
  PRUEFE(!ap::passwort_ok(std::string(65, 'x'), f) && ap::passwort_ok(std::string(64, 'x'), f));
  PRUEFE(!ap::passwort_ok("abc\ndefgh", f));
  JsonDocument d;
  deserializeJson(d, ap::meta_antwort("/einstellungen", "dunkel"));
  PRUEFE(std::string(d["theme"].as<const char *>()) == "dunkel");
  deserializeJson(d, ap::meta_antwort("/"));
  PRUEFE(d["theme"].isNull());
}

static std::string hex(const uint8_t *p, size_t n) {
  std::string s;
  char b[3];
  for (size_t i = 0; i < n; i++) { snprintf(b, sizeof(b), "%02x", p[i]); s += b; }
  return s;
}

TEST(zugang_sha256_hmac_pbkdf2) {
  uint8_t h[32];
  zg::Sha256 a; a.update((const uint8_t *) "abc", 3); a.fertig(h);
  PRUEFE(hex(h, 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  zg::Sha256 b; b.fertig(h);
  PRUEFE(hex(h, 32) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  std::string lang(1000, 'a');
  zg::Sha256 c; c.update((const uint8_t *) lang.data(), 100); c.update((const uint8_t *) lang.data() + 100, 900); c.fertig(h);
  PRUEFE(hex(h, 32) == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
  // RFC 4231, Testfall 2
  zg::hmac((const uint8_t *) "Jefe", 4, (const uint8_t *) "what do ya want for nothing?", 28, h);
  PRUEFE(hex(h, 32) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
  // PBKDF2-HMAC-SHA256 (RFC 7914 / bekannte Werte): P="password", S="salt", c=1 und c=4096
  zg::pbkdf2("password", (const uint8_t *) "salt", 4, 1, h);
  PRUEFE(hex(h, 32) == "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
  zg::pbkdf2("password", (const uint8_t *) "salt", 4, 4096, h);
  PRUEFE(hex(h, 32) == "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
}

TEST(zugang_geheim_und_extern) {
  zg::Geheim g;
  PRUEFE(!g.gesetzt() && !zg::pruefen(g, ""));
  uint8_t salz[16];
  for (int i = 0; i < 16; i++) salz[i] = (uint8_t) (i * 7 + 1);
  zg::setzen(g, "geheim-123", salz);
  PRUEFE(g.gesetzt() && zg::pruefen(g, "geheim-123") && !zg::pruefen(g, "geheim-124") && !zg::pruefen(g, ""));
  zg::Zugang2 z;
  PRUEFE(!zg::vollstaendig(z));
  ap::kopieren(z.benutzer, sizeof(z.benutzer), "jo");
  z.app = g; z.admin = g;
  PRUEFE(zg::vollstaendig(z));
  PRUEFE(ap::extern_erlaubt("/") && ap::extern_erlaubt("/app") && ap::extern_erlaubt("/api/status"));
  PRUEFE(ap::extern_erlaubt("/einstellungen") && ap::extern_erlaubt("/api/admin/einstellungen"));
  PRUEFE(!ap::extern_erlaubt("/experten") && !ap::extern_erlaubt("/update") && !ap::extern_erlaubt("/events"));
  PRUEFE(!ap::extern_erlaubt("/sensor/Laufzeit") && !ap::extern_erlaubt("/api/admin/werksreset"));
  PRUEFE(!ap::extern_erlaubt("/app/../update") && !ap::extern_erlaubt("/apix"));
}
