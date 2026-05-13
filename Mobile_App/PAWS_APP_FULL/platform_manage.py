from kivy.utils import platform

ble , weather= None, None

if platform == "android":
    from BLE_Backend.ble_Android import BLEBackend
    #from WEATHER_Backend.weather_Android import Weather
    ble = BLEBackend()
    #weather = Weather()
if platform == "linux" or platform == "win":
    from BLE_Backend.ble_Linux import BLEBackend
    from WEATHER_Backend.weather_Linux import Weather
    ble, weather = BLEBackend(), Weather()
else:
    print("Unsupported platform")
    from BLE_Backend.ble_Linux import BLEBackend
    from WEATHER_Backend.weather_Linux import Weather
    ble, weather = None, None
ble, weather = BLEBackend(), Weather()