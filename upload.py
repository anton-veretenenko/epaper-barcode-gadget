#!/bin/env python3
import asyncio
import sys
import os
from bleak import BleakClient

uuid_filename = "f5fe6e39-a786-44ae-bbdc-232ca38f0001"
uuid_operation = "f5fe6e39-a786-44ae-bbdc-232ca38f0002"
uuid_data = "f5fe6e39-a786-44ae-bbdc-232ca38f0003"

async def main(address, up_filename, img_filename):
    async with BleakClient(address) as client:
        filename = await client.read_gatt_char(uuid_filename)
        print(f"Filename old: {filename.decode('utf-8')}")
        await client.write_gatt_char(uuid_filename, up_filename.encode("utf-8"))
        filename = await client.read_gatt_char(uuid_filename)
        print(f"Filename new set: {filename.decode('utf-8')}")
        print("Set operation: delete file")
        await client.write_gatt_char(uuid_operation,bytearray([0x02]))
        await client.write_gatt_char(uuid_data, bytearray([0xFF])) # trigger delete
        print("Set operation: write file")
        await client.write_gatt_char(uuid_operation,bytearray([0x01]))
        print("Sending data...", flush=True, end="\r")
        bytes_counter = 0
        while True:
            try:
                size = os.path.getsize(img_filename)
                with open(img_filename, "rb") as f:
                    while True:
                        data = f.read(128)
                        if not data:
                            break
                        await client.write_gatt_char(uuid_data, data)
                        bytes_counter += len(data)
                        print(f"Sending data...{round(bytes_counter/size*100)}%\r", flush=True, end="\r")
                    print(f"Total bytes sent: {bytes_counter}")
            except:
                print(f"Total bytes sent: {bytes_counter}")
            break
        print("Data written")

if __name__ == "__main__":
    if len(sys.argv) > 3:
        address = sys.argv[1]
        up_filename = sys.argv[2]
        img_filename = sys.argv[3]
        if os.path.exists(img_filename):
            asyncio.run(main(address, up_filename, img_filename))
        else:
            print(f"File {img_filename} not found")
    else:
        print("Usage: python upload.py <MAC ADDRESS> <BARCODE NAME> <IMAGE FILENAME>")
