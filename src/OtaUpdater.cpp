#include "OtaUpdater.h"

#include "LteModem.h"
#include "MqttHandler.h"
#include "WsUtils.h"
#include <Update.h>
#include <TinyGsmClient.h>
#include <cstring>

namespace
{
  static constexpr size_t OTA_URL_MAX = 192;
  static constexpr size_t OTA_MD5_MAX = 33;
  static constexpr uint32_t OTA_MAX_IMAGE = 3UL * 1024UL * 1024UL;

  struct OtaRequest
  {
    char url[OTA_URL_MAX];
    char md5[OTA_MD5_MAX];
  };

  volatile bool s_running = false;

  void report(const char *message)
  {
    Serial.printf("[OTA] %s\n", message);
    wsSend(message);
    // OTA state is an event, so fleet dashboards and other MQTT consumers can
    // observe progress without depending on the originating command session.
    mqttPublishEvent(message);
    mqttPublishResponse(message);
  }

  bool parseUrl(const char *url, char *host, size_t hostLen, uint16_t &port,
               char *path, size_t pathLen)
  {
    if (!url || strncmp(url, "http://", 7) != 0)
      return false;

    const char *start = url + 7;
    const char *slash = strchr(start, '/');
    const char *end = slash ? slash : start + strlen(start);
    const char *colon = nullptr;
    for (const char *p = start; p < end; ++p)
      if (*p == ':') colon = p;

    const char *hostEnd = colon ? colon : end;
    if (hostEnd == start || (size_t)(hostEnd - start) >= hostLen)
      return false;
    memcpy(host, start, (size_t)(hostEnd - start));
    host[hostEnd - start] = '\0';

    port = 80;
    if (colon)
    {
      long parsed = strtol(colon + 1, nullptr, 10);
      if (parsed < 1 || parsed > 65535) return false;
      port = (uint16_t)parsed;
    }

    const char *pathStart = slash ? slash : "/";
    if (strlen(pathStart) >= pathLen) return false;
    strncpy(path, pathStart, pathLen - 1);
    path[pathLen - 1] = '\0';
    return true;
  }

  bool readHeaderLine(TinyGsmClient &client, char *line, size_t lineLen,
                      uint32_t timeoutMs = 15000)
  {
    size_t used = 0;
    uint32_t started = millis();
    while (millis() - started < timeoutMs && used + 1 < lineLen)
    {
      while (client.available())
      {
        char c = (char)client.read();
        if (c == '\n')
        {
          line[used] = '\0';
          if (used && line[used - 1] == '\r') line[--used] = '\0';
          return true;
        }
        line[used++] = c;
      }
      delay(1);
    }
    line[used] = '\0';
    return false;
  }

  void otaTask(void *arg)
  {
    OtaRequest request = *(OtaRequest *)arg;
    delete (OtaRequest *)arg;

    char host[96], path[160];
    uint16_t port = 0;
    // Keep MQTT on its existing socket and use another modem mux channel for
    // the firmware HTTP stream. The mutex is released between chunks below.
    TinyGsmClient client(modem, 1);
    bool updateStarted = false;

    if (!parseUrl(request.url, host, sizeof(host), port, path, sizeof(path)))
    {
      report("OTA_ERROR|only_valid_http_url_supported");
      s_running = false;
      vTaskDelete(nullptr);
      return;
    }

    if (!takeModem(5000))
    {
      report("OTA_ERROR|modem_busy");
      s_running = false;
      vTaskDelete(nullptr);
      return;
    }

    report("OTA_STATUS|downloading");
    // MQTT and OTA cannot share the modem socket. The publisher will reconnect
    // after this task releases the modem mutex.
    mqttClient.disconnect();

    bool ok = client.connect(host, port, 30);
    if (!ok)
    {
      giveModem();
      report("OTA_ERROR|download_connect_failed");
      s_running = false;
      vTaskDelete(nullptr);
      return;
    }

    client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    char line[192];
    if (!readHeaderLine(client, line, sizeof(line)) || strncmp(line, "HTTP/1.1 200", 12) != 0)
    {
      client.stop();
      giveModem();
      report("OTA_ERROR|http_status_not_200");
      s_running = false;
      vTaskDelete(nullptr);
      return;
    }

    int32_t contentLength = -1;
    while (readHeaderLine(client, line, sizeof(line)) && line[0] != '\0')
    {
      if (strncasecmp(line, "Content-Length:", 15) == 0)
        contentLength = strtol(line + 15, nullptr, 10);
    }
    // No modem operation is performed while validating the size or preparing
    // the flash update. Other tasks may use the modem from this point onward.
    giveModem();
    if (contentLength <= 0 || (uint32_t)contentLength > OTA_MAX_IMAGE)
    {
      if (takeModem(2000)) { client.stop(); giveModem(); }
      report("OTA_ERROR|invalid_image_size");
      s_running = false;
      vTaskDelete(nullptr);
      return;
    }

    if (request.md5[0] && !Update.setMD5(request.md5))
    {
      if (takeModem(2000)) { client.stop(); giveModem(); }
      report("OTA_ERROR|invalid_md5");
      s_running = false;
      vTaskDelete(nullptr);
      return;
    }
    if (!Update.begin((size_t)contentLength))
    {
      if (takeModem(2000)) { client.stop(); giveModem(); }
      report("OTA_ERROR|flash_begin_failed");
      s_running = false;
      vTaskDelete(nullptr);
      return;
    }
    updateStarted = true;

    uint8_t buffer[1024];
    int32_t received = 0;
    uint32_t lastReport = 0;
    while (received < contentLength)
    {
      if (!takeModem(2000))
      {
        report("OTA_ERROR|modem_busy_during_download");
        break;
      }
      int available = client.available();
      if (available <= 0)
      {
        bool connected = client.connected();
        giveModem();
        if (!connected) break;
        delay(2);
        continue;
      }
      size_t want = (size_t)available;
      if (want > sizeof(buffer)) want = sizeof(buffer);
      int read = client.read(buffer, want);
      if (read <= 0)
      {
        giveModem();
        break;
      }
      size_t written = Update.write(buffer, (size_t)read);
      giveModem();
      if (written != (size_t)read) break;
      received += read;
      if (millis() - lastReport > 2000)
      {
        lastReport = millis();
        char progress[64];
        snprintf(progress, sizeof(progress), "OTA_PROGRESS|%ld|%ld", (long)received, (long)contentLength);
        report(progress);
      }
    }
    if (takeModem(2000)) { client.stop(); giveModem(); }

    bool success = updateStarted && received == contentLength && Update.end();
    if (!success)
    {
      if (updateStarted) Update.abort();
      report(received == contentLength ? "OTA_ERROR|image_validation_failed" : "OTA_ERROR|incomplete_download");
      s_running = false;
      vTaskDelete(nullptr);
      return;
    }

    report("OTA_SUCCESS|rebooting");
    delay(2000);
    ESP.restart();
  }
}

bool otaStartFromModem(const char *url, const char *md5)
{
  if (s_running || !url || !*url || strlen(url) >= OTA_URL_MAX || (md5 && strlen(md5) >= OTA_MD5_MAX))
    return false;

  OtaRequest *request = new OtaRequest();
  if (!request) return false;
  strncpy(request->url, url, sizeof(request->url) - 1);
  request->url[sizeof(request->url) - 1] = '\0';
  strncpy(request->md5, md5 ? md5 : "", sizeof(request->md5) - 1);
  request->md5[sizeof(request->md5) - 1] = '\0';

  s_running = true;
  if (xTaskCreatePinnedToCore(otaTask, "OTA", 8192, request, 3, nullptr, 1) != pdPASS)
  {
    delete request;
    s_running = false;
    return false;
  }
  return true;
}

bool otaIsRunning()
{
  return s_running;
}
