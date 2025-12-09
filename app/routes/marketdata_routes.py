import asyncio
from fastapi import APIRouter, WebSocket, WebSocketDisconnect
from app.books.books import order_books
import logging

router = APIRouter()
logger = logging.getLogger(__name__)

clients = set()

BROADCAST_INTERVAL = 0.05  # 50ms


async def broadcast_market_data():
    """Global task: send book snapshot to all clients every interval"""

    while True:
        await asyncio.sleep(BROADCAST_INTERVAL)

        book = order_books.get("BTC-USD") # currently only supporting BTC-USD
        if not book:
            continue 

        snapshot = book.get_snapshot(5)

        msg = {"snapshot":snapshot}
        

        dead = []

        # broadcast to all active websockets


        for ws in clients:
            try:
                await ws.send_json(msg)
            except Exception:
                dead.append(ws)

        # cleanup 
        for ws in dead:
            try:
                ws.close()
            except:
                pass

            clients.discard(ws)
            logger.info("Dead socket removed")



@router.websocket("/marketdata")
async def get_market_data(websocket: WebSocket):
    await websocket.accept()
    clients.add(websocket)
    logger.info("Client connected: %s", websocket.client)

    try:
        while True:
            await websocket.receive_text()

    except WebSocketDisconnect:
        clients.discard(websocket)
        logger.info("Client disconnected: %s", websocket.client)
    except Exception as e:
        clients.discard(websocket)
        logger.error("WebSocket error for %s: %s", websocket.client, e)