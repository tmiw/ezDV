# ezDV sleep current lower than limit

## Prerequisites

* DC benchtop power supply set to 3.7V
* [tinyCurrent low-current measuring device](https://www.n-fuse.co/devices/tinyCurrent-precision-low-Current-Measurement-Shunt-and-Amplifier-Device.html) with BNC jack.
* JST plug to wire leads
* Multimeter

## Test Steps

1. Attach positive lead from benchtop power supply to positive banana jack on tinyCurrent.
2. Attach negative lead from negative banana jack on tinyCurrent to the positive (red) cable on the JST plug.
3. Attach negative lead from the benchtop power supply to the negative (black) cable on the JST plug.
4. Set the tinyCurrent to milliamps (1 mV/mA) and turn it on.
5. Turn on the DC output on the benchtop power supply. Wait for the LEDs on ezDV to light up and then extinguish.
6. Push and hold the Mode button on ezDV and wait for the LEDs to turn on and then extinguish.
7. Set the tinyCurrent to microamps (1 mV/uA).
8. Probe the positive and negative test points on tinyCurrent with the multimeter.

## Expected Results

ezDV should use no more than 60 uA when sleeping. This will work out to being able to be off for almost four years before draining a 2000 mAh battery.