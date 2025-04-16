# SidecarTridge Multi-device Real Time Clock Emulator

This is a microfirmware application for the **SidecarTridge Multi-device**, designed to emulate a Real Time Clock (RTC) for Atari computers, including the **Atari ST, STe, Mega ST, and Mega STe**.

---

## 🛠️ Setting Up the Development Environment

This project is based on the [SidecarTridge Multi-device Microfirmware App Template](https://github.com/sidecartridge/md-microfirmware-template).  
To set up your development environment, please follow the instructions provided in the [official documentation](https://docs.sidecartridge.com/sidecartridge-multidevice/programming/).

---

## 🚀 Installation

To install the RTC Emulator app on your SidecarTridge Multi-device:

1. Download the latest version of the app from the [Booster Loader releases page](https://github.com/sidecartridge/rp2-booster-bootloader/releases).
2. Launch the **Booster Loader** — a second-stage bootloader for the **Raspberry Pi Pico W** and the **SidecarTridge Multi-device**.
   > The current version is designed for the RP2040-based Pico W. Support for the RP235x family and other platforms is planned for future releases.
3. Open the Booster web interface.
4. In the **Apps** tab, select **"Real Time Clock"** from the list of available apps.
5. Click **"Download"** to install the app to your SidecarTridge’s microSD card.
6. Once installed, select the app and click **"Launch"** to activate it.

> **⚠️ WARNING:** Booster Loader is currently in **alpha**. Use at your own risk.

After launching, the app will automatically run every time your Atari computer is powered on.

---

## 🕹️ Usage

When you boot your Atari ST/STE/Mega ST/Mega STe, the app displays a **setup screen** for 5 seconds.  
If no key is pressed, the emulator will attempt to fetch the current time from the configured NTP server.

> Pressing any key will stop the countdown and keep the setup screen open.

---

### ⚙️ Setup Screen Commands

| Command | Description |
|---------|-------------|
| **[H]ost NTP** | Set the NTP server hostname (default: `pool.ntp.org`). |
| **[P]ort NTP** | Set the NTP server port (default: `123`). |
| **[U]TC Offset** | Set UTC offset in hours (e.g., `1` for CET, `2` for CEST). |
| **[Y]2K Patch** | Enable or disable the Y2K patch (default: **Enabled**). Disabling it may cause incorrect dates on older TOS versions. |
| **[T]ype** | Select the RTC emulation type: `SIDECART` (native SidecarTridge RTC) or `DS1307` (compatible interface). |
| **[E]xit to Desktop** | Exit setup and start the emulator. Time will be fetched and applied to the Atari clock. |
| **[X] Return to the Booster menu** | Exit setup and return to the Booster Loader main menu. |

---

### 🔁 System Reset Behavior

The RTC Emulator app is **resistant to system resets**. Pressing the reset button on your Atari will not stop the app — it will continue updating the system clock.

---

### 🔌 Power Cycling

Every time the Atari is powered on, the setup screen will be shown briefly before the app proceeds to fetch the current time.

---

### ❌ Time Fetch Failure

If the app cannot fetch a valid time from the NTP server:
- An error message will be displayed.
- The system clock will not be updated.
- You will be returned to the setup screen.

To proceed without a valid time, press **`E`** to exit setup and launch the app manually.

> ⚠️ Please ensure your network connection is properly configured before launching the app.

---

## 📄 License

This project is licensed under the **GNU General Public License v3.0**.  
See the [LICENSE](LICENSE) file for full terms.

---

Made with ❤️ by [SidecarTridge](https://sidecartridge.com)
