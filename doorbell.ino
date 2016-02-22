#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <avr/wdt.h>

#include "config.h"

#define CMD_BRIGHTNESS "brightness "
#define CMD_PULSE      "pulse "
#define CMD_WDRESET    "wdreset"
#define CMD_WDENABLE   "wdenable"
#define CMD_AUTOPULSE  "autopulse "

IPAddress localAddr(0, 0, 0, 0);
IPAddress localMask(0, 0, 0, 0);
IPAddress castAddr(0, 0, 0, 0);

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

  sock.stop();
  sock.begin(LISTEN_PORT);

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
      Serial.println("Done pulsing");
      pulse_until = 0;
      pulse_start = 0;
      Serial.print("Resetting output to standing value of ");
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

      Serial.print("Command: ");
      Serial.println(command);
  
      if (strncmp(command, CMD_BRIGHTNESS, sizeof(CMD_BRIGHTNESS - 1)) == 0) {
        char *remainder = command + sizeof(CMD_BRIGHTNESS) - 1;
        Serial.print(F("Got brightness command: "));
        Serial.print(remainder);
        Serial.print(F(" ("));
        int value = atoi(remainder);
        Serial.print(value, DEC);
        Serial.println(")");
        standing_brightness = value;
        analogWrite(VISUAL_PIN, value);
        analogWrite(STATUS_PIN, value);
      } else if (strncmp(command, CMD_PULSE, sizeof(CMD_PULSE) - 1) == 0) {
        char *remainder = command + sizeof(CMD_PULSE) - 1;
        Serial.println(F("Got pulse command: "));
        Serial.print(remainder);
        Serial.print(F(" ("));
        int value = atoi(remainder);
        Serial.print(value, DEC);
        Serial.println(")");
        pulse_start = millis();
        pulse_until = pulse_start + (value * 1000);
        Serial.print("Pulsing starting at ");
        Serial.print(pulse_start);
        Serial.print(" until ");
        Serial.print(pulse_until);
        Serial.println();
      } else if (strncmp(command, CMD_WDENABLE, sizeof(CMD_WDENABLE)) == 0) {
        Serial.println(F("Enabling watchdog."));
        wdt_enable(WDTO_8S);
      } else if (strncmp(command,CMD_WDRESET, sizeof(CMD_WDRESET)) == 0) {
        Serial.println(F("Resetting watchdog."));
        wdt_reset();
      } else if (strncmp(command,CMD_AUTOPULSE, sizeof(CMD_AUTOPULSE) - 1) == 0) {
        char *remainder = command + sizeof(CMD_AUTOPULSE) - 1;
        Serial.println(F("Setting automatic pulsing: "));
        Serial.print(remainder);
        Serial.print(F(" ("));
        int value = atoi(remainder);
        Serial.print(value, DEC);
        Serial.println(")");
        auto_pulse = value * 1000;
      } else {
        Serial.println(F("Got other command"));
      }
    }
  }
  maintain_ethernet();
  delay(final_delay);
}

void ring() {
  Serial.println(F("Pushed"));

  sock.beginPacket(castAddr, BROADCAST_PORT);
  sock.write("ding dong\n");
  sock.endPacket();

  if (auto_pulse > 0) {
    pulse_start = millis();
    pulse_until = pulse_start + 15000;
    Serial.print("Auto pulse starting at ");
    Serial.print(pulse_start);
    Serial.print(" until ");
    Serial.print(pulse_until);
    Serial.println();
  }
}
