## Kivy imports
from kivy.app import App
from kivy.uix.screenmanager import NoTransition, ScreenManager, Screen
from kivy.utils import platform
import threading

## BLE backend import
import asyncio
from platform_manage import ble, map_location

## Debug logging import
from debug import DebugLog
debug_enabled = True

class ConnectBLEScreen(Screen): ## Screen for connecting to BLE devices
    def __init__(self, **kwargs):
        super(ConnectBLEScreen, self).__init__(**kwargs)
        self.debug = DebugLog(screen="ConnectBLEScreen", enable=debug_enabled)
        self.available_devices = []

    
    def scan_button_pressed(self):
        self.debug.log("Scan button pressed")
        self.available_devices = asyncio.run(ble.scan()) # gets the available devices from the BLE backend and stores them in a variable
        if self.available_devices == []: # checks for an empty list
            self.debug.log("No devices found")
            return
        text_devices = [] # creates a list of just the device names for the drop down menu
        for device in self.available_devices: 
            self.debug.log(f"Device found: {device[0]} at address {device[1]}") # logs each device found
            text_devices.append(device[0]) # adds the device name to the list for the drop down menu
        self.ids.device_spinner.values = text_devices # sets the drop down list
        self.ids.device_spinner.disabled = False # enables the drop down list
        #self.ids.device_spinner.selection = "Select a device" # sets the default text of the drop down list to "Select a device"
    
    def device_selected(self, device_name):
        self.debug.log(f"Selected device: {device_name}")
        self.ids.connect_button.disabled = False # enables the connect button once a device is selected

    def connect_button_pressed(self):
        selected_device = self.ids.device_spinner.text
        if selected_device == "Select a device": # checks for an unselected device
            self.debug.log("No device selected")
            return
        self.debug.log(f"Connecting to: {selected_device}")# logs the selected device
        address = None
        for device in self.available_devices: # finds the address of the selected device by matching the name to the list of available devices
            if device[0] == selected_device:
                address = device[1]
                self.debug.log(f"Device address: {address}")
                break
        if address is None:
            self.debug.log("Selected device not found in available devices")
            self.ids.connect_button.disabled = True # disables the connect button if the selected device is not found in the available devices list
            return
        if ble.run_async(ble.connect(address)).result(): # attempts to connect to the device and logs the result
            self.debug.log("Connection successful")
            self.ids.connect_button.disabled = True # disables the connect button after a successful connection
            self.ids.disconnect_button.disabled = False # enables the disconnect button after a successful connection
        else:
            self.debug.log("Connection failed")
            self.ids.connect_button.disabled = False # keeps the connect button enabled if the connection failed

    def disconnect_button_pressed(self):
        if ble.run_async(ble.disconnect()).result(): # attempts to disconnect from the device and logs the result
            self.debug.log("Disconnection successful")
            self.ids.connect_button.disabled = False # enables the connect button after a successful disconnection
            self.ids.disconnect_button.disabled = True # disables the disconnect button after a successful disconnection
        else:
            self.debug.log("Disconnection failed")
            self.ids.disconnect_button.disabled = False # keeps the disconnect button enabled if the disconnection failed
    
    ## Screen lifecycle methods for logging purposes
    def on_pre_enter(self, *args):
        self.debug.start()
        self.debug.log("About to show screen")
    def on_enter(self, *args):
        self.debug.log("Screen is now visible")
    def on_leave(self, *args):
        self.debug.log("Screen hidden")
        self.debug.stop()

class DataScreen(Screen):
    def __init__(self, **kwargs):
        super(DataScreen, self).__init__(**kwargs)
        self.debug = DebugLog(screen="DataScreen", enable=debug_enabled)
        threading.Thread(target=self.update_data, daemon=True).start() 
        self.update_enabled = False
    
    def update_data(self):
        while True:
            if self.update_enabled:
                self.debug.log("Updating data...")
                self.ids.temp_label.text = f"Temp: {ble.ble_data.data_chars[0].value} °C"
                self.ids.humid_label.text = f"Humid: {ble.ble_data.data_chars[1].value} %"
                self.ids.wind_label.text = f"Wind: {ble.ble_data.data_chars[2].value} m/s"
                self.ids.rain_label.text = f"Rain: {ble.ble_data.data_chars[3].value} mm"
                self.ids.light_label.text = f"Light: {ble.ble_data.data_chars[4].value} lx"
                asyncio.sleep(5) # placeholder for actual data update logic, currently just logs every 5 seconds when updates are enabled


    def start_data_updates(self): # subscribes to notifications for all characteristics with the "Notify" property and enables data updates, also logs the result of each subscription attempt
        for char in ble.ble_data.data_chars:
            if "Notify" in char.properties:
                if ble.run_async(ble.start_notify(char.uuid)).result():
                    self.debug.log(f"Subscribed to {char.name} notifications successfully.")
                else:
                    self.debug.log(f"Failed to subscribe to {char.name} notifications.")
        self.update_enabled = True

    def stop_data_updates(self): # unsubscribes from notifications for all characteristics with the "Notify" property and disables data updates, also logs the result of each unsubscription attempt
        self.update_enabled = False
        for char in ble.ble_data.data_chars:
            if "Notify" in char.properties:
                if ble.run_async(ble.stop_notify(char.uuid)).result():
                    self.debug.log(f"Unsubscribed from {char.name} notifications successfully.")
                else:
                    self.debug.log(f"Failed to unsubscribe from {char.name} notifications.")


    def on_pre_enter(self, *args):
        self.debug.start()
        self.debug.log("About to show screen")   
    def on_enter(self, *args):
        self.debug.log("Screen is now visible")
    def on_leave(self, *args):
        self.stop_data_updates()
        self.debug.log("Screen hidden")
        self.debug.stop()

class ControlScreen(Screen):
    def __init__(self, **kwargs):
        super(ControlScreen, self).__init__(**kwargs)
        self.debug = DebugLog(screen="ControlScreen", enable=debug_enabled)

    def on_pre_enter(self, *args):
        self.debug.start()
        self.debug.log("About to show screen")
    def on_enter(self, *args):
        self.debug.log("Screen is now visible")
    def on_leave(self, *args):
        self.debug.log("Screen hidden")
        self.debug.stop()

class SettingsScreen(Screen):
    def __init__(self, **kwargs):
        super(SettingsScreen, self).__init__(**kwargs)
        self.debug = DebugLog(screen="SettingsScreen", enable=debug_enabled)

    def on_pre_enter(self, *args):
        self.debug.start()
        self.debug.log("About to show screen")
    def on_enter(self, *args):
        self.debug.log("Screen is now visible")
    def on_leave(self, *args):
        self.debug.log("Screen hidden")
        self.debug.stop()

class DebugScreen(Screen):   
    def __init__(self, **kwargs):
        super(DebugScreen, self).__init__(**kwargs)
        self.debug = DebugLog(screen="DebugScreen", enable=debug_enabled)
        
    def on_pre_enter(self, *args):
        self.debug.start()
        self.debug.log("About to show screen")
    def on_enter(self, *args):
        self.debug.log("Screen is now visible")
    def on_leave(self, *args):
        self.debug.log("Screen hidden")
        self.debug.stop()

class PAWSApp(App):
    def __init__(self, **kwargs):
        super(PAWSApp, self).__init__(**kwargs)
        self.debug = DebugLog(screen="PAWSApp", enable=debug_enabled)

    def build(self):
        sm = ScreenManager(transition=NoTransition()) ## transition is set to none to make screen changes instant
        sm.add_widget(ConnectBLEScreen(name='connect_ble')) ## the names are what are uset to call the screen
        sm.add_widget(DataScreen(name='data_screen'))
        sm.add_widget(ControlScreen(name='control_screen'))
        sm.add_widget(SettingsScreen(name='settings_screen'))
        sm.add_widget(DebugScreen(name='debug_screen'))
        return sm
    
    def on_start(self):
        self.debug.start()
    def on_stop(self):
        if ble.client and ble.client.is_connected: # checks if there is an active BLE connection before attempting to disconnect
            self.debug.log("Disconnecting from device on app exit...")
            if ble.run_async(ble.disconnect()).result():
                self.debug.log("Disconnected successfully on app exit.")
            else:                 
                self.debug.log("Failed to disconnect on app exit.")
        self.debug.stop()

if __name__ == '__main__':
    global_Debug = DebugLog(screen="Global", enable=debug_enabled)
    global_Debug.clear()
    global_Debug.start()
    global_Debug.log("Starting PAWSApp")
    PAWSApp().run()
    global_Debug.stop()
