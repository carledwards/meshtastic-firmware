<div align="center" markdown="1">

<img src=".github/meshtastic_logo.png" alt="Meshtastic Logo" width="80"/>
<h1>Meshtastic Firmware</h1>

![GitHub release downloads](https://img.shields.io/github/downloads/meshtastic/firmware/total)
[![CI](https://img.shields.io/github/actions/workflow/status/meshtastic/firmware/main_matrix.yml?branch=master&label=actions&logo=github&color=yellow)](https://github.com/meshtastic/firmware/actions/workflows/ci.yml)
[![CLA assistant](https://cla-assistant.io/readme/badge/meshtastic/firmware)](https://cla-assistant.io/meshtastic/firmware)
[![Fiscal Contributors](https://opencollective.com/meshtastic/tiers/badge.svg?label=Fiscal%20Contributors&color=deeppink)](https://opencollective.com/meshtastic/)
[![Vercel](https://img.shields.io/static/v1?label=Powered%20by&message=Vercel&style=flat&logo=vercel&color=000000)](https://vercel.com?utm_source=meshtastic&utm_campaign=oss)

<a href="https://trendshift.io/repositories/5524" target="_blank"><img src="https://trendshift.io/api/badge/repositories/5524" alt="meshtastic%2Ffirmware | Trendshift" style="width: 250px; height: 55px;" width="250" height="55"/></a>

</div>

</div>

<div align="center">
	<a href="https://meshtastic.org">Website</a>
	-
	<a href="https://meshtastic.org/docs/">Documentation</a>
</div>

## Fork Features

- User LED can either be a "heartbeat" or a "new message indicator"
  - when set to a message indicator, fast blink is a personal/direct message, slow blink is a public message
  - pressing the main pushbutton will clear the message indicator, or, by pulling messages from the PhoneAPI
  - Default is "new message indicator" for `CLIENT` and `CLIENT_MUTE`
- Added a build variants:
  - `heltec-v3-release`: no debug logging and optimized build
  - `heltec-v3-usb-detect`: using a hardware mod where PIN 19 is used to perform a voltage detection from USB power
  - `halted-v3-usb-detect-release`: no debug logging and optimized build
- Heltec V3 can have up to 400 node entries (when not using a webserver build)
  - if memory gets below 20%, nodes entries will be pruned
- Added Power Management Profiles
  - two profiles: USB and Battery
  - When on Battery, for `CLIENT` and `CLIENT_MUTE` the defaults are:
    - The screen remains off when new packets arrive
    - The User LED is disabled
    - BLE will stop advertising (but an existing paired client can connect)
    - The system will eventually go down to Light Sleep mode:
      - Will wake on new LoRa packets or the User button is pressed
      - BLE will be fully shutdown until the User button is pressed
  - when the device power switches between USB vs Battery, a new profile is created and the old one is swapped out
  - Note: on the Heltec V3, there is no "direct" detection if the USB is connected/disconnected. Once the battery reached a specific threshold, it will determine the correct power mode. This means that while charging a low/dead battery will keep the device in a "battery state" until it reaches fullness.

### Battery Saving Results
- Tested with two Helvec V3 devices, both running in `CLIENT_MUTE` mode, `US` Region, 1100mAh battery:
  - Device 1: Stock Firmware `2_6_11_beta` with Power Savings enabled: **~12 hours**
  - Device 2: This firmware using the `heltec-v3-release` build variant: **~36 hours**

### Note
- _Only tested on the Heltec V3_
- _More testing is needed for edge cases_
- _Started to change up what is shown on the screen, still needs more work to make this only show a list of new messages, not node info_

## Overview

This repository contains the official device firmware for Meshtastic, an open-source LoRa mesh networking project designed for long-range, low-power communication without relying on internet or cellular infrastructure. The firmware supports various hardware platforms, including ESP32, nRF52, RP2040/RP2350, and Linux-based devices.

Meshtastic enables text messaging, location sharing, and telemetry over a decentralized mesh network, making it ideal for outdoor adventures, emergency preparedness, and remote operations.

### Get Started

- 🔧 **[Building Instructions](https://meshtastic.org/docs/development/firmware/build)** – Learn how to compile the firmware from source.
- ⚡ **[Flashing Instructions](https://meshtastic.org/docs/getting-started/flashing-firmware/)** – Install or update the firmware on your device.

Join our community and help improve Meshtastic! 🚀

## Stats

![Alt](https://repobeats.axiom.co/api/embed/8025e56c482ec63541593cc5bd322c19d5c0bdcf.svg "Repobeats analytics image")
