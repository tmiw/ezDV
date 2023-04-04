# USB waveform has acceptable eye

## Prerequisites

* Oscilloscope with a minimum of two channels
* 2x 10x oscilloscope probes (or switching 1x/10x set to 10x)
* [USB breakout board](https://www.winford.com/products/btsubfaf.php)
* USB-A to C cable
* USB-B to A cable
* USB-C to A adapter
* PC with USB ports

## Test Steps

1. Attach ezDV and a PC to the breakout board using the cables and adapters.
2. Attach two 10x oscilloscope probes to two of the channels on the oscilloscope (and attach their ground leads to each other).
3. Probe the D+ and D- test points on the breakout board.
4. Set each channel to 1 V/div and the timebase to 10ns/div.
5. Set a trigger for ~2 V and have it fire on rising edge for the channel probing D+.
6. Set the positions of both channels to 0 volts and center the two waveforms horizontally.
7. Turn on persistence and set the persistence time to 5 seconds.

## Expected Results

The waveform should look similar to the below. The lighter colored noise around the two waveforms should be as
narrow as possible and show a visible eye. 

![USB waveform](02-usb-waveform.png)

*Note: this test is unlikely to fail if the [PC can properly enumerate ezDV](01-usb-enumeration.md) but is included for completeness.*