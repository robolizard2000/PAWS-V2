import kivy                             # main kivy module
from kivy.app import App                 # for creating the app class
from kivy.uix.gridlayout import GridLayout
from kivy.uix.screenmanager import Screen  # for managing multiple screens in the app
from kivy.uix.spinner import Spinner    # for creating a dropdown menu to select devices
from kivy.uix.dropdown import DropDown    # for creating a grid layout to organize widgets
from kivy.uix.label import Label        # for displaying text
from kivy.uix.button import Button      # for creating buttons

kivy.require('2.3.1')                   # specify the required kivy version   

from BLE_Backend.ble_Manage import ble  # import the BLE backend manager to allow for linux dev
import asyncio

async def main():
    # perform a BLE scan and print the resulting list of (name, address) tuples
    devices = await ble.scan()
    print("Scan results:", devices)

#####

class ConnectBLEScreen(Screen):
    def __init__(self, **kwargs):
        super(ConnectBLEScreen, self).__init__(**kwargs)
        self.ids.device_spinner.text = "Select a device"
        self.devices = []

    def __del__(self):
        pass
        asyncio.run(self.disconnect_from_device())
    
    async def disconnect_from_device(self):
        await ble.disconnect()
        return True

    async def scan_for_devices(self): # creates an async function to perform the BLE scan and store the results in self.devices
        self.devices = await ble.scan()

    def scan_button_pressed(self):
        asyncio.run(self.scan_for_devices())
        if self.devices == []:
            print("No devices found. Please scan for devices first.")
            return
        print("Scan results:", self.devices[0][0])
        devices_S = []
        for i in self.devices:
            devices_S.append(i[0])
        spinner = self.ids.device_spinner
        spinner.values = devices_S

        if devices_S:
            spinner.text = devices_S[0]
        #self.ids.connections_label.text = self.devices[0][0]

    def device_selected(self, device_name):
        print("Selected device:", device_name)

    def connect_button_pressed(self):
        if self.devices == []:
            print("No devices found. Please scan for devices first.")
            return
        selected_device = self.ids.device_spinner.text
        print("Connecting to:", selected_device)
        address = None
        for device in self.devices:
            if device[0] == selected_device:
                address = device[1]
                print("Device address:", address)
                break
        ble.run_async(ble.connect(address))

    def disconnect_button_pressed(self):
        print("Disconnecting from device...")
        ble.run_async(ble.disconnect())
        # asyncio.run(self.disconnect_from_device())
        print("Disconnected.")

    def explore_services_button_pressed(self):
        print("Exploring services...")
        ble.run_async(ble.read_all_characteristics())
        print("Service exploration complete.")
        
class PAWSApp(App):
    def build(self):
        return ConnectBLEScreen()

# run the async main function
if __name__ == "__main__":
    PAWSApp().run()