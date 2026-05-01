# Use Cases

Mapping of input gesture → AI prompt → ideal user experience.

| Gesture | Prompt | Best for |
|---|---|---|
| **Single click P12** (or **screen tap**) | `NAV_VISION_PROMPT` | Walking — get path/obstacle/direction |
| **Double click P12** | `NAV_READ_PROMPT` | Read text — signs, labels, prices, MRP, expiry |
| **Long press P12** (≥1.5 s) | `NAV_OBJECT_PROMPT` | Identify object held close — currency, products, medication |

## Single click — Navigation co-pilot

Default scene query. The model is instructed to act as a forward-walking
co-pilot, calling out obstacles in clock-position terms with action verbs.

**Expected outputs:**

> *"Clear path ahead. Door 2 meters at your 1 o'clock."*
> *"Person walking toward you 3 steps ahead. Step right to pass."*
> *"Wall at arm's length directly ahead. Turn left or right to continue."*
> *"Stairs going up start in 2 steps. Continue forward."*

**Pitfalls (and how the prompt addresses them):**

- Generic descriptions like "I see a hallway with people" → fixed by
  the *PATH STATUS / WHERE / ACTION* structure.
- Mentioning "image quality" or "blurry" → explicitly forbidden in the
  prompt (model otherwise tends to hedge).
- 5-paragraph essays → capped at 2–3 short sentences.

## Double click — Read text aloud

Second-most-common use case. Targets:

- **Shop signs** — "Coffee Shop", "Pharmacy", "Bus 27 → Downtown"
- **Product labels** — brand, name, MRP/price, ingredients, allergens
- **MRP / price tags** — Indian retail's MRP convention is explicitly
  named in the prompt ("read every visible … price tag, MRP, expiry date")
- **Expiry dates** on packaged food / medication
- **Restaurant menus** — read the largest items first
- **Bus / metro stop names** — read the largest sign
- **Bills, receipts, paperwork** — read top-down
- **Button labels on appliances** — "ON / OFF / TIMER / 1000 W"

**Expected outputs:**

> *"Hide & Seek. Britannia. MRP 30 rupees. Expires August 2026."*
> *"Platform 3. Train to Mumbai Central. 12:45."*
> *"Crocin Advance. Paracetamol 500 milligrams. Take after food."*

## Long press — Identify object held close

Targets things held in the hand:

- **Currency notes** — denomination + currency name. Critical accessibility
  feature — many countries' notes have similar tactile feel.
- **Coins** — denomination if the prompt can read the engraving.
- **Packaged products** — name, brand, MRP/price, expiry.
- **Medication** — read the name and dosage.
- **Keys / fobs / cards** — identify shape and any visible label.
- **Phone / remote** — describe color and which buttons are which.

**Expected outputs:**

> *"500 rupee note."*
> *"Lays Magic Masala. MRP 20 rupees. Best before December 2026."*
> *"Crocin 500. Paracetamol 500 mg, 15 tablets per strip."*
> *"Black ballpoint pen with silver clip."*

## Composing modes

The current firmware fires one mode per gesture and waits for the response
to play out before accepting the next trigger. Future enhancements (see
[SENSORS_ROADMAP.md](SENSORS_ROADMAP.md)):

- **Auto-scan mode** — tap once = scan once, double-tap = scan-and-speak
  every N seconds, double-tap again = stop. Useful for "show me everything
  I'm walking past" guided tours.
- **Voice activation ("Hey Tuya")** — KWS already wired in TuyaOpen, just
  needs the VAD → ASR → prompt-routing path connected.
- **Always-on obstacle warning** — pair with mmWave + haptic motor for
  instant collision warnings (no cloud, no speech), so the LLM is reserved
  for higher-cognition queries.

## Cost ballpark (OpenAI)

At late-2026 prices, `gpt-4o-mini` vision + `gpt-4o-mini-tts`:

| Trigger pattern | Cost per query | Per hour at 1/min | Per month at 50 queries/day |
|---|---|---|---|
| Scene navigation | ~$0.005 | $0.30 | ~$7 |
| Read text | ~$0.005 | $0.30 | ~$7 |
| Identify object | ~$0.005 | $0.30 | ~$7 |

Auto-scan mode at 1 query / 10 s would be ~$1.80/hour — costly for
continuous use; recommended only for active navigation moments.
