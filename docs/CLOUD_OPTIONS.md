# Cloud / AI Access — Three Options

There are three ways to give the board AI vision + speech. This project
implements **Option B** (local proxy), but the others are valid choices —
pick whichever fits your priorities.

## Option A — Tuya Cloud AI Multimodal Platform (Tuya's recommended path)

[TuyaOpen's official architecture](https://github.com/tuya/TuyaOpen) bridges
the device to the **Tuya Developer Cloud**, which then bridges out to
multiple AI providers — **ChatGPT, Gemini, Claude, Qwen, DeepSeek, Amazon
Nova** — through a single integrated layer.

| Feature | What you get |
|---|---|
| TLS to cloud | Handled by TuyaOpen — works out of the box (they ship the right ciphers / cert pinning) |
| AI access | Bundled in the developer platform, multi-model fallback supported |
| Low-latency speech | Streaming TTS/STT, emotion-driven |
| Drag-and-drop agent | Build flows on the Tuya developer portal without writing code |
| Long/short-term memory | Built into the platform |
| Custom MCP server | Plug your own tools into the agent |
| Mini-app panels | Companion mobile UI auto-generated |
| Price | Reportedly has a free tier for development; verify current terms on Tuya's pricing page |

**Pros**

- Zero TLS pain — Tuya engineered the device-to-cloud transport, so the
  T5's mbedtls quirks aren't your problem.
- Lowest end-to-end latency in this comparison (Tuya optimizes the
  whole pipeline).
- Easiest path to a production device — drag-and-drop AI agent + an
  auto-generated companion app.
- Multi-provider — switch between OpenAI / Gemini / Claude without
  re-flashing.

**Cons**

- Vendor lock-in to Tuya Cloud.
- Less control over the *exact* prompt / model / pricing — the agent
  layer abstracts those choices.
- Requires a Tuya developer account + product registration.

**When to pick this**

- Production hardware shipping to real users.
- You want the fastest path to a working device and don't need to own
  every layer.
- You want multi-model flexibility without code changes.

**Why we *skipped* it for this project**

- We wanted full visibility into the prompt, the model, the response, and
  the cost — for educational/content purposes.
- We wanted to demonstrate building a custom architecture that explicitly
  routes through a self-hosted server.
- The TLS issue with direct OpenAI access is a great teaching moment, and
  Tuya Cloud would have hidden it.

## Option B — Local proxy (this project)

Run a tiny Flask (or FastAPI / nginx + lambda / anything) HTTP server on
your local network. Board → plain HTTP → proxy → HTTPS → OpenAI (or any
provider).

```
[T5 board]──HTTP──▶[ proxy on Raspberry Pi / NAS / VM ]──HTTPS──▶[OpenAI / Anthropic / Google]
```

See [PROXY_SETUP.md](PROXY_SETUP.md) for the install steps. The reference
implementation in `proxy/openai_proxy.py` is ~30 lines of Python and
forwards every `/v1/*` request to `api.openai.com`.

**Pros**

- Full control over prompts, model, request/response shape, cost, logs.
- Easy to swap providers — just point the proxy at a different upstream.
- Works with any cloud LLM that has an HTTPS REST API.
- Great for debugging — log every request/response on your LAN.

**Cons**

- Requires an always-on local machine on the same network as the board.
- You manage your own API key + billing.
- Must be on the **same routable subnet** as the board (see
  [LESSONS_LEARNED.md §2](LESSONS_LEARNED.md) — subnet isolation bites).
- Not production-grade out of the box (no auth, plain HTTP).

**When to pick this**

- Prototyping, content creation, learning.
- You already have a Pi / NAS / dev VM on the network.
- You want to choose the model + prompt directly.

## Option C — Direct HTTPS from the board (FAILED on T5 + TuyaOpen 2240c01e)

```
[T5 board]──HTTPS──▶[api.openai.com]
```

This *should* be the cleanest option but **doesn't work** on the T5 with
the version of TuyaOpen we tested:

```
[tuya_tls.c:682] mbedtls_ssl_handshake returned 0x3a00
                = MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE
```

The T5's bundled mbedtls is built without TLS 1.3 and lacks the cipher
suites OpenAI accepts in TLS 1.2 fallback. Patching the user-space
`mbedtls_config.h` had no effect — the actual mbedtls being linked is the
platform-bundled PSA mbedtls under
`platform/T5AI/t5_os/{ap,cp}/components/psa_mbedtls/`, and patching that
config also did not unblock the handshake.

**See** [LESSONS_LEARNED.md §1](LESSONS_LEARNED.md) for the full diagnosis
chain. Could be solvable with a deeper dive into `tuya_tls.c` or by
swapping in upstream mbedtls; in either case it's significant platform
work, not an app-level change.

**When to pick this**

- Once Tuya ships a firmware update that includes TLS 1.3 / modern ciphers
  in the platform mbedtls. Then it becomes the simplest choice.
- For now: don't pick this; use A or B.

## Summary

| | Option A: Tuya Cloud | Option B: Local proxy | Option C: Direct |
|---|---|---|---|
| Works today | ✅ | ✅ | ❌ |
| Needs always-on host | no | yes | no |
| Multi-provider | ✅ built-in | swap upstream | – |
| Vendor lock-in | Tuya | none | none |
| Best for | production | prototyping | (future) |
| Cost | Tuya pricing | your own API key | your own API key |
| TLS complexity | hidden | bypassed | blocked |

**Recommendation**

- Building a content piece, learning, prototyping → **Option B**.
- Building a real product → **Option A**, unless you specifically need
  the lowest-cost / single-provider control.
