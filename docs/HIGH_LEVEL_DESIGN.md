# High-Level Design: Audius Transport Assessment

## Executive Summary

- **Recommended**: Yes (with caveats — see Risks)
- **Overall Score**: 7.6/10
- **Category**: Free community music streaming platform (decentralized, on the Open Audio Protocol)
- **Best For**: Unicast (per-link track); multicast via a single public track (one write, many readers); addressing via direct track ID or unlisted (secret) track
- **Transport name**: `Audius` / `audius`

## Service Overview

- **Name**: Audius
- **Website**: https://audius.co
- **API Docs**: https://docs.audius.co/api (reference), https://docs.audius.co (guides/SDK)
- **API base URL**: `https://api.audius.co/v1`
- **OpenAPI spec**: https://api.audius.co/v1/swagger.yaml (and `/v1/full/swagger.yaml`)
- **API plans / keys**: https://api.audius.co/plans ; https://audius.co/settings
- **Pricing Page**: Free plan (10 req/s, 500,000 req/month); Unlimited plan via contact (api@audius.co)
- **Free Tier**: Free account + free API key; no paid tier required to upload or stream
- **SDK**: `@audius/sdk` (npm) for Node/browser; CDN build available
- **Underlying protocol**: Open Audio Protocol — content stored on community-run Content (Creator) Nodes, metadata registered on-chain (see https://docs.audius.co/learn/architecture/content-node)

## Mandatory Criteria Assessment

| Criterion | Pass/Fail | Evidence |
|---|---|---|
| Programmatic API | ✅ | REST API at `https://api.audius.co/v1` plus `@audius/sdk`; OpenAPI 3.0 spec at https://api.audius.co/v1/swagger.yaml (source: https://audius.co/agents.md, https://audius.co/skill.md) |
| Store Capability | ✅ | Upload audio to a Content Node via SDK `uploadTrackFiles` (returns `track_cid`), then `POST /tracks` with metadata JSON creates the track; auth via Bearer (OAuth) or Basic (api key:secret) with `write` scope (source: swagger `POST /tracks`, https://audius.co/skill.md "Web Examples — upload") |
| Retrieve Capability | ✅ | `GET /tracks/{track_id}` returns metadata; `GET /tracks/{track_id}/download` returns the original file (no auth); `GET /tracks/{track_id}/stream` returns 320 kbps transcoded audio (source: swagger paths; https://docs.audius.co) |
| Addressing | ✅ | Direct track ID (`GET /tracks/{id}`), permalink (`handle/slug` via `/resolve`), and user-scoped listing (`/users/{id}/tracks`); no scanning required for direct ID |
| Cross-User Access | ✅ (public tracks) | Read endpoints require no credentials (`security: {}`); unlisted tracks reachable by direct ID (unverified whether a different API key/account can fetch an unlisted track — verify in Phase 3) |

## Detailed Scoring

| Dimension | Score | Weight | Weighted | Notes |
|---|---|---|---|---|
| Innocuousness | 8 | 30% | 2.4 | TLS to a mainstream free music platform; uploading an audio track is normal musician behavior. Tell-tales: steady cadence of many small uploads, non-audio payloads, and on-chain metadata visibility. |
| Auth Flexibility | 8 | 20% | 1.6 | Read: no auth (API key optional for higher limits). Write: API key + secret (Basic) or OAuth bearer with `write` scope. No presigned URLs. |
| Data Size Limits | 8 | 15% | 1.2 | 2 GB per uploaded file (source: Audius Help "Is there a limit…"). Free tier 10 req/s, 500k req/month. |
| Retention Control | 7 | 10% | 0.7 | `DELETE /tracks/{track_id}` exists (auth required); tracks persist indefinitely otherwise. Unlisted option for addressing privacy. On-chain metadata persistence unverified. |
| Bidirectional | 5 | 10% | 0.5 | No native bidirectional primitive; use two independent unidirectional tracks (one per direction), matching raceboat's `LD_CREATOR_TO_LOADER` model. |
| Multicast | 8 | 5% | 0.4 | A single public track is readable/streamable by many receivers via its ID; efficient for small groups. |
| Rate Limits/Cost | 7 | 5% | 0.35 | Free: 10 req/s, 500k req/month; Unlimited plan via contact (api@audius.co). Community content nodes may add their own limits (unverified). |
| Geographic | 9 | 5% | 0.45 | Global content-node network; no documented regional restrictions in reviewed sources. |
| **TOTAL** | | **100%** | **7.6** | 2.4+1.6+1.2+0.7+0.5+0.4+0.35+0.45 = 7.6 |

## Addressing Methods (Ranked by Innocuousness)

| Method | Definition | Overhead | User | Network | Service |
|---|---|---|---|---|---|
| Known track ID (direct) | Address carries `{track_id}`; receiver polls `GET /tracks/{id}` and downloads the original file | 1 GET + 1 download per poll | high | high (TLS to audius.co / content node) | medium (stored under one account, but public) |
| Unlisted (secret) track | Same as direct but `is_unlisted=true`; not shown in search/profile/trending | 1 GET + 1 download per poll | high | high | medium (exists but not enumerated) |
| Permalink (handle/slug) | Address carries `audius.co/{handle}/{slug}`; receiver resolves via `/resolve` then fetches | 1 resolve + 1 GET + download | high | high | medium |
| Tag-based discovery | Receiver searches tracks by tag/keyword and filters by sender/sequence | 1 search + GETs | medium | high | medium |
| User-scoped listing | Address carries `{user_id}`; receiver lists that user's tracks | 1 LIST + GETs | medium | high | low (clearly one account's uploads) |
| Playlist membership | Address carries `{playlist_id}`; receiver lists its tracks | 1 GET (playlist) + GETs | medium | high | low (clearly one account's collection) |

Recommendation: **Known track ID (direct)**, optionally the **unlisted (secret)** variant for extra addressing privacy — both have minimal overhead and look like a normal track share. Use unlisted when the sender wants the track not to appear in search/trending.

## Content Types & Actions

Listed with size limits and formats only (encoding component decides payload distribution). **Important constraint**: Audius transcodes uploaded audio on Content Nodes (FFmpeg) and only accepts valid audio files; arbitrary bytes must be carried inside a valid audio container (e.g., WAV/FLAC/MP3) via an encoding component. The receiver must fetch the **original** file via `GET /tracks/{id}/download` (not the `/stream` endpoint, which returns 320 kbps transcoded audio).

| Content type | MIME / format | Size limit | Notes |
|---|---|---|---|
| Audio — lossless (FLAC, WAV, AIFF/AIF/AIFC) | `audio/flac`, `audio/wav`, `audio/aiff`, … | 2 GB per file (source: Audius Help) | Native, most innocuous; original bytes preserved for download. Best carrier for exact-byte transport. |
| Audio — common (MP3, MP4, M4A, AAC) | `audio/mpeg`, `audio/mp4`, `audio/aac`, … | 2 GB per file | Native; MP3 is the most innocuous music carrier. |
| Audio — additional (OGG/OGA/OGV/OGX/OGM, OPUS, WEBM, 3GP, AMR, …) | `audio/ogg`, `audio/opus`, `audio/webm`, … | 2 GB per file | Native; broad format support (source: Audius Help "Supported File Types"). |
| Cover art (image) | `image/png`, `image/jpeg` (via `cover_art_sizes` CID) | Not documented (unverified) | Attached to track metadata; small images expected. |
| Text metadata | UTF-8 text | `title` (no stated limit, unverified); `description` ≤ 1000 chars; `tags` comma-separated string; `mood`, `genre`, `musical_key`, `license`, `isrc`, `iswc` | Carries small encoded strings directly in track metadata without an audio file. Low-medium innocuousness (metadata is expected on a track). |
| Opaque binary | `application/octet-stream` | n/a (not accepted as-is) | Trivially detectable / rejected by transcoder; must be wrapped in a valid audio container by the encoding component. |

Store actions:
- File carrier: SDK `uploadTrackFiles({ audioFile, userId, ... })` → returns `track_cid` (and cover-art CID), then `POST /tracks` with `{ title, genre, track_cid, ... }`. (Auth: Bearer/OAuth or Basic api key:secret with `write` scope.)
- Metadata carrier: `POST /tracks` with text fields only is not sufficient alone (track_cid required); metadata fields accompany the audio upload.
- Deletion: `DELETE /tracks/{track_id}?user_id=...` (auth required).

Retrieve actions:
- Metadata: `GET /tracks/{track_id}` (no auth).
- Original bytes: `GET /tracks/{track_id}/download` (no auth; returns original file).
- Stream (lossy, 320 kbps): `GET /tracks/{track_id}/stream` (no auth) — not suitable for exact-byte transport.

## Authentication Requirements

| Role | Credentials Needed | How Obtained |
|---|---|---|
| Sender (write) | Audius user account + API key (and API secret for Basic auth) OR OAuth `write`-scope bearer token acting on behalf of the user; `user_id` of the owning account | Create an app at https://audius.co/settings ("Manage Your Apps") or https://api.audius.co/plans; OAuth via "Log in with Audius" (source: https://audius.co/skill.md, https://docs.audius.co/developers/guides/log-in-with-audius) |
| Receiver (read) | None for public tracks (API key optional for higher rate limits: 10 req/s → Unlimited) | Optional: create a free API key at https://api.audius.co/plans |

Notes: Write operations are on-chain entity-manager writes relayed through Audius; the SDK handles signing. The API secret must never be exposed on the frontend (source: https://audius.co/skill.md).

## Data Visibility

- ✅ Public tracks: visible in search/trending/profile and readable/streamable by anyone (no auth).
- ✅ Unlisted tracks (`is_unlisted=true`): not shown in search/profile/trending, reachable by direct track ID — useful for addressing privacy.
- ✅ Gated tracks: `access_authorities` (wallet addresses) can restrict streaming; not needed for the public-transport design.
- ✅ `allowed_api_keys`: can restrict which API keys may stream a track; leave empty for public read.
- ❌ No presigned/time-limited URLs; access control is public vs. unlisted vs. gated.

## Retention & Deletion

- **Default Retention**: Indefinite — tracks persist on Content Nodes and are replicated across the network; on-chain metadata persists (unverified whether on-chain records are removable on delete).
- **Manual Delete**: `DELETE /tracks/{track_id}?user_id=...` (auth required) — returns "Track deleted successfully".
- **Suspicion Risk**: Low for deletion (removing a track is normal). However, Audius transcodes and may run content/copyright checks on upload (unverified); unusual upload patterns or invalid audio could be flagged. Unlisted tracks reduce discoverability.

## Bidirectional Strategy

- **Approach**: Two independent unidirectional channels (separate tracks, one per direction), each owned by an account with its own API key/secret (or OAuth token).
- **Rationale**: Audius has no native bidirectional primitive; matches raceboat's `LD_CREATOR_TO_LOADER` model (creator owns the track and posts; loader fetches).

## Multicast Support

- **Native**: No (one writer per track).
- **Workaround**: A single public (or unlisted) track readable by many receivers via its ID; efficient for small groups and indistinguishable from a normal shared track.

## Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Uploaded audio must be a valid, transcodable audio file (Audius runs FFmpeg on Content Nodes) | High (by design) | High | Encoding component must emit valid audio (WAV/FLAC/MP3) carrying the payload; verify round-trip of bytes via `/download` in Phase 3. |
| Content/copyright scanning on upload | Medium | Medium | Use innocuous titles/tags and unlisted tracks; keep volumes hobbyist-like. |
| Unlisted-track fetchability by a different account/API key unverified | Medium | Medium | Verify in Phase 3 round-trip test. |
| On-chain metadata persistence after delete (unverified) | Medium | Low | Accept accumulation or use unlisted tracks; verify delete semantics in Phase 3. |
| Rate limits (10 req/s, 500k/month free) | Medium | Medium | Conservative polling; exponential backoff on 429/5xx; contact Audius for Unlimited plan if needed. |
| Content-node availability/replication (decentralized) | Low-Medium | Medium | Retry across mirrors; the SDK handles content-node selection. |
| Upload requires a user account + OAuth/Basic auth (no anonymous write) | High (by design) | Medium | Each node needs its own Audius account + API key/secret; document in Phase 2. |
| File size 2 GB cap bounds `mtu` | Low | Low | Set `mtu` below 2 GB; chunk large payloads across multiple tracks if needed (encoding concern). |

## Recommendation

**PROCEED TO PHASE 2 with Audius**, using the "Known track ID (direct)" addressing method (optionally the unlisted/secret variant) and lossless audio carriers (FLAC/WAV) plus text metadata fields (`title`, `description`, `tags`) as carriers. Lossless formats preserve exact bytes through `/download`, which is essential for a transport. MP3 is acceptable only if the encoding component tolerates lossy transcoding (it should not; prefer lossless).

Open risks to resolve in Phase 2: on-chain metadata persistence after delete, unlisted-track fetchability by a different key, exact size limits for metadata fields, and whether Audius content moderation flags non-music payloads.

## Alternative Candidates (Summary Table)

| Service | Score | Category | Key Strength | Key Weakness |
|---|---|---|---|---|
| Audius | 7.6 | Free community music streaming (decentralized) | Free, 2 GB/file, public read, unlisted tracks, lossless download, 10 req/s free | Requires account+OAuth/secret to write; valid-audio constraint; on-chain metadata |
| SoundCloud | 6.7 | Community music streaming | Huge community, 500 MB/track, upload via API | New API apps no longer accepted; ToS restricts automated use; uploads recently unreliable |
| Internet Archive | 7.0 | Community audio archive (S3-like) | S3-like API, permanent storage, public read, S3 keys | Items are permanent public archive entries; not "streaming community" feel; derived files |
| Jamendo | n/a | Royalty-free music catalog | Large catalog, read API | Read-only API (no track upload via API) |
| HearThis.at | n/a | Community music streaming | Uploads via web, free tier | No documented third-party upload API |
| Funkwhale / Songhive | n/a | Self-hosted music streaming | Full control, ActivityPub federation | Self-hosted (not a third-party service) |

## Next-Phase Inputs

The Phase 2 questions this assessment leaves open:
1. Credential scope: one Audius account + API key/secret per node suffices for both read and write; bidirectional needs two accounts/keys.
2. Data visibility: confirm unlisted track is fetchable by a different API key holder.
3. Retention/deletion: confirm on-chain metadata is removed (or not) on `DELETE /tracks/{id}`; otherwise design cleanup/deactivation strategy (unlisted).
4. Size limits: determine practical metadata field-length limits and max original-file size via Phase 3 round-trip test.
5. Multicast: confirm a public/unlisted track is readable by an arbitrary API key holder.
6. Encoding constraint: confirm lossless (FLAC/WAV) bytes survive upload→`/download` bit-for-bit.

## Survey Note

This assessment deep-dives Audius, selected by the developer from a survey of free community music streaming services (Audius, SoundCloud, Internet Archive, Jamendo, HearThis.at, Free Music Archive, ccMixter, Musopen, plyr.fm, Funkwhale/Songhive). Self-hosted options (Funkwhale, Songhive) and read-only catalogs (FMA, ccMixter, Musopen, Jamendo) were excluded because they cannot store data via API or are not third-party services. The assessment structure (addressing methods, content types, auth, visibility, retention) follows the Phase 1 template; scores are author-assigned and indicative only. Unverified claims are marked as such.