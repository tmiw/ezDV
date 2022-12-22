# ezDV

This repository contains both the hardware and firmware for a portable battery-powered device that can
(de)modulate [FreeDV](https://freedv.org/) from an attached radio. The board is centered around the 
following components:

* [Espressif ESP32-S3](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
* [TLV320 digital audio codec chip](https://www.ti.com/cn/lit/ds/symlink/tlv320aic3254.pdf?ts=1651153824042) produced by Texas Instruments ([application notes](https://www.ti.com/lit/an/slaa408a/slaa408a.pdf?ts=1651208477772&ref_url=https%253A%252F%252Fwww.google.com%252F)).

## Building and installing the firmware

Plug your nanoESP32-S3 into your computer's USB port, then run the following:

```
. /path/to/esp-idf/export.sh
git submodule update --init --checkout --recursive
cd firmware
idf.py build
idf.py -p /dev/ttyxxx0 flash monitor
```

## Initial setup

### Assembly

You will need a 3.7V lithium polymer (LiPo) battery attached to BT1. One recommended battery can be purchased
[from Amazon](https://www.amazon.com/gp/product/B08214DJLJ/ref=ppx_yo_dt_b_search_asin_title?ie=UTF8&psc=1).

*WARNING: Do not puncture or otherwise damage lithium batteries as injury and/or property damage could result!
I do not take responsibility for any damage or injury due to misuse of batteries. A battery with a protection 
module (such as the one linked above) is highly recommended.*

See below for the location on the board where it should be attached:

![Location of BT1 on the board](docs/Battery_Location.jpg)

In the above, the positive end of the battery should be soldered to the square pad (the one closest to U1).
*Note: damage to the board could result if positive and negative are swapped!*

You will also need to solder through hole TRRS jacks at J2 and J3 as shown below:

![Location of BT1 on the board](docs/TRRS_Location.jpg)

### Radios without Wi-Fi support

Radios without Wi-Fi support will need an interface cable. One end of this cable should 
be a male 3.5mm four conductor (TRRS) with the following pinout:

* Tip: TX audio from ezDV to your radio
* Ring 1: PTT to radio (this is connected to GND when you push the PTT button)
* Ring 2: GND
* Shield: RX audio from your radio to ezDV

If you have an Elecraft KX3, you can use off the shelf parts for your interface cable
(no soldering required). These parts consist of a [3.5mm splitter](https://www.amazon.com/Headphone-Splitter-KOOPAO-Microphone-Earphones/dp/B084V3TRTV/ref=sr_1_3?crid=2V0WV9A8JJMW9&keywords=headset%2Bsplitter&qid=1671701520&sprefix=headset%2Bsplitte%2Caps%2C136&sr=8-3&th=1)
and a [3.5mm TRRS cable](https://www.amazon.com/gp/product/B07PJW6RQ7/ref=ppx_yo_dt_b_search_asin_title?ie=UTF8&psc=1). The microphone
connector on the splitter should be plugged into PHONES on the KX3 while the speaker connector should be plugged into MIC.

## License

This project is subject to the terms of the [TAPR Open Hardware License v1.0](https://tapr.org/the-tapr-open-hardware-license/) (schematics, 
other hardware documentation) and [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.en.html) (firmware).
