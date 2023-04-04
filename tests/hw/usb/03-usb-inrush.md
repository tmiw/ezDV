# USB inrush current below maximum limit

## Prerequisites

* Oscilloscope that can save .csv files on USB drives (example: Rigol DS1054Z)
* [tinyCurrent low-current measuring device](https://www.n-fuse.co/devices/tinyCurrent-precision-low-Current-Measurement-Shunt-and-Amplifier-Device.html) with BNC jack.
* [USB breakout board](https://www.winford.com/products/btsubfaf.php)
* USB-A to C cable
* USB-B to A cable
* USB-C to A adapter
* RG-58 coax with BNC connectors
* PC with USB ports that has the [USBET20 tool](https://www.usb.org/document-library/usbet20) installed
* Battery attached to ezDV that is at least 50% discharged

## Test Steps

1. Attach ezDV to the breakout board using the cables and adapters. Do not attach to the PC yet.
2. Remove the jumper bridging the 5V rail on both sides of the breakout board and attach two wires (red banana jack to the PC side of the breakout and black banana jack to the ezDV side).
3. Attach coax cable from tinyCurrent to the oscilloscope.
4. Set the oscilloscope timebase to 100ms/div and the channel to 100 mV/div. Ensure that the sample rate is >= 1 MS/sec.
5. Set the tinyCurrent device to use the 1mA/mV scale and turn it on.
6. Turn on a single shot capture using a trigger of 100 mV.
7. Plug in the breakout board to the PC. The oscilloscope should stop and display the waveform.
8. Using the oscilloscope's buttons, save a CSV file of the oscilloscope's memory (not display) to a USB drive.
9. Copy the saved file from the USB stick to the PC.
10. Ensure that the CSV file is formatted with two columns: time (in seconds) and the current. You can use [this script](03-usb-inrush.py) as an example for the Rigol DS1054Z if you need to massage the data.
11. Run the USBET20 tool and select the Inrush Current tab. Select the CSV file you generated or saved and enter 5 volts for the voltage.
12. Click the Test button. After a few minutes, a web browser should display with the results.

## Expected Results

The USBET20 tool should display an inrush figure below 55 uC, which is the upper limit for USB 2.0.