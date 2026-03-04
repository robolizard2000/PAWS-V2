import kivy                             # main kivy module
from kivy.uix.label import Label        # for displaying text

kivy.require('1.9.0')                   # specify the required kivy version   

from BLE_Backend.ble_Manage import ble  # import the BLE backend manager
import asyncio

async def main():
    # perform a BLE scan and print the resulting list of (name, address) tuples
    devices = await ble.scan()
    print("Scan results:", devices)

# run the async main function
if __name__ == "__main__":
    asyncio.run(main())