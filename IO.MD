## Settings 
 - Clock                - R/n   - how fequantly the sensors are checked and data is read from BLE 
 - Back-light-timer     - R/N   - time in mins bofore screen times out and back light goes out
 - LCD-Lock             - R/N   - if set to true locks out the LCD  and rotary encoder 
 - debug                - R/N   - if set to true then it will expose the debug settings 
    - service-name      - W     - Service name on device
    - Local-name        - W     - The local name for the device

## Controls 
 - Temp-Setpoint        - R/N   - temp we want to achive
 - Temp-hysterises      - R/N   - how much slack there is 
 - Humid-Setpoint       - R/N   - read above 
 - Humid-Hysterises     - R/N   - read above
 - Wind-Max             - R/N   - The max wind before the windows shut
 - Light-Min            - R/N   - the automatic min threshold before the sytem turns light on
 - Light-overide        - R/N   - if light overde is on then controls the light form the app
 - Light-Set            - R/N   - Lignt control
 - window-overide       - R/N   -
 - window-Set           - R/N   -

 ## Data Reading
All data there is reported to the mobile app
  - temp    - W
  - Humid   - W
  - Wind    - W
  - Rain    - W
  - light   - W
