from fastapi import FastAPI
from fastapi.responses import JSONResponse
from app.routes import order_routes,marketdata_routes,tradedata_routes
from fastapi.middleware.cors import CORSMiddleware
from app.utils.logging import setup_logging
import asyncio
from app.routes.marketdata_routes import broadcast_market_data


setup_logging()


app = FastAPI(title="Crypto Matching Engine", version="1.0.0")

app.include_router(order_routes.router, prefix="/api/v1",tags=["orders"])
app.include_router(marketdata_routes.router, prefix="/api/v1",tags=["marketdata"] )
app.include_router(tradedata_routes.router, prefix="/api/v1",tags=["trades"] )

@app.on_event("startup")
async def start_broadcaster():
    asyncio.create_task(broadcast_market_data())

app.add_middleware(CORSMiddleware,
    allow_origins=["*"],  # change for production
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],)

@app.get("/")
def root():
    return JSONResponse(content={"message": "Crypto Matching Engine is running."})


def main():
    import uvicorn
    uvicorn.run("app.main:app", host="0.0.0.0", port=8000, reload=True)
    print("Hello from crypto-matching-engine!")


if __name__ == "__main__":
    main()
