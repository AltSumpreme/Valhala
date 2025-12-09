from fastapi import APIRouter,Body
from asyncio import Queue, create_task, get_event_loop
from concurrent.futures import ThreadPoolExecutor
from app.books.books import order_books
from typing import List, Dict,Union
import matching_engine

router = APIRouter()

order_queue = Queue()
executor = ThreadPoolExecutor(max_workers=8)  


def match_order(order):
    symbol = order["symbol"]
    book = order_books.setdefault(symbol, matching_engine.OrderBook(symbol))
    return book.add_order(float(order["price"]), float(order["quantity"]), order["side"].upper(), order["order_type"].upper())

async def queue_consumer():
    while True:
        order = await order_queue.get()
        await get_event_loop().run_in_executor(executor, match_order, order)
        order_queue.task_done()

@router.on_event("startup")
async def run_consumer():
    create_task(queue_consumer())

@router.post("/orders")
async def submit_orders(orders: Union[Dict, List[Dict]] = Body(...)):
    #single order wrapped in a list
    if isinstance(orders,dict):
        orders = [orders]
    for order in orders:
        await order_queue.put(order)
    return {"status": "queued", "count": len(orders)}
