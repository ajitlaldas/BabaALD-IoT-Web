# BabaALD-IoT-Web
Attempting to make a WebAapp and developing firmware (for ESP32/ESP8266) for Smart Home Devices.
ESP32 Firmware code for a "SmartLamp + Buzzer/Speaker" is also added in a separate folder.
Link for the WebApp (Published through Firebase): 
https://esp32-babaald.web.app/index.html

Following fixes done in Revision: 1 of this web files:

1) Ticker speed is faster
2) Orange highlighting not appearing on live control page after new device add is fixed.
3) Logout button going out of the view on phone screen is fixed. Now on phone nav bar is stacked horizontally
4) Knob wrap around due to 360 degree rotation is fixed. Limited rotation to 250 degrees only
5) Live control overlapping rendering on mobile screen is fixed. Now vertical staxking is made mandatory dor smaller screen widths
6) On manage device page you can change the device type also.

Issues to address:
1) On iPad or tablet screen the rendering is still not proper
2) On the home page ticker (Insight feed) only initial 2 -3 quotes are appearing and then it refreshes
