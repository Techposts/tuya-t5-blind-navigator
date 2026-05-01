#!/usr/bin/env python3
"""
OpenAI proxy for Tuya T5 board.
T5 sends plain HTTP to this proxy → proxy forwards to api.openai.com via HTTPS.
Solves Tuya mbedTLS RSA cipher incompatibility with OpenAI.
"""
from flask import Flask, request, Response
import requests, os, sys

app = Flask(__name__)

OPENAI_KEY = os.environ.get("OPENAI_API_KEY") or sys.exit("OPENAI_API_KEY env var required")
OPENAI_BASE = "https://api.openai.com"

@app.route("/v1/<path:subpath>", methods=["POST", "GET"])
def proxy(subpath):
    url = f"{OPENAI_BASE}/v1/{subpath}"
    
    headers = {k: v for k, v in request.headers if k.lower() not in 
               ("host", "content-length", "transfer-encoding")}
    headers["Authorization"] = f"Bearer {OPENAI_KEY}"

    resp = requests.request(
        method=request.method,
        url=url,
        headers=headers,
        data=request.get_data(),
        stream=True,
        timeout=60,
    )

    excluded = {"content-encoding", "transfer-encoding", "connection"}
    resp_headers = {k: v for k, v in resp.headers.items()
                    if k.lower() not in excluded}

    return Response(resp.content, status=resp.status_code, headers=resp_headers)

if __name__ == "__main__":
    print("OpenAI proxy on 0.0.0.0:8888")
    app.run(host="0.0.0.0", port=8888, threaded=True)
