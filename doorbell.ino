#include <avr/pgmspace.h>
#include <avr/wdt.h>

#include <stdarg.h>

#include <NetEEPROM.h>
#include <NetEEPROM_defs.h>

#include <SPI.h>

#include <Ethernet.h>
#include <Dhcp.h>
#include <IPAddress.h>

#include <Agentuino.h>

#include "debug.h"
#include "LEDBrightness.h"
#include "MemoryLayout.h"
#include "SNMP.h"

#include "doorbell.h"
#include "config.h"
#include "state.h"

struct doorbell_state state;

#define HOST_FORMAT     "%hhu.%hhu.%hhu.%hhu"
#define HOST_OCTETS(H)  H[0], H[1], H[2], H[3]

void rgb(int red, int green, int blue) {
  analogWrite(RED_PIN,    red);
  analogWrite(GREEN_PIN,  green);
  analogWrite(BLUE_PIN,   blue);
  analogWrite(STATUS_PIN, red);
}

void mono(int lum) {
  rgb(lum, lum, lum);
}

void setup() {
  //  Serial.begin(9600);
  debug(F("Initializing I/O..."));

  // When pin 9 is left as INPUT, the floating means it blinks strangely
  // with ethernet activity. Configure for output to quell that.
  pinMode(STATUS_PIN, OUTPUT);
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  mono(0);

  // Digital Input for the button on the doorbell. Ground to activate.
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  //attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), fire, CHANGE);

  debug(F("...done"));

  rgb(64, 0, 0);
  delay(500);
  mono(0);

  debug(F("Initializing Ethernet..."));
  rgb(0, 64, 0);
  int backoff = 1;
  while (true) {
    uint8_t mac_copy[sizeof(mac)];
    memcpy_P(mac_copy, mac, sizeof(mac));
    debug(F("Trying to get an IP address using DHCP..."));
    if (Ethernet.begin(mac_copy)) {
      gotip();
      state.net_online = true;
      debug(F("...done"));
      break;
    }
    debug(F("...Failed. Pausing for %d seconds and retrying."), backoff);

    rgb(64, 64, 0);

    delay(backoff * 1000);
    if (backoff < 60)
      backoff *= 2;
  }

  delay(500);
  mono(0);

  debug(F("...done"));


  debug(F("Initializing SNMP..."));

  api_status = Agentuino.begin("public", "private", 0);

  if (api_status == SNMP_API_STAT_SUCCESS) {
    Agentuino.onPduReceive(myPduReceived);
    state.snmp_online = true;
    debug(F("...done"));
  } else {
    state.snmp_online = false;
    debug(F("...failed"));
  }

  rgb(0, 0, 64);
  delay(300);
  mono(0);

  debug(F("Data: %hu"), memory_data());
  debug(F("BSS: %hu"), memory_bss());
  debug(F("Heap: %hu"), memory_heap());
  debug(F("Stack: %hu"), memory_stack());
  debug(F("Total: %hu"), memory_total());
  debug(F("Free: %hu"), memory_free());
}

void gotip() {
  debug(F("Address: " HOST_FORMAT), HOST_OCTETS(Ethernet.localIP()));
  debug(F("Netmask: " HOST_FORMAT), HOST_OCTETS(Ethernet.subnetMask()));
  debug(F("Cast: " HOST_FORMAT), HOST_OCTETS(broadcastIP()));

  uint8_t mac_copy[sizeof(mac)];
  memcpy_P(mac_copy, mac, sizeof(mac));

  NetEEPROM.writeNet(mac_copy, Ethernet.localIP(), Ethernet.gatewayIP(), Ethernet.subnetMask());
}

/*
  void fire() {
  static bool debouncing = false;

  static int previous = HIGH; // Initial state, pulled low for button press.
  static long debounce_until;

  long now = millis();
  int value = digitalRead(BUTTON_PIN);

  if (value == LOW) {
    if (debouncing && debounce_until < now) {
      ring();
      debounce_until = 0;
      debouncing = false;
    } else if (debouncing) {

    }
  }
  if (value == previous)
    return;
  if (debounce_until > now)
    return;
  if (value == LOW) {
    ring();
  }

  debounce_until = now + DEBOUNCE_DELAY;
  previous = value;
  }
*/

IPAddress broadcastIP() {
  uint32_t ip = Ethernet.localIP();
  uint32_t mask = Ethernet.subnetMask();

  IPAddress ret = ((ip & mask) | (0xFFFFFFFF & ~mask));

  return ret;
}

void maintain_ethernet() {
  int status = Ethernet.maintain();

  switch (status) {

    case DHCP_CHECK_NONE:
      return;
      break;
    case DHCP_CHECK_RENEW_FAIL:
      // TODO: try again
      break;
    case DHCP_CHECK_RENEW_OK:
      break;
    case DHCP_CHECK_REBIND_FAIL:
      // TODO: try again
      break;
    case DHCP_CHECK_REBIND_OK:
      gotip();
      break;
  }
}

void loop() {
  int final_delay = 50;

  unsigned long now = millis();

  if (state.pulse_until == 0) {
    switch (digitalRead(BUTTON_PIN)) {
      case LOW: // Pressed
        if (state.ring_at == 0)
          state.ring_at = now + 50;
        else if (state.ring_at <= now)
          ring();

        final_delay = 0;
        break;
      case HIGH: // Released
        state.ring_at = 0;
        break;
    }
  } else {
    if (state.pulse_until <= now) {
      debug(F("Done pulsing"));
      state.pulse_until = 0;
      state.ring_at = 0;
      debug(F("Resetting output to standing value of %d"), state.standing_brightness);
      mono(state.standing_brightness);
    } else {
      final_delay = 0;

      unsigned int x = (now - state.ring_at) / 2 % 512;
      unsigned int y = ((now - 333) - state.ring_at) / 2 % 512;
      unsigned int z = ((now - 667) - state.ring_at) / 2 % 512;

      if (x >= 256)  // Make the second half mirror the first
        x = 511 - x; // Bias to get 0 thru  255, rather than 0 thru 256

      if (y >= 256)
        y = 511 - y;

      if (z >= 256)
        z = 511 - z;


      uint8_t r = brightness_to_pwm(x);
      uint8_t g = brightness_to_pwm(y);
      uint8_t b = brightness_to_pwm(z);

      rgb(r, g, b);
    }
  }

  // listen/handle for incoming SNMP requests
  Agentuino.listen();

  maintain_ethernet();
  delay(final_delay);
}

void ring() {
  debug(F("Button Pushed"));

  // send trap
  Agentuino.Trap("ding dong", broadcastIP(), 0);
  //Agentuino.Trap("Arduino SNMP trap", RemoteIP, locUpTime, "1.3.6.1.4.1.28032.1.1.1");
  //Agentuino.Trap("Arduino SNMP trap", RemoteIP, locUpTime, "1.3.6.1.4.1.36061.0", "1.3.6.1.4.1.36061.3.1.1.1");

  if (state.auto_pulse > 0) {
    state.pulse_until = state.ring_at + 15000;
    debug(F("Auto pulse starting at %d until %d"), state.ring_at, state.pulse_until);
  }
}
