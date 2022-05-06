# esp-idf-fm-radio
FM radio using esp-idf.

![fm-radio](https://user-images.githubusercontent.com/6020549/146466018-221b7d81-4eb1-4798-8371-2aca658488bd.jpg)

I used [this](https://github.com/Molorius/esp32-websocket) component.   
This component can communicate directly with the browser.   
There is an example of using the component [here](https://github.com/Molorius/ESP32-Examples).
It's a great job.   

I used [this](https://github.com/CodeDrome/seven-segment-display-javascript) for segment display.   

# Hardware requirements   
TEA5767 FM Stereo Radio Module.   
I bought this on AliExpress about $4.   

![tea5767-1](https://user-images.githubusercontent.com/6020549/146292319-adf96f9a-f076-4b4f-be9f-2a2928c0b92f.JPG)
![tea5767-2](https://user-images.githubusercontent.com/6020549/146292325-c70aaddb-6f61-45ca-8de3-42ba3f375876.JPG)

The module has a standard antenna, but if you want to use it in a room, you need a long antenna.   
With a long antenna, you can get more signals.   
I used an AC power cable.   

![tea5767-3](https://user-images.githubusercontent.com/6020549/146294473-9b514cf8-ca94-49d8-a723-ec67185ec119.JPG)

# Installation

```
git clone https://github.com/nopnop2002/esp-idf-fm-radio
cd esp-idf-fm-radio
git clone https://github.com/Molorius/esp32-websocket components/websocket
idf.py set-target {esp32/esp32s2/esp32s3/esp32c3}
idf.py menuconfig
idf.py flash
```


# Configuration   

![config-top](https://user-images.githubusercontent.com/6020549/146466041-44d8769e-955f-4ff2-a820-19d7462baa21.jpg)
![config-app](https://user-images.githubusercontent.com/6020549/146466049-ce9d22d5-d056-45e6-a630-9d0094a6a4b9.jpg)


## WiFi Setting
![config-wifi-1](https://user-images.githubusercontent.com/6020549/146466210-9d808b99-7782-412d-ac11-fc69a31f66c1.jpg)

You can use Static IP.   
![config-wifi-2](https://user-images.githubusercontent.com/6020549/146466213-bc88ec7a-0a60-4ff5-83d0-332eac07a28b.jpg)

You can use the MDNS hostname instead of the IP address.   
![config-wifi-3](https://user-images.githubusercontent.com/6020549/146466214-1a076345-7f39-4a13-b472-27eeeff3485c.jpg)

- esp-idf V4.3 or earlier   
 You will need to manually change the mDNS strict mode according to [this](https://github.com/espressif/esp-idf/issues/6190) instruction.   
- esp-idf V4.4 or later  
 If you set CONFIG_MDNS_STRICT_MODE = y in sdkconfig.default, the firmware will be built with MDNS_STRICT_MODE = 1.


## TEA5767 Setting

![config-tea5767](https://user-images.githubusercontent.com/6020549/146466270-aa361a27-04c5-4132-bd5d-6998beb0914b.jpg)

- CONFIG_SCL_GPIO   
 GPIO number(IOxx) to SCL.
- CONFIG_SDA_GPIO   
 GPIO number(IOxx) to SDA.
- CONFIG_FM_BAND   
 In US/EU it ranges from 87.5 MHz to 108 MHz.   
 In Japan it ranges from 76 MHz to 91 MHz.   
 Used when wrapping in a search.   


# Wireing

|TEA5767||ESP32|ESP32-S2/S3|ESP32-C3||
|:-:|:-:|:-:|:-:|:-:|:-:|
|SCL|--|GPIO22|GPIO4|GPIO19|(*1)|
|SDA|--|GPIO21|GPIO3|GPIO18|(*1)|
|GND|--|GND|GND|GND||
|VCC|--|3.3V|3.3V|3.3V|(*2)|

(*1)   
You can change it to any pin using menuconfig.   
__But it may not work with other GPIOs.__

(*2)   
The marking is +5V, but it works at 3.3V.   


# How to use   
- Open browser   

- Enter the esp32 address in your browser's address bar   

- Search Up   
 Search for radio stations upwards.   

- Search Down   
 Search for radio stations downwards.   

- Add Preset   
 Record the current radio station in NVS.   

- Segment Color   
 Change segment color.   

- Goto   
 Goto preset station.   

- As system default   
 At boot time, set to this Radio station.   

# Clear preset   
```
idf.py erase-flash
```


# Reference   
https://github.com/nopnop2002/esp-idf-tea5767
