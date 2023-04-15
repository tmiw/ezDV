# ESD on USB jack doesn't damage ezDV

## Prerequisites

* ESD gun ([low cost example](https://bartek.com/page/ESD.html))
* Aluminum sheet which to attach ground lead of ESD gun
* Non-conductive material to prevent ezDV from touching the sheet (e.g. styrofoam)
* PC for testing

## Test Steps

1. Attach ground lead of the ESD gun to the aluminum sheet.
2. Place ezDV on top of the piece of non-conductive material, then place both onto the aluminum sheet.
3. Press and hold PTT and Mode on ezDV to enter hardware test mode. Ensure nothing is plugged into ezDV.
4. Place ESD gun 1cm or less from the USB jack, then press trigger 10 or more times to apply ESD current.
5. Repeat [the USB enumeration test]()../../usb/01-usb-enumeration.md).

## Expected Results

ezDV can still be enumerated over USB by a PC without rebooting or showing other unusual behavior.