// Copyright 2021 BotanicFields, Inc.
// for RTCx(PCf8563) and NTP
//
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <lwip/dns.h>
#include "BF_Pcf8563.h"
#include "BF_RtcxNtp.h"
#include "BF_Log.h"

volatile bool sntp_sync_status_complete(false);
static bool   _ever_synced(false);

// 30s retry until first sync; 3600s after (NICT: max 20 requests/hour)
const uint32_t sntp_interval_initial_ms(30000);
const uint32_t sntp_interval_regular_ms(3600000);

static char      _ntp_server[64]   = "";
static IPAddress _gateway_addr(192, 168, 1, 1);
static IPAddress _internet_addr(8, 8, 8, 8);
static IPAddress _dns_override(0, 0, 0, 0);
static bool      _dns_override_set(false);

void RtcxSetDiagHosts(const char* gateway, const char* internet)
{
  _gateway_addr.fromString(gateway);
  _internet_addr.fromString(internet);
}

void RtcxSetDnsOverride(const char* dns_ip)
{
  if (_dns_override.fromString(dns_ip))
    _dns_override_set = true;
}

static void ApplyDnsOverride()
{
  if (!_dns_override_set) return;
  ip_addr_t dns4;
  memset(&dns4, 0, sizeof(dns4));
  IP4_ADDR(&dns4.u_addr.ip4, _dns_override[0], _dns_override[1], _dns_override[2], _dns_override[3]);
  dns4.type = IPADDR_TYPE_V4;
  dns_setserver(0, &dns4);
}

static void CheckExternalConnectivity()
{
  if (_ntp_server[0] == '\0') return;

  IPAddress ip;
  if (WiFi.hostByName(_ntp_server, ip) && ip != IPAddress(0, 0, 0, 0)) {
    LogPrintf(LOG_WARNING, "[NTP]DNS ok: %s -> %s via %s (UDP 123 may be blocked)\n",
              _ntp_server, ip.toString().c_str(), WiFi.dnsIP(0).toString().c_str());
    return;
  }

  LogPrintf(LOG_WARNING, "[NTP]DNS failed: %s (DNS servers: %s / %s)\n",
            _ntp_server, WiFi.dnsIP(0).toString().c_str(), WiFi.dnsIP(1).toString().c_str());

  // gateway check: TCP port 80
  WiFiClient client;
  bool gw_ok = client.connect(_gateway_addr, 80, 2000);
  if (gw_ok) client.stop();
  LogPrintf(LOG_WARNING, "[NET]gateway %s %s\n", _gateway_addr.toString().c_str(), gw_ok ? "reachable" : "unreachable");

  if (gw_ok) {
    // internet check: TCP port 53 (Google DNS — always open)
    bool inet_ok = client.connect(_internet_addr, 53, 3000);
    if (inet_ok) client.stop();
    LogPrintf(LOG_WARNING, "[NET]internet %s %s\n", _internet_addr.toString().c_str(), inet_ok ? "reachable" : "unreachable");
  }
}

void NtpBegin(const char* time_zone, const char* ntp_server, const char* ntp_server2)
{
  strncpy(_ntp_server, ntp_server, sizeof(_ntp_server) - 1);
  configTzTime(time_zone, ntp_server, ntp_server2);
  sntp_set_sync_interval(sntp_interval_initial_ms);  // retry every 30s until first sync
  LogPrintf(LOG_INFO, "NtpBegin: config TZ time = %s\n", time_zone);
  LogPrintf(LOG_INFO, "NtpBegin: SNTP sync mode = %d (0:IMMED 1:SMOOTH)\n", sntp_get_sync_mode());
  LogPrintf(LOG_INFO, "NtpBegin: SNTP sync interval = %dms\n", sntp_get_sync_interval());

  struct tm tm_start;
  if (getLocalTime(&tm_start)) {
    char buf[64];
    strftime(buf, sizeof(buf), "%A, %B %d %Y %H:%M:%S", &tm_start);
    LogPrintf(LOG_INFO, "NtpBegin: current time = %s\n", buf);
  } else {
    LogPrintf(LOG_WARNING, "NtpBegin: current time not available\n");
  }

  sntp_sync_status_complete = false;
  sntp_set_time_sync_notification_cb(SntpTimeSyncNotificationCallback);
}

void SntpTimeSyncNotificationCallback(struct timeval *tv)
{
  // CAUTION: called from LwIP task — do NOT call WiFiUDP (LogPrintf) here
  sntp_sync_status_t sntp_sync_status = sntp_get_sync_status();
  Serial.printf("SNTP callback: status = %d\n", (int)sntp_sync_status);
  if (sntp_sync_status == SNTP_SYNC_STATUS_COMPLETED) {
    sntp_sync_status_complete = true;
  }
}

const unsigned int sntp_status_interval_ms(60000);  // check status every 60s

bool RtcxUpdate(bool rtcx_avail)
{
  static unsigned int       sntp_status_last_ms(0);
  static sntp_sync_status_t sntp_status_prev((sntp_sync_status_t)0xff);  // sentinel: not yet checked

  if (millis() - sntp_status_last_ms >= sntp_status_interval_ms) {
    sntp_status_last_ms = millis();
    ApplyDnsOverride();  // RA RDNSS may have overwritten DNS since last check
    sntp_sync_status_t status = sntp_get_sync_status();
    if (status != sntp_status_prev) {
      sntp_status_prev = status;
      PrintSntpStatus("SNTP status changed:", status);
      if (status == SNTP_SYNC_STATUS_RESET && !_ever_synced) {
        CheckExternalConnectivity();
      }
    }
  }

  if (sntp_sync_status_complete) {
    sntp_sync_status_complete = false;

    _ever_synced = true;
    static bool first_sync_done(false);
    if (!first_sync_done) {
      first_sync_done = true;
      sntp_set_sync_interval(sntp_interval_regular_ms);
      LogPrintf(LOG_INFO, "SNTP first sync done: interval changed to %dms\n", sntp_interval_regular_ms);
    }

    struct tm tm_sync;
    getLocalTime(&tm_sync);
    char buf[64];
    strftime(buf, sizeof(buf), "%A, %B %d %Y %H:%M:%S", &tm_sync);
    LogPrintf(LOG_INFO, "SNTP sync: %s (server: %s)\n", buf, sntp_getservername(0));

    if (rtcx_avail) {
      if (rtcx.WriteTime(&tm_sync) == 0) {
        LogPrintf(LOG_INFO, "RTCx updated\n");
      }
      else {
        LogPrintf(LOG_WARNING, "RTCx update failed\n");
      }
    }
    return true;
  }
  return false;
}

void PrintSntpStatus(const char* header, sntp_sync_status_t sntp_sync_status)
{
  static const char* sntp_sync_status_str[] = {
    "SNTP_SYNC_STATUS_RESET       ",  // 0
    "SNTP_SYNC_STATUS_COMPLETED   ",  // 1
    "SNTP_SYNC_STATUS_IN_PROGRESS ",  // 2
    "sntp_sync_status invalid     ",  // 3
  };
  int sntp_sync_status_index = 3;
  if (sntp_sync_status >= 0 && sntp_sync_status <= 2) {
    sntp_sync_status_index = sntp_sync_status;
  }
  LogPrintf(LOG_INFO, "%s status = %d %s\n", header, sntp_sync_status, sntp_sync_status_str[sntp_sync_status_index]);
}

bool SetTimeFromRtcx(const char* time_zone)
{
  bool rtcx_valid(false);
  struct tm tm_init;

  setenv("TZ", time_zone, 1);
  tzset();  // assign timezone with setenv for mktime()

  if (rtcx.ReadTime(&tm_init) == 0) {
    rtcx_valid = true;
    struct timeval tv = { mktime(&tm_init), 0 };
    settimeofday(&tv, NULL);
    LogPrintf(LOG_INFO, "RTCx valid, the localtime was set\n");
  } else {
    LogPrintf(LOG_WARNING, "RTCx not valid\n");
  }
  return rtcx_valid;
}
