from kivy.utils import platform

if platform == "android":
#    from jnius import autoclass
#    from android import mActivity
#
#    BluetoothAdapter = autoclass('android.bluetooth.BluetoothAdapter')
#
#    adapter = BluetoothAdapter.getDefaultAdapter()
#
#    if adapter is None:
#        print("Bluetooth not supported")
#    elif not adapter.isEnabled():
#        print("Bluetooth is OFF")
#    else:
#        print("Bluetooth is ready")
    ## from ble_Android import BLEBackend
    pass
else:
    from BLE_Backend.ble_Linux import BLEBackend

ble = BLEBackend()