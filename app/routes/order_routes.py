from fastapi import APIRouter,Body
from fastapi.responses import JSONResponse
import asyncio
from asyncio import Queue, create_task, get_event_loop
from concurrent.futures import ThreadPoolExecutor
from app.books.books import order_books
from typing import List, Dict,Union
import time
import matching_engine

router = APIRouter()

order_queue = Queue(maxsize=10000)
executor = ThreadPoolExecutor(max_workers=8)  

MAX_BATCH_SIZE = 64
MAX_BATCH_DELAY = 2


def match_order(order):
    symbol = order["symbol"]
    book = order_books.setdefault(symbol, matching_engine.OrderBook(symbol))
    return book.add_order(float(order["price"]), float(order["quantity"]), order["side"].upper(), order["order_type"].upper())

async def queue_consumer():
    loop = get_event_loop()

    while True:
        batch = []

        start = time.perf_counter()

        # Block until at least one item arrives
        order = await order_queue.get()
        batch.append(order)

        # Try to fill batch opportunistically
        while len(batch) < MAX_BATCH_SIZE:
            elapsed = time.perf_counter() - start
            if elapsed >= MAX_BATCH_DELAY:
                break

            try:
                order = order_queue.get_nowait()
                batch.append(order)
            except asyncio.QueueEmpty:
                break

        # Process entire batch in one executor call
        await loop.run_in_executor(executor, process_batch, batch)

        for _ in batch:
            order_queue.task_done()


def process_batch(batch):
    for order in batch:
        symbol = order["symbol"]
        book = order_books.setdefault(symbol, matching_engine.OrderBook(symbol))
        book.add_order(
            float(order["price"]),
            float(order["quantity"]),
            order["side"].upper(),
            order["order_type"].upper(),
        )

@router.on_event("startup")
async def run_consumer():
    for _ in range(4):
     create_task(queue_consumer())

@router.post("/orders")
async def submit_orders(orders: Union[Dict, List[Dict]] = Body(...)):
    if isinstance(orders, dict):
        orders = [orders]

    accepted = 0

    for order in orders:
        try:
            order_queue.put_nowait(order)
            accepted += 1
        except asyncio.QueueFull:
            return JSONResponse(
                status_code=429,
                content={
                    "error": "overloaded",
                    "accepted": accepted,
                    "rejected": len(orders) - accepted,
                },
            )

    return {"status": "queued", "count": accepted}