# Linearity of 700D signal

## Prerequisites

* USB audio adapter with 3.5mm jacks for microphone/speaker
* 3.5mm TRRS audio cable ([example](https://www.amazon.com/gp/product/B07PJW6RQ7/ref=ppx_yo_dt_b_search_asin_title?ie=UTF8&psc=1))
* TRSS to separate 3.5mm microphone and speaker splitter ([example](https://www.amazon.com/UGREEN-Headphone-Splitter-Computer-Smartphone/dp/B073ZDDTH2/ref=sr_1_4?keywords=trrs+to+mic+and+headphone&qid=1679887399&sprefix=trrs+to+mic+%2Caps%2C149&sr=8-4))
* ezDV flashed to latest firmware
* Laptop or other PC with [Audacity](https://www.audacityteam.org/) installed
* ezDV *NOT* attached to the above PC via USB during test

## Test Steps

### Initial Configuration

1. Attach one end of the TRRS cable to the Radio 3.5mm jack on ezDV and the other to the splitter.
2. Attach headphone connector on splitter to the USB audio adapter's microphone input.
3. Attach microphone connector on splitter to the USB audio adapter's headphone/speaker output.
4. Hold down PTT and Mode to turn on ezDV in Hardware Test mode. Release both buttons when LEDs light up.
5. Start Audcaity on test PC and configure it to use the USB audio adapter for input and output and record in mono mode.
6. Create a new Audacity project with a sample rate of 8000 Hz.
7. Begin recording and adjust input volume using your operating system's volume controls until Audacity just hits 0 dB.
8. Stop recording and delete the track that Audacity created.
9. Power off ezDV (press and hold Mode until LEDs extinguish), then press and hold Mode to boot ezDV in normal operation mode.
10. Push Mode once to switch to 700D mode, then press and hold PTT to begin transmitting.
11. Begin recording and adjust volume on ezDV until the peaks are just below 0 dB on Audacity's input meter.
12. Stop recording and delete the track that Audacity created.

### Test Execution

1. Record approximately 5 seconds of audio from ezDV.
2. Release PTT to stop recording.
3. Select all the audio on the track (Ctrl/Command-A) and go to Analyze->Plot Spectrum.
4. Use the following settings in the Frequency Analysis window:
    * *Algorithm*: Spectrum
    * *Size*: 1024
    * *Function*: Hann window
    * *Axis*: Linear frequency

## Expected Results

For the frequencies between 1 KHz and 2 KHz, all audio peaks should be at approximately the same level (+/- a few dB). Additionally, frequencies outside of this passband should be significantly lower due to the use of the Codec2 software bandpass filter.