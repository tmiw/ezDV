# ESD on audio jacks doesn't damage ezDV

## Prerequisites

* ESD gun ([low cost example](https://bartek.com/page/ESD.html))
* Aluminum sheet which to attach ground lead of ESD gun
* Non-conductive material to prevent ezDV from touching the sheet (e.g. styrofoam)
* Wired headset for testing

## Test Steps

1. Attach ground lead of the ESD gun to the aluminum sheet.
2. Place ezDV on top of the piece of non-conductive material, then place both onto the aluminum sheet.
3. Press and hold PTT and Mode on ezDV to enter hardware test mode. Ensure nothing is plugged into ezDV.
4. Place ESD gun 1cm or less from each audio jack, then press trigger 10 or more times to apply ESD current.
5. Plug in a wired headset to each audio jack and listen to the audio coming from ezDV.

## Expected Results

ezDV should continue to play audio on both audio jacks without rebooting or otherwise showing unusual behavior.