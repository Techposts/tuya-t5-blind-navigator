# IRIS — OpenAI proxy

A tiny Flask service that sits between the IRIS device and `api.openai.com`. The T5AI board sends plain HTTP to the proxy, which terminates TLS to OpenAI on its behalf.

**Why?** TuyaOpen's bundled mbedTLS does not negotiate the cipher suites OpenAI's API accepts (RSA-only on the device side, OpenAI requires ECDHE). Adding a proxy on a Raspberry Pi or any always-on Linux host on the same network sidesteps the problem entirely, and lets us use the cheap T5AI for vision queries without rebuilding the TLS stack.

The Pro track in this repo (everything in `firmware/blind_navigator/`) is hardcoded to talk to this proxy. The Cloud track (Tuya AI Agent Platform) does not need it.

---

## Quick setup (Linux / Raspberry Pi)

```bash
# 1. Install dependencies
cd /path/to/this/proxy
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt

# 2. Set your OpenAI API key as an env var
export OPENAI_API_KEY="sk-proj-..."

# 3. Run
python3 openai_proxy.py
# → listening on 0.0.0.0:8888
```

Test it from any other machine on the same network:

```bash
curl -X POST http://<host-ip>:8888/v1/models \
  -H "Authorization: Bearer $OPENAI_API_KEY"
# Should return a JSON list of available models.
```

---

## Run as a systemd service (always-on)

```bash
# Copy the service file (edit paths inside it first if needed):
sudo cp openai-proxy.service /etc/systemd/system/
sudo systemctl daemon-reload

# Set the API key in the service environment file:
sudo mkdir -p /etc/openai-proxy
echo "OPENAI_API_KEY=sk-proj-..." | sudo tee /etc/openai-proxy/env

# Enable + start
sudo systemctl enable --now openai-proxy

# Check it's running
sudo systemctl status openai-proxy
sudo journalctl -u openai-proxy -f
```

The service binds to `:8888` by default. If you want a different port, edit `openai_proxy.py` (the bottom `app.run(...)` call) and the service file.

---

## Configure the IRIS firmware to use it

Edit `firmware/blind_navigator/include/nav_config.h` (copy from `nav_config.h.example`):

```c
#define NAV_PROXY_HOST  "192.168.0.171"   // your proxy server's LAN IP
#define NAV_PROXY_PORT  8888
```

Rebuild + flash. The device will hit `http://<NAV_PROXY_HOST>:<NAV_PROXY_PORT>/v1/...` and the proxy forwards to OpenAI.

---

## What's running on the dev VM today (for reference)

The reference instance for this project runs on a Proxmox VM at `192.168.0.171`:

- File: `/home/tuya/openai_proxy.py`
- Service: `openai-proxy.service` (systemd)
- Port: `8888`
- API key: stored in the service environment file

The `proxy/` directory in this repo is the canonical version — copy from here if you're setting up your own.

---

## Security notes

- The proxy has **no authentication** by default — anyone on your LAN can use your OpenAI key by hitting the port.
- For LAN-only use this is fine. **Do NOT expose the proxy to the public internet** without adding API key validation.
- If you want to lock it down, edit `openai_proxy.py` to require a shared secret in a custom header, and have the firmware send that header. Trivial change — Cursor or Claude can do it for you in one diff.

---

## Asking AI to set it up for you

If you're using Cursor, Claude Code, or another AI coding assistant, you can paste this prompt:

> "I want to run the IRIS OpenAI proxy on this Linux box. Read `openai_proxy.py`, `requirements.txt`, and `openai-proxy.service` in this directory, install dependencies, set up the systemd service with my OPENAI_API_KEY, and start it on port 8888. Verify it's reachable from another device on my LAN."

The assistant has all the context it needs from the files in this directory.
