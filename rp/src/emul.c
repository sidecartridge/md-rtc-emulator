/**
 * File: emul.c
 * Author: Diego Parrilla Santamaría
 * Date: February 2025
 * Copyright: 2025 - GOODDATA LABS
 * Description: Template code for the core emulation
 */

#include "emul.h"

// inclusw in the C file to avoid multiple definitions
#include "target_firmware.h"  // Include the target firmware binary

// Command handlers
static void cmdMenu(const char *arg);
static void cmdClear(const char *arg);
static void cmdExit(const char *arg);
static void cmdHelp(const char *arg);
static void cmdBooster(const char *arg);
static void cmdY2KPatch(const char *arg);
static void cmdType(const char *arg);
static void cmdHost(const char *arg);
static void cmdPort(const char *arg);
static void cmdUTCOffset(const char *arg);

// Command table
static const Command commands[] = {
    {" ", cmdMenu},
    {"m", cmdMenu},
    {"e", cmdExit},
    {"x", cmdBooster},
    {"y", cmdY2KPatch},
    {"t", cmdType},
    {"h", cmdHost},
    {"p", cmdPort},
    {"u", cmdUTCOffset},
    {"s", term_cmdSettings},
    {"settings", term_cmdSettings},
    {"print", term_cmdPrint},
    {"save", term_cmdSave},
    {"erase", term_cmdErase},
    {"get", term_cmdGet},
    {"put_int", term_cmdPutInt},
    {"put_bool", term_cmdPutBool},
    {"put_str", term_cmdPutString},
};

// Number of commands in the table
static const size_t numCommands = sizeof(commands) / sizeof(commands[0]);

// Boot countdown
static int countdown = 0;

// Halt the contdown
static bool haltCountdown = false;

// Keep active loop or exit
static bool keepActive = true;

// Jump to the booster app
static bool jumpBooster = false;

// GEM launched
static bool gemLaunched = false;

// Do we have network or not?
static bool hasNetwork = false;

// app status
static int appStatus = APP_MODE_SETUP;

#define MAX_DOMAIN_LENGTH 255
#define MAX_LABEL_LENGTH 63

// Function to verify if a domain name is valid
static bool is_valid_domain(const char *domain) {
  if (domain == NULL) return false;
  size_t len = strlen(domain);
  if (len == 0 || len > MAX_DOMAIN_LENGTH) {
    return false;
  }

  int label_length = 0;
  for (size_t i = 0; i < len; i++) {
    char c = domain[i];

    if (c == '.') {
      // Dot found: end of a label
      if (label_length ==
          0) {  // Empty label (e.g., consecutive dots, leading dot)
        return false;
      }
      label_length = 0;  // Reset for the next label
    } else {
      // Check for valid characters: letters, digits, or hyphen.
      if (!(isalnum((unsigned char)c) || c == '-')) {
        return false;
      }
      // The first character of a label cannot be a hyphen.
      if (label_length == 0 && c == '-') {
        return false;
      }
      label_length++;
      if (label_length > MAX_LABEL_LENGTH) {
        return false;  // Label too long.
      }
    }
  }

  // After looping, ensure the last label is not empty and does not end with a
  // hyphen.
  if (label_length == 0 || domain[len - 1] == '-') {
    return false;
  }
  return true;
}

static void showTitle() {
  term_printString(
      "\x1B"
      "E"
      "RTC SidecarTridge Multidevice -" RELEASE_VERSION "\n");
}

static void menu(void) {
  showTitle();
  term_printString("\n\n");
  term_printString("[H]ost NTP: ");
  // Print the NTP server host
  SettingsConfigEntry *ntpHost = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_RTC_NTP_SERVER_HOST);
  if (ntpHost != NULL) {
    term_printString(ntpHost->value);
  } else {
    term_printString("Not set");
  }
  term_printString("\n[P]ort NTP: ");
  // Print the NTP server port
  SettingsConfigEntry *ntpPort = settings_find_entry(
      aconfig_getContext(), ACONFIG_PARAM_RTC_NTP_SERVER_PORT);
  if (ntpPort != NULL) {
    term_printString(ntpPort->value);
  } else {
    term_printString("Not set");
  }
  term_printString("\n[U]TC Offset: ");
  // Print the UTC offset
  SettingsConfigEntry *utcOffset =
      settings_find_entry(aconfig_getContext(), ACONFIG_PARAM_RTC_UTC_OFFSET);
  if (utcOffset != NULL) {
    term_printString(utcOffset->value);
  } else {
    term_printString("Not set");
  }
  term_printString("\n[Y]2K Patch: ");
  // Print the Y2K patch
  SettingsConfigEntry *y2kPatch =
      settings_find_entry(aconfig_getContext(), ACONFIG_PARAM_RTC_Y2K_PATCH);
  if (y2kPatch != NULL) {
    term_printString(y2kPatch->value[0] == 't' || y2kPatch->value[0] == 'T' ||
                             y2kPatch->value[0] == 'Y' ||
                             y2kPatch->value[0] == 'y'
                         ? "Enabled"
                         : "Disabled");
  } else {
    term_printString("Not set");
  }
  term_printString("\n[T]ype:");
  // Print the RTC type
  SettingsConfigEntry *rtcType =
      settings_find_entry(aconfig_getContext(), ACONFIG_PARAM_RTC_TYPE);
  if (rtcType != NULL) {
    term_printString(rtcType->value);
  } else {
    term_printString("Not set");
  }
  term_printString("\n\n[E] Exit to desktop\n");
  term_printString("[X] Return to booster menu\n\n");

  term_printString("\n");

  term_printString("[M] Refresh this menu\n");

  term_printString("\n");

  // Display network status
  term_printString("Network status: ");
  ip_addr_t currentIp = network_getCurrentIp();

  hasNetwork = currentIp.addr != 0;
  if (hasNetwork) {
    term_printString("Connected\n");
  } else {
    term_printString("Not connected\n");
  }

  term_printString("\n");
  term_printString("Select an option: ");
}

static void showCounter(int cdown) {
  // Clear the bar
  char msg[64];
  if (cdown > 0) {
    sprintf(msg, "Boot will continue in %d seconds...", cdown);
  } else {
    showTitle();
    sprintf(msg, "Booting... Please wait...               ");
  }
  u8g2_SetDrawColor(display_getU8g2Ref(), 1);
  u8g2_DrawBox(display_getU8g2Ref(), 0,
               DISPLAY_HEIGHT - DISPLAY_TERM_CHAR_HEIGHT, DISPLAY_WIDTH,
               DISPLAY_TERM_CHAR_HEIGHT);
  u8g2_SetFont(display_getU8g2Ref(), u8g2_font_squeezed_b7_tr);
  u8g2_SetDrawColor(display_getU8g2Ref(), 0);
  u8g2_DrawStr(display_getU8g2Ref(), 0, DISPLAY_HEIGHT - 1, msg);
  u8g2_SetDrawColor(display_getU8g2Ref(), 1);
  u8g2_SetFont(display_getU8g2Ref(), u8g2_font_amstrad_cpc_extended_8f);
}

// Command handlers
void cmdMenu(const char *arg) {
  haltCountdown = true;
  menu();
}

void cmdHelp(const char *arg) {
  // term_printString("\x1B" "E" "Available commands:\n");
  term_printString("Available commands:\n");
  term_printString(" General:\n");
  term_printString("  clear   - Clear the terminal screen\n");
  term_printString("  exit    - Exit the terminal\n");
  term_printString("  help    - Show available commands\n");
  haltCountdown = true;
}

void cmdClear(const char *arg) {
  haltCountdown = true;
  term_clearScreen();
}

void cmdExit(const char *arg) {
  showTitle();
  term_printString("\n\n");
  term_printString("Exiting terminal...\n");
  // Send continue to desktop command
  haltCountdown = true;
  appStatus = APP_EMULATION_INIT;
}

void cmdBooster(const char *arg) {
  showTitle();
  term_printString("\n\n");
  term_printString("Launching Booster app...\n");
  term_printString("The computer will boot shortly...\n\n");
  term_printString("If it doesn't boot, power it on and off.\n");
  jumpBooster = true;
  keepActive = false;  // Exit the active loop
  haltCountdown = true;
}

void cmdY2KPatch(const char *arg) {
  // Y2K patch command
  SettingsConfigEntry *y2kPatch =
      settings_find_entry(aconfig_getContext(), ACONFIG_PARAM_RTC_Y2K_PATCH);
  if (y2kPatch != NULL) {
    DPRINTF("Y2K patch value: %s\n", y2kPatch->value);
    if (y2kPatch->value[0] == 't' || y2kPatch->value[0] == 'T' ||
        y2kPatch->value[0] == 'Y' || y2kPatch->value[0] == 'y') {
      // Change the Y2K patch value to true
      settings_put_bool(aconfig_getContext(), ACONFIG_PARAM_RTC_Y2K_PATCH,
                        false);
    } else {
      settings_put_bool(aconfig_getContext(), ACONFIG_PARAM_RTC_Y2K_PATCH,
                        true);
    }
    settings_save(aconfig_getContext(), true);
    haltCountdown = true;
    menu();
    display_refresh();
  } else {
    DPRINTF("Y2K patch not found in the settings.\n");
  }
}

void cmdType(const char *arg) {
  // RTC type command
  SettingsConfigEntry *rtcType =
      settings_find_entry(aconfig_getContext(), ACONFIG_PARAM_RTC_TYPE);
  if (rtcType != NULL) {
    DPRINTF("RTC type value: %s\n", rtcType->value);
    if (strcmp(rtcType->value, "SIDECART") == 0) {
      // Change the RTC type value to SIDECART
      settings_put_string(aconfig_getContext(), ACONFIG_PARAM_RTC_TYPE,
                          "DALLAS");
    } else {
      settings_put_string(aconfig_getContext(), ACONFIG_PARAM_RTC_TYPE,
                          "SIDECART");
    }
    settings_save(aconfig_getContext(), true);
    haltCountdown = true;
    menu();
    display_refresh();
  } else {
    DPRINTF("RTC type not found in the settings.\n");
  }
}

void cmdHost(const char *arg) {
  if (term_getCommandLevel() == TERM_COMMAND_LEVEL_SINGLE_KEY) {
    showTitle();
    term_printString("\n\n");
    term_printString("Enter the NTP server host:\n");
    term_setCommandLevel(TERM_COMMAND_LEVEL_DATA_INPUT);
    haltCountdown = true;
  } else {
    DPRINTF("Host command not in single key mode.\n");
    term_setCommandLevel(TERM_COMMAND_LEVEL_SINGLE_KEY);
    // Verify that the NTP server in the input buffer is valid
    // Check if the input buffer is empty
    if (strlen(term_getInputBuffer()) == 0) {
      term_printString("Invalid NTP server host.\n");
      term_printString("Press SPACE to continue...\n");
    }
    // Check if the input buffer is a valid domain name
    else if (!is_valid_domain(term_getInputBuffer())) {
      term_printString("Invalid NTP server host.\n");
      term_printString("Press SPACE to continue...\n");
    }
    // Store the NTP server host
    else {
      settings_put_string(aconfig_getContext(),
                          ACONFIG_PARAM_RTC_NTP_SERVER_HOST,
                          term_getInputBuffer());
      settings_save(aconfig_getContext(), true);
      menu();
    }
  }
}

void cmdPort(const char *arg) {
  if (term_getCommandLevel() == TERM_COMMAND_LEVEL_SINGLE_KEY) {
    showTitle();
    term_printString("\n\n");
    term_printString("Enter the NTP server port:\n");
    term_setCommandLevel(TERM_COMMAND_LEVEL_DATA_INPUT);
    haltCountdown = true;
  } else {
    DPRINTF("Port command not in single key mode.\n");
    term_setCommandLevel(TERM_COMMAND_LEVEL_SINGLE_KEY);
    // Verify that the NTP server in the input buffer is valid
    // Check if the input buffer is empty
    if (strlen(term_getInputBuffer()) == 0) {
      term_printString("Invalid NTP server port.\n");
      term_printString("Press SPACE to continue...\n");
    }
    // Convert the input buffer to an integer
    const char *input = term_getInputBuffer();
    char *endptr;
    long port = strtol(input, &endptr, 10);

    // Check if the conversion was successful and within valid port range
    if (input == endptr || *endptr != '\0' || port < 1 || port > 65535) {
      term_printString("Invalid NTP server port.\n");
      term_printString("Press SPACE to continue...\n");
    }
    // Store the NTP server host
    else {
      settings_put_string(aconfig_getContext(),
                          ACONFIG_PARAM_RTC_NTP_SERVER_PORT,
                          term_getInputBuffer());
      settings_save(aconfig_getContext(), true);
      menu();
    }
  }
}

void cmdUTCOffset(const char *arg) {
  if (term_getCommandLevel() == TERM_COMMAND_LEVEL_SINGLE_KEY) {
    showTitle();
    term_printString("\n\n");
    term_printString("Enter the UTC offset:\n");
    term_setCommandLevel(TERM_COMMAND_LEVEL_DATA_INPUT);
    haltCountdown = true;
  } else {
    DPRINTF("UTC Offset command not in single key mode.\n");
    term_setCommandLevel(TERM_COMMAND_LEVEL_SINGLE_KEY);
    // Verify that the UTC offset in the input buffer is valid
    // Check if the input buffer is empty
    if (strlen(term_getInputBuffer()) == 0) {
      term_printString("Invalid UTC offset.\n");
      term_printString("Press SPACE to continue...\n");
    }
    // Convert the input buffer to an integer
    const char *input = term_getInputBuffer();
    char *endptr;
    long utcOffset = strtol(input, &endptr, 10);

    // Check if the conversion was successful and within valid range
    if (input == endptr || *endptr != '\0' || utcOffset < -12 ||
        utcOffset > 14) {
      term_printString("Invalid UTC offset.\n");
      term_printString("Press SPACE to continue...\n");
    }
    // Store the NTP server host
    else {
      settings_put_string(aconfig_getContext(), ACONFIG_PARAM_RTC_UTC_OFFSET,
                          term_getInputBuffer());
      settings_save(aconfig_getContext(), true);
      menu();
    }
  }
}

// This section contains the functions that are called from the main loop

static bool getKeepActive() { return keepActive; }

static bool getJumpBooster() { return jumpBooster; }

static void preinit() {
  // Initialize the terminal
  term_init();

  // Clear the screen
  term_clearScreen();

  // Show the title
  showTitle();
  term_printString("\n\n");
  term_printString("Configuring network... please wait...\n");
  term_printString("or press SHIFT to boot to desktop.\n");

  display_refresh();
}

void failure(const char *message) {
  // Initialize the terminal
  term_init();

  // Clear the screen
  term_clearScreen();

  // Show the title
  showTitle();
  term_printString("\n\n");
  term_printString(message);

  display_refresh();
}

static void init(const char *folder) {
  // Set the command table
  term_setCommands(commands, numCommands);

  // Clear the screen
  term_clearScreen();

  // Init contdown
  countdown = 20;

  // Set command level
  term_setCommandLevel(TERM_COMMAND_LEVEL_SINGLE_KEY);  // Single key command

  // Display the menu
  menu();

  // Example 1: Move the cursor up one line.
  // VT52 sequence: ESC A (moves cursor up)
  // The escape sequence "\x1BA" will move the cursor up one line.
  // term_printString("\x1B" "A");
  // After moving up, print text that overwrites part of the previous line.
  // term_printString("Line 2 (modified by ESC A)\n");

  // Example 2: Move the cursor right one character.
  // VT52 sequence: ESC C (moves cursor right)
  // term_printString("\x1B" "C");
  // term_printString(" <-- Moved right with ESC C\n");

  // Example 3: Direct cursor addressing.
  // VT52 direct addressing uses ESC Y <row> <col>, where:
  //   row_char = row + 0x20, col_char = col + 0x20.
  // For instance, to move the cursor to row 0, column 10:
  //   row: 0 -> 0x20 (' ')
  //   col: 10 -> 0x20 + 10 = 0x2A ('*')
  // term_printString("\x1B" "Y" "\x20" "\x2A");
  // term_printString("Text at row 0, column 10 via ESC Y\n");

  // term_printString("\x1B" "Y" "\x2A" "\x20");

  display_refresh();
}

void emul_start() {
  // The anatomy of an app or microfirmware is as follows:
  // - The driver code running in the remote device (the computer)
  // - the driver code running in the host device (the rp2040/rp2350)
  //
  // The driver code running in the remote device is responsible for:
  // 1. Perform the emulation of the device (ex: a ROM cartridge)
  // 2. Handle the communication with the host device
  // 3. Handle the configuration of the driver (ex: the ROM file to load)
  // 4. Handle the communication with the user (ex: the terminal)
  //
  // The driver code running in the host device is responsible for:
  // 1. Handle the communication with the remote device
  // 2. Handle the configuration of the driver (ex: the ROM file to load)
  // 3. Handle the communication with the user (ex: the terminal)
  //
  // Hence, we effectively have two drivers running in two different devices
  // with different architectures and capabilities.
  //
  // Please read the documentation to learn to use the communication protocol
  // between the two devices in the tprotocol.h file.
  //

  // 1. Check if the host device must be initialized to perform the emulation
  //    of the device, or start in setup/configuration mode
  SettingsConfigEntry *appMode =
      settings_find_entry(aconfig_getContext(), ACONFIG_PARAM_MODE);
  int appModeValue = APP_MODE_SETUP;  // Setup menu
  if (appMode == NULL) {
    DPRINTF(
        "APP_MODE_SETUP not found in the configuration. Using default value\n");
  } else {
    appModeValue = atoi(appMode->value);
    DPRINTF("Start emulation in mode: %i\n", appModeValue);
  }

  // 2. Initialiaze the normal operation of the app, unless the configuration
  // option says to start the config app Or a SELECT button is (or was) pressed
  // to start the configuration section of the app

  // In this example, the flow will always start the configuration app first
  // The ROM Emulator app for example will check here if the start directly
  // in emulation mode is needed or not

  // 3. If we are here, it means the app is not in emulation mode, but in
  // setup/configuration mode

  // As a rule of thumb, the remote device (the computer) driver code must
  // be copied to the RAM of the host device where the emulation will take
  // place.
  // The code is stored as an array in the target_firmware.h file
  //
  // Copy the terminal firmware to RAM
  COPY_FIRMWARE_TO_RAM((uint16_t *)target_firmware, target_firmware_length);

  // Initialize the terminal emulator PIO programs
  // The communication between the remote (target) computer and the RP2040 is
  // done using a command protocol over the cartridge bus
  // term_dma_irq_handler_lookup is the implementation of the terminal emulator
  // using the command protocol.
  // Hence, if you want to implement your own app or microfirmware, you should
  // implement your own command handler using this protocol.
  init_romemul(NULL, term_dma_irq_handler_lookup, false);

  // After this point, the remote computer can execute the code

  // 4. During the setup/configuration mode, the driver code must interact
  // with the user to configure the device. To simplify the process, the
  // terminal emulator is used to interact with the user.
  // The terminal emulator is a simple text-based interface that allows the
  // user to configure the device using text commands.
  // If you want to use a custom app in the remote computer, you can do it.
  // But it's easier to debug and code in the rp2040

  // No SD card is needed to run the RTC

  // 5. Init the sd card
  // Initialize the display again (in case the terminal emulator changed it)
  display_setupU8g2();

  // Pre-init the stuff
  // In this example it only prints the please wait message, but can be used as
  // a place to put other code that needs to be run before the network is
  // initialized
  preinit();

  // 6. Init the network, if needed
  // It's always a good idea to wait for the network to be ready
  // Get the WiFi mode from the settings
  // If you are developing code that does not use the network, you can
  // comment this section
  // It's important to note that the network parameters are taken from the
  // global configuration of the Booster app. The network parameters are
  // ready only for the microfirmware apps.
  SettingsConfigEntry *wifiMode =
      settings_find_entry(gconfig_getContext(), PARAM_WIFI_MODE);
  wifi_mode_t wifiModeValue = WIFI_MODE_STA;
  if (wifiMode == NULL) {
    DPRINTF("No WiFi mode found in the settings. No initializing.\n");
  } else {
    wifiModeValue = (wifi_mode_t)atoi(wifiMode->value);
    if (wifiModeValue != WIFI_MODE_AP) {
      DPRINTF("WiFi mode is STA\n");
      wifiModeValue = WIFI_MODE_STA;
      int err = network_wifiInit(wifiModeValue);
      if (err != 0) {
        DPRINTF("Error initializing the network: %i. No initializing.\n", err);
      } else {
        // Set the term_loop as a callback during the polling period
        network_setPollingCallback(term_loop);
        // Connect to the WiFi network
        int maxAttempts = 3;  // or any other number defined elsewhere
        int attempt = 0;
        err = NETWORK_WIFI_STA_CONN_ERR_TIMEOUT;

        while ((attempt < maxAttempts) &&
               (err == NETWORK_WIFI_STA_CONN_ERR_TIMEOUT)) {
          err = network_wifiStaConnect();
          attempt++;

          if ((err > 0) && (err < NETWORK_WIFI_STA_CONN_ERR_TIMEOUT)) {
            DPRINTF("Error connecting to the WiFi network: %i\n", err);
          }
        }

        if (err == NETWORK_WIFI_STA_CONN_ERR_TIMEOUT) {
          DPRINTF("Timeout connecting to the WiFi network after %d attempts\n",
                  maxAttempts);
          // Optionally, return an error code here.
        }
        network_setPollingCallback(NULL);
      }
    } else {
      DPRINTF("WiFi mode is AP. No initializing.\n");
    }
  }

  // 7. Now complete the terminal emulator initialization
  // The terminal emulator is used to interact with the user to configure the
  // device.
  init(NULL);

  // Blink on
#ifdef BLINK_H
  blink_on();
#endif

  // Configure the SELECT button
  // 1. Short press: reset the device and restart the app
  // 2. Long press: reset the device and erase the flash.
  select_configure();
  select_coreWaitPush(
      reset_device,
      reset_deviceAndEraseFlash);  // Wait for the SELECT button to be pushed

  // 8. Start the main loop
  // The main loop is the core of the app. It is responsible for running the
  // app, handling the user input, and performing the tasks of the app.
  // The main loop runs until the user decides to exit.
  // For testing purposes, this app only shows commands to manage the settings
  DPRINTF("Start the app loop here\n");

  // Preconfigure the RTC
  rtc_preinit();

  absolute_time_t wifiScanTime = make_timeout_time_ms(
      WIFI_SCAN_TIME_MS);  // 3 seconds minimum for network scanning

  // Initialize the timer for decrementing the countdown
  absolute_time_t lastDecrement = get_absolute_time();

  while (getKeepActive()) {
#if PICO_CYW43_ARCH_POLL
    network_safePoll();
    cyw43_arch_wait_for_work_until(wifiScanTime);
#else
    sleep_ms(SLEEP_LOOP_MS);
    DPRINTF("Polling...\n");
#endif
    switch (appStatus) {
      case APP_EMULATION_RUNTIME: {
        // The app is running in emulation mode
        // Call the RTC loop to handle the RTC commands
        rtc_loop();
        if (!gemLaunched) {
          DPRINTF("Jumping to desktop...\n");
          SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_START);
          // sleep_ms(SLEEP_LOOP_MS * 10);
          // SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_NOP);
          gemLaunched = true;
        }
        break;
      }
      case APP_EMULATION_INIT: {
        // The app is running in initialization mode
        DPRINTF("Start runtime commands...\n");
        term_printString("\n\nQuerying NTP...");
        int res = rtc_queryNTPTime();
        if (res == 0) {
          term_printString("Time set successfully!\n");
          datetime_t rtcTime = {0};
          rtc_get_datetime(&rtcTime);
          char msg[40];
          snprintf(msg, sizeof(msg),
                   "Clock set to: %02d/%02d/%04d %02d:%02d:%02d UTC+0\n",
                   rtcTime.day, rtcTime.month, rtcTime.year, rtcTime.hour,
                   rtcTime.min, rtcTime.sec);
          term_printString(msg);

          // We can continue with the post-init process
          rtc_postinit();

          // Now we need to change the function that handles the RTC commands
          DPRINTF("Changing the RTC command handler\n");
          dma_setResponseCB(rtc_dma_irq_handler_lookup);  // Set the rtc handler
          DPRINTF("RTC command handler changed\n");

          appStatus = APP_EMULATION_RUNTIME;
        } else {
          term_printString("Error setting time :-(\n");
          appStatus = APP_MODE_SETUP;
        }
        // Check remote commands
        term_loop();
        break;
      }
      case APP_MODE_SETUP:
      default: {
        // Check remote commands
        term_loop();
        if (!haltCountdown) {
          // Check if at least one second (1,000,000 µs) has passed since the
          // last decrement
          absolute_time_t now = get_absolute_time();
          if (absolute_time_diff_us(lastDecrement, now) >= 1000000) {
            // Update the lastDecrement time for the next second
            lastDecrement = now;
            countdown--;
            showCounter(countdown);
            display_refresh();
            if (countdown <= 0) {
              haltCountdown = true;
              appStatus = APP_EMULATION_INIT;
            }
          }
        }
      }
    }
  }

  DPRINTF("Exiting the app loop...\n");

  if (jumpBooster) {
    select_coreWaitPushDisable();  // Disable the SELECT button
    sleep_ms(SLEEP_LOOP_MS);
    // We must reset the computer
    SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_RESET);
    sleep_ms(SLEEP_LOOP_MS);

    // Jump to the booster app
    reset_jump_to_booster();
  } else {
    // 9. Send CONTINUE computer command to continue booting
    SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_CONTINUE);
  }

  while (1) {
    // Wait for the computer to start
    sleep_ms(SLEEP_LOOP_MS);
  }
}