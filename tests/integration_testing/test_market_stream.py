import asyncio
import aiohttp
import websockets
import json
import random
import time

HTTP_URL = "http://127.0.0.1:8000/api/v1/orders"
WS_URI = "ws://127.0.0.1:8000/api/v1/marketdata"

NUM_HTTP_CLIENTS = 50
ORDERS_PER_CLIENT = 200
NUM_WS_CLIENTS = 100
LIVE_DURATION = 30  # seconds of streaming after prefill
PREFILL_DURATION = 5  # seconds to build the order book

metrics = {
    "http_orders_sent": 0,
    "http_errors": 0,
    "ws_messages_received": 0,
    "ws_errors": 0,
    "ws_latency": []
}

async def submit_orders(client_id, session, num_orders=ORDERS_PER_CLIENT):
    global metrics
    for i in range(num_orders):
        order = {
            "symbol": "BTC-USD",
            "side": "BUY" if i % 2 == 0 else "SELL",
            "order_type": "LIMIT",
            "price": 100 + random.randint(0, 10),
            "quantity": random.randint(1, 10),
            "client_id": client_id,
        }
        try:
            async with session.post(HTTP_URL, json=order) as resp:
                if resp.status == 200:
                    metrics["http_orders_sent"] += 1
                else:
                    metrics["http_errors"] += 1
        except Exception:
            metrics["http_errors"] += 1
        await asyncio.sleep(0.01)  # small delay to let engine process

async def ws_client(client_id, stop_event):
    global metrics
    try:
        async with websockets.connect(WS_URI) as ws:
            while not stop_event.is_set():
                try:
                    start = time.time()
                    msg = await asyncio.wait_for(ws.recv(), timeout=1.0)
                    duration = time.time() - start
                    metrics["ws_messages_received"] += 1
                    metrics["ws_latency"].append(duration)
                    data = json.loads(msg)
                except asyncio.TimeoutError:
                    continue   # wait again
    except Exception:
        metrics["ws_errors"] += 1

async def main():
    stop_event = asyncio.Event()

    # Step 0: Start WebSocket clients
    print(f"Starting {NUM_WS_CLIENTS} WebSocket clients...")
    ws_tasks = [asyncio.create_task(ws_client(i, stop_event)) for i in range(NUM_WS_CLIENTS)]

    # Step 1: Prefill order book for PREFILL_DURATION seconds
    print(f"Prefilling order book for {PREFILL_DURATION}s...")
    prefill_start = time.time()
    async with aiohttp.ClientSession() as session:
        while time.time() - prefill_start < PREFILL_DURATION:
            tasks = [asyncio.create_task(submit_orders(i, session, num_orders=5)) for i in range(NUM_HTTP_CLIENTS)]
            await asyncio.gather(*tasks)
    print("Prefill complete.\n")

    # Step 2: Submit HTTP orders concurrently
    print("Submitting HTTP orders...")
    start_http = time.time()
    async with aiohttp.ClientSession() as session:
        http_tasks = [asyncio.create_task(submit_orders(i, session)) for i in range(NUM_HTTP_CLIENTS)]
        await asyncio.gather(*http_tasks)
    http_duration = time.time() - start_http
    print(f"HTTP Orders sent:   {metrics['http_orders_sent']}")
    print(f"HTTP errors: {metrics['http_errors']}")
    print(f"HTTP duration: {http_duration:.2f}s")
    

    # Step 3: Keep WS clients alive for LIVE_DURATION
    print(f"Streaming WebSocket messages for {LIVE_DURATION}s...")
    await asyncio.sleep(LIVE_DURATION)
    stop_event.set()
    await asyncio.gather(*ws_tasks, return_exceptions=True)
    print("WebSocket streaming finished.\n")

    # Step 4: Compute WS metrics
    latencies = sorted(metrics["ws_latency"])
    avg_latency = sum(latencies)/len(latencies) if latencies else 0
    p50 = latencies[int(0.5*len(latencies))] if latencies else 0
    p95 = latencies[int(0.95*len(latencies))] if latencies else 0
    print(f"WS messages received: {metrics['ws_messages_received']}")
    print(f"WS errors: {metrics['ws_errors']}")
    print(f"WS throughput: {metrics['ws_messages_received']/LIVE_DURATION:.2f} msg/sec")
    print(f"WS latency (s) | avg: {avg_latency:.6f}, p50: {p50:.6f}, p95: {p95:.6f}")
    print(f"HTTP throughput: {metrics['http_orders_sent']/http_duration:.2f} orders/sec\n")

if __name__ == "__main__":
    asyncio.run(main())
