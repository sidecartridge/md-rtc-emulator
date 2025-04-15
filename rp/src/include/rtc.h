/**
 * File: rtc.h
 * Author: Diego Parrilla Santamaría
 * Date: July 2023
 * Copyright: 2023 - GOODDATA LABS SL
 * Description: Header file for the RTC emulator C program.
 */

#ifndef RTC_H
#define RTC_H

#include "aconfig.h"
#include "constants.h"
#include "debug.h"
#include "hardware/rtc.h"
#include "lwip/dns.h"
#include "lwip/udp.h"
#include "memfunc.h"
#include "network.h"
#include "pico/cyw43_arch.h"
#include "time.h"
#include "tprotocol.h"

// Size of the shared variables of the shared functions
#define SHARED_VARIABLE_SHARED_FUNCTIONS_SIZE \
  16  // Leave a gap for the shared variables of the shared functions

// Index for the shared variables
#define SHARED_VARIABLE_HARDWARE_TYPE 0
#define SHARED_VARIABLE_SVERSION 1
#define SHARED_VARIABLE_BUFFER_TYPE 2

#define RTCEMUL_RANDOM_TOKEN_OFFSET \
  0xF000  // Random token offset in the shared memory
#define RTCEMUL_RANDOM_TOKEN_SEED_OFFSET \
  (RTCEMUL_RANDOM_TOKEN_OFFSET + 4)  // random_token + 4 bytes
#define RTCEMUL_NTP_SUCCESS \
  (RTCEMUL_RANDOM_TOKEN_SEED_OFFSET + 4)  // random_token_seed + 4 bytes
#define RTCEMUL_DATETIME_BCD (RTCEMUL_NTP_SUCCESS + 4)  // ntp_success + 4 bytes
#define RTCEMUL_DATETIME_MSDOS \
  (RTCEMUL_DATETIME_BCD + 8)  // datetime_bcd + 8 bytes
#define RTCEMUL_OLD_XBIOS_TRAP \
  (RTCEMUL_DATETIME_MSDOS + 8)  // datetime_msdos + 8 bytes
#define RTCEMUL_REENTRY_TRAP \
  (RTCEMUL_OLD_XBIOS_TRAP + 4)                        // old_bios trap + 4 bytes
#define RTCEMUL_Y2K_PATCH (RTCEMUL_REENTRY_TRAP + 4)  // reentry_trap + 4 byte
#define RTCEMUL_SHARED_VARIABLES (RTCEMUL_Y2K_PATCH + 8)  // y2k_patch + 4 bytes

#define NTP_DEFAULT_HOST "pool.ntp.org"
#define NTP_DEFAULT_PORT 123
#define NTP_DELTA 2208988800  // seconds between 1 Jan 1900 and 1 Jan 1970
#define NTP_MSG_LEN 48        // ignore Authenticator (optional)

#define ADDRESS_HIGH_BIT 0x8000  // High bit of the address

#ifndef ROM3_GPIO
#define ROM3_GPIO 26
#endif

// The commands code is the combinatino of two bytes:
// - The most significant byte is the application code. All the commands of an
// app should have the same code
// - The least significant byte is the command code. Each command of an app
// should have a different code
#define APP_RTCEMUL 0x03  // The RTC emulator app

// APP_RTCEMUL commands
#define RTCEMUL_READ_TIME \
  (APP_RTCEMUL << 8 | 1)  // Read the time from the internal RTC
#define RTCEMUL_SAVE_VECTORS \
  (APP_RTCEMUL << 8 | 2)  // Save the vectors of the RTC emulator
#define RTCEMUL_REENTRY_LOCK \
  (APP_RTCEMUL << 8 |        \
   3)  // Command code to lock the reentry to XBIOS in the Sidecart
#define RTCEMUL_REENTRY_UNLOCK \
  (APP_RTCEMUL << 8 |          \
   4)  // Command code to unlock the reentry to XBIOS in the Sidecart
#define RTCEMUL_SET_SHARED_VAR (APP_RTCEMUL << 8 | 5)  // Set a shared variable

#define RTCEMUL_PARAMETERS_MAX_SIZE 20  // Maximum size of the parameters

typedef enum {
  RTC_SIDECART,
  RTC_DALLAS,
  RTC_AREAL,
  RTC_FMCII,
  RTC_UNKNOWN
} RTC_TYPE;

typedef struct NTP_TIME_T {
  ip_addr_t ntp_ipaddr;
  struct udp_pcb *ntp_pcb;
  bool ntp_server_found;
  bool ntp_error;
} NTP_TIME;

// DAllas RTC. Info here:
// https://pdf1.alldatasheet.es/datasheet-pdf/view/58439/DALLAS/DS1216.html
typedef struct {
  uint64_t last_magic_found;
  uint16_t retries;
  uint64_t magic_sequence_hex;
  uint8_t clock_sequence[64];
  uint8_t read_address_bit;
  uint8_t write_address_bit_zero;
  uint8_t write_address_bit_one;
  uint8_t magic_sequence[66];
  uint16_t size_magic_sequence;
  uint16_t size_clock_sequence;
  uint32_t rom_address;
} DallasClock;

int rtc_queryNTPTime();
int rtc_preinit();
int rtc_postinit();
void rtc_loop();
void __not_in_flash_func(rtc_dma_irq_handler_lookup)(void);

#endif  // RTC_H
