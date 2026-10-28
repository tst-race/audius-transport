# Precise Design: Audius Transport

**Phase 2 deliverable.** Expands the top-rated Phase 1 option — **Known track ID (direct) addressing, listed (public) tracks** — into a precise channel definition. This document fixes manifest values, `LinkAddress`, `ServiceClient` contract and credential handling for the `Audius` transport. Unverified claims are marked **(unverified)**.

Developer decisions reflected here:
1. Begin with **listed (public) tracks only**; unlisted (secret) tracks are a follow-up TODO.
2. Deletion / on-chain metadata persistence is a **TODO**; deletion is designed as **best-effort** and does not block.
3. Three content types: (a) text/base64 in metadata fields (primary messaging piece, implemented now with a small valid audio file uploaded alongside); (b) lossless audio steganography; (c) loss-tolerant audio steganography. Only text encodings exist today, so (b)/(c) are **TODO** until an audio steganography encoding is found.
4. Primary messaging piece is text metadata fields; valid audio files are uploaded alongside as a placeholder.

---

## 1. Authentication credentials

Audius has three write-capable auth mechanisms (swagger `securitySchemes`: `BasicAuth`, `BearerAuth`, `OAuth2`) plus optional read-side API keys. For this transport we use the **developer-app / SDK style** (API key + API secret) for writes, which is the path exercised by Audius's own upload example (https://github.com/audiusproject/apps, `packages/web/examples/upload-server`).

| Role | Credential | Scope/permissions | How acquired out-of-band | Secret? |
|---|---|---|---|---|
| Sender (write) | Audius user account + API key + API secret (developer app) **OR** OAuth2 `write`-scope bearer JWT **OR** Basic auth with the user's Ethereum private key | `write` scope for `POST/PUT/DELETE /tracks` | Create an app at https://audius.co/settings ("Manage Your Apps") or https://api.audius.co/plans; obtain API Key + API Secret. OAuth via "Log in with Audius" (https://docs.audius.co/developers/guides/log-in-with-audius). Ethereum-private-key Basic auth is documented in the swagger but is a different (wallet-direct) path. | API secret and Ethereum private key and bearer JWT are **secret**; API key is an identifier (treat as secret in logs). |
| Sender (write) — user identity | `user_id` of the owning Audius account | The account whose tracks are created/deleted | Returned by `GET /users/...` / SDK `getUserByHandle`; the developer registers it out-of-band | Not secret, but per-node |
| Receiver (read) | None required for public tracks; optional API key for higher rate limits | Read endpoints declare `security: {}` or `OAuth2: read` | Optional free API key at https://api.audius.co/plans | Not secret |
| Receiver (read) — optional | API key (header `x-api-key` or query `api_key`) | Higher rate limit (10 req/s → Unlimited) | Same as above | Not secret |

Environment variables (per-node, host-specific, **never in the link address**):

| Variable | Purpose | Host-specific? |
|---|---|---|
| `AUDIUS_API_KEY` | Developer app API key; sent as `x-api-key` (read rate-limit identity; used by SDK) | Per node (app can be shared, but each node should have its own app for traceability) |
| `AUDIUS_API_SECRET` | Developer app API secret; server-side only; used by SDK for writes | Per node |
| `AUDIUS_USER_ID` | Audius user id of the account that owns this node's tracks | Per node (each direction/node needs its own account for bidirectional) |
| `AUDIUS_BEARER_TOKEN` | (Alternative) OAuth/developer-app bearer JWT for writes; use instead of API secret when manager/OAuth flow is chosen | Per node |
| `AUDIUS_CONTENT_NODE` | (Optional) preferred content node base URL; SDK auto-selects if unset | Shared/optional (unverified whether pinning helps) |

The component ingests these via `requestPluginUserInput` with keys `apiKey`, `apiSecret`, `userId` (and optionally `bearerToken`, `contentNode`). The developer must place them in `/home/ubuntu/racegen/.env` (gitignored). They reach the component through `--param audiusComposition.<key>`.

Rotation: API secret/bearer token can be rotated in the developer app settings; user_id is stable per account. No presigned/time-limited URLs exist (visibility is public/unlisted/gated).

Sources: https://audius.co/skill.md, https://api.audius.co/v1/swagger.yaml (`securitySchemes`, `POST /tracks` security `BearerAuth`/`BasicAuth`/`OAuth2: write`), https://docs.audius.co/developers/guides/log-in-with-audius. The swagger's `BasicAuth` is documented as Ethereum-private-key auth (wallet-direct), which differs from the skill.md's "API key:secret" description — **reconcile in Phase 3** (unverified which path the SDK actually uses; the SDK example uses `apiKey`+`bearerToken`).

---

## 2. Data visibility

- **Public (listed) tracks** (this design): visible in search/trending/profile and readable/streamable by anyone with no credentials. Read endpoints declare `security: {}` or `OAuth2: read`. This is the chosen mode for Phase 1/2 ("listed tracks only").
- **Unlisted tracks** (`is_unlisted=true`): not shown in search/profile/trending, reachable by direct track ID. **TODO** — follow-up after listed works; verify that a different API key holder can fetch an unlisted track (unverified).
- **Gated tracks** (`access_authorities`, `allowed_api_keys`, `stream_conditions`): restrict streaming/downloads; not needed for the public-transport design. Leave `allowed_api_keys` empty and `access_authorities` empty for public read.
- No presigned/time-limited URLs; access control is public vs. unlisted vs. gated.

Multicast is inherent: a single public track is readable by many receivers via its ID (see section 5).

Sources: swagger `create_track_request_body` (`is_unlisted`, `access_authorities`, `allowed_api_keys`, `stream_conditions`, `download_conditions`), https://audius.co/skill.md. Unlisted fetchability by a different key is **(unverified)**.

---

## 3. Content types and actions

The transport is content-agnostic: it moves whatever bytes the encoding produces. Audius transcodes uploaded audio on Content Nodes (FFmpeg) and only accepts valid audio files, so arbitrary bytes must be carried either in metadata text fields or inside a valid audio container (lossless/lossy) via an encoding component. The receiver must fetch the **original** file via `GET /tracks/{id}/download` (not `/stream`, which returns 320 kbps transcoded audio).

| Content type | MIME / format | Size limit | Store action | Retrieve action | Notes |
|---|---|---|---|---|---|
| Text metadata (primary messaging piece) | UTF-8 text in `title`, `description`, `tags`, `mood`, `genre`, `musical_key`, `license`, `isrc`, `iswc` | `description` ≤ 1000 chars (swagger `maxLength: 1000`); `title`/`tags` no stated limit **(unverified)** | `POST /tracks?user_id=...` with metadata JSON (requires `track_cid`; so a valid audio file is uploaded alongside as placeholder) | `GET /tracks/{track_id}` (metadata) | Carries small encoded strings directly. Low-medium innocuousness (metadata is expected on a track). |
| Audio carrier — lossless (FLAC, WAV, AIFF/AIF/AIFC) | `audio/flac`, `audio/wav`, `audio/aiff`, … | 2 GB per file (Audius Help) | SDK `uploadTrackFiles`/`createAudioUpload` (tus upload to a content node → `track_cid`, `orig_file_cid`) then `POST /tracks` | `GET /tracks/{track_id}/download` (original bytes) | Native, most innocuous; original bytes preserved for download. Best carrier for exact-byte transport. |
| Audio carrier — common (MP3, MP4, M4A, AAC) | `audio/mpeg`, `audio/mp4`, `audio/aac` | 2 GB per file | same as above | same | Native; MP3 is the most innocuous music carrier. |
| Audio carrier — additional (OGG/OGA/OGV/OGX/OGM, OPUS, WEBM, 3GP, AMR, …) | `audio/ogg`, `audio/opus`, `audio/webm`, … | 2 GB per file | same | same | Broad format support (Audius Help "Supported File Types"). |
| Cover art (image) | `image/png`, `image/jpeg` (via `cover_art_sizes` CID) | Not documented **(unverified)** | SDK image upload → `cover_art_sizes` CID; `POST /tracks` | Track artwork URLs (with mirror retry) | Small images expected. |
| Opaque binary | `application/octet-stream` | n/a (not accepted as-is) | Rejected by transcoder; must be wrapped in a valid audio container by the encoding component | n/a | Trivially detectable; not a direct carrier. |

**Steganographic audio content (TODO):**
- (b) **Lossless audio steganography** — payload hidden in a lossless audio carrier (FLAC/WAV). Carrier limit 2 GB. **TODO**: no lossless steganography encoding exists yet; when one is found, the transport already supports the lossless audio carrier above.
- (c) **Loss-tolerant audio steganography** — payload hidden in a lossy audio carrier (MP3/MP4). Carrier limit 2 GB. **TODO**: no loss-tolerant encoding exists yet; additionally, the receiver must use `/download` (original) not `/stream` (320 kbps transcoded) or payload is destroyed.

**Implemented now (Phase 3/4):** content type (a) text metadata, with a small valid WAV uploaded alongside to satisfy Audius's valid-audio requirement. Steganographic types (b)/(c) are listed for completeness and marked TODO.

Store actions summary:
- File carrier: SDK `createAudioUpload` (tus upload to selected content node → `track_cid`, `orig_file_cid`, duration, bpm, key) then `POST /tracks?user_id=...` with `{ title, genre, track_cid, ... }`. Auth: Bearer/OAuth/Basic write.
- Metadata carrier: text fields accompany the audio upload in the same `POST /tracks` body; `track_cid` is required so metadata cannot be stored alone.
- Deletion: `DELETE /tracks/{track_id}?user_id=...` (auth required) — best-effort (see section 7).

Retrieve actions:
- Metadata: `GET /tracks/{track_id}` (no auth).
- Original bytes: `GET /tracks/{track_id}/download` (no auth; returns original file; supports `Range`).
- Stream (lossy 320 kbps): `GET /tracks/{track_id}/stream` (no auth) — **not** suitable for exact-byte transport.

Sources: swagger `create_track_request_body`, `track` schema, `GET /tracks/{track_id}/download`, `GET /tracks/{track_id}/stream`; Audius Help "Supported File Types" and "Is there a limit…"; https://github.com/audiusproject/apps `packages/sdk/.../Storage/Storage.ts` (tus upload to `${contentNode}/files/`, poll `/uploads/{id}`).

---

## 4. Bidirectional support

**Two independent unidirectional channels.** Audius has no native bidirectional primitive; matches raceboat's `LD_CREATOR_TO_LOADER` model (creator owns the track and posts; loader fetches). Each direction is a separate track owned by a separate account:

- Direction A→B: account A owns track T_A; A is creator (posts), B is loader (fetches T_A).
- Direction B→A: account B owns track T_B; B is creator (posts), A is loader (fetches T_B).

Each direction has independent credentials, lifecycle, track_id and cursor. No single-channel bidirectional design.

---

## 5. Multicast or unicast

**Inherent type: multicast (`TT_MULTICAST`).** A single public (listed) track is readable/streamable by many receivers via its track ID, with one write. Efficient for small groups and indistinguishable from a normal shared track. Unicast semantics, if ever needed, come from encryption at a higher layer — not from the transport design.

---

## 6. Data retention

Classify as **persist-until-deleted (with possible permanent residual)**:
- Tracks persist indefinitely on Content Nodes and are replicated across the network; on-chain metadata persists.
- No automatic expiry, no cache/LRU eviction documented.
- `DELETE /tracks/{track_id}?user_id=...` removes the track (returns "Track deleted successfully") but **on-chain metadata persistence after delete is unverified (TODO)** — the `track` schema even carries `is_delete`. Accept accumulation or use unlisted tracks if residual records matter.
- Content Nodes may cache/replicate; CDN/mirror hosts (see `mirrors` in artwork responses) may retain.
- Legal retention / copyright scanning: uploads are scanned by content identification (Audius copyright policy); unusual patterns may be flagged (unverified).
- Timing policies (how long a link stays open) are a User Model concern; the service itself does not expire.

Manifest: `duration_s = -1`, `period_s = -1` (link never auto-closes; service does not expire).

Sources: swagger `DELETE /tracks/{track_id}`, `track.is_delete`; Audius Help copyright policy; https://audius.co/skill.md. On-chain persistence after delete is **(unverified)**.

---

## 7. Safe post-retrieval deletion

- **Can delete after retrieval?** Yes — `DELETE /tracks/{track_id}?user_id=...` is a normal operational action (artists delete tracks routinely), so it does not by itself create suspicious service usage. Deletion is best-effort.
- **Who deletes:** creator (owner) only. The loader never deletes. The creator owns the track and its container.
- **When:** on `destroyLink`, the owning side deletes its track. Reused links (e.g., a reply link that points at an already-existing track) are marked non-owning and must **not** delete the track (it may still be in use by the peer).
- **Avoiding deleting something in use:** ownership flag per link; only the original creator of a container deletes; containers shared across links are non-owning.
- **Caveats (TODO):** on-chain metadata may remain after delete (unverified); content-node replicas/CDN caches may lag; copyright/content scanning on upload is unverified. Deletion does not guarantee removal from all mirrors.

---

## 8. Address format and link lifecycle

### LinkAddress (JSON exchanged between nodes; nothing secret)

```json
{
  "track_id": "D7KyD",
  "owner_user_id": "abc123",
  "is_unlisted": false,
  "maxTries": 5,
  "singleReceive": true
}
```

Fields:
- `track_id` (string, required): the Audius track to fetch (the creator's container).
- `owner_user_id` (string, optional): owning user id; helps resolve mirrors/download. Not secret.
- `is_unlisted` (bool, default false): whether the track is unlisted. Phase 1 uses listed (`false`); unlisted is a TODO.
- `maxTries` (int): fetch retry count.
- `singleReceive` (bool): whether the link delivers one payload (mailbox semantics).

### Lifecycle

1. **Created (creator side):** `createLink` → `createContainer` uploads a small valid placeholder audio (WAV) to a content node (tus) and `POST /tracks` (metadata) → service-assigned `track_id` → stored as container id. Emits link address with `track_id`.
2. **Posting (creator side):** `post` updates the track (metadata and/or audio) with the new payload; on success emits `posted`. Mailbox semantics: a new post overwrites the previous payload on the same track.
3. **Loading (loader side):** `loadLinkAddress` validates the address and creates a receive link. Tolerates a not-yet-existing track (creator may not have posted) by retrying.
4. **Fetching (loader side):** polls `GET /tracks/{track_id}`; if `updated_at`/`blocknumber` is newer than the cursor, `GET /tracks/{track_id}/download` (original bytes) and emits `fetched`. Otherwise no new data.
5. **Destroyed:** creator (owner) calls `DELETE /tracks/{track_id}` (best-effort); loader does not delete; non-owning/reused links do not delete. Always notify SDK `onLinkStatusChanged(..., LINK_DESTROYED, {})` on failure paths except when the id already exists.

Note on container id assignment: Audius assigns `track_id` server-side (swagger: "Optional track ID (will be generated if not provided)"). `ServiceClient.createContainer` must therefore create the track and return/use the assigned id; the template's `createContainer(id)` signature may need adaptation (return the id or maintain an id→track_id map) — flag for Phase 3. **(unverified whether a client-supplied track_id is accepted.)**

---

## 9. Limits, errors, security

### Rate / size limits
- Free plan: **10 req/s**, **500,000 req/month**; Unlimited plan via contact (api@audius.co). (https://api.audius.co/plans, https://docs.audius.co/sdk)
- File size: **2 GB per uploaded file** (Audius Help).
- Metadata: `description` ≤ 1000 chars; `title`/`tags` unstated **(unverified)**.
- Content nodes may impose additional limits **(unverified)**.

Backoff: exponential on 429/5xx; retry on 5xx and network errors; treat 404 as `NotFound`, 401/403 as `Auth`, 429 as `RateLimited`, other 4xx as `Fatal`, 5xx as `Transient`.

### Error mapping
| Service response | Kind |
|---|---|
| network error, 5xx | `Transient` |
| 404 track not found | `NotFound` |
| 401/403 unauthorized | `Auth` |
| 429 rate limited | `RateLimited` |
| other 4xx | `Fatal` |

### Security
- What the service, another user and a network observer can learn: track titles/metadata (listed public track), upload cadence and file sizes, account identity, content-node endpoints, on-chain transaction hashes. The payload (text metadata or audio) is visible to anyone who has the track ID; confidentiality must come from encryption at a higher layer.
- Credentials are never logged, never written to files/manifests/logs/docs; they arrive via `--param audiusComposition.<key>` (adapters pass `${ENV_VAR}` placeholders). `race-cli --debug` prints parameter values, so raw test logs are secret.
- All traffic is TLS to audius.co / content nodes.
- Multicast is inherent (public track); unicast semantics require higher-layer encryption.

### Parameters (per composition `audiusComposition`)
| `--param` key | Meaning | Secret | Required | Default |
|---|---|---|---|---|
| `audiusComposition.apiKey` | Developer app API key | yes (treat as credential) | yes | — |
| `audiusComposition.apiSecret` | Developer app API secret (server-side writes) | yes | yes (unless bearerToken) | — |
| `audiusComposition.userId` | Owning Audius account user id | no | yes | — |
| `audiusComposition.bearerToken` | OAuth/developer bearer JWT (alternative to apiSecret) | yes | no | — |
| `audiusComposition.contentNode` | Preferred content node base URL | no | no | auto-select |

---

## 10. Output summary (manifest values fixed)

| Manifest field | Value |
|---|---|
| `linkDirection` | `LD_CREATOR_TO_LOADER` |
| `transmissionType` | `TT_MULTICAST` |
| `connectionType` | `CT_INDIRECT` |
| `sendType` | `ST_STORED_ASYNC` |
| `reliable` | `true` |
| `isFlushable` | `false` |
| `bootstrap` | `false` |
| `multiAddressable` | `false` |
| `maxLinks` | `10` |
| `supportedHints` | `[]` |
| `duration_s` | `-1` |
| `period_s` | `-1` |
| `mtu` | `-1` (unspecified; audio carrier ≤ 2 GB, text metadata ≤ ~1 KB description) |
| `maxSendsPerInterval` | `10` |
| `secondsPerInterval` | `1` |
| `channelGid` | `audiusComposition` |
| `roles` | `[{"roleName":"default","linkSide":"LS_BOTH"}]` |
| MIME types (`getTransportProperties`) | `text/plain` (metadata), `audio/flac`, `audio/wav`, `audio/aiff`, `audio/mpeg`, `audio/mp4`, `audio/aac`, `audio/ogg`, `audio/opus`, `audio/webm`, …, `image/png`, `image/jpeg` |
| Parameters | `apiKey`, `apiSecret`, `userId`, `bearerToken` (opt), `contentNode` (opt) |
| `creatorsPerLoader`/`loadersPerCreator` | `-1` (unlimited) |

---

## Open risks / TODOs

1. **Unlisted (secret) tracks** — follow-up after listed works; verify fetchability by a different API key (unverified).
2. **Deletion / on-chain metadata persistence** — unverified whether `DELETE /tracks/{id}` removes on-chain records; deletion designed as best-effort; residual records possible. (TODO, does not block.)
3. **Steganographic audio content (b)/(c)** — no lossless/loss-tolerant audio steganography encoding exists yet; transport lists the audio carriers but marks steganographic content as TODO. Until then, text metadata is the messaging piece with a small valid WAV uploaded alongside.
4. **ServiceClient container id assignment** — Audius assigns `track_id` server-side; adapt `createContainer(id)` (return id or id→track_id map) in Phase 3.
5. **Mailbox semantics** — one in-flight payload per track; rapid posts may overwrite un-fetched payloads. Acceptable for current scenarios; a playlist/user-listing container is a future alternative for true multi-message streams.
6. **Auth path reconciliation** — swagger `BasicAuth` is Ethereum-private-key auth, while skill.md describes API key:secret; verify which the SDK uses in Phase 3.
7. **Metadata field length limits** for `title`/`tags` — unstated (unverified); determine via Phase 3 round-trip.
8. **Content-node availability/replication** and extra content-node limits — unverified.
9. **Copyright/content scanning** on upload — unverified; use innocuous titles/tags and unlisted (later) tracks.
10. **Unverified claims** throughout are marked; verify each in Phase 3 round-trip tests.

---

## Sources

- Phase 1: `designs/audius/HIGH_LEVEL_DESIGN.md`
- Template: `PHASE2_PRECISE_DESIGN.md`, `AGENTS.md` (Phase 2 Design Principles)
- Channel/link properties: `raceboat/documentation/ChannelAndLinkProperties.md`
- Audius API: https://api.audius.co/v1/swagger.yaml (tracks, playlists, security schemes, schemas), https://audius.co/skill.md, https://audius.co/agents.md
- Audius docs: https://docs.audius.co, https://docs.audius.co/developers/guides/log-in-with-audius
- Audius help: https://help.audius.co/product/supported-files, https://help.audius.co/faq/is-there-a-limit-to-how-much-i-can-upload-to-my-audius-profile
- Audius SDK/storage: https://github.com/audiusproject/apps (`packages/web/examples/upload-server/server/server.js`), https://github.com/audiusproject/audius-protocol (`packages/sdk/src/sdk/services/Storage/Storage.ts`, `packages/sdk/src/sdk/api/uploads/UploadsApi.ts`, `packages/sdk/src/sdk/api/tracks/TrackUploadHelper.ts`)
- Plans/rate limits: https://api.audius.co/plans, https://docs.audius.co/sdk

---

## Decisions that change manifest values (summary for Phase 3/4)

- `linkDirection = LD_CREATOR_TO_LOADER`; `transmissionType = TT_MULTICAST`; `connectionType = CT_INDIRECT`; `sendType = ST_STORED_ASYNC`.
- `reliable = true`, `isFlushable = false`, `bootstrap = false`, `multiAddressable = false`.
- `duration_s = -1`, `period_s = -1`, `mtu = -1`.
- `maxLinks = 10`; `supportedHints = []`; `maxSendsPerInterval = 10` / `secondsPerInterval = 1`.
- MIME types: text metadata + lossless/lossy audio carriers + cover art (see section 3).
- Parameters: `apiKey`, `apiSecret`, `userId` (required), `bearerToken`, `contentNode` (optional).
- Container = track; address carries `track_id` (see section 8); `is_unlisted = false` for now.

## Credentials the developer must provide before Phase 3

Add these to `/home/ubuntu/racegen/.env` (gitignored, one `NAME=value` per line; do not commit or print values):

- `AUDIUS_API_KEY` — developer app API key (read rate-limit identity + SDK).
- `AUDIUS_API_SECRET` — developer app API secret (server-side writes).
- `AUDIUS_USER_ID` — Audius user id of the account owning this node's tracks (per node; separate accounts for bidirectional).
- `AUDIUS_BEARER_TOKEN` (optional) — OAuth/developer bearer JWT alternative to API secret.
- `AUDIUS_CONTENT_NODE` (optional) — preferred content node base URL.

Verify a variable is set by name only (e.g. `grep -q '^AUDIUS_API_KEY=' .env`); never read or echo values.