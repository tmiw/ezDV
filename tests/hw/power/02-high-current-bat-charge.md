# ezDV properly charges at high current with supporting USB-C ports

## Prerequisites

* USB-C PD power supply
* USB measurement cable (e.g. [these from Amazon](https://www.amazon.com/gp/product/B09V81BTMK/ref=ppx_yo_dt_b_asin_title_o04_s00?ie=UTF8&psc=1))
* 2000 mAh LiPo battery discharged to 3.6V

## Test Steps

1. Attach battery to ezDV. Power off ezDV if necessary (i.e. if firmware has already been flashed).
2. Attach USB measurement cable to ezDV and the USB power supply.
3. Read measurement on cable.

## Expected Results

The measurement cable should read greate than 3 watts, which corresponds to (3W / 5V = 0.6A) per Ohm's Law. A red LED should also light on the bottom of the board indicating that charging is in progress.