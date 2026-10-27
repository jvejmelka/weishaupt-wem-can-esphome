#include "esphome/core/defines.h"
#ifdef USE_WC_DATEIEN
#include "dateisystem.h"
#include "esphome/core/log.h"
#include <esp_flash.h>
#include <esp_littlefs.h>
#include <esp_partition.h>
#include <cstdio>

namespace esphome {
namespace weishaupt_can {

static const char *const TAG = "dateien";
static const uint32_t BEGINN = 0x400000, GROESSE = 0x400000;   // obere 4 MB

void Dateisystem::einbinden() {
  uint32_t echt = 0, gebaut = 0;
  esp_flash_get_physical_size(nullptr, &echt);
  esp_flash_get_size(nullptr, &gebaut);
  if (echt < BEGINN + GROESSE || gebaut > BEGINN) {
    this->fehler = "kein freier Flash (" + std::to_string(echt >> 20) + " MB im Chip)";
    ESP_LOGI(TAG, "%s", this->fehler.c_str());
    return;
  }
  // die obere Haelfte ansprechbar machen (der Bootloader kennt nur die gebauten 4 MB)
  esp_flash_default_chip->size = echt;
  const esp_partition_t *p = nullptr;
  esp_err_t e = esp_partition_register_external(esp_flash_default_chip, BEGINN, GROESSE, "dateien",
                                                ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, &p);
  if (e != ESP_OK) { this->fehler = std::string("Partition: ") + esp_err_to_name(e); ESP_LOGW(TAG, "%s", this->fehler.c_str()); return; }
  esp_vfs_littlefs_conf_t c = {};
  c.base_path = "/fs";
  c.partition_label = "dateien";
  c.format_if_mount_failed = true;   // erster Start: leeren Bereich einmal formatieren (dauert einige Sekunden)
  e = esp_vfs_littlefs_register(&c);
  if (e != ESP_OK) { this->fehler = std::string("LittleFS: ") + esp_err_to_name(e); ESP_LOGW(TAG, "%s", this->fehler.c_str()); return; }
  this->bereit = true;
  this->messen();
  ESP_LOGI(TAG, "LittleFS unter /fs: %u KB, belegt %u KB", (unsigned) (this->gesamt >> 10), (unsigned) (this->belegt >> 10));
}

void Dateisystem::messen() {
  if (this->bereit) esp_littlefs_info("dateien", &this->gesamt, &this->belegt);
}

bool Dateisystem::lesen(const std::string &pfad, std::string &inhalt, size_t max) {
  FILE *f = fopen(pfad.c_str(), "rb");
  if (f == nullptr) return false;
  inhalt.clear();
  char puffer[512];
  size_t n;
  while ((n = fread(puffer, 1, sizeof(puffer), f)) > 0) {
    if (inhalt.size() + n > max) { fclose(f); return false; }
    inhalt.append(puffer, n);
  }
  fclose(f);
  return true;
}

// erst in eine Hilfsdatei schreiben, dann umbenennen - ein Stromausfall laesst die alte Datei heil
bool Dateisystem::schreiben(const std::string &pfad, const std::string &inhalt) {
  std::string neu = pfad + ".neu";
  FILE *f = fopen(neu.c_str(), "wb");
  if (f == nullptr) return false;
  bool ok = fwrite(inhalt.data(), 1, inhalt.size(), f) == inhalt.size();
  ok = fclose(f) == 0 && ok;
  if (!ok) { remove(neu.c_str()); return false; }
  remove(pfad.c_str());
  return rename(neu.c_str(), pfad.c_str()) == 0;
}

}  // namespace weishaupt_can
}  // namespace esphome
#endif
