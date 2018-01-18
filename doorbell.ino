#include <avr/pgmspace.h>
#include <avr/wdt.h>

#include <stdarg.h>

#include <NetEEPROM.h>
#include <NetEEPROM_defs.h>

#include <SPI.h>

#include <Ethernet.h>
#include <EthernetUdp.h>
#include <IPAddress.h>

#include <Agentuino.h>
#include <MIB.h>

#include "config.h"

#define CMD_BRIGHTNESS "brightness "
#define CMD_PULSE      "pulse "
#define CMD_WDRESET    "wdreset"
#define CMD_WDENABLE   "wdenable"
#define CMD_AUTOPULSE  "autopulse "
#define CMD_REPROGRAM  "reprogram"
#define CMD_TRIGGER    "trigger"

const char btBase[] PROGMEM = "1.3.6.1.4.1.50174";
const char ifBase[] PROGMEM = "1.3.6.1.2.1.4.20.1.1";

const char Description[] PROGMEM = "SNMP Doorbell";
const char Contact[]     PROGMEM = "Jonathan Steinert";
const char Name[]        PROGMEM = "Front Door";
const char Location[]    PROGMEM = "Hachi's house";

#define HOST_FORMAT     "%hhu.%hhu.%hhu.%hhu"
#define HOST_OCTETS(H)  H[0], H[1], H[2], H[3]

const  uint8_t brightcurve[] PROGMEM = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4,
  4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6,
  7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 9, 10,
  10, 10, 10, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 14, 14, 14,
  15, 15, 15, 16, 16, 17, 17, 17, 18, 18, 19, 19, 19, 20, 20, 21,
  21, 22, 22, 23, 23, 24, 25, 25, 26, 26, 27, 28, 28, 29, 30, 30,
  31, 32, 32, 33, 34, 35, 35, 36, 37, 38, 39, 40, 41, 42, 42, 43,
  44, 45, 46, 48, 49, 50, 51, 52, 53, 54, 56, 57, 58, 59, 61, 62,
  64, 65, 66, 68, 69, 71, 73, 74, 76, 78, 79, 81, 83, 85, 87, 89,
  91, 93, 95, 97, 99, 101, 103, 106, 108, 111, 113, 116, 118, 121, 123, 126,
  129, 132, 135, 138, 141, 144, 147, 150, 154, 157, 161, 164, 168, 172, 175, 179,
  183, 187, 191, 196, 200, 204, 209, 214, 218, 223, 228, 233, 238, 244, 249, 255,
};

struct {
  IPAddress localAddr, localMask, castAddr;
  EthernetUDP sock;

  volatile unsigned int auto_pulse = 0;

  volatile unsigned long ring_at = 0;
  volatile unsigned long pulse_until = 0;

  volatile uint8_t standing_brightness = 0;

} state;

void setup() {
  //  Serial.begin(9600);
  debug(F("Initializing I/O..."));

  // When pin 9 is left as INPUT, the floating means it blinks strangely
  // with ethernet activity. Configure for output to quell that.
  pinMode(STATUS_PIN, OUTPUT);
  analogWrite(STATUS_PIN, 0);

  // PWM Output for the LED Ring on the doorbell.
  pinMode(RED_PIN, OUTPUT);
  analogWrite(RED_PIN, 0);

  pinMode(GREEN_PIN, OUTPUT);
  analogWrite(GREEN_PIN, 0);

  pinMode(BLUE_PIN, OUTPUT);
  analogWrite(BLUE_PIN, 0);

  // Digital Input for the button on the doorbell. Ground to activate.
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  //attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), fire, CHANGE);

  debug(F("...done"));

  analogWrite(RED_PIN, 64);
  delay(500);
  analogWrite(RED_PIN, 0);

  debug(F("Initializing Ethernet..."));
  analogWrite(GREEN_PIN, 64);
  int backoff = 1;
  while (true) {
    analogWrite(STATUS_PIN, 15);
    debug(F("Trying to get an IP address using DHCP."));
    if (Ethernet.begin(mac))
      break;
    debug(F("Failed. Pausing for %d seconds and retrying."), backoff);

    analogWrite(STATUS_PIN, 1);
    analogWrite(RED_PIN, 64);

    delay(backoff * 1000);
    if (backoff < 60)
      backoff *= 2;
  }

  delay(500);
  analogWrite(RED_PIN, 0);
  analogWrite(GREEN_PIN, 0);

  debug(F("...done"));
  

  debug(F("Initializing SNMP..."));

  api_status = Agentuino.begin();

  if (api_status == SNMP_API_STAT_SUCCESS) {
    Agentuino.onPduReceive(myPduReceived);
    debug(F("...done"));
  } else {
    debug(F("...failed"));
  }

  analogWrite(STATUS_PIN, 255);
  delay(300);
  analogWrite(STATUS_PIN, 0);

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

  state.sock.stop();
  state.sock.begin(LISTEN_PORT);
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
      analogWrite(RED_PIN,    state.standing_brightness);
      analogWrite(GREEN_PIN,  state.standing_brightness);
      analogWrite(BLUE_PIN,   state.standing_brightness);
      analogWrite(STATUS_PIN, state.standing_brightness);
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


      uint8_t r = pgm_read_byte_near(brightcurve + x);
      uint8_t g = pgm_read_byte_near(brightcurve + y);
      uint8_t b = pgm_read_byte_near(brightcurve + z);

      analogWrite(RED_PIN, r);
      analogWrite(GREEN_PIN, g);
      analogWrite(BLUE_PIN, b);

      analogWrite(STATUS_PIN, r);
    }
  }

  char packetBuffer[UDP_TX_PACKET_MAX_SIZE];
  int packetSize = state.sock.parsePacket();
  
  if (packetSize)
  {
    debug(F("Received %d bytes from " HOST_FORMAT ":%hu"), packetSize, HOST_OCTETS(state.sock.remoteIP()), state.sock.remotePort());

    memset(packetBuffer, 0, UDP_TX_PACKET_MAX_SIZE);
    state.sock.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE);

    char *tokenstring = packetBuffer;
    while (true) {
      char *command = strtok(tokenstring, "\n");
      if (command == NULL)
        break;

      tokenstring = NULL;

      debug(F("Command: %s"), command);

      if (strncmp_PF(command, F(CMD_BRIGHTNESS), sizeof(CMD_BRIGHTNESS - 1)) == 0) {
        char *remainder = command + sizeof(CMD_BRIGHTNESS) - 1;
        int value = atoi(remainder);
        debug(F("Got brightness command: %s (%d)"), remainder, value);
        state.standing_brightness = value;
        analogWrite(RED_PIN, value);
        analogWrite(GREEN_PIN, value);
        analogWrite(BLUE_PIN, value);
        analogWrite(STATUS_PIN, value);
      } else if (strncmp_PF(command, F(CMD_PULSE), sizeof(CMD_PULSE) - 1) == 0) {
        char *remainder = command + sizeof(CMD_PULSE) - 1;
        int value = atoi(remainder);
        debug(F("Got pulse command: %s (%d)"), remainder, value);
        // TODO using state.ring_at as the base for pulsing animation is probably wrong
        state.pulse_until = state.ring_at + (value * 1000);
        debug(F("Pusing starting at %d until %d"), state.ring_at, state.pulse_until);
      } else if (strncmp_PF(command, F(CMD_WDENABLE), sizeof(CMD_WDENABLE)) == 0) {
        debug(F("Enabling watchdog."));
        wdt_enable(WDTO_8S);
      } else if (strncmp_PF(command, F(CMD_WDRESET), sizeof(CMD_WDRESET)) == 0) {
        debug(F("Resetting watchdog."));
        wdt_reset();
        Agentuino.Trap("Arduino SNMP reset WDT", state.castAddr, locUpTime);
      } else if (strncmp_PF(command, F(CMD_AUTOPULSE), sizeof(CMD_AUTOPULSE) - 1) == 0) {
        char *remainder = command + sizeof(CMD_AUTOPULSE) - 1;
        int value = atoi(remainder);
        debug(F("Setting automatic pulsing: %s (%d)"), remainder, value);
        state.auto_pulse = value * 1000;
      } else if (strncmp_PF(command, F(CMD_REPROGRAM), sizeof(CMD_REPROGRAM)) == 0) {
        NetEEPROM.writeImgBad();
        wdt_disable();
        wdt_enable(WDTO_2S);
        while (1);
      } else if (strncmp_PF(command, F(CMD_TRIGGER), sizeof(CMD_TRIGGER)) == 0) {
        ring();
      } else {
        debug(F("Got unknown command: %s"), command);
      }
    }
  }

  // listen/handle for incoming SNMP requests
  Agentuino.listen();

  maintain_ethernet();
  delay(final_delay);
}

void ring() {
  debug(F("Button Pushed"));

  state.sock.beginPacket(state.castAddr, BROADCAST_PORT);
  state.sock.print(F("ding dong\n"));
  state.sock.endPacket();

  // send trap
  Agentuino.Trap("ding dong", state.castAddr, locUpTime);
  //Agentuino.Trap("Arduino SNMP trap", RemoteIP, locUpTime, "1.3.6.1.4.1.28032.1.1.1");
  //Agentuino.Trap("Arduino SNMP trap", RemoteIP, locUpTime, "1.3.6.1.4.1.36061.0", "1.3.6.1.4.1.36061.3.1.1.1");

  if (state.auto_pulse > 0) {
    state.pulse_until = state.ring_at + 15000;
    debug(F("Auto pulse starting at %d until %d"), state.ring_at, state.pulse_until);
  }
}

void myPduReceived()
{
  SNMP_PDU pdu;
  api_status = Agentuino.requestPdu(&pdu);

  if ((pdu.type == SNMP_PDU_GET || pdu.type == SNMP_PDU_GET_NEXT || pdu.type == SNMP_PDU_SET)
      && pdu.error == SNMP_ERR_NO_ERROR && api_status == SNMP_API_STAT_SUCCESS ) {
    //
    pdu.OID.toString(oid);
    size_t oid_size;

    // Implementation SNMP GET NEXT
    if ( pdu.type == SNMP_PDU_GET_NEXT ) {
      char tmpOIDfs[SNMP_MAX_OID_LEN]; // Exact matches to trigger iteration
      if ( strcmp_P( oid, sysDescr ) == 0 ) {
        strcpy_P ( oid, sysUpTime );
        strcpy_P ( tmpOIDfs, sysUpTime );
        pdu.OID.fromString(tmpOIDfs, oid_size);
      } else if ( strcmp_P(oid, sysUpTime ) == 0 ) {
        strcpy_P ( oid, sysContact );
        strcpy_P ( tmpOIDfs, sysContact );
        pdu.OID.fromString(tmpOIDfs, oid_size);
      } else if ( strcmp_P(oid, sysContact ) == 0 ) {
        strcpy_P ( oid, sysName );
        strcpy_P ( tmpOIDfs, sysName );
        pdu.OID.fromString(tmpOIDfs, oid_size);
      } else if ( strcmp_P(oid, sysName ) == 0 ) {
        strcpy_P ( oid, sysLocation );
        strcpy_P ( tmpOIDfs, sysLocation );
        pdu.OID.fromString(tmpOIDfs, oid_size);
      } else if ( strcmp_P(oid, sysLocation ) == 0 ) {
        strcpy_P ( oid, sysServices );
        strcpy_P ( tmpOIDfs, sysServices );
        pdu.OID.fromString(tmpOIDfs, oid_size);
      } else if ( strcmp_P(oid, sysServices ) == 0 ) {
        strcpy_P ( oid, "1.0" );
      } else { // Partial matches below here
        int ilen = strlen(oid);
        if ( strncmp_P(oid, sysDescr, ilen ) == 0 ) {
          strcpy_P ( oid, sysDescr );
          strcpy_P ( tmpOIDfs, sysDescr );
          pdu.OID.fromString(tmpOIDfs, oid_size);
        } else if ( strncmp_P(oid, sysUpTime, ilen ) == 0 ) {
          strcpy_P ( oid, sysUpTime );
          strcpy_P ( tmpOIDfs, sysUpTime );
          pdu.OID.fromString(tmpOIDfs, oid_size);
        } else if ( strncmp_P(oid, sysContact, ilen ) == 0 ) {
          strcpy_P ( oid, sysContact );
          strcpy_P ( tmpOIDfs, sysContact );
          pdu.OID.fromString(tmpOIDfs, oid_size);
        } else if ( strncmp_P(oid, sysName, ilen ) == 0 ) {
          strcpy_P ( oid, sysName );
          strcpy_P ( tmpOIDfs, sysName );
          pdu.OID.fromString(tmpOIDfs, oid_size);
        } else if ( strncmp_P(oid, sysLocation, ilen ) == 0 ) {
          strcpy_P ( oid, sysLocation );
          strcpy_P ( tmpOIDfs, sysLocation );
          pdu.OID.fromString(tmpOIDfs, oid_size);
        } else if ( strncmp_P(oid, sysServices, ilen ) == 0 ) {
          strcpy_P ( oid, sysServices );
          strcpy_P ( tmpOIDfs, sysServices );
          pdu.OID.fromString(tmpOIDfs, oid_size);
        }
      }
    }
    // End of implementation SNMP GET NEXT / WALK

    char temporary[32];
    memset(temporary, 0, sizeof(temporary));

    if ( strcmp_P(oid, sysDescr ) == 0 ) {
      // handle sysDescr (set/get) requests
      if ( pdu.type == SNMP_PDU_SET ) {
        // response packet from set-request - object is read-only
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = SNMP_ERR_READ_ONLY;
      } else {
        // response packet from get-request - locDescr
        strcpy_P(temporary, Description);
        status = pdu.VALUE.encode(SNMP_SYNTAX_OCTETS, temporary);
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = status;
      }
      //
    } else if ( strcmp_P(oid, sysUpTime ) == 0 ) {
      // handle sysName (set/get) requests
      if ( pdu.type == SNMP_PDU_SET ) {
        // response packet from set-request - object is read-only
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = SNMP_ERR_READ_ONLY;
      } else {
        // response packet from get-request - locUpTime
        status = pdu.VALUE.encode(SNMP_SYNTAX_TIME_TICKS, millis() / 10);
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = status;
      }
      //
    } else if ( strcmp_P(oid, sysName ) == 0 ) {
      // handle sysName (set/get) requests
      if ( pdu.type == SNMP_PDU_SET ) {
        // response packet from set-request - object is read/write
        //status = pdu.VALUE.decode(locName, strlen(locName));
        //pdu.type = SNMP_PDU_RESPONSE;
        //pdu.error = status;
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = SNMP_ERR_READ_ONLY;
      } else {
        // response packet from get-request - locName
        strcpy_P(temporary, Name);
        status = pdu.VALUE.encode(SNMP_SYNTAX_OCTETS, temporary);
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = status;
      }
      //
    } else if ( strcmp_P(oid, sysContact ) == 0 ) {
      // handle sysContact (set/get) requests
      if ( pdu.type == SNMP_PDU_SET ) {
        // response packet from set-request - object is read/write
        //status = pdu.VALUE.decode(locContact, strlen(locContact));
        //pdu.type = SNMP_PDU_RESPONSE;
        //pdu.error = status;
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = SNMP_ERR_READ_ONLY;
      } else {
        // response packet from get-request - locContact
        strcpy_P(temporary, Contact);
        status = pdu.VALUE.encode(SNMP_SYNTAX_OCTETS, temporary);
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = status;
      }
      //
    } else if ( strcmp_P(oid, sysLocation ) == 0 ) {
      // handle sysLocation (set/get) requests
      if ( pdu.type == SNMP_PDU_SET ) {
        // response packet from set-request - object is read/write
        //status = pdu.VALUE.decode(locLocation, strlen(locLocation));
        //pdu.type = SNMP_PDU_RESPONSE;
        //pdu.error = status;
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = SNMP_ERR_READ_ONLY;

      } else {
        // response packet from get-request - locLocation
        strcpy_P(temporary, Location);
        status = pdu.VALUE.encode(SNMP_SYNTAX_OCTETS, temporary);
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = status;
      }
      //
    } else if ( strcmp_P(oid, sysServices) == 0 ) {
      // handle sysServices (set/get) requests
      if ( pdu.type == SNMP_PDU_SET ) {
        // response packet from set-request - object is read-only
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = SNMP_ERR_READ_ONLY;
      } else {
        // response packet from get-request - locServices
        status = pdu.VALUE.encode(SNMP_SYNTAX_INT, locServices);
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = status;
      }
      //
    } else {
      // oid does not exist
      // response packet - object not found
      pdu.type = SNMP_PDU_RESPONSE;
      pdu.error = SNMP_ERR_NO_SUCH_NAME;
    }
    //
    Agentuino.responsePdu(&pdu);
  }
  //
  Agentuino.freePdu(&pdu);
}
