import asyncio
import threading
from bleak import BleakScanner, BleakClient
from BLE_Backend.ble_structure import BLE_Data #, Service, Characteristic



class BLEBackend:
    def __init__(self):
        self.on_result_callback = None

        self.client = None 
        self.loop = asyncio.new_event_loop()
        threading.Thread(target=self.loop.run_forever, daemon=True).start()
        self.ble_data = BLE_Data()
        self.state_changed = False ## goes high when a notification appears 
    
    def run_async(self, coro): # creates a method to run an async function in the event loop and return the result as a Future object
        return asyncio.run_coroutine_threadsafe(coro, self.loop)
    
    ##async def scan(self): # creates an async function to perform the BLE scan and return the results as a list of tuples (device name, device address)
    ##    devices = await BleakScanner.discover(timeout=5.0)
    ##    return [(d.name, d.address) for d in devices if d.name]
    def start_scan(self):
        threading.Thread(target=self._run_scan, daemon=True).start()
    def _run_scan(self):
        async def scan():
            devices = await BleakScanner.discover(timeout=5.0)
            return devices

        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            devices = loop.run_until_complete(scan())
            # Call the callback when done
            self.on_result_callback(devices)
        finally:
            loop.close()

    async def connect(self, address): # creates an async function to connect to a BLE device given its address and returns True if the connection was successful, False otherwise
        self.client = BleakClient(address)
        await self.client.connect()
        print("Connected")
        return True

    async def disconnect(self): # creates an async function to disconnect from the currently connected BLE device
        if self.client and self.client.is_connected:
            await self.client.disconnect()
            print("Disconnected")
            return True
        return False
    
    def notification_handler(self, sender, data): ## creates a notification handler function that will be called whenever a notification is received from the BLE device and prints the received data to the console
        sender = str(sender)[0:36].upper() ## converts the sender to a string and uppercase to make it easier to compare with the UUIDs defined in the BLE_Data class
        print(f"Notification from {sender}: {data}")
        for char in self.ble_data.data_chars:
            ## sender is in the form  12347005-0000-1000-8000-00805f9b34fb (Handle: 61)
            if char.uuid == sender: ## checks all chariteristics defined in the BLE_Data class to find the one that matches the sender of the notification and has the "Notify" property, then updates the value of that characteristic with the received data
                char.value = int.from_bytes(data, byteorder='little')
                print(f"Updated {char.name} value: {char.value}")
                self.state_changed = True ## sets the state_changed flag to True to indicate that a new notification has been received and processed
                break

    ## creates a notification handler function that will be called whenever a notification is received from the BLE device and prints the received data to the console
    async def start_notify(self, char_uuid):
        if not self.client or not self.client.is_connected:
            print("Not connected")
            return False

        await self.client.start_notify(char_uuid, self.notification_handler)
        print(f"Subscribed to {char_uuid}")
        return True

    ## stop notifications for a characteristic given its UUID
    async def stop_notify(self, char_uuid):
        if not self.client or not self.client.is_connected:
            return False

        await self.client.stop_notify(char_uuid)
        print(f"Unsubscribed from {char_uuid}")
        return True

    async def read_characteristic(self, char_uuid): # creates an async function to read the value of a characteristic given its UUID and return the value
        if self.client and self.client.is_connected:
            print(f"Reading characteristic {char_uuid}...")
            value = await self.client.read_gatt_char(char_uuid)
            print(f"Value read from {char_uuid}: {value}")
            return value
        else:
            raise Exception("Not connected to any device.")
        
    def read_all_characteristics(self): # creates a method to read all characteristics defined in the BLE_Data class and print their values to the console
        if not self.client or not self.client.is_connected: # checks if there is a connected device before attempting to read characteristics
            print("Not connected to any device.")
            return
        for char in self.ble_data.setting_chars + self.ble_data.control_chars + self.ble_data.data_chars:
            if "Read" in char.properties:
                value = self.run_async(self.read_characteristic(char.uuid)).result()
                print(f"{char.name} ({char.uuid}): {value}")
