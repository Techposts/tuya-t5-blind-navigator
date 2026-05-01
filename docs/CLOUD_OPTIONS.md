# Cloud / AI Access — Three Options

There are three ways to give the board AI vision + speech. This project
implements **Option B** (local proxy), but the others are valid choices —
pick whichever fits your priorities.

## Option A — Tuya Cloud AI Multimodal Platform (Tuya's official path)

The [TuyaOpen](https://github.com/tuya/TuyaOpen) SDK pairs the device with
**Tuya Cloud's low-latency multimodal AI platform** (the official phrase
from Tuya's README). Through that platform, the device can talk to
multiple AI providers — **ChatGPT, Gemini, Claude, Qwen, DeepSeek, Doubao,
Amazon Nova** — through a single integrated layer with drag-and-drop
agent workflows.

Official references:

| Resource | URL |
|---|---|
| Source code / SDK | <https://github.com/tuya/TuyaOpen> |
| Documentation home | <https://tuyaopen.ai/docs/about-tuyaopen> |
| **AI Agent Platform** (the Option A entry point) | <https://tuyaopen.ai/docs/cloud/tuya-cloud/ai-agent/ai-agent-dev-platform> |
| Quick-start | <https://tuyaopen.ai/docs/quick-start/enviroment-setup> |
| Pricing (free tier) | <https://tuyaopen.ai/pricing> |
| T5AI Board overview | <https://tuyaopen.ai/docs/hardware-specific/tuya-t5/t5-ai-board/overview-t5-ai-board> |
| Discord community | <https://discord.com/invite/yPPShSTttG> |

| Feature | What you get |
|---|---|
| TLS to cloud | Handled by TuyaOpen — works out of the box (right ciphers + cert pinning shipped in the platform mbedtls) |
| AI access | Pair with Tuya Cloud's multimodal AI; ChatGPT / Gemini / Claude / Qwen / DeepSeek / Doubao / Amazon Nova |
| Drag-and-drop workflows | Build AI agent flows in the Tuya developer portal without writing code |
| Streaming speech | Low-latency TTS / STT via the platform |
| Pricing | Free tier available; check the [pricing page](https://tuyaopen.ai/pricing) for current commercial terms |

**Pros**

- Zero TLS pain — Tuya engineered the device-to-cloud transport, so the
  T5's mbedtls quirks aren't your problem.
- Lowest end-to-end latency — Tuya optimizes the whole pipeline.
- Easiest path to a production device — drag-and-drop AI agent flows.
- Multi-provider — switch between OpenAI / Gemini / Claude / Doubao / etc.
  without re-flashing.
- Free tier exists for development.

**Cons**

- Vendor lock-in to Tuya Cloud.
- Less direct control over the *exact* prompt / model selection / cost
  ceiling — the agent layer abstracts those choices.
- Requires a Tuya developer account + product registration.

**When to pick this**

- Production hardware shipping to real users.
- You want the fastest path to a working device.
- You want multi-model flexibility without code changes.

**Why we *skipped* it for this project**

- We wanted full visibility into the prompt, the model, the response, and
  the cost — for educational/content purposes.
- We wanted to demonstrate building a custom architecture that explicitly
  routes through a self-hosted server.
- The TLS issue with direct OpenAI access is a great teaching moment, and
  Tuya Cloud would have hidden it.

> **Note**: this comparison reflects the TuyaOpen README at the time of
> writing. Tuya updates the platform regularly — refer to the
> [official docs](https://tuyaopen.ai/docs/about-tuyaopen) for the
> current feature set.

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

Note that this is the failure mode for **direct HTTPS to OpenAI**.
Connecting to **Tuya Cloud** (Option A) does not have this problem because
the Tuya cloud endpoint uses ciphers the platform mbedtls *does* support
(they ship for that combination).

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
| Cost | free tier + Tuya pricing | your own API key | your own API key |
| TLS complexity | hidden | bypassed | blocked |

**Recommendation**

- Building a content piece, learning, prototyping, want to own every
  layer → **Option B**.
- Building a real product, want fastest time-to-working,
  multi-model flexibility, free-tier friendliness → **Option A**.

## Same pattern, different scope

Worth noting: our local proxy and Tuya Cloud (Option A) implement **the
same architectural pattern** — a thin server in front of a TLS-limited
embedded device, fronting multiple AI providers. We just stopped at the
30-line minimum-viable version; Tuya productized it with managed hosting
and an agent layer.

| | Local proxy (Option B) | Tuya Cloud (Option A) |
|---|---|---|
| Transport | Device→server→AI providers | Device→server→AI providers |
| Multi-model | Yes (change upstream URL) | Yes (drag-and-drop selector) |
| Hosted | You (LAN) | Tuya (managed, global) |
| Auth abstraction | Server holds the API key | Tuya holds keys + free dev licenses |
| Streaming TTS/STT | Pass-through | Optimized streaming layer |
| Long/short-term memory | None | Built-in |
| Custom MCP server | None | Built-in |
| Companion mini-app | None | Auto-generated |
| Cost model | You pay each AI provider | Tuya billing + free tier |
| Time to working | ~30 min (set up Flask + Wi-Fi) | ~hours (account, product, license) |

The proxy approach is the right tool for **prototyping, learning, and
content** — total transparency, no account dependencies, easy to debug.
Tuya Cloud is the right tool for **products** — managed scale, free dev
licenses, agent abstractions, mobile UI.
