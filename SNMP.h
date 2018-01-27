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

static void snmp_read_string_progmem(SNMP_PDU &pdu, void *storage) {
  char temporary[64];
  strlcpy_P(temporary, storage, sizeof(temporary));
  status = pdu.VALUE.encode(SNMP_SYNTAX_OCTETS, temporary);
  pdu.type = SNMP_PDU_RESPONSE;
  pdu.error = status;
}

static void snmp_read_uptime(SNMP_PDU &pdu, void *storage) {
  status = pdu.VALUE.encode(SNMP_SYNTAX_TIME_TICKS, millis() / 10);
  pdu.type = SNMP_PDU_RESPONSE;
  pdu.error = status;
}

static void snmp_read_long(SNMP_PDU &pdu, void *storage) {
  long value = *(long*)storage;
  status = pdu.VALUE.encode(SNMP_SYNTAX_INT, value);
  pdu.type = SNMP_PDU_RESPONSE;
  pdu.error = status;
}

static void snmp_read_uint16_t_function(SNMP_PDU &pdu, void *storage) {
  //typedef void (*log_func_pointer_t)(void *p, int level, const char *format, ...);
  typedef uint16_t (*uint16_func_pointer_t)();
  uint16_func_pointer_t foo = storage;
  int32_t value = foo();
  //((void(*)())e.fn)();
  //uint16_t value = (((uint16_t)())storage)();
  //uint16_t value = (storage)();
  status = pdu.VALUE.encode(SNMP_SYNTAX_INT, value);
  pdu.type = SNMP_PDU_RESPONSE;
  pdu.error = status;
}

static void snmp_write_long(SNMP_PDU &pdu, void *storage) {
  long value;
  status = pdu.VALUE.decode(&value);
  snmp_read_long(pdu, storage);
}

static void snmp_write_brightness(SNMP_PDU &pdu, void *storage) {
  snmp_write_long(pdu, storage);
  long value = *(long*)storage;
  debug(F("Got brightness command: %d"), value);
  state.standing_brightness = value;
  mono(value);
}

static void snmp_trigger_pulse(SNMP_PDU &pdu, void *storage) {
  long value;
  status = pdu.VALUE.decode(&value);
  debug(F("Got pulse command: %d"), value);
  // TODO using state.ring_at as the base for pulsing animation is probably wrong
  state.ring_at = millis();
  state.pulse_until = state.ring_at + (value * 1000);

  snmp_read_string_progmem(pdu, F("Pulse starting"));
}

static void snmp_write_autopulse(SNMP_PDU &pdu, void *storage) {
  snmp_write_long(pdu, storage);
  long value = *(long*)storage;
  debug(F("Setting automatic pulsing: %d"), value);
  state.auto_pulse = value * 1000;
}

static void snmp_trigger_wdreset(SNMP_PDU &pdu, void *storage) {
  debug(F("Resetting watchdog."));
  wdt_reset();
  Agentuino.Trap("Arduino SNMP reset WDT", broadcastIP(), 0);
  snmp_read_string_progmem(pdu, F("WD Reset"));
}

static void snmp_trigger_wdenable(SNMP_PDU &pdu, void *storage) {
  debug(F("Enabling watchdog."));
  wdt_enable(WDTO_8S);
  snmp_read_string_progmem(pdu, F("WD Enable"));
}

static void snmp_trigger_reprogram(SNMP_PDU &pdu, void *storage) {
  cli();
  NetEEPROM.writeImgBad();
  wdt_disable();
  wdt_enable(WDTO_1S);
  snmp_read_string_progmem(pdu, F("Reprogramming"));
  Agentuino.responsePdu(&pdu); // Manually send because we're about to stall the CPU
  while (1);
}

static void snmp_trigger_ring(SNMP_PDU &pdu, void *storage) {
  snmp_read_string_progmem(pdu, F("Ringing"));
  ring();
}

static void snmp_trigger_factory_reset(SNMP_PDU &pdu, void *storage) {
  snmp_read_string_progmem(pdu, F("Factory reset"));
  NetEEPROM.eraseNetSig();
}

void myPduReceived()
{
  /* From Agentuino.h implied globals :/
      extern SNMP_API_STAT_CODES api_status;
      extern SNMP_ERR_CODES status;
      extern char oid[SNMP_MAX_OID_LEN];
      extern uint32_t prevMillis;
  */

  struct oid_config {
    const char *oid;
    void *storage;
    void (*reader)(SNMP_PDU &pdu, void *storage);
    void (*writer)(SNMP_PDU &pdu, void *storage);
  };

  const struct oid_config all_oids[] = {
    {
      .oid = PSTR(OID_BASE_SYSTEM ".1.0"),
      .storage = Description,
      .reader = snmp_read_string_progmem,
      .writer = NULL,
    },
    {
      .oid = PSTR(OID_BASE_SYSTEM ".2.0"),
      .storage = ObjectID,
      .reader = snmp_read_string_progmem,
      .writer = NULL,
    },
    {
      .oid = PSTR(OID_BASE_SYSTEM ".3.0"),
      .storage = NULL,
      .reader = snmp_read_uptime,
      .writer = NULL,
    },
    {
      .oid = PSTR(OID_BASE_SYSTEM ".4.0"),
      .storage = Contact,
      .reader = snmp_read_string_progmem,
      .writer = NULL,
    },
    {
      .oid = PSTR(OID_BASE_SYSTEM ".5.0"),
      .storage = Name,
      .reader = snmp_read_string_progmem,
      .writer = NULL,
    },
    {
      .oid = PSTR(OID_BASE_SYSTEM ".6.0"),
      .storage = Location,
      .reader = snmp_read_string_progmem,
      .writer = NULL,
    },
    // +++++ ACTIONS
    { // Ring
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.1.1.0"),
      .storage = NULL,
      .reader = NULL,
      .writer = snmp_trigger_ring,
    },
    { // Pulse now
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.1.2.0"),
      .storage = NULL,
      .reader = NULL,
      .writer = snmp_trigger_pulse,
    },
    { // Reprogram
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.1.3.0"),
      .storage = NULL,
      .reader = NULL,
      .writer = snmp_trigger_reprogram,
    },
    { // Watchdog Enable
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.1.4.0"),
      .storage = NULL,
      .reader = NULL,
      .writer = snmp_trigger_wdenable,
    },
    { // Watchdog Reset
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.1.5.0"),
      .storage = NULL,
      .reader = NULL,
      .writer = snmp_trigger_wdreset,
    },
    // ++++++ SETTINGS
    { // Brightness
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.2.1.0"),
      .storage = &(state.standing_brightness),
      .reader = snmp_read_long,
      .writer = snmp_write_brightness,
    },
    { // Auto Pulse Length
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".258.1.2.2.0"),
      .storage = &(state.auto_pulse),
      .reader = snmp_read_long,
      .writer = snmp_write_autopulse,
    },
    // ++++++ MEMORY MONITORING
    {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".328.1.1.0"),
      .storage = memory_total,
      .reader = snmp_read_uint16_t_function,
      .writer = NULL,
    },
    {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".328.1.2.0"),
      .storage = memory_used,
      .reader = snmp_read_uint16_t_function,
      .writer = NULL,
    },
    {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".328.1.3.0"),
      .storage = memory_free,
      .reader = snmp_read_uint16_t_function,
      .writer = NULL,
    },
    {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".328.1.4.0"),
      .storage = memory_data,
      .reader = snmp_read_uint16_t_function,
      .writer = NULL,
    },
    {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".328.1.5.0"),
      .storage = memory_bss,
      .reader = snmp_read_uint16_t_function,
      .writer = NULL,
    },
    {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".328.1.6.0"),
      .storage = memory_heap,
      .reader = snmp_read_uint16_t_function,
      .writer = NULL,
    },
    {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".328.1.7.0"),
      .storage = memory_stack,
      .reader = snmp_read_uint16_t_function,
      .writer = NULL,
    },
    {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".328.2.1.0"),
      .storage = flash_total,
      .reader = snmp_read_uint16_t_function,
      .writer = NULL,
    },
    {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".328.2.2.0"),
      .storage = flash_used,
      .reader = snmp_read_uint16_t_function,
      .writer = NULL,
    },
    {
      .oid = PSTR(OID_BASE_BEEKEEPER_TECH ".328.2.3.0"),
      .storage = flash_free,
      .reader = snmp_read_uint16_t_function,
      .writer = NULL,
    },
    {
      .oid = PSTR("1.0"),
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
      if (oid_selection->reader == NULL) {
        pdu.type = SNMP_PDU_RESPONSE;
        pdu.error = SNMP_ERR_READ_ONLY;
      } else {
        oid_selection->writer(pdu, oid_selection->storage);
      }
      goto respond;
    } else { // SNMP_PDU_GET and SNMP_PDU_GET_NEXT
      if (oid_selection->reader == NULL) {
        snmp_read_string_progmem(pdu, F("Write-only endpoint"));
      } else {
        oid_selection->reader(pdu, oid_selection->storage);
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
