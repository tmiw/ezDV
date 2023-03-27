# PTT button properly closing PTT line on Radio jack

## Prerequisites

* ezDV flashed to latest firmware
* TRRS breakout adapter ([example](https://www.amazon.com/gp/product/B07QQPG6BT/ref=ppx_yo_dt_b_search_asin_title?ie=UTF8&psc=1))
* Multimeter set to Resistance mode

## Test Steps

1. Hold down Mode for one second to turn on ezDV in normal operation mode.
2. Attach TRRS breakout adapter to the Radio jack (left hand side) on ezDV.
3. Press and hold the PTT button while measuring the resistance between the middle two terminal screws on the adapter.

## Expected Results

The measured resistance should be below 20 ohms when PTT is held and out of range of the multimeter (typically "OL" on Fluke meters, for example) when PTT is released.