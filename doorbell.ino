#include <avr/pgmspace.h>
#include <NetEEPROM.h>
#include <NetEEPROM_defs.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <Agentuino.h>
#include <MIB.h>
#include <avr/wdt.h>

#include "config.h"

#define CMD_BRIGHTNESS "brightness "
#define CMD_PULSE      "pulse "
#define CMD_WDRESET    "wdreset"
#define CMD_WDENABLE   "wdenable"
#define CMD_AUTOPULSE  "autopulse "
#define CMD_REPROGRAM  "reprogram"
#define CMD_TRIGGER    "trigger"

IPAddress localAddr(0, 0, 0, 0);
IPAddress localMask(0, 0, 0, 0);
IPAddress castAddr(0, 0, 0, 0);

static byte RemoteIP[4] = {10, 8, 0, 255};

const char btBase[] PROGMEM = "1.3.6.1.4.1.50174";
const char ifBase[] PROGMEM = "1.3.6.1.2.1.4.20.1.1";

EthernetUDP sock;
char packetBuffer[UDP_TX_PACKET_MAX_SIZE];

volatile unsigned int auto_pulse = 0;

volatile unsigned long pulse_start = 0;
volatile unsigned long pulse_until = 0;

volatile int standing_brightness = 0;

void setup() {
  Serial.begin(9600);

  Serial.println();
  Serial.println(F("Initializing I/O..."));

  // When pin 9 is left as INPUT, the floating means it blinks strangely
  // with ethernet activity. Configure for output to quell that.
  pinMode(STATUS_PIN, OUTPUT);
  analogWrite(STATUS_PIN, 0);

  // PWM Output for the LED Ring on the doorbell.
  pinMode(VISUAL_PIN, OUTPUT);
  analogWrite(VISUAL_PIN, 0);

  // Digital Input for the button on the doorbell. Ground to activate.
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), fire, CHANGE);

  Serial.println(F("...done"));


  Serial.println(F("Initializing Ethernet..."));
  int backoff = 1;
  while (true) {
    analogWrite(STATUS_PIN, 15);
    Serial.println(F("Trying to get an IP address using DHCP."));
    if (Ethernet.begin(mac))
      break;
    Serial.print(F("Failed. Pausing for "));
    Serial.print(backoff, DEC);
    Serial.println(F(" seconds and retrying."));

    analogWrite(STATUS_PIN, 1);

    delay(backoff * 1000);
    if (backoff < 60)
      backoff *= 2;
  }
  
  Serial.println(F("...done"));

  Serial.println(F("Initializing SNMP..."));

  api_status = Agentuino.begin();
  
  if (api_status == SNMP_API_STAT_SUCCESS) {
    Agentuino.onPduReceive(myPduReceived);
    Serial.println(F("...done"));
  } else {
    Serial.println(F("..failed"));
  }
  
  analogWrite(STATUS_PIN, 255);
  delay(300);
  analogWrite(STATUS_PIN, 0);
  
  gotip();
}

void printDottedQuad(IPAddress data) {
  Serial.print(data[0], DEC);
  Serial.print('.');
  Serial.print(data[1], DEC);
  Serial.print('.');
  Serial.print(data[2], DEC);
  Serial.print('.');
  Serial.print(data[3], DEC);
}

void gotip() {
  localAddr = Ethernet.localIP();
  localMask = Ethernet.subnetMask();

  Serial.print(F("Address: "));
  printDottedQuad(localAddr);
  Serial.println();

  Serial.print(F("Netmask: "));
  printDottedQuad(localMask);
  Serial.println();
  
  for (byte thisByte = 0; thisByte < 4; thisByte++) {
    castAddr[thisByte] = (
      (localAddr[thisByte] &  localMask[thisByte]) |
      (0xFF                & ~localMask[thisByte])
    );
  }
  
  Serial.print(F("Broadcast: "));
  printDottedQuad(castAddr);
  Serial.println();

  NetEEPROM.writeNet(mac, localAddr, Ethernet.gatewayIP(), localMask); 

  sock.stop();
  sock.begin(LISTEN_PORT);
  char buf[16];

  sprintf(buf, "%hhu.%hhu.%hhu.%hhu", localAddr[0], localAddr[1], localAddr[2], localAddr[3]);
  Agentuino.Trap(buf, RemoteIP, locUpTime);
  
  sprintf(buf, "%hhu.%hhu.%hhu.%hhu", localMask[0], localMask[1], localMask[2], localMask[3]);
  Agentuino.Trap(buf, RemoteIP, locUpTime);
  
  sprintf(buf, "%hhu.%hhu.%hhu.%hhu", castAddr[0], castAddr[1], castAddr[2], castAddr[3]);
  Agentuino.Trap(buf, RemoteIP, locUpTime);

  sprintf(buf, "%hu", BROADCAST_PORT);
  Agentuino.Trap(buf, RemoteIP, locUpTime);
}

void fire() {
  static int previous;
  static long debounce_until;

  long now = millis();
  int value = digitalRead(BUTTON_PIN);
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

  if (pulse_until != 0 || pulse_start != 0) {
    unsigned long now = millis();
  
    if (pulse_until <= now) {
      Serial.println(F("Done pulsing"));
      pulse_until = 0;
      pulse_start = 0;
      Serial.print(F("Resetting output to standing value of "));
      Serial.print(standing_brightness, DEC);
      Serial.println();
      analogWrite(VISUAL_PIN, standing_brightness);
      analogWrite(STATUS_PIN, standing_brightness);
    } else {
      final_delay = 0;

      unsigned int x = (now - pulse_start) % 1000;
/*
      Serial.print("X=");
      Serial.print(x, DEC);
*/    
      if (x >= 500)
        x = 1000 - x;
/*
      Serial.print(" X'=");
      Serial.print(x, DEC);
*/
      uint8_t y = pow(2, x * 8 / 500.0) - 1;
/*
      Serial.print(" Y=");
      Serial.print(y, DEC);
      Serial.println();
*/
      analogWrite(VISUAL_PIN, y);
      analogWrite(STATUS_PIN, y);
    }
  }
  
  int packetSize = sock.parsePacket();
  if(packetSize)
  {
    Serial.print(F("Received "));
    Serial.print(packetSize);
    Serial.print(F(" bytes from "));
    printDottedQuad(sock.remoteIP());
    Serial.print(':');
    Serial.print(sock.remotePort(), DEC);
    Serial.println();
    
    memset(packetBuffer, 0, UDP_TX_PACKET_MAX_SIZE);
    sock.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE);
    
    char *tokenstring = packetBuffer;
    while (true) {
      char *command = strtok(tokenstring, "\n");
      if (command == NULL)
        break;
        
      tokenstring = NULL;

      Serial.print(F("Command: "));
      Serial.println(command);
  
      if (strncmp_PF(command, F(CMD_BRIGHTNESS), sizeof(CMD_BRIGHTNESS - 1)) == 0) {
        char *remainder = command + sizeof(CMD_BRIGHTNESS) - 1;
        Serial.print(F("Got brightness command: "));
        Serial.print(remainder);
        Serial.print(F(" ("));
        int value = atoi(remainder);
        Serial.print(value, DEC);
        Serial.println(')');
        standing_brightness = value;
        analogWrite(VISUAL_PIN, value);
        analogWrite(STATUS_PIN, value);
      } else if (strncmp_PF(command, F(CMD_PULSE), sizeof(CMD_PULSE) - 1) == 0) {
        char *remainder = command + sizeof(CMD_PULSE) - 1;
        Serial.println(F("Got pulse command: "));
        Serial.print(remainder);
        Serial.print(F(" ("));
        int value = atoi(remainder);
        Serial.print(value, DEC);
        Serial.println(')');
        pulse_start = millis();
        pulse_until = pulse_start + (value * 1000);
        Serial.print(F("Pulsing starting at "));
        Serial.print(pulse_start);
        Serial.print(F(" until "));
        Serial.print(pulse_until);
        Serial.println();
      } else if (strncmp_PF(command, F(CMD_WDENABLE), sizeof(CMD_WDENABLE)) == 0) {
        Serial.println(F("Enabling watchdog."));
        wdt_enable(WDTO_8S);
      } else if (strncmp_PF(command, F(CMD_WDRESET), sizeof(CMD_WDRESET)) == 0) {
        Serial.println(F("Resetting watchdog."));
        wdt_reset();
        Agentuino.Trap("Arduino SNMP reset WDT", RemoteIP, locUpTime);
      } else if (strncmp_PF(command, F(CMD_AUTOPULSE), sizeof(CMD_AUTOPULSE) - 1) == 0) {
        char *remainder = command + sizeof(CMD_AUTOPULSE) - 1;
        Serial.println(F("Setting automatic pulsing: "));
        Serial.print(remainder);
        Serial.print(F(" ("));
        int value = atoi(remainder);
        Serial.print(value, DEC);
        Serial.println(')');
        auto_pulse = value * 1000;
      } else if (strncmp_PF(command, F(CMD_REPROGRAM), sizeof(CMD_REPROGRAM)) == 0) {
        NetEEPROM.writeImgBad();
        wdt_disable();
        wdt_enable(WDTO_2S);
        while(1);
      } else if (strncmp_PF(command, F(CMD_TRIGGER), sizeof(CMD_TRIGGER)) == 0) {
        ring();
      } else {
        Serial.println(F("Got other command"));
      }
    }
  }

  // listen/handle for incoming SNMP requests
  Agentuino.listen();
  
  maintain_ethernet();
  delay(final_delay);
}

void ring() {
  Serial.println(F("Pushed"));

  sock.beginPacket(castAddr, BROADCAST_PORT);
  sock.print(F("ding dong\n"));
  sock.endPacket();

  // send trap
  Agentuino.Trap("ding dong", RemoteIP, locUpTime);
  //Agentuino.Trap("Arduino SNMP trap", RemoteIP, locUpTime, "1.3.6.1.4.1.28032.1.1.1");
  //Agentuino.Trap("Arduino SNMP trap", RemoteIP, locUpTime, "1.3.6.1.4.1.36061.0", "1.3.6.1.4.1.36061.3.1.1.1");

  if (auto_pulse > 0) {
    pulse_start = millis();
    pulse_until = pulse_start + 15000;
    Serial.print(F("Auto pulse starting at "));
    Serial.print(pulse_start);
    Serial.print(F(" until "));
    Serial.print(pulse_until);
    Serial.println();
  }
}

void myPduReceived()
{
  SNMP_PDU pdu;
  api_status = Agentuino.requestPdu(&pdu);
  //
  if ((pdu.type == SNMP_PDU_GET || pdu.type == SNMP_PDU_GET_NEXT || pdu.type == SNMP_PDU_SET)
    && pdu.error == SNMP_ERR_NO_ERROR && api_status == SNMP_API_STAT_SUCCESS ) {
    //
    pdu.OID.toString(oid);
    size_t oid_size;
    // Implementation SNMP GET NEXT
    if ( pdu.type == SNMP_PDU_GET_NEXT ) {
      char tmpOIDfs[SNMP_MAX_OID_LEN];
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
      } else {
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
    
    if ( strcmp_P(oid, sysDescr ) == 0 ) {
      // handle sysDescr (set/get) requests
      if ( pdu.type == SNMP_PDU_SET ) {
        // response packet from set-request - object is read-only
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = SNMP_ERR_READ_ONLY;
      } else {
        // response packet from get-request - locDescr
        status = pdu.VALUE.encode(SNMP_SYNTAX_OCTETS, locDescr);
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
        status = pdu.VALUE.encode(SNMP_SYNTAX_TIME_TICKS, locUpTime);       
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = status;
      }
      //
    } else if ( strcmp_P(oid, sysName ) == 0 ) {
      // handle sysName (set/get) requests
      if ( pdu.type == SNMP_PDU_SET ) {
        // response packet from set-request - object is read/write
        status = pdu.VALUE.decode(locName, strlen(locName)); 
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = status;
      } else {
        // response packet from get-request - locName
        status = pdu.VALUE.encode(SNMP_SYNTAX_OCTETS, locName);
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = status;
      }
      //
    } else if ( strcmp_P(oid, sysContact ) == 0 ) {
      // handle sysContact (set/get) requests
      if ( pdu.type == SNMP_PDU_SET ) {
        // response packet from set-request - object is read/write
        status = pdu.VALUE.decode(locContact, strlen(locContact)); 
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = status;
      } else {
        // response packet from get-request - locContact
        status = pdu.VALUE.encode(SNMP_SYNTAX_OCTETS, locContact);
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = status;
      }
      //
    } else if ( strcmp_P(oid, sysLocation ) == 0 ) {
      // handle sysLocation (set/get) requests
      if ( pdu.type == SNMP_PDU_SET ) {
        // response packet from set-request - object is read/write
        status = pdu.VALUE.decode(locLocation, strlen(locLocation)); 
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = status;
      } else {
        // response packet from get-request - locLocation
        status = pdu.VALUE.encode(SNMP_SYNTAX_OCTETS, locLocation);
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
