# Kivy imports
import os

from kivy.app import App
from kivy.uix.screenmanager import NoTransition, ScreenManager, Screen
from kivy.clock import Clock
import threading

# BLE backend import
import asyncio
import time
from platform_manage import ble, weather

# Other Imports

# Debug logging import
from debug import DebugLog
debug_enabled = True


class ConnectBLEScreen(Screen):  # Screen for connecting to BLE devices
    def __init__(self, **kwargs):
        super(ConnectBLEScreen, self).__init__(**kwargs)
        self.debug = DebugLog(screen="ConnectBLEScreen", enable=debug_enabled)
        self.available_devices = []
        ble.on_result_callback = self.ble_scan_complete  # This gives the BLE class the ability to update the screen

    # --- Scan Button Code
    def scan_button_pressed(self):
        self.debug.log("Scan button pressed")
        ble.start_scan()

    def ble_scan_complete(self, devices):  # Call back
        Clock.schedule_once(lambda dt: self.on_devices_found(devices))

    def on_devices_found(self, devices):
        self.available_devices = devices  
        # self.available_devices = asyncio.run(ble.scan())  # gets the available devices from the BLE backend and stores them in a variable
        if self.available_devices is None:  # checks for an empty list
            self.debug.log("No devices found")
            return
        text_devices = []  # creates a list of just the device names for the drop down menu
        for device in self.available_devices: 
            if device.name is None:
                device.name = "None"  # Stops an error where the name is of type None
            self.debug.log(f"Device found: {device.name} at address {device.address}")  # logs each device found
            text_devices.append(device.name)  # adds the device name to the list for the drop down menu
        self.ids.device_spinner.values = text_devices  # sets the drop down list
        self.ids.device_spinner.disabled = False  # enables the drop down list
        # self.ids.device_spinner.selection = "Select a device" # sets the default text of the drop down list to "Select a device"
    # --- Scan Button
    
    # --- Device Selector
    def device_selected(self, device_name):
        self.debug.log(f"Selected device: {device_name}")
        self.ids.connect_button.disabled = False  # enables the connect button once a device is selected
    # --- Device Selector

    # --- Connect Button
    def connect_button_pressed(self):
        selected_device = self.ids.device_spinner.text
        if selected_device == "Select a device":  # checks for an unselected device
            self.debug.log("No device selected")
            return
        self.debug.log(f"Connecting to: {selected_device}")  # logs the selected device
        address = None
        for device in self.available_devices:  # finds the address of the selected device by matching the name to the list of available devices
            if device.name == selected_device:
                address = device.address
                self.debug.log(f"Device address: {address}")
                break
        if address is None:
            self.debug.log("Selected device not found in available devices")
            self.ids.connect_button.disabled = True  # disables the connect button if the selected device is not found in the available devices list
            return
        try:
            if ble.run_async(ble.connect(address)).result():  # attempts to connect to the device and logs the result
                self.debug.log("Connection successful")
                self.ids.connect_button.disabled = True  # disables the connect button after a successful connection
                self.ids.disconnect_button.disabled = False  # enables the disconnect button after a successful connection
                self.ids.title_Lable.color = (0, 1, 0, 1)
        
            else:
                self.debug.log("Connection failed")
                self.ids.connect_button.disabled = False  # keeps the connect button enabled if the connection failed
                self.ids.title_Lable.color = (1, 0, 0, 1)
        except:
            pass
    # --- Connect Button

    # --- Disconnect Button
    def disconnect_button_pressed(self):
        if ble.run_async(ble.disconnect()).result():  # attempts to disconnect from the device and logs the result
            self.debug.log("Disconnection successful")
            self.ids.connect_button.disabled = False  # enables the connect button after a successful disconnection
            self.ids.disconnect_button.disabled = True  # disables the disconnect button after a successful disconnection
        else:
            self.debug.log("Disconnection failed")
            self.ids.disconnect_button.disabled = False  # keeps the disconnect button enabled if the disconnection failed
    # --- Disconnect Button

    # Screen lifecycle methods for logging purposes
    def on_pre_enter(self, *args):
        self.debug.start()
        self.debug.log("About to show screen")

    def on_enter(self, *args):
        self.debug.log("Screen is now visible")
        if ble.client is None:
            return
        if not ble.client.is_connected:
            self.ids.title_Lable.color = (1, 0, 0, 1)
            self.debug.log("Disconection")
            self.ids.connect_button.disabled = False  # enables the connect button after a successful disconnection
            self.ids.disconnect_button.disabled = True  # disables the disconnect button after a successful disconnection
            return False
        else:
            self.ids.title_Lable.color = (0, 1, 0, 1)

    def on_leave(self, *args):
        self.debug.log("Screen hidden")
        self.debug.stop()


class DataScreen(Screen):
    def __init__(self, **kwargs):
        super(DataScreen, self).__init__(**kwargs)
        self.debug = DebugLog(screen="DataScreen", enable=debug_enabled)
        self.data_updater_thread = None
        self.data_caller_thread = None
        self.update_enabled = False
        self.weather_updater = None
        weather.UI_Updater = self.Update_Weather_Data_Callback

    def Update_Weather_Data_Callback(self, data) -> None:
        Clock.schedule_once(lambda dt: self.Update_Weather_Data(data))

    def Update_Weather_Data(self, data):
        print(f"Current data {data}")
        weather.data = data
        self.ids.weather_Date.text = weather.data.date
        self.ids.weather_temp.text = str(weather.data.current_temp)
        self.ids.weather_Humidity.text = str(weather.data.current_humid)
        self.ids.weather_Wind.text = str(weather.data.current_wind)
        self.ids.weather_Rain.text = str(weather.data.chance_rain)
    
    def update_data(self):
        while self.update_enabled:
            if not ble.client.is_connected:
                self.ids.title_Lable.color = (1, 0, 0, 1)
                self.debug.log("Disconection")
                self.update_enabled = False
                return False
            else:
                self.ids.title_Lable.color = (0, 1, 0, 1)
            if ble.state_changed:  # checks if updates are enabled and if a new notification has been received and processed, then updates the labels with the new data values from the BLE backend
                self.debug.log("Updating data...")
                self.ids.temp_label.text = f"Temp: {ble.ble_data.data_chars[0].value / 100} °C"
                self.ids.humidity_label.text = f"Humid: {ble.ble_data.data_chars[1].value/100} %"
                self.ids.wind_speed_label.text = f"Wind: {ble.ble_data.data_chars[2].value/10} m/s"
                self.ids.rainfall_label.text = f"Rain: {ble.ble_data.data_chars[3].value} mm"
                self.ids.light_level_label.text = f"Light: {(ble.ble_data.data_chars[4].value/10)} lx"
                ble.state_changed = False
            time.sleep(1)

    def start_data_updates(self):  # subscribes to notifications for all characteristics with the "Notify" property and enables data updates, also logs the result of each subscription attempt
        weather.Location = self.ids.data_location.text
        weather.start_updating()
        for char in ble.ble_data.data_chars:
            if "Notify" in char.properties:
                if ble.run_async(ble.start_notify(char.uuid)).result():
                    self.debug.log(f"Subscribed to {char.name} notifications successfully.")
                else:
                    self.debug.log(f"Failed to subscribe to {char.name} notifications.")
        self.update_enabled = True
        self.data_updater_thread = threading.Thread(target=self.update_data, daemon=True)  # daemon=True
        self.data_updater_thread.start()

    def stop_data_updates(self):  # unsubscribes from notifications for all characteristics with the "Notify" property and disables data updates, also logs the result of each unsubscription attempt
        self.update_enabled = False
        weather.stop_updating()
        if self.data_updater_thread:
            self.data_updater_thread.join()
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
        self.debug.log("Screen hidden")
        self.debug.stop()


class ControlScreen(Screen):
    def __init__(self, **kwargs):
        super(ControlScreen, self).__init__(**kwargs)
        self.debug = DebugLog(screen="ControlScreen", enable=debug_enabled)
        self.Weather_Update_Toggle_State: bool = False
        self.Window_Override: bool  = False
        self.Window_Sate: bool      = False
        self.Light_Override: bool   = False
        self.Light_Sate: bool       = False
        self.check_idle: int        = 5
        self.Update_Sens_Run        = True

    # ---- Weather Updater
    def Update_Sens(self):
        while self.Update_Sens_Run:
            time.sleep(ble.ble_data.setting_chars[0].value)
            if ble.state_changed:
                self.ids.temp_label.text        = f"Temp: {ble.ble_data.data_chars[0].value / 100}°C"
                self.ids.humidity_label.text    = f"Humid: {ble.ble_data.data_chars[1].value / 100}%"
                self.ids.wind_speed_label.text  = f"Wind: {ble.ble_data.data_chars[2].value / 10} mph"
                self.ids.rainfall_label.text    = f"Rain: {ble.ble_data.data_chars[3].value / 100} T/Min"
                self.ids.light_level_label.text = f"Light: {ble.ble_data.data_chars[4].value / 100}"

    def Weather_Updater_Loop(self):
        while self.Weather_Update_Toggle_State:
            self.debug.log("Checking Weather states...")
            # Do the shit
            time.sleep(self.check_idle)

    def Weather_Updater_Toggle(self):
        if self.Weather_Update_Toggle_State is False and self.Window_Override is False and self.Light_Override is False:
            self.Weather_Update_Toggle_State    = True
            self.ids.window_override.disabled   = True
            self.ids.light_override.disabled    = True
            self.ids.Weather_Monitoring_Toggle.text = "Auto Manule"
            threading.Thread(
                target=self.Weather_Updater_Loop,
                daemon=True
            ).start()
        else: 
            self.Weather_Update_Toggle_State    = False
            self.ids.window_override.disabled   = False
            self.ids.light_override.disabled    = False
            self.ids.Weather_Monitoring_Toggle.text = "Auto Control"
    # ---- Weather Updater

    # ---- Overrides
    def Toggle_Window_Override(self):
        self.debug.log("Toggled Window Override")
        if self.Window_Override is True:
            self.ids.window_override.text = "Override enable"
            self.ids.window_toggle.disabled = True
            self.Window_Override = False
            self.ids.window_state.text = "Auto Mode"
            if self.Light_Override is False:
                self.ids.Weather_Monitoring_Toggle.disabled = False
        else:
            self.ids.window_override.text = "Override disable"
            self.ids.window_toggle.disabled = False
            self.Window_Override = True
            self.ids.window_state.text = "Manule"
            self.ids.Weather_Monitoring_Toggle.disabled = True

    def Toggle_Window(self):
        self.debug.log("Toggled Window")
        pass

    def Toggle_Light_Override(self):
        self.debug.log("Toggled Light Overide")
        if self.Light_Override is True:
            self.ids.light_override.text = "Override enable"
            self.ids.light_toggle.disabled = True
            self.Light_Override = False
            self.ids.light_state.text = "Auto Mode"
            if self.Window_Override is False:
                self.ids.Weather_Monitoring_Toggle.disabled = False
        else:
            self.ids.light_override.text = "Override disable"
            self.ids.light_toggle.disabled = False
            self.Light_Override = True
            self.ids.light_state.text = "Manule"
            self.ids.Weather_Monitoring_Toggle.disabled = True

    def Toggle_Light(self):
        self.debug.log("Toggled Light")
        pass
    # ---- Overrides

    def on_pre_enter(self, *args):
        self.debug.start()
        self.debug.log("About to show screen")
        self.Update_Sens_Run = True
        threading.Thread(
            target=self.Update_Sens,
            daemon=True
        ).start()

    def on_enter(self, *args):
        self.debug.log("Screen is now visible")

    def on_leave(self, *args):
        self.debug.log("Screen hidden")
        self.Update_Sens_Run = False
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
    
    def load(self, path, filename):
        with open(os.path.join(path, filename[0])) as stream:
            self.ids.debug_text.text = stream.read()


class DebugScreenSave(Screen):
    def __init__(self, **kwargs):
        super(DebugScreenSave, self).__init__(**kwargs)

    def save(self, path, filename):
        with open(os.path.join(path, filename), 'w') as stream:
            stream.write(self.ids.text_input.text)


class PAWSApp(App):
    def __init__(self, **kwargs):
        super(PAWSApp, self).__init__(**kwargs)
        self.debug = DebugLog(screen="PAWSApp", enable=debug_enabled)

    def build(self):
        sm = ScreenManager(transition=NoTransition())  # transition is set to none to make screen changes instant
        sm.add_widget(ConnectBLEScreen(name='connect_ble'))  # the names are what are uset to call the screen
        sm.add_widget(DataScreen(name='data_screen'))
        sm.add_widget(ControlScreen(name='control_screen'))
        sm.add_widget(SettingsScreen(name='settings_screen'))
        sm.add_widget(DebugScreen(name='debug_screen'))
        sm.add_widget(DebugScreenSave(name='debug_screen_save'))
        return sm
    
    def on_start(self):
        self.debug.start()

    def on_stop(self):
        if ble.client and ble.client.is_connected:  # checks if there is an active BLE connection before attempting to disconnect
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
