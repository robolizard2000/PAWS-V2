import kivy                             # main kivy module
from kivy.app import App                 # for creating the app class
from kivy.uix.gridlayout import GridLayout
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

class ConnectBLEScreen(GridLayout):
    def __init__(self, **kwargs):
        super(ConnectBLEScreen, self).__init__(**kwargs)
        self.ids.connections_label.text = "Not connected"
        #self.cols = 1
        #self.add_widget(Label(text="BLE Connection Screen"))
        #self.add_widget(Button(text="Scan for Devices", on_press=self.Scan_Button_Pressed()))

        self.devices = []

    async def scan_for_devices(self): # creates an async function to perform the BLE scan and store the results in self.devices
        self.devices = await ble.scan()

    def scan_button_pressed(self):
        asyncio.run(self.scan_for_devices())
        print("Scan results:", self.devices[0][0])
        self.ids.connections_label.text = self.devices[0][0]

class PAWSApp(App):
    def build(self):
        return ConnectBLEScreen()

# run the async main function
if __name__ == "__main__":
    PAWSApp().run()