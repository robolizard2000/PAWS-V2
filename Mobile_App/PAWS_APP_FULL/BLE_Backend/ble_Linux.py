import asyncio
import threading
from bleak import BleakScanner, BleakClient
from BLE_Backend.ble_structure import BLE_Data #, Service, Characteristic

class BLEBackend:
    def __init__(self):
        self.client = None 
        self.loop = asyncio.new_event_loop()
        threading.Thread(target=self.loop.run_forever, daemon=True).start()
        self.ble_data = BLE_Data()

    def run_async(self, coro): # creates a method to run an async function in the event loop and return the result as a Future object
        return asyncio.run_coroutine_threadsafe(coro, self.loop)
    
    async def scan(self): # creates an async function to perform the BLE scan and return the results as a list of tuples (device name, device address)
        devices = await BleakScanner.discover(timeout=5.0)
        return [(d.name, d.address) for d in devices if d.name]

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

    async def explore_services(self): # creates an async function to explore the services and characteristics of the currently connected BLE device and print them to the console
        try:
            services = list(self.client.services)
            if not services:
                print("No services found on this device.")
                return

            print(f"Found {len(services)} service(s):")

            for service in services:
                print(f"\nService: {service.uuid}")
                print(f"Description: {service.description}")
                print(f"Handle: {service.handle}")

                # Get characteristics for this service
                characteristics = service.characteristics

                if characteristics:
                    print(f"  Characteristics ({len(characteristics)}):")
                    print("  " + "-" * 76)

                    for char in characteristics:
                        print(f"    UUID: {char.uuid}")
                        print(f"    Description: {char.description}")
                        print(f"    Handle: {char.handle}")
                        print(f"    Properties: {', '.join(char.properties)}")

                        # Try to read the characteristic if it's readable
                        if "read" in char.properties:
                            try:
                                value = await self.client.read_gatt_char(char.uuid)
                                # Try to decode as string, otherwise show as hex
                                try:
                                    decoded_value = value.decode('utf-8')
                                    print(f"    Value (string): {decoded_value}")
                                except UnicodeDecodeError:
                                    hex_value = ' '.join(f'{b:02x}' for b in value)
                                    print(f"    Value (hex): {hex_value}")
                                    print(f"    Value (raw bytes): {value}")
                            except Exception as e:
                                print(f"    Value: <Could not read - {e}>")

                        # Show descriptors only if requested
                        descriptors = char.descriptors
                        if descriptors:
                            print(f"    Descriptors ({len(descriptors)}):")
                            for desc in descriptors:
                                print(f"      UUID: {desc.uuid}")
                                print(f"      Description: {desc.description}")
                                print(f"      Handle: {desc.handle}")
                                # Try to read descriptor if possible
                                try:
                                    desc_value = await self.client.read_gatt_descriptor(desc.handle)
                                    try:
                                        decoded_desc = desc_value.decode('utf-8')
                                        print(f"      Value (string): {decoded_desc}")
                                    except UnicodeDecodeError:
                                        hex_desc = ' '.join(f'{b:02x}' for b in desc_value)
                                        print(f"      Value (hex): {hex_desc}")
                                except Exception as e:
                                    print(f"      Value: <Could not read - {e}>")
                            print()  # Empty line between characteristics
                        else:
                            print()
                else:
                    print("    No characteristics found for this service.")

                print("-" * 80)

        except Exception as e:
            print(f"Error exploring services: {e}")
