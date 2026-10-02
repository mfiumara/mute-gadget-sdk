/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Talking to Mute.
 *
 * Mute holds one HTTP-over-Noise connection to its VM (the transport
 * Home Link uses) and runs each push-to-talk turn over it, entirely on the VM:
 *   speech -> POST /api/voice/dictation (streamed 24 kHz PCM, NDJSON transcripts)
 *   text   -> POST /chat/stream, reply events on POST /chat/subscribe
 *   reply  -> GET /api/voice/tts-stream?message_id=... (MP3, decoded here)
 *
 * Credentials: a device token, exchanged through the Mute API for the VM's own
 * token. If the account API rejects it and a VM ID is set, the token is tried
 * as that VM's token directly.
 *
 * Without CONFIG_MUTE_HATCH (no PSRAM) the same API is backed by
 * mute_chat_link.c: the note rides Home Link's session to its paired VM and
 * replies come back as text only (no audio from turn_read).
 */

typedef enum {
    MUTE_HATCH_NOT_SET,       /* no token */
    MUTE_HATCH_OFFLINE,       /* configured, no Wi-Fi */
    MUTE_HATCH_UNTESTED,
    MUTE_HATCH_TESTING,
    MUTE_HATCH_REACHABLE,     /* connected to the VM */
    MUTE_HATCH_UNREACHABLE,
} mute_hatch_state_t;

typedef struct {
    mute_hatch_state_t state;
    char detail[48];
} mute_hatch_status_t;

/* Starts the connection task; it connects once Wi-Fi is up. */
void mute_hatch_start(void);

/* The session task connects once Wi-Fi is up, and again whenever a turn needs it. */
void mute_hatch_status(mute_hatch_status_t *out);
/* (Re)connects to the VM and reports the result in the status. */
void mute_hatch_test(void);
/* Call when host/VM/token change: forgets the last result and the connection. */
void mute_hatch_config_changed(void);
const char *mute_hatch_state_name(mute_hatch_state_t state);
/* Screen off, voice idle: check the connection less often. */
void mute_hatch_set_resting(bool resting);

/* ---- Push-to-talk turns (voice task only) ---- */

/* Token set and Wi-Fi up: a press can start a turn. */
bool mute_hatch_ready(void);

/* Press: connects if needed and starts streaming speech to the VM. */
void mute_hatch_turn_begin(void);
/* 16 kHz mono speech, in order, from press to release. */
void mute_hatch_turn_audio(const int16_t *pcm, size_t frames);
/* Release: no more audio; the reply follows. */
void mute_hatch_turn_end(void);
/* Abandons the turn (tap, or barge-in during the reply). */
void mute_hatch_turn_cancel(void);

typedef enum {
    MUTE_HATCH_EV_NONE,
    MUTE_HATCH_EV_HEARD,    /* transcript so far (partial while talking, then final) */
    MUTE_HATCH_EV_REPLY,    /* reply text so far */
    MUTE_HATCH_EV_DONE,     /* reply complete; the audio stream is drained after this */
    MUTE_HATCH_EV_ERROR,    /* turn failed; text says why */
    MUTE_HATCH_EV_SENT,     /* the VM has the note (CONFIG_MUTE_HATCH only) */
} mute_hatch_ev_t;

/* Non-blocking; copies the event's text. Events of cancelled turns are dropped. */
mute_hatch_ev_t mute_hatch_turn_event(char *text, size_t cap);

/*
 * The page of reply text holding what's being said after `played` frames of
 * reply speech (sized by mute_state_page), or before the speech its opening
 * page. False until there's reply text to page.
 */
bool mute_hatch_turn_caption(size_t played, char *out, size_t cap);

/* Reply speech as 16 kHz mono. Waits up to wait_ms for some; returns frames read. */
size_t mute_hatch_turn_read(int16_t *pcm, size_t frames, int wait_ms);

/* Bench test: decodes a built-in MP3 to 16 kHz; caller frees *pcm. Returns frames. */
size_t mute_hatch_mp3_selftest(int16_t **pcm);

/*
 * As mute_hatch_turn_audio, for a note recorded before the turn began: takes
 * what fits of it, waiting up to wait_ms for room if not all of it does, and
 * returns the frames taken (CONFIG_MUTE_HATCH only).
 */
size_t mute_hatch_turn_audio_wait(const int16_t *pcm, size_t frames, int wait_ms);

/* ---- Typed turns from the serial console (CONFIG_MUTE_HATCH only) ---- */

/*
 * Sends typed text to the chat. The reply isn't spoken or captioned: it goes to
 * the console as it streams in, however long it is, one line per piece:
 *   @chat {"seq":N,"type":TYPE,...}
 * TYPE is "sent" (bytes), "busy" (on: the agent is working), "text" (msg, text),
 * "message_done" (msg, bytes), "done" (messages, complete) or "error" (text).
 * tools/mute/chat.py reads them. Takes `text` (malloc'd) and frees it. A voice
 * press ends a typed turn; a typed turn is refused while a voice turn runs.
 */
void mute_hatch_text_turn(char *text);
void mute_hatch_text_cancel(void);

/*
 * Prints one "@chat" line per call (more if `text` is long): the type, then the
 * printf-style `fields` (JSON members, or NULL), then `text` escaped (or none).
 */
void mute_hatch_console(const char *type, const char *text, const char *fields, ...)
    __attribute__((format(printf, 3, 4)));
/* Undoes the escapes in a line typed at the console (\n \r \t \\) in place; returns the length. */
size_t mute_hatch_unescape(char *s);

#ifdef __cplusplus
}
#endif
