#!/bin/env python3
import asyncio
import sys
import os
from bleak import BleakClient

uuid_filename = "f5fe6e39-a786-44ae-bbdc-232ca38f0001"
uuid_operation = "f5fe6e39-a786-44ae-bbdc-232ca38f0002"
uuid_data = "f5fe6e39-a786-44ae-bbdc-232ca38f0003"

async def main(address, up_filename):
    async with BleakClient(address) as client:
        filename = await client.read_gatt_char(uuid_filename)
        print(f"Filename old: {filename.decode('utf-8')}")
        await client.write_gatt_char(uuid_filename, up_filename.encode("utf-8"))
        filename = await client.read_gatt_char(uuid_filename)
        print(f"Filename new set: {filename.decode('utf-8')}")
        print("Set operation: delete file")
        await client.write_gatt_char(uuid_operation,bytearray([0x02]))
        await client.write_gatt_char(uuid_data, bytearray([0xFF])) # trigger delete
        print("deleted")

if __name__ == "__main__":
    if len(sys.argv) > 2:
        address = sys.argv[1]
        up_filename = sys.argv[2]
        asyncio.run(main(address, up_filename))

    else:
        print("Usage: python delete.py <MAC ADDRESS> <BARCODE NAME>")
