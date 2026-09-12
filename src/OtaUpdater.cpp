#include "OtaUpdater.h"

#include "LteModem.h"
#include "MqttHandler.h"
#include "WsUtils.h"
#include <Update.h>
#include <TinyGsmClient.h>
#include <cstring>
#include <cctype>

namespace
{
  static constexpr size_t OTA_URL_MAX = 192;
  static constexpr size_t OTA_MD5_MAX = 33;
  static constexpr uint32_t OTA_MAX_IMAGE = OTA_MAX_IMAGE_SIZE;
  static constexpr uint32_t OTA_OVERALL_TIMEOUT_MS = OTA_MAX_DURATION_MS;
  static constexpr uint32_t OTA_CONNECT_TIMEOUT_MS = 30000;
  static constexpr uint32_t OTA_READ_TIMEOUT_MS = 15000;

  struct OtaRequest
  {
    char url[OTA_URL_MAX];
    char md5[OTA_MD5_MAX];
    bool useTls;
  };

  volatile bool s_running = false;

  void report(const char *message)
  {
    Serial.printf("[OTA] %s\n", message);
    wsSend(message);
    mqttPublishEvent(message);
    mqttPublishResponse(message);
  }

  bool isValidMd5(const char *md5)
  {
    if (!md5 || !*md5) return true;
    size_t len = strlen(md5);
    if (len != 32) return false;
    for (size_t i = 0; i < 32; ++i)
      if (!isxdigit((unsigned char)md5[i])) return false;
    return true;
  }

  bool parseUrl(const char *url, char *host, size_t hostLen, uint16_t &port,
               char *path, size_t pathLen, bool &useTls)
  {
    if (!url) return false;

    useTls = false;
    const char *start = nullptr;
    uint16_t defaultPort = 80;

    if (strncmp(url, "http://", 7) == 0)
    {
      start = url + 7;
      useTls = false;
      defaultPort = 80;
    }
    else if (strncmp(url, "https://", 8) == 0)
    {
      start = url + 8;
      useTls = true;
      defaultPort = 443;
    }
    else
    {
      return false;
    }

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

    port = defaultPort;
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

  template <typename ClientType>
  bool readHeaderLine(ClientType &client, char *line, size_t lineLen,
                      uint32_t timeoutMs = OTA_READ_TIMEOUT_MS)
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
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    line[used] = '\0';
    return false;
  }

  void otaFinishAndExit(const char *errMsg, bool modemHeld, bool updateStarted)
  {
    if (errMsg) report(errMsg);
    if (updateStarted) Update.abort();
    if (modemHeld) { otaSetModemExclusive(false); giveModem(); }
    else           { otaSetModemExclusive(false); }
    s_running = false;
    vTaskDelete(nullptr);
  }

  // doOtaDownload runs with the modem already held exclusively by otaTask.
  // The modem mutex stays held the whole time — no give/take per chunk —
  // because exclusivity guarantees no other task can interfere, and holding
  // it prevents any interleaved AT traffic from corrupting the TCP stream.
  template <typename ClientT>
  void doOtaDownload(ClientT &client, const char *host, uint16_t port,
                     const char *path, const OtaRequest &req,
                     uint32_t otaStartTime)
  {
    bool updateStarted = false;

    if (!client.connect(host, port, OTA_CONNECT_TIMEOUT_MS / 1000))
    {
      otaFinishAndExit("OTA_ERROR|download_connect_failed", true, false);
      return;
    }

    client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);

    char line[192];
    if (!readHeaderLine(client, line, sizeof(line)) || !strstr(line, " 200"))
    {
      client.stop();
      otaFinishAndExit("OTA_ERROR|http_status_not_200", true, false);
      return;
    }

    int32_t contentLength = -1;
    while (readHeaderLine(client, line, sizeof(line)) && line[0] != '\0')
    {
      if (strncasecmp(line, "Content-Length:", 15) == 0)
        contentLength = strtol(line + 15, nullptr, 10);
    }

    if (contentLength <= 0 || (uint32_t)contentLength > OTA_MAX_IMAGE)
    {
      client.stop();
      otaFinishAndExit("OTA_ERROR|invalid_image_size", true, false);
      return;
    }

    if (req.md5[0] && !Update.setMD5(req.md5))
    {
      client.stop();
      otaFinishAndExit("OTA_ERROR|invalid_md5", true, false);
      return;
    }

    if (!Update.begin((size_t)contentLength))
    {
      client.stop();
      otaFinishAndExit("OTA_ERROR|flash_begin_failed", true, false);
      return;
    }
    updateStarted = true;

    uint8_t buffer[1024];
    int32_t received = 0;
    uint32_t lastReport = 0;

    while (received < contentLength)
    {
      if (millis() - otaStartTime > OTA_OVERALL_TIMEOUT_MS)
      {
        report("OTA_ERROR|overall_timeout");
        break;
      }

      int available = client.available();
      if (available <= 0)
      {
        if (!client.connected()) break;
        vTaskDelay(pdMS_TO_TICKS(2));
        continue;
      }

      size_t want = (size_t)available;
      if (want > sizeof(buffer)) want = sizeof(buffer);
      int rd = client.read(buffer, want);
      if (rd <= 0) break;
      size_t written = Update.write(buffer, (size_t)rd);
      if (written != (size_t)rd) break;
      received += rd;

      if (millis() - lastReport > 2000)
      {
        lastReport = millis();
        char progress[64];
        snprintf(progress, sizeof(progress), "OTA_PROGRESS|%ld|%ld",
                 (long)received, (long)contentLength);
        report(progress);
      }
    }

    client.stop();

    bool success = updateStarted && received == contentLength && Update.end();
    if (!success)
    {
      if (updateStarted) Update.abort();
      report(received == contentLength ? "OTA_ERROR|image_validation_failed" : "OTA_ERROR|incomplete_download");
      otaSetModemExclusive(false);
      giveModem();
      s_running = false;
      vTaskDelete(nullptr);
      return;
    }

    otaSetModemExclusive(false);
    giveModem();
    report("OTA_SUCCESS|rebooting");
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP.restart();
  }

  void otaTask(void *arg)
  {
    OtaRequest req = *(OtaRequest *)arg;
    delete (OtaRequest *)arg;

    char host[96], path[160];
    uint16_t port = 0;
    bool useTls = false;
    uint32_t otaStartTime = millis();

    if (!parseUrl(req.url, host, sizeof(host), port, path, sizeof(path), useTls))
    {
      otaFinishAndExit("OTA_ERROR|only_valid_http_or_https_url_supported", false, false);
      return;
    }

    if (!isValidMd5(req.md5))
    {
      otaFinishAndExit("OTA_ERROR|invalid_md5_format", false, false);
      return;
    }

    // Acquire modem for the whole OTA — from here every other task's
    // takeModem() will instantly fail until we clear exclusivity.
    if (!takeModem(5000))
    {
      otaFinishAndExit("OTA_ERROR|modem_busy", false, false);
      return;
    }
    otaSetModemExclusive(true);

    report("OTA_STATUS|downloading");
    mqttClient.disconnect();

    if (useTls)
    {
#if defined(TINY_GSM_MODEM_HAS_SSL)
      TinyGsmClientSecure tlsClient(modem, 1);
      doOtaDownload(tlsClient, host, port, path, req, otaStartTime);
#else
      report("OTA_ERROR|https_not_supported_on_this_modem");
      otaSetModemExclusive(false);
      giveModem();
      s_running = false;
      vTaskDelete(nullptr);
      return;
#endif
    }
    else
    {
      TinyGsmClient plainClient(modem, 1);
      doOtaDownload(plainClient, host, port, path, req, otaStartTime);
    }
  }
}

bool otaStartFromModem(const char *url, const char *md5)
{
  if (s_running || !url || !*url || strlen(url) >= OTA_URL_MAX
      || (md5 && strlen(md5) >= OTA_MD5_MAX))
    return false;

  OtaRequest *request = new OtaRequest();
  if (!request) return false;
  strncpy(request->url, url, sizeof(request->url) - 1);
  request->url[sizeof(request->url) - 1] = '\0';
  strncpy(request->md5, md5 ? md5 : "", sizeof(request->md5) - 1);
  request->md5[sizeof(request->md5) - 1] = '\0';
  request->useTls = false;

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