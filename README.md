# ezDV

This repository contains both the hardware schematic and firmware for an eventual device that is able to 
(de)modulate [FreeDV](https://freedv.org/) from an attached radio. Currently this is a development board 
centered around the following components:

* [TLV320 digital audio codec chip](https://www.ti.com/cn/lit/ds/symlink/tlv320aic3254.pdf?ts=1651153824042) produced by Texas Instruments ([application notes](https://www.ti.com/lit/an/slaa408a/slaa408a.pdf?ts=1651208477772&ref_url=https%253A%252F%252Fwww.google.com%252F)).
* The [nanoESP32-S3](https://github.com/wuxx/nanoESP32-S3) board (which can be purchased from [Tindie](https://www.tindie.com/products/johnnywu/nanoesp32-s3-development-board/)).

## Building and installing the firmware

Plug your nanoESP32-S3 into your computer's USB port, then run the following:

```
. /path/to/esp-idf/export.sh
git submodule update --init --checkout --recursive
cd firmware
idf.py build
idf.py -p /dev/ttyxxx0 flash monitor
```

## License

This project is subject to the terms of the [TAPR Open Hardware License v1.0](https://tapr.org/the-tapr-open-hardware-license/) (schematics, 
other hardware documentation) and [GNU General Public License v3.0](https://www.gnu.org/licenses/gpl-3.0.en.html) (firmware).
