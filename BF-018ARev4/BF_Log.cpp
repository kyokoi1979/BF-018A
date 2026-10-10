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

void LogBegin(const char* server, uint16_t port, const char* hostname, const char* appname)
{
  _server.fromString(server);
  _port = port;
  strncpy(_hostname, hostname, sizeof(_hostname) - 1);
  strncpy(_appname,  appname,  sizeof(_appname)  - 1);
  Serial.printf("LogBegin: syslog server = %s:%d\n", server, port);
}

// RFC 3164: <PRI>Mmm dd HH:MM:SS HOSTNAME APPNAME: MESSAGE
// facility = LOG_DAEMON(3), pri = facility*8 + severity
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

  int pri = (3 << 3) | (severity & 0x07);  // LOG_DAEMON facility

  char ts[16];
  struct tm t;
  if (getLocalTime(&t)) {
    strftime(ts, sizeof(ts), "%b %e %T", &t);  // "Jan  1 00:00:00"
  } else {
    strncpy(ts, "Jan  1 00:00:00", sizeof(ts));
  }

  char packet[320];
  snprintf(packet, sizeof(packet), "<%d>%s %s %s: %s", pri, ts, _hostname, _appname, msg);

  _udp.beginPacket(_server, _port);
  _udp.write((const uint8_t*)packet, strlen(packet));
  _udp.endPacket();
}
