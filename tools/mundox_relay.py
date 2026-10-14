#!/usr/bin/env python3
import asyncio
from collections import defaultdict

rooms = defaultdict(set)
client_meta = {}

async def send(writer, line):
    writer.write((line + "\n").encode("utf-8"))
    await writer.drain()

async def broadcast(room, sender, line):
    dead = []
    for writer in list(rooms.get(room, ())):
        if writer is sender:
            continue
        try:
            await send(writer, line)
        except Exception:
            dead.append(writer)
    for writer in dead:
        rooms[room].discard(writer)
        client_meta.pop(writer, None)

async def handle(reader, writer):
    peer = writer.get_extra_info("peername")
    room = None
    name = "unknown"

    try:
        first = await asyncio.wait_for(reader.readline(), timeout=10)
        if not first:
            return

        line = first.decode("utf-8", "replace").strip()
        parts = line.split(" ", 2)
        if len(parts) < 3 or parts[0] != "JOIN":
            await send(writer, "ERROR expected: JOIN <room> <name>")
            return

        room, name = parts[1], parts[2]
        rooms[room].add(writer)
        client_meta[writer] = (room, name)
        await send(writer, f"OK room={room} name={name}")
        await broadcast(room, writer, f"PEER_JOIN name={name}")

        while True:
            raw = await reader.readline()
            if not raw:
                break
            if len(raw) > 8192:
                await send(writer, "ERROR line_too_large")
                continue

            msg = raw.decode("utf-8", "replace").rstrip("\r\n")
            await broadcast(room, writer, f"FROM {name} {msg}")

    except asyncio.TimeoutError:
        try:
            await send(writer, "ERROR join_timeout")
        except Exception:
            pass
    except Exception as exc:
        print("client error", peer, repr(exc))
    finally:
        if room is not None:
            rooms[room].discard(writer)
            client_meta.pop(writer, None)
            try:
                await broadcast(room, writer, f"PEER_LEAVE name={name}")
            except Exception:
                pass
            if not rooms[room]:
                rooms.pop(room, None)
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:
            pass

async def main():
    server = await asyncio.start_server(handle, "0.0.0.0", 7777)
    addresses = ", ".join(str(s.getsockname()) for s in server.sockets or [])
    print("Mundo X relay listening:", addresses)
    async with server:
        await server.serve_forever()

if __name__ == "__main__":
    asyncio.run(main())
