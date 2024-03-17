# Using the web interface

ezDV provides a web interface to allow users to adjust its configuration and operate some of its features.
When Wi-Fi is enabled on ezDV, this interface is accessible through a regular web browser (such as Google Chrome
or Microsoft Edge).

## Accessing the web interface

By default, Wi-Fi on ezDV is disabled for security reasons. To enable it (or recover from a bad configuration), 
hold down the Mode and Volume Down buttons until ezDV turns on. This will cause ezDV to create an access point
named "ezDV" followed by the last two octets of its MAC address. From there, you can connect your PC or mobile
device to that network and open http://192.168.4.1/ in your web browser.

If ezDV is configured to connect to an existing Wi-Fi network, you can open a web browser on a PC or mobile device
connected to the same network as ezDV and enter ezDV's IP address into the browser's location box. This IP address
can typically be found in your router's listing of connected devices; ezDV will also beep the letter "N" followed
by the last octet of the IP address when it connects to the network (for example, "N100" for "192.168.1.100").  

Alternatively, depending on your router, you can also use the configured hostname in your browser instead.
By default, ezDV's hostname is "ezdv", which means that you can enter http://ezdv/ instead of its IP address.

Regardless of how you access ezDV's web interface, you should see a page similar to the below:

![Screenshot showing ezDV's main Web page](images/2-setup-webpage-general.png)

From here, you can choose one of several tabs:

* General: Contains miscallenous ezDV configuration options.
* Reporting: Configures and operates the [FreeDV Reporter](https://qso.freedv.org/) reporting feature.
* Voice Keyer: Configures ezDV's voice keyer feature.
* Wi-Fi: Configures how ezDV connects to Wi-Fi.
* Radio: Configures how ezDV connects to your network-enabled radio.
* Firmware Update: Allows the user to perform a firmware update on ezDV.

## Changing FreeDV mode

The web interface provides the ability to change the current FreeDV mode, similar to when pushing the Mode
button on the top of ezDV. To change the current FreeDV mode, simply push one of the mode buttons in the General 
tab:

![ezDV mode buttons on web interface](images/4-mode-buttons.png)

Once pushed, ezDV will behave identically as if the mode was switched using the Mode button, including beeping
the current mode in Morse Code through the wired headset.

## Using and configuring the voice keyer

To activate and deactivate the voice keyer, you can push the Voice Keyer button in the General tab. When active,
the Voice Keyer button will turn red as shown below:

![Voice Keyer button when active](images/4-voice-keyer-active.png)

Pushing the button again will turn off the voice keyer:

![Voice Keyer button when not active](images/4-voice-keyer-inactive.png)

If the voice keyer feature is disabled, the Voice Keyer button will be grayed out. In order to configure the voice
keyer, you will need to go into the Voice Keyer tab:

![Voice Keyer tab for configuration](images/4-voice-keyer-config.png)

The following can be set here to enable the voice keyer feature:

| Setting | Description |
|---------|-------------|
| Enable voice keyer | When checked, this enables use of the voice keyer. The Voice Keyer button in the General tab will be allowed to be pushed, as well as be able to be activated by pushing on the Mode button on the front of ezDV while holding down PTT. |
| Voice keyer file | Allows uploading of a new voice keyer file. Optional if one has already been uploaded to ezDV.<br/> *Note: ezDV requires a WAV file encoded with a sample rate of 8000 Hz and containing only one audio channel. If a file is uploaded that does not meet these requirements, ezDV will reject the upload.* |
| Number of times to transmit | The number of times that ezDV will transmit the voice keyer file before it disables the voice keyer. You can also disable the voice keyer early by pushing on the Voice Keyer button or pushing on any of the physical buttons on the front of ezDV. |
| Number of seconds to wait after transmit | The number of seconds to wait after transmitting the voice keyer file before starting another transmit cycle. |

Pushing Save will immediately update the voice keyer configuration and enable its use if desired.

## Reporting to FreeDV Reporter

ezDV has the ability to report its current state to the [FreeDV Reporter](https://qso.freedv.org/) spotting service. 
Operation of this feature can be done through the Reporting tab:

![Example of a reporting configuration on ezDV](images/4-reporting-tab.png)

When configured with a callsign and grid square, ezDV will automatically connect to FreeDV Reporter if it has access
to an internet connection and is able to connect to a supported radio over Wi-Fi. You can also force a connection to
FreeDV Reporter for radios without Wi-Fi support by checking the "Force reporting without radio connection" checkbox
and manually entering the current frequency.

Once connected to FreeDV Reporter, a new row will appear corresponding to your connection:

![Example of an entry on FreeDV Reporter](images/4-freedv-reporter.png)

If ezDV is able to decode a callsign from a received signal, it will transmit the received callsign and SNR to FreeDV
Reporter. Your row will then update to indicate the received callsign, SNR and mode as well as highlight your row in 
a blue background for several seconds to alert others that you've receiving a signal. When ezDV is transmitting, FreeDV
Reporter will also highlight your row with a red background to indicate to others that you're transmitting.

With radios that support connections over Wi-Fi, ezDV will also follow frequency changes on the radio and report them
to FreeDV Reporter. FreeDV Reporter will then indicate your current frequency, allowing others to change to your frequency
and potentially make a contact with you. Manual frequency updates (for those radios attached to ezDV with a wired connection)
will also be sent to FreeDV Reporter as they're made.

The following can be updated in the Reporting tab to operate FreeDV Reporter reporting:

| Setting | Description |
|---------|-------------|
| Your callsign | The callsign to report to FreeDV Reporter. This callsign is also transmitted as part of your FreeDV signal so that others can report receipt of your signal. |
| Grid square/locator | The 4-6 digit grid square to report to FreeDV Reporter. You can use a service such as [this](https://www.levinecentral.com/ham/grid_square.php) to calculate your grid square if not known. *Note: Clearing this field disables FreeDV Reporter reporting.* |
| Message | A short message to display on the FreeDV Reporter website next to your callsign. This is transmitted solely on the internet, not over RF. |
| Force reporting without radio connection | When checked, ezDV will connect to FreeDV Reporter even without a valid radio configuration. |
| Current frequency (MHz) | When "Force reporting without radio connection" is checked, ezDV will transmit this frequency to FreeDV Reporter as your current frequency.|

Pushing Save will immediately connect or disconnect to FreeDV Reporter (as desired/required) as well as transmit updates to the current frequency or short message.

## Configuring Wi-Fi

The Wi-Fi tab allows configuration of ezDV's Wi-Fi connection:

![ezDV showing the Wi-Fi tab](images/2-setup-webpage-wifi.png)

The following can be configured in this screen:

| Setting | Description |
|---------|-------------|
| Enable Wi-Fi | Enables or disables the Wi-Fi radio on ezDV. |
| Hostname | The name to call ezDV on the network (default "ezdv"). Only letters, numbers and '-' can be used for the name. |
| Wireless Mode | Can be either "Client" (to connect to an existing Wi-Fi network) or "Access Point" (to have ezDV generate its own Wi-Fi network). |
| Network Name | The Wi-Fi network to connect to in "Client" mode. ezDV populates this list with the networks that it's able to see, or you can choose "(other)" and manually enter the name of the network in the "SSID" field. |
| SSID | Typically automatically filled in if you select a network in the "Network Name" list. If "(other)" is selected, you can manually enter the name of the Wi-Fi network here (for example, if the network doesn't broadcast its presence). |
| Security Type | In "Access Point" mode, controls the security of the Wi-Fi network ezDV provides. Can be either "Open" (no encryption/security) or one of "WEP", "WPA", "WPA2" or "WPA/WPA2". *Note: WPA2 is recommended for the best security.* |
| Channel | In "Access Point" mode, this controls the 2.4 GHz Wi-Fi channel (1-11) ezDV uses. |
| Password | In "Access Point" mode (and when something other than "Open" is selected for "Security Type"), the password ezDV will require to allow other devices to connect to it. In "Client" mode, this is the password for the network selected or entered in "Network Name"/"SSID". |

Pushing Save here will save these settings to ezDV's internal flash but require rebooting or power cycling ezDV for the settings to take effect.
