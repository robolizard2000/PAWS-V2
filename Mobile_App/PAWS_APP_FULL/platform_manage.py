from kivy.utils import platform

ble = None
map_location = None

if platform == "android":
    from BLE_Backend.ble_Android import BLEBackend
    #from MAP_Backend.map_Android import MapLocation
    ble = BLEBackend()
    #map_location = MapLocation()
if platform == "linux":
    from BLE_Backend.ble_Linux import BLEBackend
    #from MAP_Backend.map_Linux import MapLocation
    ble = BLEBackend()
    #map_location = MapLocation()
if platform == "win":
    pass
    #from BLE_Backend.ble_Windows import BLEBackend
    #from MAP_Backend.map_Windows import MapLocation
    #ble = BLEBackend()
    #map_location = MapLocation()
else:
    print("Unsupported platform")
    BLEBackend = None