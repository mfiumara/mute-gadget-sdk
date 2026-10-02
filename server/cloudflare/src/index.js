// Mute server on Cloudflare: the Python server runs unchanged in a container,
// one instance shared by every gadget so they can see each other's tools.
// /__ai/v1/* is Workers AI behind an OpenAI-compatible surface, gated by the
// device token; the container uses it as its model provider by default.

import { Container, getContainer } from "@cloudflare/containers";

const PASSTHROUGH = [
  "MUTE_DEVICE_TOKEN", "MUTE_LLM_BASE_URL", "MUTE_LLM_API_KEY", "MUTE_LLM_MODEL",
  "MUTE_STT_MODEL", "MUTE_TTS_MODEL", "MUTE_TTS_VOICE", "MUTE_SYSTEM_PROMPT", "MUTE_AGENT_NAME",
];

function toBase64(bytes) {
  let s = "";
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
  return btoa(s);
}

function authorized(request, env) {
  const enc = new TextEncoder();
  const got = enc.encode(request.headers.get("Authorization") || "");
  const want = enc.encode(`Bearer ${env.MUTE_DEVICE_TOKEN}`);
  return Boolean(env.MUTE_DEVICE_TOKEN) && got.byteLength === want.byteLength
    && crypto.subtle.timingSafeEqual(got, want);
}

async function workersAi(path, req, env) {
  try {
    if (path === "/v1/chat/completions") {
      const { model, messages, tools } = await req.json();
      const out = await env.AI.run(model, tools?.length ? { messages, tools } : { messages });
      if (out.choices) return Response.json(out);
      // Older models answer in Workers AI's own shape.
      const calls = (out.tool_calls || []).map((c, i) => ({
        id: `call_${i}`, type: "function",
        function: { name: c.name, arguments: JSON.stringify(c.arguments ?? {}) },
      }));
      return Response.json({ choices: [{ message: {
        role: "assistant", content: out.response ?? null, ...(calls.length ? { tool_calls: calls } : {}),
      } }] });
    }
    if (path === "/v1/audio/transcriptions") {
      const form = await req.formData();
      const audio = new Uint8Array(await form.get("file").arrayBuffer());
      const out = await env.AI.run(form.get("model"), { audio: toBase64(audio) });
      return Response.json({ text: out.text ?? "" });
    }
    if (path === "/v1/audio/speech") {
      const { model, input, voice } = await req.json();
      const out = await env.AI.run(model, { text: input, speaker: voice }, { returnRawResponse: true });
      const body = out instanceof Response ? out.body : out;
      return new Response(body, { headers: { "Content-Type": "audio/mpeg" } });
    }
    return Response.json({ error: "not found" }, { status: 404 });
  } catch (err) {
    console.error("workers ai", path, err);
    return Response.json({ error: String(err) }, { status: 502 });
  }
}

export class MuteServer extends Container {
  defaultPort = 8080;
  // Open gadget connections keep it awake; this is the grace after the last one.
  sleepAfter = "1h";
  enableInternet = true;

  constructor(ctx, env) {
    super(ctx, env);
    const vars = Object.fromEntries(PASSTHROUGH.filter((k) => env[k]).map((k) => [k, env[k]]));
    // The built-in Workers AI shim takes the device token as its key.
    if (vars.MUTE_LLM_BASE_URL?.includes("/__ai/") && !vars.MUTE_LLM_API_KEY) {
      vars.MUTE_LLM_API_KEY = env.MUTE_DEVICE_TOKEN;
    }
    this.envVars = vars;
  }
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname.startsWith("/__ai/")) {
      if (!authorized(request, env)) return Response.json({ error: "unauthorized" }, { status: 401 });
      return workersAi(url.pathname.slice("/__ai".length), request, env);
    }
    return getContainer(env.MUTE_SERVER, "main").fetch(request);
  },
};
