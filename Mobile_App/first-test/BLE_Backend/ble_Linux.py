import asyncio
from bleak import BleakScanner, BleakClient

class BLEBackend:
    def __init__(self):
        self.client = None

    async def scan(self):
        devices = await BleakScanner.discover(timeout=5.0)
        return [(d.name, d.address) for d in devices if d.name]

    async def connect(self, address):
        self.client = BleakClient(address)
        await self.client.connect()
        return True

    async def disconnect(self):
        if self.client:
            await self.client.disconnect()