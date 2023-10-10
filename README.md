# espbraille
ESP32 Firmware for BrailleRAP WIFI extension with MKS GEN / L

This is work in progress and experimental. ESP32-IDF needed at the moment.


## wiring
Simply wire ESP32 UART to MKS GEN-L UART
![Wiring schema](images/mks-esp.png)

## building
You will need ESP32-IDF 5.1 from espressif installed

## configure
You can configure project option with :

>idf.py menuconfig

available options are :

-SSID : Wifi network SSID for AP

-PASSWORD: Wifi password

## building firmware

>idf.py build

## flash firmware

>idf.py flash


