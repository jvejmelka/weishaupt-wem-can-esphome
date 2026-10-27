#include "esphome/core/defines.h"
#ifdef USE_WC_SD
#include "sd_karte.h"
#include "esphome/core/log.h"
#include <driver/sdspi_host.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

namespace esphome {
namespace weishaupt_can {

static const char *const TAG = "sd";
static const char *const WURZEL = "/sd";

void SdKarte::einbinden() {
  spi_bus_config_t bus = {};
  bus.mosi_io_num = GPIO_NUM_15;
  bus.miso_io_num = GPIO_NUM_2;
  bus.sclk_io_num = GPIO_NUM_14;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 4096;
  esp_err_t e = spi_bus_initialize(SPI2_HOST, &bus, SDSPI_DEFAULT_DMA);
  if (e != ESP_OK) { this->fehler = std::string("SPI: ") + esp_err_to_name(e); ESP_LOGW(TAG, "%s", this->fehler.c_str()); return; }
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI2_HOST;
  host.max_freq_khz = 10000;   // konservativ: lange Leitungen auf dem Board, Strapping-Pin als MISO
  sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
  dev.gpio_cs = GPIO_NUM_13;
  dev.host_id = SPI2_HOST;
  esp_vfs_fat_sdmmc_mount_config_t cfg = {};
  cfg.format_if_mount_failed = false;   // fremde Karten nie formatieren
  cfg.max_files = 4;
  cfg.allocation_unit_size = 16 * 1024;
  sdmmc_card_t *karte = nullptr;
  e = esp_vfs_fat_sdspi_mount(WURZEL, &host, &dev, &cfg, &karte);
  if (e != ESP_OK) {
    this->fehler = e == ESP_ERR_TIMEOUT || e == ESP_ERR_INVALID_RESPONSE ? "keine Karte" : esp_err_to_name(e);
    ESP_LOGI(TAG, "keine nutzbare Karte (%s)", esp_err_to_name(e));
    spi_bus_free(SPI2_HOST);
    return;
  }
  this->eingebunden = true;
  this->name = karte->cid.name;
  this->groesse = (uint64_t) karte->csd.capacity * karte->csd.sector_size;
  this->frei_messen();
  ESP_LOGI(TAG, "Karte %s, %llu MB, frei %llu MB", this->name.c_str(), this->groesse >> 20, this->frei >> 20);
}

void SdKarte::frei_messen() {
  if (!this->eingebunden) return;
  uint64_t gesamt = 0, frei = 0;
  if (esp_vfs_fat_info(WURZEL, &gesamt, &frei) == ESP_OK) this->frei = frei;
}

}  // namespace weishaupt_can
}  // namespace esphome
#endif
