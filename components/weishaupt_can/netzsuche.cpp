#include "netzsuche.h"
#include "esphome/core/log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <cstring>
#include <cerrno>

namespace esphome {
namespace weishaupt_can {

static const char *const TAG = "netzsuche";
static const int GLEICHZEITIG = 4;    // der ESP32 hat nur 14 Sockets fuer alles (Webserver, MQTT, OTA)
static const int WARTEN_MS = 300;

bool Netzsuche::starten(uint32_t eigene_ip, uint32_t maske) {
  if (this->laeuft_) return false;
  (void) maske;   // bewusst nur /24: groessere Netze wuerden zu lange dauern
  this->netz_ = eigene_ip & 0x00FFFFFF;   // lwIP: erstes Oktett im niedrigsten Byte
  this->eigene_ = eigene_ip;
  this->fertig_ = 0;
  this->fehlschlaege_ = 0;
  { std::lock_guard<std::mutex> g(this->sperre_); this->gefunden_.clear(); }
  this->laeuft_ = true;
  if (xTaskCreate([](void *p) { static_cast<Netzsuche *>(p)->lauf_(); vTaskDelete(nullptr); }, "netzsuche", 4096, this, 1,
                  nullptr) != pdPASS) {
    this->laeuft_ = false;
    return false;
  }
  return true;
}

std::vector<std::string> Netzsuche::gefunden() {
  std::lock_guard<std::mutex> g(this->sperre_);
  return this->gefunden_;
}

void Netzsuche::lauf_() {
  ESP_LOGI(TAG, "suche den WEM im Netz (Port 80, je Adresse eine Anfrage)");
  for (int start = 1; start <= 254; start += GLEICHZEITIG) {
    int fd[GLEICHZEITIG];
    uint32_t ziel[GLEICHZEITIG];
    int n = 0;
    for (int h = start; h <= 254 && n < GLEICHZEITIG; h++, n++) {
      ziel[n] = this->netz_ | ((uint32_t) h << 24);
      if (ziel[n] == this->eigene_) { fd[n] = -1; continue; }   // sich selbst nicht fragen
      fd[n] = lwip_socket(AF_INET, SOCK_STREAM, 0);
      if (fd[n] < 0) { vTaskDelay(pdMS_TO_TICKS(200)); fd[n] = lwip_socket(AF_INET, SOCK_STREAM, 0); }   // kurz frei werden lassen
      if (fd[n] < 0) { this->fehlschlaege_++; continue; }
      int fl = lwip_fcntl(fd[n], F_GETFL, 0);
      lwip_fcntl(fd[n], F_SETFL, fl | O_NONBLOCK);
      sockaddr_in a = {};
      a.sin_family = AF_INET;
      a.sin_port = htons(80);
      a.sin_addr.s_addr = ziel[n];
      lwip_connect(fd[n], (sockaddr *) &a, sizeof(a));
    }
    // auf Verbindungen warten
    fd_set w;
    FD_ZERO(&w);
    int maxfd = -1;
    for (int i = 0; i < n; i++) if (fd[i] >= 0) { FD_SET(fd[i], &w); if (fd[i] > maxfd) maxfd = fd[i]; }
    timeval tv = {0, WARTEN_MS * 1000};
    if (maxfd >= 0) lwip_select(maxfd + 1, nullptr, &w, nullptr, &tv);
    for (int i = 0; i < n; i++) {
      if (fd[i] < 0) continue;
      int err = 0;
      socklen_t l = sizeof(err);
      bool offen = FD_ISSET(fd[i], &w) && lwip_getsockopt(fd[i], SOL_SOCKET, SO_ERROR, &err, &l) == 0 && err == 0;
      if (offen) {
        static const char ANFRAGE[] = "GET / HTTP/1.0\r\n\r\n";
        lwip_send(fd[i], ANFRAGE, sizeof(ANFRAGE) - 1, 0);
        fd_set r;
        FD_ZERO(&r);
        FD_SET(fd[i], &r);
        timeval t2 = {1, 0};
        char puffer[400];
        int k = 0;
        if (lwip_select(fd[i] + 1, &r, nullptr, nullptr, &t2) > 0) k = lwip_recv(fd[i], puffer, sizeof(puffer) - 1, 0);
        if (k > 0) {
          puffer[k] = 0;
          if (ist_wem(puffer)) {
            char t[16];
            uint32_t z = ziel[i];
            snprintf(t, sizeof(t), "%u.%u.%u.%u", (unsigned) (z & 0xFF), (unsigned) ((z >> 8) & 0xFF),
                     (unsigned) ((z >> 16) & 0xFF), (unsigned) (z >> 24));
            ESP_LOGI(TAG, "WEM gefunden: %s", t);
            std::lock_guard<std::mutex> g(this->sperre_);
            this->gefunden_.push_back(t);
          }
        }
      }
      lwip_close(fd[i]);
    }
    this->fertig_ += n;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  ESP_LOGI(TAG, "Netzsuche fertig (%d Adressen ohne freie Verbindung uebersprungen)", (int) this->fehlschlaege_);
  this->laeuft_ = false;
}

}  // namespace weishaupt_can
}  // namespace esphome
