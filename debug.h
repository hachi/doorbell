#include "doorbell.h"
#include "state.h"

void debug(const char *msg, ... ) {
  va_list argList;
  char buffer[64] = { 0 };
  va_start(argList, msg);
  int rv = vsnprintf(buffer, sizeof(buffer), msg, argList);
  va_end(argList);

  if (state.snmp_online)
    Agentuino.Trap(buffer, broadcastIP(), 0);

  // Serial.println(buffer);

  return;
}

void debug(const __FlashStringHelper *msg, ... ) {
  va_list argList;
  char buffer[64] = { 0 };
  va_start(argList, msg);
  PGM_P p = reinterpret_cast<PGM_P>(msg);
  int rv = vsnprintf_P(buffer, sizeof(buffer), p, argList);
  va_end(argList);

  if (state.snmp_online)
    Agentuino.Trap(buffer, broadcastIP(), 0);

  return;
}
