# LDO properly able to provide 3.3V

## Prerequisites

* Multimeter
* USB power supply
* USB cable (with one end being USB-C male)

## Test Steps

1. Attach USB cable to ezDV and the USB power supply.
2. Plug USB power supply into a working AC wall outlet.
3. Set multimeter to DC voltage mode (and if necessary, to a scale that includes 3.3V).
4. Probe pin 1 (positive lead) and pin 4 (negative lead) on J7 with the multimeter. *Note: pin 1 is towards the top of the board.*

## Expected Results

The multimeter should read 3.3V +/- 2% (3.23-3.37V). (See [NCP167 datasheet](https://www.onsemi.com/download/data-sheet/pdf/ncp167-d.pdf) for origin of expected output voltage range.)