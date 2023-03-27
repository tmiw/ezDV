# ezDV properly stops charging at low temperature

## Prerequisites

* USB power supply (either legacy USB 2.0 or USB-C PD capable)
* Cables as appropriate to attach ezDV to power supply
* LiPo battery that is partially discharged
* Can of compressed air

## Test Steps

1. Attach battery to ezDV. Power off ezDV if necessary (i.e. if firmware has already been flashed).
2. Attach USB cable to ezDV and to the power supply.
3. Hold compressed air upside down and spray onto TH1 (lower right of U1).

## Expected Results

The red charging LED at the bottom of the ezDV board should extinguish while cold air is sprayed onto the thermistor and turn back on once the thermistor's temperature returns to room temperature.