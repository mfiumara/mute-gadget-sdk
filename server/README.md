# Mute server

The backend your Mute gadgets connect to. One Python file, no database. It
speaks the gadget protocol and answers with any OpenAI-compatible model
provider: OpenAI, OpenRouter, Groq, Together, a local Ollama, LM Studio,
vLLM or llama.cpp server, or anything else that serves `/v1/chat/completions`.

Every connected gadget registers its commands, and the model gets all of them
as tools. You can talk to the voice gadget on your desk and have it run a
command on your Raspberry Pi.

## Run it

```sh
export MUTE_DEVICE_TOKEN="$(openssl rand -hex 32)"   # gadgets use this to connect
export MUTE_LLM_API_KEY=sk-...
uv run --with ../linux --with aiohttp mute_server.py
```

It listens on `0.0.0.0:8080`. Put it behind a TLS reverse proxy (Caddy,
nginx, Tailscale Serve) so gadgets reach it as `https://your-host`. The ESP32
firmware only connects over TLS. The Linux client also accepts `http://`
on a network you trust.

| Variable | Default | |
|---|---|---|
| `MUTE_DEVICE_TOKEN` | required | Shared secret every gadget presents |
| `MUTE_LLM_BASE_URL` | `https://api.openai.com/v1` | Any OpenAI-compatible endpoint |
| `MUTE_LLM_API_KEY` | empty | Sent as a bearer token if set |
| `MUTE_LLM_MODEL` | `gpt-4o-mini` | Chat model; needs tool calling |
| `MUTE_STT_MODEL` | `whisper-1` | Speech to text, for voice gadgets |
| `MUTE_TTS_MODEL` | `tts-1` | Text to speech, for voice gadgets |
| `MUTE_TTS_VOICE` | `alloy` | |
| `MUTE_SYSTEM_PROMPT` | a short default | |
| `MUTE_HOST`, `MUTE_PORT` | `0.0.0.0`, `8080` | |

Local model with Ollama:

```sh
MUTE_LLM_BASE_URL=http://localhost:11434/v1 MUTE_LLM_MODEL=qwen3:8b \
MUTE_DEVICE_TOKEN=... uv run --with ../linux --with aiohttp mute_server.py
```

Voice gadgets also need `/v1/audio/transcriptions` and `/v1/audio/speech` at
the same base URL. Ollama has neither; text gadgets work without them.

## Run it on Cloudflare

[`cloudflare/`](cloudflare) runs this server unchanged in a Cloudflare
Container behind a Worker, on your `workers.dev` hostname with a public TLS
certificate, which is what the ESP32 needs. It uses Workers AI by default, so
it needs no other provider key: Mistral Small 3.1 for chat and tools, Whisper
for speech to text, and Deepgram Aura for speech. Containers need the Workers
Paid plan.

```sh
cd cloudflare
npm install
npx wrangler secret put MUTE_DEVICE_TOKEN     # paste a long random string
# set MUTE_LLM_BASE_URL in wrangler.jsonc to https://mute-server.<your-subdomain>.workers.dev/__ai/v1
npx wrangler deploy
```

To use another provider, point `MUTE_LLM_BASE_URL` and the model variables in
`wrangler.jsonc` at it and `wrangler secret put MUTE_LLM_API_KEY`. One
container serves every gadget; chat history lives in its memory.

Check a deployment end to end, with real gadget clients and the real model:

```sh
MUTE_DEVICE_TOKEN=... uv run --with ../linux live_test.py https://mute-server.<your-subdomain>.workers.dev
```

## What it implements

| Endpoint | |
|---|---|
| `GET /fetch_vms` | Lists one VM, `mute`, with the WebSocket URL and bearer |
| `POST /device_token/refresh` | Returns the same token; it never expires |
| `GET /v1/noise` (WebSocket) | Noise XX, then HTTP-like streams inside it |
| `POST /link-control` (in Noise) | `link.register`, `link.invoke`, `link.result` |
| `POST /chat/stream` (in Noise) | A user turn from a gadget: text, or voice |

Chat history is kept in memory per session and is lost on restart.

## Security

The device token is the only credential: anyone who has it can connect a
gadget and use your model. Make it long and random, serve over TLS, and
rotate it by changing the variable and re-pairing your gadgets. The Noise
handshake encrypts traffic but does not authenticate the server, so TLS is
what stops an active man-in-the-middle.

## Tests

```sh
uv run --with pytest --with aiohttp --with ../linux pytest
```
