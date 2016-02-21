#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>

#include "config.h"

IPAddress localAddr(0, 0, 0, 0);
IPAddress localMask(0, 0, 0, 0);
IPAddress castAddr(0, 0, 0, 0);

EthernetUDP sock;
char packetBuffer[UDP_TX_PACKET_MAX_SIZE];

volatile long blink_until = 0;

void setup() {
  Serial.begin(9600);

  Serial.println();
  Serial.println("Initializing I/O...");

  // When pin 9 is left as INPUT, the floating means it blinks strangely
  // with ethernet activity. Configure for output to quell that.
  pinMode(9, OUTPUT);

  // PWM Output for the LED Ring on the doorbell.
  pinMode(VISUAL_PIN, OUTPUT);
  analogWrite(VISUAL_PIN, 0);

  // Digital Input for the button on the doorbell. Ground to activate.
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), fire, CHANGE);

  Serial.println("...done");


  Serial.println("Initializing Ethernet...");
  int backoff = 1;
  while (true) {
    Serial.println("Trying to get an IP address using DHCP.");
    if (Ethernet.begin(mac))
      break;
    Serial.print("Failed. Pausing for ");
    Serial.print(backoff, DEC);
    Serial.println(" seconds and retrying.");
    
    delay(backoff * 1000);
    if (backoff < 60)
      backoff *= 2;
  }
  
  Serial.println("...done");

  
  gotip();
}

void printDottedQuad(IPAddress data) {
  Serial.print(data[0], DEC);
  Serial.print('.');
  Serial.print(data[1], DEC);
  Serial.print('.');
  Serial.print(data[2], DEC);
  Serial.print('.');
}

void gotip() {
  localAddr = Ethernet.localIP();
  localMask = Ethernet.subnetMask();

  Serial.print("Address: ");
  printDottedQuad(localAddr);
  Serial.println();

  Serial.print("Netmask: ");
  printDottedQuad(localMask);
  Serial.println();
  
  for (byte thisByte = 0; thisByte < 4; thisByte++) {
    castAddr[thisByte] = (
      (localAddr[thisByte] &  localMask[thisByte]) |
      (0xFF                & ~localMask[thisByte])
    );
  }
  
  Serial.print("Broadcast: ");
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
    blink_until = now + 15000;
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

  long now = millis();
  
  if (now < blink_until) {
    
  }
  
  int packetSize = sock.parsePacket();
  if(packetSize)
  {
    const char *prefix = "brightness ";
    Serial.print("Received ");
    Serial.print(packetSize);
    Serial.print(" bytes from ");
    printDottedQuad(sock.remoteIP());
    Serial.print(':');
    Serial.print(sock.remotePort(), DEC);
    Serial.println();
    
    sock.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE);
    
    char *tokenstring = packetBuffer;
    while (true) {
      char *command = strtok(tokenstring, "\n");
      if (command == NULL)
        break;
        
      tokenstring = NULL;

      Serial.print("Command: ");
      Serial.println(command);
  
      if (strncmp(command, prefix, strlen(prefix)) == 0) {
        char *remainder = command + strlen(prefix);
        Serial.print("Got brightness command: ");
        Serial.print(remainder);
        Serial.print(" (");
        int value = atoi(remainder);
        Serial.print(value, DEC);
        Serial.println(")");
        analogWrite(VISUAL_PIN, value);
      } else {
        Serial.println("Got other command");
      }
    }
  }
  maintain_ethernet();
  delay(50);
}

void ring() {
  Serial.println("Pushed");

  sock.beginPacket(castAddr, BROADCAST_PORT);
  sock.write("ding dong\n");
  sock.endPacket();
}
