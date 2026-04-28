from kivy.utils import platform

ble , weather= None, None

if platform == "android":
    from BLE_Backend.ble_Android import BLEBackend
    #from WEATHER_Backend.weather_Android import Weather
    ble = BLEBackend()
    #weather = Weather()
if platform == "linux":
    from BLE_Backend.ble_Linux import BLEBackend
    from WEATHER_Backend.weather_Linux import Weather
    ble, weather = BLEBackend(), Weather()
if platform == "win":
    pass
    #from BLE_Backend.ble_Windows import BLEBackend
    #from WEATHER_Backend.weather_Windows import Weather
    #ble = BLEBackend()
    #weather = Weather()
else:
    print("Unsupported platform")
    ble,weather  = None, None
ble = BLEBackend()
weather = Weather()