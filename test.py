import asyncio
import websockets

async def hello(websocket, path):
    async for message in websocket:
        print(message)
       

asyncio.get_event_loop().run_until_complete(
    websockets.serve(hello, '0.0.0.0', 8765))
asyncio.get_event_loop().run_forever()