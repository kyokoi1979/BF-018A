// Copyright 2021 BotanicFields, Inc.
// for RTCx(PCf8563) and NTP
//
#include <Arduino.h>
#include "BF_Pcf8563.h"
#include "BF_RtcxNtp.h"
#include "BF_Log.h"

volatile bool sntp_sync_status_complete(false);

// 30s retry until first sync; 3600s after (NICT: max 20 requests/hour)
const uint32_t sntp_interval_initial_ms(30000);
const uint32_t sntp_interval_regular_ms(3600000);

void NtpBegin(const char* time_zone, const char* ntp_server, const char* ntp_server2)
{
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

const unsigned int sntp_status_interval_ms(60000);  // print status every 60s

bool RtcxUpdate(bool rtcx_avail)
{
  static unsigned int sntp_status_last_ms(0);
  if (millis() - sntp_status_last_ms >= sntp_status_interval_ms) {
    sntp_status_last_ms = millis();
    PrintSntpStatus("SNTP periodic:", sntp_get_sync_status());
  }

  if (sntp_sync_status_complete) {
    sntp_sync_status_complete = false;

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
    Serial.print("RTCx valid, the localtime was set\n");
  } else {
    Serial.print("RTCx not valid\n");
  }
  return rtcx_valid;
}
