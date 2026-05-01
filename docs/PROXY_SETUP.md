# OpenAI Proxy Setup

The T5 board cannot speak HTTPS to `api.openai.com` directly because its
mbedTLS implementation lacks the cipher suites OpenAI accepts (see
[LESSONS_LEARNED.md](LESSONS_LEARNED.md)). The Flask proxy in `proxy/`
runs on a machine that has a modern TLS stack and forwards `/v1/*`
requests to OpenAI on the board's behalf.

## Requirements

- Any always-on machine on the same Wi-Fi as the board (Linux box,
  Raspberry Pi, NAS, dev VM, even a laptop while you're testing).
- Python 3.8+, outbound HTTPS to `api.openai.com:443`.

## Local install (dev)

```bash
cd proxy
pip install -r requirements.txt
cp .env.example .env
$EDITOR .env   # set OPENAI_API_KEY=sk-proj-...
export $(cat .env | xargs)
python3 openai_proxy.py
```

The proxy listens on **`0.0.0.0:8888`** by default. Test it from the same
network:

```bash
curl http://<proxy-host>:8888/v1/chat/completions \
    -H "Content-Type: application/json" \
    -d '{"model":"gpt-4o-mini","messages":[{"role":"user","content":"reply ok"}],"max_tokens":5}'
```

If you get a JSON `choices[0].message.content` back, the proxy is wired
correctly. Then set `NAV_PROXY_HOST` in `nav_config.h` to the proxy
machine's IP and rebuild the firmware.

## Permanent install (systemd)

Suitable for a Raspberry Pi, dev VM, or any always-on Linux box.

```bash
sudo cp openai-proxy.service /etc/systemd/system/
sudo $EDITOR /etc/systemd/system/openai-proxy.service
# Replace USERNAME with the system user that will run the service
# Set the correct paths to openai_proxy.py and .env

sudo mkdir -p /home/USERNAME/proxy
sudo cp openai_proxy.py .env /home/USERNAME/proxy/
sudo chown -R USERNAME:USERNAME /home/USERNAME/proxy
sudo chmod 600 /home/USERNAME/proxy/.env

sudo systemctl daemon-reload
sudo systemctl enable openai-proxy
sudo systemctl start openai-proxy
sudo systemctl status openai-proxy
```

Logs:

```bash
journalctl -u openai-proxy -f
```

## Network considerations

The board needs **TCP reachability** to the proxy on port 8888. If your
home network has multiple subnets (e.g. wired `192.168.0.x` and Wi-Fi
`192.168.1.x` from a double-NAT router), board-on-Wi-Fi may not be able
to reach proxy-on-wired even though they're "on the same Wi-Fi". Quick
test from your phone (also on the board's Wi-Fi):

```
http://<proxy-host>:8888/
```

If the browser shows "Not Found" (a Flask 404), the proxy is reachable.
If it hangs / "site can't be reached", you have a routing/firewall issue.

Common fixes:
- Run the proxy on a machine that's on the same Wi-Fi as the board (not
  on a separate wired LAN).
- Set the secondary router to bridge mode instead of NAT.
- Add a port-forward on the upstream router for port 8888 → proxy host.

## Hardening (production)

The default proxy is **not production-grade** and intentionally minimal:
- No authentication (anyone on your LAN can use your OpenAI quota).
- HTTP only (no TLS — fine for LAN, not for public exposure).
- Flask development server (single-threaded; not optimized for load).

For a production deploy:
1. Bind to a non-standard port and add a shared-secret `Authorization`
   header check.
2. Run behind nginx + WSGI (gunicorn / uwsgi) for proper concurrency.
3. Add request rate limiting and per-source budgeting.
4. **Never expose the raw proxy to the public internet** — your OpenAI
   key would leak.
