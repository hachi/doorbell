#ifndef STATE_H
#define STATE_H

#ifdef __cplusplus
extern "C" {
#endif

struct doorbell_state {
  volatile unsigned int auto_pulse = 0;

  volatile unsigned long ring_at = 0;
  volatile unsigned long pulse_until = 0;

  volatile int standing_brightness = 0;

  bool net_online = false;
  bool snmp_online = false;
};

extern struct doorbell_state state;

#ifdef  __cplusplus
}
#endif

#endif


