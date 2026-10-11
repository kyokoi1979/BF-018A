// Copyright 2025 BotanicFields, Inc.
// Serial + UDP syslog logger (RFC 3164, no external library)
//
#include <Arduino.h>
#include <WiFiUDP.h>
#include <time.h>
#include "BF_Log.h"

static WiFiUDP   _udp;
static IPAddress _server;
static uint16_t  _port;
static char      _hostname[32];
static char      _appname[32];

static bool _ready = false;

// pre-WiFi buffer: each entry = severity(1 byte) + message(null-terminated)
static char _queue[1024];
static int  _queue_len = 0;

static void SendSyslog(const char* msg, int severity)
{
  int pri = (3 << 3) | (severity & 0x07);  // LOG_DAEMON facility

  char ts[16];
  struct tm t;
  if (getLocalTime(&t)) {
    strftime(ts, sizeof(ts), "%b %e %T", &t);
  } else {
    strncpy(ts, "Jan  1 00:00:00", sizeof(ts));
  }

  char packet[320];
  snprintf(packet, sizeof(packet), "<%d>%s %s %s: %s", pri, ts, _hostname, _appname, msg);

  _udp.beginPacket(_server, _port);
  _udp.write((const uint8_t*)packet, strlen(packet));
  _udp.endPacket();
  delay(50);  // give receiver time to process each packet
}

void LogBegin(const char* server, uint16_t port, const char* hostname, const char* appname)
{
  _server.fromString(server);
  _port = port;
  strncpy(_hostname, hostname, sizeof(_hostname) - 1);
  strncpy(_appname,  appname,  sizeof(_appname)  - 1);
  _ready = true;
  Serial.printf("LogBegin: syslog server = %s:%d\n", server, port);

  // flush messages buffered before WiFi connected
  int i = 0;
  while (i < _queue_len) {
    int sev = (uint8_t)_queue[i++];
    const char* msg = _queue + i;
    SendSyslog(msg, sev);
    i += strlen(msg) + 1;
  }
  _queue_len = 0;
}

// RFC 3164: <PRI>Mmm dd HH:MM:SS HOSTNAME APPNAME: MESSAGE
void LogPrintf(int severity, const char* fmt, ...)
{
  char msg[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);

  // remove trailing newline for syslog (serial keeps it)
  Serial.print(msg);
  int len = strlen(msg);
  if (len > 0 && msg[len - 1] == '\n') msg[len - 1] = '\0';

  if (!_ready) {
    // buffer: severity(1 byte) + msg(null-terminated)
    int needed = 1 + (int)strlen(msg) + 1;
    if (_queue_len + needed <= (int)sizeof(_queue)) {
      _queue[_queue_len++] = (char)severity;
      strcpy(_queue + _queue_len, msg);
      _queue_len += strlen(msg) + 1;
    }
    return;
  }

  SendSyslog(msg, severity);
}
