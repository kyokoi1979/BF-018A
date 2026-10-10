// Copyright 2025 BotanicFields, Inc.
// Serial + UDP syslog logger (RFC 3164, no external library)
//
#pragma once

// syslog severity constants (RFC 3164)
#define LOG_EMERG   0
#define LOG_ALERT   1
#define LOG_CRIT    2
#define LOG_ERR     3
#define LOG_WARNING 4
#define LOG_NOTICE  5
#define LOG_INFO    6
#define LOG_DEBUG   7

void LogBegin(const char* server, uint16_t port = 514,
              const char* hostname = "BF-018A", const char* appname = "JJY");
void LogPrintf(int severity, const char* fmt, ...);
