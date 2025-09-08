<div align="center">

# Hack3r T-Deck v1.0  

![Main UI](https://github.com/out0fstep/Hack3r-T-Deck/blob/main/Banner.png)  
<p align="center">
-=⟦ Hack3r T-Deck is a custom UI firmware designed for the LilyGO T-Deck Plus ⟧=-
</p>

-=[ **Created by** · [out0fstep](https://github.com/out0fstep) ]=-  

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](https://opensource.org/licenses/MIT)
[![Downloads](https://img.shields.io/github/downloads/out0fstep/Hack3r-T-Deck/total.svg?color=brightgreen)](https://github.com/out0fstep/Hack3r-T-Deck/releases)  
[![☕️ Buy Me a Coffee](https://img.shields.io/badge/%E2%98%95%EF%B8%8F-Buy%20Me%20a%20Coffee-yellow)](https://buymeacoffee.com/out0fstep)
[![Follow @DorkfeastTeam](https://img.shields.io/badge/follow-@DorkfeastTeam-1DA1F2?logo=x&logoColor=white)](https://x.com/DorkfeastTeam)

**Ḥą̥̥̍c̷̙̆k̘̝̰̭ T̻ȟ̔̓̀e̛̪̒̌ P̡̢̼̂l̟̑̀a̭n̨̹̖̆e̯̍ṯ̎̕!̶̐̒**

</div>

---

## 📖 What is Hack3r T-Deck?

Hack3r T-Deck is a **custom UI firmware** for the **LilyGO T-Deck Plus**  

**North Star Goal:** a **stable, good-looking daily driver** for T-Deck Plus that unifies quality UX, practical tooling, and extensibility — so you don’t have to hop between firmware as often just to get your work done.

---

## ✨ Features (current & planned)

- ✅ **USB HID** — Payload selection + deployment.
- 🚧 **Wi-Fi Tools** *(Marauder port, in progress)*
  - Sniffers: Probe Request, Beacon, Deauth, EAPOL/PMKID, Packet Monitor, Detect Pwnagotchi, Scan APs, Raw Capture
  - War-driving: Wardrive, Station Wardrive
  - Attacks: Beacon Spam (List/Random), “Rick Roll” Beacon, Probe Flood, Evil Portal, Deauth Flood/Targeted, AP Clone Spam
  - Utilities: Generate/Add/Clear SSIDs, Save/Load, Select APs/Stations
- 🚧 **BTE Tools** *(Marauder port, in progress)*
  - Sniffers: Bluetooth, Flipper, Airtag, BT Wardrive (std/continuous), Detect Card Skimmers
  - Attacks: Sour Apple, SwiftPair Spam, Samsung/Google/Flipper BLE spam, BLE Spam All, Spoof Airtag
- 🚧 **OUI Foxhunter** — Vendor picker from `/OUI/vendors.csv`, “Start Hunt” radar view
- ⚙️ **Settings**
  - UI Color, Clock, Wi-Fi, Hardware, Audio
- ℹ️ **About** — Project info & credits

---

## 📸 Screenshots
coming soon!

### UI Animation
![UI Animation](https://github.com/out0fstep/Hack3r-T-Deck/raw/main/animation.gif)

---

## 🧰 Hardware & I/O (T-Deck Plus)

- **Display / Touch:** LGFX `LGFX_TDeck` (320×240, landscape)
- **Keyboard (I²C1)**: address `0x55` (profiles: SDA=18/SCL=8 or SDA=18/SCL=17)
- **Trackball (I²C1)**: auto-detect `0x0A…0x0D`
- **Peripheral Power:** `PERIPH_POWER = 10` (rails for KB/Trackball)
- **SD (FSPI):** CS=39, SCK=40, MOSI=41, MISO=38
- **Battery (ADC):** pin 4, 2:1 divider, EMA smoothing
- **USB HID:** TinyUSB Keyboard (deploy Ducky scripts)

> **Folders on SD**  
> ` /duckyscripts/` → .txt ducky payloads  
> ` /OUI/vendors.txt` → `OUI,Vendor list` 

---

## 🚀 Build & Flash (quick start)

1. **Install dependencies** (ESP32 board pkg, LGFX, TinyUSB HID, etc.).  
2. **Set board**: *ESP32S3 Dev Module* (16MB), PSRAM **enabled**, Huge APP 3MB or equivalent.  
3. **Configure pins** if your Plus variant differs (see Hardware & I/O above).  
4. **Build** a `.bin`.  
5. **Flash** directly, or copy the `.bin` to your launcher (e.g., M5Launcher) and load it.

> If you boot to a backlit black screen, re-verify: rotation=1, PSRAM mode, app partition size, and that the display driver matches your T-Deck Plus rev.

---

## 🗺️ Roadmap

- [ ] Wi-Fi tool wiring (all sniff/wardrive/attack entries callable)
- [ ] BLE tool wiring (sniffers & attack actions)
- [ ] OUI Foxhunter scanning + hits list
- [ ] Audio events (clicks / hunt pips) with volume control
- [ ] Per-page trackball gestures (left/right page hooks)
- [ ] Optional OTA partition map + settings export/import
- [ ] Theming presets & font pass for small labels
- [ ] Localization hooks (EN first, then i18n keys)
- [ ] LoRa?
- [ ] Webserver?
      
---

## 🤝 Credits

> 📝 Portions of this project are derived from [Marauder](https://github.com/justcallmekoko/Marauder), by [justcallmekoko](https://www.instagram.com/just.call.me.koko/#).  
> A suite of Wi-Fi/Bluetooth offensive and defensive tools for ESP32.  
> Used with credit, under the terms of their license.  

---

## 🛡️ Safety & Legal

This firmware includes features intended for **authorized testing, research, and education**. Ensure you have **explicit permission** before scanning or interacting with any network or device. **Use at your own risk!!**

---

## 📄 License

MIT for the original Hack3r T-Deck code.  
Marauder components and ports remain under their respective licenses; see upstream.

---

## 📊 GitHub Stats

<div align="center">

<img
  src="https://github-readme-stats.vercel.app/api?username=out0fstep&show_icons=true&bg_color=00000000&hide_border=true&title_color=c9d1d9&text_color=8b949e&icon_color=58a6ff"
  alt="out0fstep GitHub Stats"><img
  src="https://github-readme-stats.vercel.app/api/top-langs/?username=out0fstep&layout=compact&bg_color=00000000&hide_border=true&title_color=c9d1d9&text_color=8b949e"
  alt="Top Languages">

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=out0fstep/Hack3r-T-Deck&type=Date)](https://www.star-history.com/#out0fstep/Hack3r-T-Deck&Date)

</div>
