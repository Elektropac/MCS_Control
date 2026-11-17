#!/usr/bin/env python3
"""BLE end-to-end test against Control, same protocol as the Flutter app:
scan Control_*, connect, subscribe main output, take:<key>, encrypted
state_get_all (chunked reply until '\\n'), config_get (large reply), done:."""
import asyncio, os, json, hashlib, sys, time
from bleak import BleakScanner, BleakClient
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
IN  = "2c3b4d5e-6f7a-8b9c-d0e1-f2a3b4c5d6e7"
OUT = "1b9a473a-4493-4536-8b2b-9d4133488256"
KEY16 = "abcdefghijklmnop"
AES = AESGCM(hashlib.sha256(KEY16.encode()).digest())
def enc(s):
    iv=os.urandom(12); ct=AES.encrypt(iv,s.encode(),None)
    return (iv+ct[-16:]+ct[:-16]).hex().upper()
def dec(h):
    b=bytes.fromhex(h); iv,tag,ct=b[:12],b[12:28],b[28:]
    return AES.decrypt(iv,ct+tag,None).decode()
async def main():
    t0=time.time()
    dev = await BleakScanner.find_device_by_filter(lambda d,a: (d.name or a.local_name or "").startswith("Control_"), timeout=15)
    if not dev: print("FAIL scan: no Control_* found"); return 1
    print(f"scan: {dev.name} ({time.time()-t0:.1f}s)")
    buf=[]; q=asyncio.Queue()
    def on(_, data):
        buf.append(data.decode("latin1"))
        s="".join(buf)
        if "\n" in s:
            line,rest=s.split("\n",1); buf.clear(); buf.append(rest); q.put_nowait(line)
    async with BleakClient(dev) as c:
        print("connected, mtu", c.mtu_size)
        svcs=[s.uuid for s in c.services]; print("services:", len(svcs))
        await c.start_notify(OUT, on)
        # take: reply is ONE notify without '\n'
        got=asyncio.Event(); first={}
        def on_take(_, data): first["v"]=data.decode(); got.set()
        await c.stop_notify(OUT); await c.start_notify(OUT, on_take)
        await c.write_gatt_char(IN, f"take:{KEY16}".encode(), response=True)
        await asyncio.wait_for(got.wait(), 5)
        print("take ->", dec(first["v"]))
        await c.stop_notify(OUT); await c.start_notify(OUT, on)
        ok=0
        for subj in ["ping","state_get_all","config_get","pump_get_all","state_get_all"]:
            payload=enc(json.dumps({"subject":subj}))+"\n"
            t=time.time()
            for i in range(0,len(payload),120):
                await c.write_gatt_char(IN, payload[i:i+120].encode(), response=True)
            line=await asyncio.wait_for(q.get(), 20)
            txt=dec(line); dt=time.time()-t
            try: json.loads(txt); valid="valid JSON"; ok+=1
            except Exception: valid="NOT JSON"
            print(f"{subj}: {len(txt)} B in {dt:.2f}s, {valid}")
        await c.write_gatt_char(IN, f"done:{KEY16}".encode(), response=True)
        print("RESULT:", "PASS" if ok==5 else f"FAIL ({ok}/5)")
asyncio.run(main())
