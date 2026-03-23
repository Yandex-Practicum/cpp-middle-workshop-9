#!/usr/bin/env python3
"""WebSocket simulator: depth stream."""

import asyncio
import json
import random
import string
from websockets.server import serve

# Настройки
SERVERS = [
    {"host": "0.0.0.0", "port": 8080, "name": "Server-A"},
    {"host": "0.0.0.0", "port": 8081, "name": "Server-B"},
]


def generate_orderbook_level(base_price: float, level: int, is_bid: bool) -> list[str]:
    """Генерирует уровень стакана: [price, quantity]."""
    price_step = 0.5 * (level + 1)
    if is_bid:
        price = base_price - price_step
    else:
        price = base_price + price_step
    quantity = round(random.uniform(0.1, 5.0), 4)
    return [f"{price:.2f}", f"{quantity}"]


def generate_depth(base_price: float, symbol, port: string) -> dict:
    """Генерирует стакан."""
    last_update_id = random.randint(1_000_000, 9_999_999)
    bids = [generate_orderbook_level(base_price, i, is_bid=True) for i in range(10)]
    asks = [generate_orderbook_level(base_price, i, is_bid=False) for i in range(10)]
    return {
        "serv_id": port,
        "symbol": symbol,
        "lastUpdateId": last_update_id,
        "bids": bids,
        "asks": asks
    }


async def handler(websocket, name, port: str):
    """Обработка подключения: ждём подписку, затем шлём данные."""
    print(f"[{name}] Client connected: {websocket.remote_address}")
    try:
        # Ждём корректное сообщение о подписке
        symbol = None
        while symbol is None:
            subscribe_msg = await websocket.recv()
            print(f"[{name}] Received subscribe message: {subscribe_msg}")

            # Проверяем корректность сообщения подписки
            try:
                msg = json.loads(subscribe_msg)
                if (msg.get("op") != "subscribe" or
                    not isinstance(msg.get("args"), list) or
                    len(msg["args"]) == 0 or
                    not msg["args"][0].startswith("orderbook.")):
                    print(f"[{name}] Invalid subscription message, waiting for next...")
                    continue
                symbol = msg["args"][0].split(".", 1)[1]
                print(f"[{name}] Subscribed to orderbook.{symbol}")
            except (json.JSONDecodeError, KeyError, IndexError):
                print(f"[{name}] Failed to parse subscription message, waiting for next...")
                await websocket.send(f"Invalid subscription format. Expected: {{'op': 'subscribe', 'args': ['orderbook.<symbol>']}}")
                continue

        # Отправляем подтверждение подписки
        await websocket.send( json.dumps(
            {
            "op": "subscribe",
            "args": symbol
            }
        ))
        print(f"[{name}] Subscription confirmed")

        # Отправляем данные стакана каждые 200мс
        base_price = 43000.0
        while True:
            base_price = 43000 + random.uniform(-100, 100)
            depth_message = generate_depth(base_price, symbol, port)
            await websocket.send(json.dumps(depth_message))
            await asyncio.sleep(0.1)
    except Exception as e:
        print(f"[{name}] Client disconnected: {e}")

async def start_server(config: dict):
    """Запуск одного сервера."""
    async with serve(
        lambda ws: handler(ws, config["name"], config["port"]),
        config["host"],
        config["port"]
    ) as server:
        print(f"✅ {config['name']} running on ws://{config['host']}:{config['port']}")
        await server.wait_closed()

async def main():
    """Запуск всех серверов параллельно."""
    tasks = [start_server(cfg) for cfg in SERVERS]
    await asyncio.gather(*tasks)

if __name__ == "__main__":
    print("🚀 Starting minimal WebSocket simulator...")
    asyncio.run(main())
