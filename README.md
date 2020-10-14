# Analog Data Meter for Fritz Box

This project displays the currently used bandwidth of a FritzBox on an analog 3.3V voltmeter.
The data is pulled every 2.5 seconds from the FritzBox via WLAN, using the UPnP interface.

To use this code, the WLAN SSID and password need to be configured in the `skdconfig`. To adjust the range of the voltmeter to the available internet bandwidth, the 'MAX_INTERNET_BANDWIDTH' needs to be set appropriately in the code. The voltmeter itself needs to be attached to DAC channel 1.

The project is based on ESP32 sample code provided by Espressif in the public domain.
