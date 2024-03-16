# Setting Up ezDV

There are several steps required to get ezDV up and running with your radio.

## Initial Assembly

If ezDV did not already come with a battery, you will need to obtain a 3.7V lithium polymer (LiPo) battery.
One recommended battery can be purchased [from Amazon](https://www.amazon.com/gp/product/B08214DJLJ/ref=ppx_yo_dt_b_search_asin_title?ie=UTF8&psc=1).

*WARNING: Do not puncture or otherwise damage lithium batteries as injury and/or property damage could result!
Neither the project nor its creator take responsibility for any damage or injury due to misuse of batteries. A 
battery with a protection module (such as the one linked above) is highly recommended.*

The battery should be plugged into the BT1 connector on the bottom of the ezDV circuit board, which is
the circled connector below:

![The back of the ezDV circuit board with BT1 circled](images/2-setup-battery-location.jpg)

Finally, plug in a wired headset on the TRRS jack located on the right side of the ezDV board (when looking
from above). This headset should be wired for compatibility with Android phones, which is as follows:

* Tip: RX audio from ezDV
* Ring 1: RX audio from ezDV
* Ring 2: GND
* Shield: TX microphone audio to ezDV

Once attached, ezDV will appear as follows:

![ezDV with headset attached](images/2-setup-headset-location.jpg)

## Radio Configuration

### Radios without Wi-Fi support

Radios without Wi-Fi support will need an interface cable. One end of this cable should 
be a male 3.5mm four conductor (TRRS) with the following pinout:

* Tip: TX audio from ezDV to your radio
* Ring 1: PTT to radio (this is connected to GND when you push the PTT button)
* Ring 2: GND
* Shield: RX audio from your radio to ezDV

There is also silkscreen labeling on the back of the board corresponding to the location of 
the radio jack that indicates the pinout. This jack is on the left hand side of ezDV when looking
downward at the front of the device.

Some example configurations are below.

#### Elecraft KX3

If you have an Elecraft KX3, you can use off the shelf parts for your interface cable
(no soldering required). These parts consist of a [3.5mm splitter](https://www.amazon.com/Headphone-Splitter-KOOPAO-Microphone-Earphones/dp/B084V3TRTV/ref=sr_1_3?crid=2V0WV9A8JJMW9&keywords=headset%2Bsplitter&qid=1671701520&sprefix=headset%2Bsplitte%2Caps%2C136&sr=8-3&th=1)
and a [3.5mm TRRS cable](https://www.amazon.com/gp/product/B07PJW6RQ7/ref=ppx_yo_dt_b_search_asin_title?ie=UTF8&psc=1). The microphone
connector on the splitter should be plugged into PHONES on the KX3 while the speaker connector should be plugged into MIC.

This setup will look like the following when plugged in:

![ezDV plugged into an Elecraft KX3](images/2-setup-radio-kx3.jpg)

### Radios with Wi-Fi support

If you have a radio that is capable of remote network access, you can configure ezDV to connect to the radio
over Wi-Fi. Currently this is supported only for the [FlexRadio](https://www.flexradio.com/) 6000 series radios 
and the Icom IC-705.

#### Flex 6000 series radios

Ensure that you can access your radio using the SmartSDR software from a PC on the same network (i.e. *not* using SmartLink).
Once this is confirmed, skip to TBD.

#### Icom IC-705

Several settings will need to be adjusted in the IC-705 before it can be used with ezDV. First, you will need to decide
whether you want to join your IC-705 to an existing Wi-Fi network or whether you want to configure ezDV to use the IC-705's
built-in access point.

*Note: it is recommended to join your IC-705 to an existing Wi-Fi network in order to be able to access ezDV's Web interface
(and to take advantage of certain features if an Internet connection is available).*

Next, you'll need to turn on your radio and push the Menu button. This will bring up the following screen:

![Icom IC-705 on menu screen](images/2-setup-radio-ic705-mainmenu.jpg)

Tap the Set button on the lower-right corner of the display and then select "WLAN Set" as shown below:

![Icom IC-705 with WLAN Set selected](images/2-setup-radio-ic705-setmode-3.jpg)

Next, tap the WLAN option on the first page and turn it OFF. Once switched OFF, tap the Connection Type setting and tap either
Station (for connecting to an existing Wi-Fi network) or Access Point (for connecting ezDV to the IC-705's access point):

![Icom IC-705 displaying WLAN Set options](images/2-setup-radio-ic705-wlan-set-1.jpg)

![Icom IC-705 displaying Connection Type menu](images/2-setup-radio-ic705-connection-type.jpg)

##### Configuring Wi-Fi on the IC-705

###### Using the IC-705's built-in access point

If you're using the IC-705's Access Point mode, it is a good idea to double-check your radio's SSID (aka Wi-Fi network name) and 
Wi-Fi password by tapping "Connection Settings (Access Point)" and then tapping each option as shown below, updating them as needed:

![Icom IC-705 displaying Access Point settings](images/2-setup-radio-ic705-connection-settings-ap.jpg)

Also, note the IP address that your IC-705 is using on this page as you will need it for later configuration of ezDV. Once the settings
here are satisfactory, tap the Back button on the lower right hand corner of the screen, tap the WLAN option and then change it to ON.

###### Connecting the IC-705 to an existing network

First, tap the WLAN option on the WLAN Set page and change it to ON. This is needed in order to turn on the Wi-Fi radio in the IC-705
and allow it to scan for networks.

Next, tap "Connection Settings (Station)". You will see the following page on the IC-705:

![Icom IC-705 displaying the Connection Settings (Station) page](images/2-setup-radio-ic705-connection-settings-sta.jpg)

From here, you can either tap "Access Point" to select your network from a list or "Manual Connect" if it's not broadcasting
its name. Tapping "Access Point" will bring up a page similar to the following:

![Icom IC-705 displaying a list of Wi-Fi networks](images/2-setup-radio-ic705-access-point.jpg)

Tap your preferred network from the list that appears. If it's not already saved in the IC-705, you may see a page similar
to the following:

![Icom IC-705 displaying the Connect page](images/2-setup-radio-ic705-connect.jpg)

Tapping Password will display the following page that will allow you to enter your Wi-Fi network's password:

![Icom IC-705 displaying the Password page](images/2-setup-radio-ic705-password.jpg)

Once entered, tap the ENT button to save it, then tap the Connect option to connect to the network. If successful,
the Access Point page will show "Connected" below your network's name. Tap the Back button until you return to the
"Connection Settings (Station)" page and make sure you see an IP address under "DHCP (Valid After Restart)":

![Icom IC-705 displaying an IP address](images/2-setup-radio-ic705-connection-settings-sta-with-ip.jpg)

Note this address as it will be needed for later configuration of ezDV.

##### Configuring the Radio User

The IC-705 requires a username and password to connect to the radio. If this is not already set up, you will need
to do so now before continuing with ezDV setup. TBD