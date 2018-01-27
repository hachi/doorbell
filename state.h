#ifndef STATE_H
#define STATE_H

#ifdef __cplusplus
extern "C" {
#endif

struct doorbell_state {
  volatile long auto_pulse = 0;

  volatile long ring_at = 0;
  volatile long pulse_until = 0;

  volatile long standing_brightness = 0;

  bool net_online = false;
  bool snmp_online = false;
};

extern struct doorbell_state state;

#ifdef  __cplusplus
}
#endif

#endif


