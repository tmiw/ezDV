# ezDV properly charges at low current with legacy USB ports.

## Prerequisites

* DC bench power supply set to 5 volts (and maximum 2A of current) with meters.
* USB to alligator clips ([example](https://www.amazon.com/HCFeng-Alligator-Adapter-Testing-Circuit/dp/B0BG5T6CPF/ref=sr_1_5?keywords=usb+to+alligator+clips&qid=1679882225&sr=8-5))
* USB-A male to USB-C male cable
* 2000 mAh LiPo battery discharged to 3.6V or less

## Test Steps

1. Attach battery to ezDV. Power off ezDV if necessary (i.e. if firmware has already been flashed).
2. Attach USB cable to ezDV and to the bench power supply.
3. Enable output on the DC bench supply.
4. Read current and voltage from bench supply's meters.

## Expected Results

ezDV should consume no more than 500 mA of current while charging as exceeding this violates the USB 2.0 specification. (Typically 250-300 mA per [TP4056 datasheet](https://dlnmh9ip6v2uc.cloudfront.net/datasheets/Prototyping/TP4056.pdf).) A red LED should also light on the bottom of the board indicating that charging is in progress.