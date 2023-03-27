# ezDV properly stops charging at high temperature

## Prerequisites

* USB power supply (either legacy USB 2.0 or USB-C PD capable)
* Cables as appropriate to attach ezDV to power supply
* LiPo battery that is partially discharged
* Heat gun

## Test Steps

1. Attach battery to ezDV. Power off ezDV if necessary (i.e. if firmware has already been flashed).
2. Attach USB cable to ezDV and to the power supply.
3. Turn on heat gun and apply heat to TH1 (lower right of U1). *WARNING: heat should only be applied briefly to prevent inadvertent desoldering!*

## Expected Results

The red charging LED at the bottom of the ezDV board should extinguish while heat is being applied and turn back on once the thermistor's temperature drops.