#include <avr/pgmspace.h>
#include <avr/wdt.h>

#include <stdarg.h>

#include <NetEEPROM.h>
#include <NetEEPROM_defs.h>

#include <SPI.h>

#include <Ethernet.h>
#include <IPAddress.h>

#include <Agentuino.h>

#include "LEDBrightness.h"

#include "config.h"

#define HOST_FORMAT     "%hhu.%hhu.%hhu.%hhu"
#define HOST_OCTETS(H)  H[0], H[1], H[2], H[3]

struct {
  IPAddress localAddr, localMask, castAddr;

  volatile unsigned int auto_pulse = 0;

  volatile unsigned long ring_at = 0;
  volatile unsigned long pulse_until = 0;

  volatile int standing_brightness = 0;
} state;

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
    debug(F("Trying to get an IP address using DHCP."));
    if (Ethernet.begin(mac))
      break;
    debug(F("Failed. Pausing for %d seconds and retrying."), backoff);

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
    debug(F("...done"));
  } else {
    debug(F("...failed"));
  }

  rgb(0, 0, 64);
  delay(300);
  mono(0);

  gotip();
}

void debug(const char *msg, ... ) {
  va_list argList;
  char buffer[64] = { 0 };
  va_start(argList, msg);
  int rv = vsnprintf(buffer, sizeof(buffer), msg, argList);
  va_end(argList);

  Agentuino.Trap(buffer, state.castAddr, locUpTime);
  // Serial.println(buffer);
}

void debug(const __FlashStringHelper *msg, ... ) {
  va_list argList;
  char buffer[64] = { 0 };
  va_start(argList, msg);
  PGM_P p = reinterpret_cast<PGM_P>(msg);
  int rv = vsnprintf_P(buffer, sizeof(buffer), p, argList);
  va_end(argList);

  Agentuino.Trap(buffer, state.castAddr, locUpTime);
}

void gotip() {
  state.localAddr = Ethernet.localIP();
  state.localMask = Ethernet.subnetMask();

  for (byte thisByte = 0; thisByte < 4; thisByte++) {
    state.castAddr[thisByte] = (
                                 (state.localAddr[thisByte] &  state.localMask[thisByte]) |
                                 (0xFF                      & ~state.localMask[thisByte])
                               );
  }

  debug(F("Address: " HOST_FORMAT), HOST_OCTETS(state.localAddr));
  debug(F("Netmask: " HOST_FORMAT), HOST_OCTETS(state.localMask));
  debug(F("Cast: " HOST_FORMAT ":%hu"), HOST_OCTETS(state.castAddr), BROADCAST_PORT);

  NetEEPROM.writeNet(mac, state.localAddr, Ethernet.gatewayIP(), state.localMask);
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

void maintain_ethernet() {
  int status = Ethernet.maintain();

  switch (status) {
    case 0: // Noop
      return;
      break;
    case 1: // Renew Failure
      // TODO: try again
      break;
    case 2: // Renew Success
      break;
    case 3: // Rebind Failure
      // TODO: try again
      break;
    case 4: // Rebind Success
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
  Agentuino.Trap("ding dong", state.castAddr, locUpTime);
  //Agentuino.Trap("Arduino SNMP trap", RemoteIP, locUpTime, "1.3.6.1.4.1.28032.1.1.1");
  //Agentuino.Trap("Arduino SNMP trap", RemoteIP, locUpTime, "1.3.6.1.4.1.36061.0", "1.3.6.1.4.1.36061.3.1.1.1");

  if (state.auto_pulse > 0) {
    state.pulse_until = state.ring_at + 15000;
    debug(F("Auto pulse starting at %d until %d"), state.ring_at, state.pulse_until);
  }
}

#define OID_BASE_SYSTEM           "1.3.6.1.2.1.1"
#define OID_BASE_INTERFACE        "1.3.6.1.2.1.4.20.1.1"
#define OID_BASE_BEEKEEPER_TECH   "1.3.6.1.4.1.50174"

const char Description[] PROGMEM = "SNMP Doorbell";
const char Contact[]     PROGMEM = "Jonathan Steinert";
const char Name[]        PROGMEM = "Front Door";
const char Location[]    PROGMEM = "Hachi's house";
const char ObjectID[]    PROGMEM = OID_BASE_BEEKEEPER_TECH ".258.1";


void myPduReceived()
{
  /* From Agentuino.h implied globals :/
      extern SNMP_API_STAT_CODES api_status;
      extern SNMP_ERR_CODES status;
      extern char oid[SNMP_MAX_OID_LEN];
      extern uint32_t prevMillis;
  */

  enum oid_keys {
    OID_SYS_DESCR = 0,
    OID_SYS_OBJECTID,
    OID_SYS_UPTIME,
    OID_SYS_CONTACT,
    OID_SYS_NAME,
    OID_SYS_LOCATION,
    OID_SYS_SERVICES,
    OID_BT_DB_BRIGHTNESS,
    OID_BT_DB_PULSE,
    OID_BT_DB_WDRESET,
    OID_BT_DB_WDENABLE,
    OID_BT_DB_AUTOPULSE,
    OID_BT_DB_REPROGRAM,
    OID_BT_DB_TRIGGER,
    OID_TERMINATOR,
  };

  struct oid_config {
    const char *oid;
    const byte readonly;
  };

  const struct oid_config all_oids[] = {
    [OID_SYS_DESCR] = {
      .oid = PSTR(OID_BASE_SYSTEM ".1.0"),
      .readonly = true,
    },
    [OID_SYS_OBJECTID] = {
      .oid = PSTR(OID_BASE_SYSTEM ".2.0"),
      .readonly = true,
    },
    [OID_SYS_UPTIME] = {
      .oid = PSTR(OID_BASE_SYSTEM ".3.0"),
      .readonly = true,
    },
    [OID_SYS_CONTACT] = {
      .oid = PSTR(OID_BASE_SYSTEM ".4.0"),
      .readonly = true,
    },
    [OID_SYS_NAME] = {
      .oid = PSTR(OID_BASE_SYSTEM ".5.0"),
      .readonly = true,
    },
    [OID_SYS_LOCATION] = {
      .oid = PSTR(OID_BASE_SYSTEM ".6.0"),
      .readonly = true,
    },
    [OID_SYS_SERVICES] = {
      .oid = PSTR(OID_BASE_SYSTEM ".7.0"),
      .readonly = true,
    },
    [OID_BT_DB_BRIGHTNESS] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".94062.258.1.1.0"),
      .readonly = false,
    },
    [OID_BT_DB_PULSE] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".94062.258.1.2.0"),
      .readonly = false,
    },
    [OID_BT_DB_WDRESET] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".94062.258.1.3.0"),
      .readonly = false,
    },
    [OID_BT_DB_WDENABLE] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".94062.258.1.4.0"),
      .readonly = false,
    },
    [OID_BT_DB_AUTOPULSE] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".94062.258.1.5.0"),
      .readonly = false,
    },
    [OID_BT_DB_REPROGRAM] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".94062.258.1.6.0"),
      .readonly = false,
    },
    [OID_BT_DB_TRIGGER] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".94062.258.1.7.0"),
      .readonly = false,
    },
    [OID_TERMINATOR] = {
      .oid = PSTR("1.0"),
      .readonly = false,
    },
  };

  SNMP_PDU pdu;
  api_status = Agentuino.requestPdu(&pdu);

  if (pdu.error != SNMP_ERR_NO_ERROR || api_status != SNMP_API_STAT_SUCCESS)
    goto cleanup;

  { // Lexical scope
    pdu.OID.toString(oid);
    size_t oid_size;
    int selection = -1;
    int ilen = strlen(oid);
    struct oid_config *oid_selection;

    switch (pdu.type) {
      case SNMP_PDU_GET_NEXT:
        char tmpOIDfs[SNMP_MAX_OID_LEN];

        for (int i = 0; i < (sizeof(all_oids) / sizeof(all_oids[0])) - 1; i++) {
          struct oid_config *oid_current = &(all_oids[i]);
          struct oid_config *oid_next    = &(all_oids[i + 1]);

          if (strcmp_P( oid, oid_current->oid ) == 0) { // Exact matches to iterate
            strcpy_P(oid, oid_next->oid);
            strcpy_P(tmpOIDfs, oid_next->oid);
            pdu.OID.fromString(tmpOIDfs, oid_size);
            selection = i + 1;
            break;
          }

          // I think prefix match has a bug where 1.1 is considered a prefix to 1.10, which is wrong
          if (strncmp_P(oid, oid_current->oid, ilen) == 0) { // Prefix match to search
            strcpy_P(oid, oid_current->oid);
            strcpy_P(tmpOIDfs, oid_current->oid);
            pdu.OID.fromString(tmpOIDfs, oid_size);
            selection = i;
            break;
          }
        }
        break;
      case SNMP_PDU_GET:
      case SNMP_PDU_SET:
        for (int i = 0; i < (sizeof(all_oids) / sizeof(all_oids[0])) - 1; i++) {
          struct oid_config *oid_current = pgm_read_word(&(all_oids[i]));
          if (strcmp_P(oid, oid_current->oid) == 0) { // Exact matches to iterate
            selection = i;
            break;
          }
        }
        break;
      default:
        goto cleanup;
    }

    oid_selection = pgm_read_word(&(all_oids[selection]));

    if (pdu.type == SNMP_PDU_SET) {
      if (pgm_read_byte(&(oid_selection->readonly)) > 0) {
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = SNMP_ERR_READ_ONLY;
      } else {
        int value;
        switch (selection) {
          case OID_BT_DB_BRIGHTNESS:
            status = pdu.VALUE.decode(&value);
            debug(F("Got brightness command: %d"), value);
            state.standing_brightness = value;

            mono(value);
            break;
          case OID_BT_DB_PULSE:
            status = pdu.VALUE.decode(&value);
            debug(F("Got pulse command: %d"), value);
            // TODO using state.ring_at as the base for pulsing animation is probably wrong
            state.ring_at = millis();
            state.pulse_until = state.ring_at + (value * 1000);
            debug(F("Pusing starting at %d until %d"), state.ring_at, state.pulse_until);
            break;
          case OID_BT_DB_WDRESET:
            debug(F("Resetting watchdog."));
            wdt_reset();
            Agentuino.Trap("Arduino SNMP reset WDT", state.castAddr, locUpTime);
            break;
          case OID_BT_DB_WDENABLE:
            debug(F("Enabling watchdog."));
            wdt_enable(WDTO_8S);
            break;
          case OID_BT_DB_AUTOPULSE:
            status = pdu.VALUE.decode(&value);
            debug(F("Setting automatic pulsing: %d"), value);
            state.auto_pulse = value * 1000;
            break;
          case OID_BT_DB_REPROGRAM:
            NetEEPROM.writeImgBad();
            wdt_disable();
            wdt_enable(WDTO_2S);
            while (1);
            break;
          case OID_BT_DB_TRIGGER:
            ring();
            break;
          default:
            pdu.type = SNMP_PDU_RESPONSE;
            pdu.error = SNMP_ERR_NO_SUCH_NAME;
            break;
        }
      }
      goto respond;
    } else { // SNMP_PDU_GET and SNMP_PDU_GET_NEXT

      char temporary[32];
      memset(temporary, 0, sizeof(temporary));

      switch (selection) {
        case OID_SYS_DESCR:
          strcpy_P(temporary, Description);
          status = pdu.VALUE.encode(SNMP_SYNTAX_OCTETS, temporary);
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = status;
          break;
        case OID_SYS_OBJECTID:
          strcpy_P(temporary, ObjectID);
          status = pdu.VALUE.encode(SNMP_SYNTAX_OCTETS, temporary);
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = status;
          break;
        case OID_SYS_UPTIME:
          status = pdu.VALUE.encode(SNMP_SYNTAX_TIME_TICKS, millis() / 10);
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = status;
          break;
        case OID_SYS_CONTACT:
          strcpy_P(temporary, Contact);
          status = pdu.VALUE.encode(SNMP_SYNTAX_OCTETS, temporary);
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = status;
          break;
        case OID_SYS_NAME:
          strcpy_P(temporary, Name);
          status = pdu.VALUE.encode(SNMP_SYNTAX_OCTETS, temporary);
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = status;
          break;
        case OID_SYS_LOCATION:
          strcpy_P(temporary, Location);
          status = pdu.VALUE.encode(SNMP_SYNTAX_OCTETS, temporary);
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = status;
          break;
        case OID_SYS_SERVICES:
          status = pdu.VALUE.encode(SNMP_SYNTAX_INT, 72);
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = status;
          break;
        case OID_BT_DB_BRIGHTNESS:
          status = pdu.VALUE.encode(SNMP_SYNTAX_INT, state.standing_brightness);
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = status;
          break;
        case OID_BT_DB_PULSE:
          status = pdu.VALUE.encode(SNMP_SYNTAX_INT, 2);
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = status;
          break;
        case OID_BT_DB_AUTOPULSE:
          status = pdu.VALUE.encode(SNMP_SYNTAX_INT, 3);
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = status;
          break;
        case OID_BT_DB_WDRESET:
        case OID_BT_DB_WDENABLE:
        case OID_BT_DB_REPROGRAM:
        case OID_BT_DB_TRIGGER:
          strcpy_P(temporary, PSTR("Write-only node"));
          status = pdu.VALUE.encode(SNMP_SYNTAX_OCTETS, temporary);
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = status;
          break;
        default:
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = SNMP_ERR_NO_SUCH_NAME;
          break;
      }
      goto respond;
    }
  }

  goto cleanup;

respond:

  Agentuino.responsePdu(&pdu);

cleanup:

  Agentuino.freePdu(&pdu);
}
