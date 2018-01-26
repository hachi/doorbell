#include <Agentuino.h>

#include "MemoryLayout.h"

#include "doorbell.h"
#include "state.h"

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
    OID_BT_RAM_TOTAL,
    OID_BT_RAM_DATA,
    OID_BT_RAM_BSS,
    OID_BT_RAM_HEAP,
    OID_BT_RAM_STACK,
    OID_BT_RAM_FREE,
    OID_BT_FLASH_TOTAL,
    OID_BT_FLASH_USED,
    OID_BT_FLASH_FREE,
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
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.1.0"),
      .readonly = false,
    },
    [OID_BT_DB_PULSE] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.2.0"),
      .readonly = false,
    },
    [OID_BT_DB_WDRESET] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.3.0"),
      .readonly = false,
    },
    [OID_BT_DB_WDENABLE] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.4.0"),
      .readonly = false,
    },
    [OID_BT_DB_AUTOPULSE] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.5.0"),
      .readonly = false,
    },
    [OID_BT_DB_REPROGRAM] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.6.0"),
      .readonly = false,
    },
    [OID_BT_DB_TRIGGER] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.7.0"),
      .readonly = false,
    },
    [OID_BT_RAM_TOTAL] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.8.0"),
      .readonly = true,
    },
    [OID_BT_RAM_DATA] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.9.0"),
      .readonly = true,
    },
    [OID_BT_RAM_BSS] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.10.0"),
      .readonly = true,
    },
    [OID_BT_RAM_HEAP] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.11.0"),
      .readonly = true,
    },
    [OID_BT_RAM_STACK] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.12.0"),
      .readonly = true,
    },
    [OID_BT_RAM_FREE] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.13.0"),
      .readonly = true,
    },
    [OID_BT_FLASH_TOTAL] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.14.0"),
      .readonly = true,
    },
    [OID_BT_FLASH_USED] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.15.0"),
      .readonly = true,
    },
    [OID_BT_FLASH_FREE] = {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.16.0"),
      .readonly = true,
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
          struct oid_config *oid_current = &(all_oids[i]);
          if (strcmp_P(oid, oid_current->oid) == 0) { // Exact matches to iterate
            selection = i;
            break;
          }
        }
        break;
      default:
        goto cleanup;
    }

    struct oid_config* oid_selection = &(all_oids[selection]);

    if (pdu.type == SNMP_PDU_SET) {
      if (oid_selection->readonly) {
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

            status = pdu.VALUE.encode(SNMP_SYNTAX_INT, state.standing_brightness);
            pdu.type = SNMP_PDU_RESPONSE;
            pdu.error = status;

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
            Agentuino.Trap("Arduino SNMP reset WDT", broadcastIP(), 0);
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
      int32_t value;
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
        case OID_BT_RAM_TOTAL:
          value = memory_total();
          status = pdu.VALUE.encode(SNMP_SYNTAX_INT, value);
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = status;
          break;
        case OID_BT_RAM_DATA:
          value = memory_data();
          status = pdu.VALUE.encode(SNMP_SYNTAX_INT, value);
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = status;
          break;
        case OID_BT_RAM_BSS:
          value = memory_bss();
          status = pdu.VALUE.encode(SNMP_SYNTAX_INT, value);
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = status;
          break;
        case OID_BT_RAM_HEAP:
          value = memory_heap();
          status = pdu.VALUE.encode(SNMP_SYNTAX_INT, value);
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = status;
          break;
        case OID_BT_RAM_STACK:
          value = memory_stack();
          status = pdu.VALUE.encode(SNMP_SYNTAX_INT, value);
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = status;
          break;
        case OID_BT_RAM_FREE:
          value = memory_free();
          status = pdu.VALUE.encode(SNMP_SYNTAX_INT, value);
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = status;
          break;
        case OID_BT_FLASH_TOTAL:
          value = flash_total();
          status = pdu.VALUE.encode(SNMP_SYNTAX_INT, value);
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = status;
          break;
        case OID_BT_FLASH_USED:
          value = flash_used();
          status = pdu.VALUE.encode(SNMP_SYNTAX_INT, value);
          pdu.type = SNMP_PDU_RESPONSE;
          pdu.error = status;
          break;
        case OID_BT_FLASH_FREE:
          value = flash_free();
          status = pdu.VALUE.encode(SNMP_SYNTAX_INT, value);
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
