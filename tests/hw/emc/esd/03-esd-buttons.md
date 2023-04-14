# ESD on buttons doesn't damage ezDV

## Prerequisites

* ESD gun ([low cost example](https://bartek.com/page/ESD.html))
* Aluminum sheet which to attach ground lead of ESD gun
* Non-conductive material to prevent ezDV from touching the sheet (e.g. styrofoam)

## Test Steps

1. Attach ground lead of the ESD gun to the aluminum sheet.
2. Place ezDV on top of the piece of non-conductive material, then place both onto the aluminum sheet.
3. Press and hold PTT and Mode on ezDV to enter hardware test mode. Ensure nothing is plugged into ezDV.
4. Place ESD gun 1cm or less from each button on the left hand side of the board, then press trigger 10 or more times to apply ESD current.
5. Repeat each of the button tests: [PTT](../../ui/01-ptt-button.md), [Mode](../../ui/02-mode-button.md), [Volume Up](../../ui/03-vol-up-button.md), [Volume Down](../../ui/04-vol-down-button.md).

## Expected Results

The buttons on ezDV can still be pressed. ezDV also doesn't show any unusual behavior while being zapped with the ESD gun.