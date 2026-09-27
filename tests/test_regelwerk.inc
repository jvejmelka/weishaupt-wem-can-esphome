// ---------------------------------------------------------------- Regelformat ab v30 (regelwerk.h, verdacht.h)
// Wird in test_regeln.cpp eingebunden.

// Speicher- und Standform der Kurzform ist byteweise die von v29
TEST(regelwerk_kurzform_wie_v29) {
  vd::Datei d;
  std::string f;
  PRUEFE(vd::datei_lesen(datei("regeln.json.example"), d, f));
  vd::eigene().clear();
  for (auto &r : d.eigene) if (vd::kurzform(r)) vd::eigene().push_back(r);
  PRUEFE(vd::eigene().size() >= 1);
  vd::eigene().resize(1);
  PRUEFE(vd::speicherform() ==
         "[{\"name\":\"Portal-Seitenaufruf\",\"an\":false,\"can_id\":\"0x602\",\"maske\":\"00 FF FF 00 00 00 00 00\","
         "\"muster\":\"00 3E 22 00 00 00 00 00\",\"lesen\":\"beide\",\"abstand_min\":30,\"beschreibung\":\"WEM fragt den Kessel "
         "0x223E ab - kam einmal beim Oeffnen einer Portalseite; Zusammenhang unbelegt\"}]");
  PRUEFE(vd::anzeige() == "Portal-Seitenaufruf AUS: 602 [.. 3E 22] -> beide, 30 min, zuletzt -, 0 x");
  bool an[5] = {true, true, true, false, false};
  int ab[5] = {1, 10, 1, 30, 5};
  PRUEFE(vd::stand_json("vX", 3, an, ab, 6, false) ==
         "{\"firmware\":\"vX\",\"version\":3,\"R1\":{\"an\":true,\"abstand_min\":1},\"R2\":{\"an\":true,\"abstand_min\":10},"
         "\"R3\":{\"an\":true,\"abstand_min\":1},\"R4\":{\"an\":false,\"abstand_min\":30},\"R5\":{\"an\":false,\"abstand_min\":5},"
         "\"max_pro_stunde\":6,\"eigene_regeln_an\":false,\"eigene\":[{\"name\":\"Portal-Seitenaufruf\",\"an\":false,"
         "\"can_id\":\"0x602\",\"maske\":\"00 FF FF 00 00 00 00 00\",\"muster\":\"00 3E 22 00 00 00 00 00\",\"lesen\":\"beide\","
         "\"abstand_min\":30,\"beschreibung\":\"WEM fragt den Kessel 0x223E ab - kam einmal beim Oeffnen einer Portalseite; "
         "Zusammenhang unbelegt\",\"zuletzt\":\"\",\"anzahl\":0}]}");
  vd::eigene().clear();
}

static const char *LANG_REGEL =
    "{\"name\":\"ww-folge\",\"lesen\":\"ww\",\"abstand_min\":1,"
    "\"ausloeser\":[{\"folge\":[{\"can_id\":\"0x602\",\"maske\":\"FF FF FF FF\",\"muster\":[\"A4 01 21 0A\",\"40 01 21 0A\"],\"laenge_min\":5,\"fenster_ms\":15000},"
    "{\"can_id\":\"0x602\",\"maske\":\"FF FF FF FF\",\"muster\":[\"A4 02 21 0D\",\"40 02 21 0D\"],\"laenge_min\":5,\"fenster_ms\":10000},"
    "{\"can_id\":\"0x602\",\"maske\":\"FF FF FF FF\",\"muster\":[\"A4 02 21 01\",\"40 02 21 01\"],\"laenge_min\":5}],"
    "\"text\":\"WW-Folge\"},"
    "{\"flanke\":{\"can_id\":\"0x182\",\"laenge_min\":1},\"byte\":0,\"gleich\":[15],\"wenn\":{\"ww\":[2]}}],"
    "\"ruhe_nach\":{\"can_id\":\"0x701\",\"muster\":\"00\",\"laenge_min\":1,\"ms\":300000,\"grund\":\"WEM-Neustart\"}}";

TEST(regelwerk_langform_einlesen_und_rundreise) {
  vd::Datei d;
  std::string f;
  PRUEFE(vd::datei_lesen(std::string("{\"eigene\":[") + LANG_REGEL + "]}", d, f));
  if (!f.empty()) std::printf("  %s\n", f.c_str());
  PRUEFE(d.eigene.size() == 1 && !vd::kurzform(d.eigene[0]));
  auto &r = d.eigene[0];
  PRUEFE(r.ausloeser.size() == 2 && r.ausloeser[0].art == vd::Art::FOLGE && r.ausloeser[0].glieder.size() == 3);
  PRUEFE(r.ausloeser[0].glieder[0].muster.size() == 2 && r.ausloeser[0].glieder[0].fenster_ms == 15000);
  PRUEFE(r.ausloeser[1].art == vd::Art::FLANKE && r.ausloeser[1].ww == std::vector<int>{2});
  PRUEFE(r.ruhe_ms == 300000 && r.ruhe_grund == "WEM-Neustart");
  vd::eigene() = d.eigene;
  std::string s = vd::speicherform();
  PRUEFE(s.size() < 2400);
  PRUEFE(vd::aus_speicher(s.c_str(), f) && vd::speicherform() == s);
  PRUEFE(enthaelt(vd::anzeige(), "folge 602 x3 + flanke 182"));
  vd::eigene().clear();
}

TEST(regelwerk_langform_wirkt_wie_R1) {
  // die eigene Folgeregel oben verhaelt sich auf dem Bus wie R1 (+ Kesselstatus-Flanke)
  vd::Datei d;
  std::string f;
  PRUEFE(vd::datei_lesen(std::string("{\"eigene\":[") + LANG_REGEL + "]}", d, f));
  rw::Werk w;
  rw::Kontext k;
  k.eigene_haupt = true;
  auto lauf = [&](uint32_t t, uint32_t id, F x) { k.jetzt = t; return rw::frame(w, d.eigene, id, x, k); };
  lauf(1000, 0x602, sdo(0xA4, 0x2101, 0x0A));
  lauf(1300, 0x602, sdo(0x40, 0x2102, 0x0D));
  auto a = lauf(1600, 0x602, sdo(0xA4, 0x2102, 0x01));
  PRUEFE(a.size() == 1 && a[0].art == rw::Aktion::AUSFUEHREN && a[0].regel == "ww-folge" && a[0].ziel == 2 && a[0].an);
  lauf(5000, 0x701, F{0x00});
  lauf(6000, 0x602, sdo(0xA4, 0x2101, 0x0A));
  lauf(6300, 0x602, sdo(0xA4, 0x2102, 0x0D));
  a = lauf(6600, 0x602, sdo(0xA4, 0x2102, 0x01));
  PRUEFE(a.size() == 1 && a[0].art == rw::Aktion::LOG_D && a[0].text == "ww-folge (WW-Folge) ruht (WEM-Neustart)");
  // Flanke Kesselstatus 15 nur bei bekanntem "Aus" (die Ruhe nach dem WEM-Neustart ist vorbei)
  lauf(407000, 0x182, F{10});
  PRUEFE(lauf(408000, 0x182, F{15}).empty());          // ww unbekannt
  k.ww = 2;
  lauf(409000, 0x182, F{10});
  a = lauf(410000, 0x182, F{15});
  PRUEFE(a.size() == 1 && a[0].art == rw::Aktion::AUSFUEHREN);
  // eigener Befehl: ruht, Hauptschalter aus: Schatten (an = false)
  k.eigen = true;
  a = lauf(411000, 0x182, F{10});
  a = lauf(412000, 0x182, F{15});
  PRUEFE(a.size() == 1 && a[0].text == "ww-folge ruht (eigener Befehl)");
  k.eigen = false;
  k.eigene_haupt = false;
  lauf(413000, 0x182, F{10});
  a = lauf(414000, 0x182, F{15});
  PRUEFE(a.size() == 1 && a[0].art == rw::Aktion::AUSFUEHREN && !a[0].an);
}

TEST(regelwerk_langform_fehler) {
  vd::Datei d;
  std::string f;
  auto nein = [&](const std::string &regel, const char *teil) {
    bool ok = !vd::datei_lesen("{\"eigene\":[" + regel + "]}", d, f) && enthaelt(f, teil);
    if (!ok) std::printf("  erwartet '%s', bekommen '%s'\n", teil, f.c_str());
    return ok;
  };
  PRUEFE(nein("{\"name\":\"t\",\"can_id\":\"0x602\",\"muster\":\"40\",\"ausloeser\":[{\"frame\":{\"can_id\":\"0x602\"}}]}", "entweder"));
  PRUEFE(nein("{\"name\":\"t\",\"ausloeser\":[]}", "1-4"));
  PRUEFE(nein("{\"name\":\"t\",\"ausloeser\":[{\"frame\":{\"can_id\":\"0x602\"},\"folge\":[]}]}", "genau eines"));
  PRUEFE(nein("{\"name\":\"t\",\"ausloeser\":[{\"folge\":[{\"can_id\":\"0x602\"}]}]}", "2-4"));
  PRUEFE(nein("{\"name\":\"t\",\"ausloeser\":[{\"frame\":{\"can_id\":\"0x581\"}}]}", "gesperrt"));
  PRUEFE(nein("{\"name\":\"t\",\"ausloeser\":[{\"frame\":{\"can_id\":\"0x602\"},\"byte\":1}]}", "nur bei flanke"));
  PRUEFE(nein("{\"name\":\"t\",\"ausloeser\":[{\"flanke\":{\"can_id\":\"0x1C1\"},\"byte\":7,\"bytes\":2}]}", "hoechstens 8"));
  PRUEFE(nein("{\"name\":\"t\",\"ausloeser\":[{\"baustein\":\"xyz\"}]}", "baustein unbekannt"));
  PRUEFE(nein("{\"name\":\"t\",\"ausloeser\":[{\"frame\":{\"can_id\":\"0x602\"},\"wenn\":{\"xx\":[1]}}]}", "unbekanntes Feld"));
  PRUEFE(nein("{\"name\":\"t\",\"ausloeser\":[{\"frame\":{\"can_id\":\"0x602\",\"muster\":[\"40\",\"40 01\"]}}]}", "maske angeben"));
  PRUEFE(nein("{\"name\":\"t\",\"can_id\":\"0x602\",\"muster\":\"40\",\"ruhe_nach\":{\"can_id\":\"0x701\"}}", "ruhe_nach.ms"));
  PRUEFE(nein("{\"name\":\"R1\",\"can_id\":\"0x602\",\"muster\":\"40\"}", "name"));
  // Kurzform mit Muster-Liste und Bedingung ist erlaubt (Langform-Speicherung)
  PRUEFE(vd::datei_lesen("{\"eigene\":[{\"name\":\"t\",\"can_id\":\"0x602\",\"maske\":\"FF\",\"muster\":[\"A4\",\"40\"],\"wenn\":{\"hk\":[1,5]}}]}", d, f));
  PRUEFE(d.eigene[0].ausloeser[0].glieder[0].muster.size() == 2 && d.eigene[0].ausloeser[0].hk.size() == 2);
}

TEST(regelwerk_feste_regeln_vollstaendig) {
  auto v = rw::feste_regeln();
  PRUEFE(v.size() == 5);
  for (int i = 0; i < 5; i++) PRUEFE(v[i].nr == i + 1 && v[i].name == "R" + std::to_string(i + 1));
  PRUEFE(v[0].lesen == 2 && v[1].lesen == 2 && v[2].lesen == 1 && v[3].lesen == 1 && v[4].lesen == 1);
  // keine feste Regel sieht auf die eigenen Anfragen/Antworten
  for (auto &r : v)
    for (auto &a : r.ausloeser)
      for (auto &g : a.glieder) PRUEFE(!rl::gesperrte_id(g.can_id));
  PRUEFE(v[3].ausloeser.back().art == vd::Art::BAUSTEIN && v[3].ausloeser.back().baustein == "r4_vorlauf");
}
