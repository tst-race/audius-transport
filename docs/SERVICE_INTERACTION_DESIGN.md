# Service Interaction Design: Audius

Phase 3 deliverable. Precise programmatic actions for the Audius transport's `ServiceClient` (`AudiusClient`), behind `source/transport/ServiceClient.h`. Free of raceboat types.

## 1. Overview

- Base API URL: `https://api.audius.co/v1` (override via constructor).
- Content node (tus upload): `https://creatornode.audius.co` (default; override).
- Auth: HTTP Basic (`Authorization: Basic base64(apiKey:apiSecret)`) + `x-api-key` header, OR Bearer JWT (`Authorization: Bearer <token>`). Writes require `write` scope.
- Container = Audius **track**. Audius assigns `track_id` server-side. `createContainer` uploads a placeholder WAV via tus and creates the track, returning `track_id`.
- Message = base64-encoded payload stored in the track's `description` field (≤1000 chars). Cursor = track `block_number`.
- Mailbox semantics: one in-flight payload per track (post overwrites description).

## 2. Operations

### createContainer(hint) -> track_id
1. Build a small valid WAV (placeholder) in memory.
2. Tus upload to content node:
   - `POST {contentNode}/files/` with headers `Tus-Resumable: 1.0.0`, `Upload-Length: <n>`, `Upload-Metadata: filename <b64>,filetype <b64>,template <b64(audio)>`. → 201 + `Location`.
   - `PATCH {Location}` with `Tus-Resumable: 1.0.0`, `Content-Type: application/offset+octet-stream`, `Upload-Offset: 0`, body = WAV bytes. → 204.
   - Poll `GET {contentNode}/uploads/{id}` until `status == "done"`; read `results.320` → `track_cid`.
3. `POST {apiBaseUrl}/tracks?user_id={resolvedUserId}` JSON `{title, genre, track_cid, description:"", is_downloadable:true}`. → `{track_id, block_number, ...}`.
4. Return `track_id`.

### containerExists(track_id)
- `GET {apiBaseUrl}/tracks/{track_id}` → 200 (true) or 404 (false).

### deleteContainer(track_id)
- `DELETE {apiBaseUrl}/tracks/{track_id}?user_id={resolvedUserId}` → 200/404.

### post(track_id, bytes) -> message_id
- base64-encode bytes → b64.
- `PUT {apiBaseUrl}/tracks/{track_id}?user_id={resolvedUserId}` JSON `{description: b64}`.
- Return `to_string(block_number)` from response (fallback: ms timestamp).

### fetchAfter(track_id, afterMessageId)
- `GET {apiBaseUrl}/tracks/{track_id}` → 404 → empty list.
- block_number = data.block_number. If afterMessageId empty or block_number > stoull(afterMessageId):
  - base64-decode data.description → bytes.
  - return one ServiceMessage{id=to_string(block_number), content=bytes}.
- else empty.

## 3. Authentication

- Basic: `Authorization: Basic base64(apiKey:apiSecret)` + `x-api-key: apiKey`. Server-side only (apiSecret is secret).
- Bearer: `Authorization: Bearer <token>` + `x-api-key`. Used when bearerToken provided.
- userId resolution: supplied value may be handle or encoded id; resolved via `GET /users/{id}` or `GET /users/handle/{handle}` at construction.
- Tus upload (content node) requires no auth for un-attested uploads; userId omitted.

## 4. Limits

- Free plan: 10 req/s, 500,000 req/month; Unlimited via contact (api@audius.co).
- File size: 2 GB per uploaded file (placeholder WAV is tiny).
- description ≤ 1000 chars → base64 payload ≤ ~750 bytes per message (use chunking at higher layer if needed; transport passes through).
- title/tags limits unstated (unverified).

## 5. Retry / error policy

| Service response | Kind |
|---|---|
| network error, 5xx | Transient |
| 404 track not found | NotFound |
| 401/403 | Auth |
| 429 | RateLimited |
| other 4xx | Fatal |
| tus poll timeout | Transient/Fatal |

Exponential backoff on 429/5xx; retry on 5xx and network errors.

## 6. Implied address fields

LinkAddress.container = Audius track_id (returned by createContainer). Other fields (owner_user_id, is_unlisted, maxTries, singleReceive) are Phase 4 concerns; unlisted is TODO.

## 7. What the service sees

- Track titles/metadata (listed public track), upload cadence and file sizes, account identity, content-node endpoints, on-chain transaction hashes.
- Payload (base64 in description) is visible to anyone with the track ID; confidentiality must come from higher-layer encryption.
- All traffic TLS.

## 8. Credentials (out-of-band)

Env vars: `AUDIUS_API_KEY`, `AUDIUS_API_SECRET`, `AUDIUS_USER_ID`, `AUDIUS_BEARER_TOKEN` (opt), `AUDIUS_CONTENT_NODE` (opt). Loaded via `set -a; . ./.env; set +a`. Never in code/manifests/logs/docs.

## 9. TODOs

- Unlisted (secret) tracks: follow-up.
- Deletion/on-chain metadata persistence: best-effort, unverified.
- Steganographic audio content (b)/(c): TODO until encoding found.
- Original-file byte recovery (download returns transcoded MP3): payload relies on metadata text, not audio bytes.
- Auth path reconciliation (swagger BasicAuth = Ethereum key vs API key:secret): verified Basic (api key:secret) works for POST/PUT/DELETE.
