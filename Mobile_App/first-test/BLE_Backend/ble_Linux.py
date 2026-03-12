import asyncio
import threading
from bleak import BleakScanner, BleakClient

class BLEBackend:
    def __init__(self):
        self.client = None 
        self.loop = asyncio.new_event_loop()
        threading.Thread(target=self.loop.run_forever, daemon=True).start()

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

