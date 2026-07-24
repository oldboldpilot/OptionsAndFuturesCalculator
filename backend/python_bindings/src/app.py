import yfinance as yf
from fastapi import FastAPI, HTTPException
import uvicorn
import math

app = FastAPI(title="Options and Futures Calculator API")

@app.get("/api/market-data/{symbol}")
def get_market_data(symbol: str):
    """
    Fetches market data using yfinance for injection into the C++ compute engine.
    """
    try:
        ticker = yf.Ticker(symbol)
        info = ticker.fast_info
        
        # In a real app we'd also pull options chains here for IV
        return {
            "symbol": symbol,
            "regularMarketPrice": info.last_price,
            "regularMarketPreviousClose": info.previous_close,
            "impliedVolatility": 0.20 # Stubbed for now
        }
    except Exception as e:
        raise HTTPException(status_code=404, detail=f"Failed to fetch {symbol}: {str(e)}")

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)
