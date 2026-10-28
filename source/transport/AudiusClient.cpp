#include "AudiusClient.h"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>

namespace {

size_t writeCallback(char *data, size_t size, size_t count, void *userdata) {
    static_cast<std::string *>(userdata)->append(data, size * count);
    return size * count;
}

size_t headerCallback(char *data, size_t size, size_t count, void *userdata) {
    std::string line(data, size * count);
    auto *headers = static_cast<std::map<std::string, std::string> *>(userdata);
    auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        // trim
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());
        while (!val.empty() && (val.back() == '\r' || val.back() == '\n' || val.back() == ' ')) val.pop_back();
        std::string lkey = key;
        std::transform(lkey.begin(), lkey.end(), lkey.begin(), ::tolower);
        (*headers)[lkey] = val;
    }
    return size * count;
}

struct CurlHandle {
    CURL *curl = curl_easy_init();
    curl_slist *headers = nullptr;
    ~CurlHandle() {
        curl_slist_free_all(headers);
        if (curl) curl_easy_cleanup(curl);
    }
};

const std::string kBase64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

}  // namespace

AudiusClient::AudiusClient(std::string apiKey, std::string apiSecret,
                          std::string userId, std::string bearerToken,
                          std::string contentNode, std::string apiBaseUrl) :
    apiKey(std::move(apiKey)),
    apiSecret(std::move(apiSecret)),
    userId(std::move(userId)),
    bearerToken(std::move(bearerToken)),
    contentNode(std::move(contentNode)),
    apiBaseUrl(std::move(apiBaseUrl)) {
    while (!this->contentNode.empty() && this->contentNode.back() == '/') this->contentNode.pop_back();
    while (!this->apiBaseUrl.empty() && this->apiBaseUrl.back() == '/') this->apiBaseUrl.pop_back();
    try { this->resolvedUserId = resolveUserId(); } catch (...) { this->resolvedUserId = userId; }
}

AudiusClient::AudiusClient(std::string serverUrl, std::string accessToken) :
    AudiusClient("", "", "", std::move(accessToken), "https://creatornode.audius.co",
                serverUrl.empty() ? "https://api.audius.co/v1" : std::move(serverUrl)) {}

std::string AudiusClient::resolveUserId() {
    // Try as an encoded id first: GET /users/{id}
    try {
    {
        HttpResponse r = request("GET", apiBaseUrl + "/users/" + urlEncode(userId));
        if (r.status == 200) {
            try {
                return nlohmann::json::parse(r.body).at("data").at("id").get<std::string>();
            } catch (...) {}
            return userId;
        }
    }
    // Otherwise treat as a handle: GET /users/handle/{handle}
    {
        HttpResponse r = request("GET", apiBaseUrl + "/users/handle/" + urlEncode(userId));
        if (r.status == 200) {
            try {
                return nlohmann::json::parse(r.body).at("data").at("id").get<std::string>();
            } catch (...) {}
        }
    }
    } catch (...) {}
    return userId;
}

std::string AudiusClient::urlEncode(const std::string &value) {
    CurlHandle handle;
    char *escaped = curl_easy_escape(handle.curl, value.c_str(), static_cast<int>(value.size()));
    std::string result = escaped ? escaped : "";
    curl_free(escaped);
    return result;
}

AudiusClient::HttpResponse AudiusClient::request(const std::string &method, const std::string &url,
                                             const std::string &body, const std::string &contentType,
                                             const std::vector<std::pair<std::string, std::string>> &extraHeaders) {
    CurlHandle handle;
    if (!handle.curl) {
        throw ServiceError(ServiceErrorKind::Fatal, "curl_easy_init failed");
    }
    HttpResponse response;
    curl_easy_setopt(handle.curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(handle.curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(handle.curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(handle.curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(handle.curl, CURLOPT_HEADERDATA, &response.headers);
    curl_easy_setopt(handle.curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(handle.curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(handle.curl, CURLOPT_FOLLOWLOCATION, 1L);

    if (!body.empty()) {
        curl_easy_setopt(handle.curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(handle.curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }

    // Auth: prefer bearer token if provided, else Basic (api key:secret).
    if (!bearerToken.empty()) {
        handle.headers = curl_slist_append(handle.headers, ("Authorization: Bearer " + bearerToken).c_str());
    } else if (!apiKey.empty() && !apiSecret.empty()) {
        // Basic auth
        std::string creds = apiKey + ":" + apiSecret;
        // base64 encode creds
        std::string b64;
        int i = 0;
        unsigned char arr3[3], arr4[4];
        for (size_t n = 0; n < creds.size(); n++) {
            arr3[i++] = creds[n];
            if (i == 3) {
                arr4[0] = (arr3[0] & 0xfc) >> 2;
                arr4[1] = ((arr3[0] & 0x03) << 4) + ((arr3[1] & 0xf0) >> 4);
                arr4[2] = ((arr3[1] & 0x0f) << 2) + ((arr3[2] & 0xc0) >> 6);
                arr4[3] = arr3[2] & 0x3f;
                for (int j = 0; j < 4; j++) b64 += kBase64Chars[arr4[j]];
                i = 0;
            }
        }
        if (i) {
            for (int j = i; j < 3; j++) arr3[j] = '\0';
            arr4[0] = (arr3[0] & 0xfc) >> 2;
            arr4[1] = ((arr3[0] & 0x03) << 4) + ((arr3[1] & 0xf0) >> 4);
            arr4[2] = ((arr3[1] & 0x0f) << 2) + ((arr3[2] & 0xc0) >> 6);
            for (int j = 0; j < i + 1; j++) b64 += kBase64Chars[arr4[j]];
            while (i++ < 3) b64 += '=';
        }
        handle.headers = curl_slist_append(handle.headers, ("Authorization: Basic " + b64).c_str());
    }
    if (!apiKey.empty()) {
        handle.headers = curl_slist_append(handle.headers, ("x-api-key: " + apiKey).c_str());
    }
    if (!contentType.empty()) {
        handle.headers = curl_slist_append(handle.headers, ("Content-Type: " + contentType).c_str());
    }
    for (auto &h : extraHeaders) {
        handle.headers = curl_slist_append(handle.headers, (h.first + ": " + h.second).c_str());
    }
    curl_easy_setopt(handle.curl, CURLOPT_HTTPHEADER, handle.headers);

    CURLcode code = curl_easy_perform(handle.curl);
    if (code != CURLE_OK) {
        throw ServiceError(ServiceErrorKind::Transient, curl_easy_strerror(code));
    }
    curl_easy_getinfo(handle.curl, CURLINFO_RESPONSE_CODE, &response.status);
    return response;
}

void AudiusClient::throwForStatus(const HttpResponse &response, const std::string &what) {
    if (response.status >= 200 && response.status < 300) return;
    std::string message = what + " failed with HTTP " + std::to_string(response.status);
    if (response.status == 401 || response.status == 403) throw ServiceError(ServiceErrorKind::Auth, message);
    if (response.status == 404) throw ServiceError(ServiceErrorKind::NotFound, message);
    if (response.status == 429) throw ServiceError(ServiceErrorKind::RateLimited, message);
    if (response.status >= 500) throw ServiceError(ServiceErrorKind::Transient, message);
    throw ServiceError(ServiceErrorKind::Fatal, message);
}

std::string AudiusClient::buildPlaceholderWav() {
    // Minimal valid WAV: 44-byte header, 0 samples (silence). Audius accepted a 1s wav in testing;
    // use a small 0.1s buffer to be safe and tiny.
    std::vector<uint8_t> wav;
    // We'll build a 8kHz mono 16-bit WAV with 800 samples (0.1s)
    uint32_t sampleRate = 8000;
    uint16_t bitsPerSample = 16;
    uint16_t numChannels = 1;
    uint32_t numSamples = 800;
    uint32_t byteRate = sampleRate * numChannels * bitsPerSample / 8;
    uint16_t blockAlign = numChannels * bitsPerSample / 8;
    uint32_t dataSize = numSamples * blockAlign;
    uint32_t chunkSize = 36 + dataSize;
    // RIFF header
    const char *riff = "RIFF";
    wav.insert(wav.end(), riff, riff + 4);
    // chunk size (little endian)
    for (int i = 0; i < 4; i++) wav.push_back((chunkSize >> (i * 8)) & 0xff);
    const char *wave = "WAVE";
    wav.insert(wav.end(), wave, wave + 4);
    const char *fmt = "fmt ";
    wav.insert(wav.end(), fmt, fmt + 4);
    uint32_t subchunk1Size = 16;
    for (int i = 0; i < 4; i++) wav.push_back((subchunk1Size >> (i * 8)) & 0xff);
    uint16_t audioFormat = 1;
    wav.push_back(audioFormat & 0xff); wav.push_back((audioFormat >> 8) & 0xff);
    wav.push_back(numChannels & 0xff); wav.push_back((numChannels >> 8) & 0xff);
    for (int i = 0; i < 4; i++) wav.push_back((sampleRate >> (i * 8)) & 0xff);
    for (int i = 0; i < 4; i++) wav.push_back((byteRate >> (i * 8)) & 0xff);
    wav.push_back(blockAlign & 0xff); wav.push_back((blockAlign >> 8) & 0xff);
    wav.push_back(bitsPerSample & 0xff); wav.push_back((bitsPerSample >> 8) & 0xff);
    const char *data = "data";
    wav.insert(wav.end(), data, data + 4);
    for (int i = 0; i < 4; i++) wav.push_back((dataSize >> (i * 8)) & 0xff);
    for (uint32_t i = 0; i < dataSize; i++) wav.push_back(0);
    return std::string(wav.begin(), wav.end());
}

std::string AudiusClient::uploadPlaceholderTrackCid() {
    std::string wav = buildPlaceholderWav();
    std::vector<uint8_t> bytes(wav.begin(), wav.end());

    // 1. POST /files/ to create tus upload
    std::string fnameB64, ftypeB64;
    // base64 encode filename and filetype
    auto b64enc = [](const std::string &in) {
        std::string out;
        int i = 0;
        unsigned char arr3[3], arr4[4];
        for (size_t n = 0; n < in.size(); n++) {
            arr3[i++] = in[n];
            if (i == 3) {
                arr4[0] = (arr3[0] & 0xfc) >> 2;
                arr4[1] = ((arr3[0] & 0x03) << 4) + ((arr3[1] & 0xf0) >> 4);
                arr4[2] = ((arr3[1] & 0x0f) << 2) + ((arr3[2] & 0xc0) >> 6);
                arr4[3] = arr3[2] & 0x3f;
                for (int j = 0; j < 4; j++) out += kBase64Chars[arr4[j]];
                i = 0;
            }
        }
        if (i) {
            for (int j = i; j < 3; j++) arr3[j] = '\0';
            arr4[0] = (arr3[0] & 0xfc) >> 2;
            arr4[1] = ((arr3[0] & 0x03) << 4) + ((arr3[1] & 0xf0) >> 4);
            arr4[2] = ((arr3[1] & 0x0f) << 2) + ((arr3[2] & 0xc0) >> 6);
            for (int j = 0; j < i + 1; j++) out += kBase64Chars[arr4[j]];
            while (i++ < 3) out += '=';
        }
        return out;
    };
    std::string filenameB64 = b64enc("placeholder.wav");
    std::string filetypeB64 = b64enc("audio/wav");
    std::string templateB64 = b64enc("audio");
    std::string meta = "filename " + filenameB64 + ",filetype " + filetypeB64 + ",template " + templateB64;

    std::string createUrl = contentNode + "/files/";
    std::vector<std::pair<std::string, std::string>> extra = {
        {"Tus-Resumable", "1.0.0"},
        {"Upload-Length", std::to_string(bytes.size())},
        {"Upload-Metadata", meta},
    };
    HttpResponse r = request("POST", createUrl, "", "", extra);
    if (r.status != 201) {
        throwForStatus(r, "tus create upload");
    }
    auto it = r.headers.find("location");
    if (it == r.headers.end()) {
        throw ServiceError(ServiceErrorKind::Fatal, "tus create: no Location header");
    }
    std::string uploadUrl = it->second;
    if (uploadUrl.rfind("http", 0) != 0) {
        uploadUrl = contentNode + uploadUrl;
    }

    // 2. PATCH the bytes
    std::vector<std::pair<std::string, std::string>> patchHeaders = {
        {"Tus-Resumable", "1.0.0"},
        {"Content-Type", "application/offset+octet-stream"},
        {"Upload-Offset", "0"},
    };
    HttpResponse pr = request("PATCH", uploadUrl, wav, "", patchHeaders);
    if (pr.status != 204) {
        throwForStatus(pr, "tus patch upload");
    }

    // 3. Poll /uploads/{id}
    std::string uploadId = uploadUrl.substr(uploadUrl.find_last_of('/') + 1);
    std::string pollUrl = contentNode + "/uploads/" + uploadId;
    std::string trackCid;
    for (int attempt = 0; attempt < 60; attempt++) {
        HttpResponse sr = request("GET", pollUrl);
        if (sr.status != 200) {
            throwForStatus(sr, "tus poll upload");
        }
        nlohmann::json j = nlohmann::json::parse(sr.body);
        std::string status = j.value("status", "");
        if (status == "done") {
            if (j.contains("results") && j["results"].contains("320")) {
                trackCid = j["results"]["320"].get<std::string>();
            }
            break;
        }
        if (status == "error") {
            throw ServiceError(ServiceErrorKind::Fatal, "tus upload failed: " + sr.body);
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    if (trackCid.empty()) {
        throw ServiceError(ServiceErrorKind::Fatal, "tus upload: no track_cid after polling");
    }
    return trackCid;
}

std::string AudiusClient::createContainer(const std::string &containerIdHint) {
    std::string trackCid = uploadPlaceholderTrackCid();
    nlohmann::json body;
    body["title"] = containerIdHint.empty() ? "transport-container" : containerIdHint;
    body["genre"] = "Electronic";
    body["track_cid"] = trackCid;
    body["description"] = "";
    body["is_downloadable"] = true;

    std::string url = apiBaseUrl + "/tracks?user_id=" + urlEncode(resolvedUserId);
    HttpResponse r = request("POST", url, body.dump(), "application/json");
    throwForStatus(r, "create track");
    nlohmann::json j = nlohmann::json::parse(r.body);
    // response: {block_hash, block_number, track_id, transaction_hash}
    if (j.contains("track_id")) {
        return j["track_id"].get<std::string>();
    }
    throw ServiceError(ServiceErrorKind::Fatal, "create track: no track_id in response");
}

bool AudiusClient::containerExists(const std::string &containerId) {
    HttpResponse r = request("GET", apiBaseUrl + "/tracks/" + urlEncode(containerId));
    if (r.status == 404) return false;
    throwForStatus(r, "check track " + containerId);
    return true;
}

void AudiusClient::deleteContainer(const std::string &containerId) {
    std::string url = apiBaseUrl + "/tracks/" + urlEncode(containerId) + "?user_id=" + urlEncode(resolvedUserId);
    HttpResponse r = request("DELETE", url);
    if (r.status == 404) return;
    throwForStatus(r, "delete track " + containerId);
}

std::string AudiusClient::post(const std::string &containerId, const std::vector<uint8_t> &content) {
    std::string b64 = base64Encode(content);
    nlohmann::json body;
    body["description"] = b64;

    std::string url = apiBaseUrl + "/tracks/" + urlEncode(containerId) + "?user_id=" + urlEncode(resolvedUserId);
    HttpResponse r = request("PUT", url, body.dump(), "application/json");
    throwForStatus(r, "update track " + containerId);
    nlohmann::json j = nlohmann::json::parse(r.body);
    if (j.contains("block_number")) {
        return std::to_string(j["block_number"].get<long long>());
    }
    // fallback: timestamp-based id
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

std::vector<ServiceMessage> AudiusClient::fetchAfter(const std::string &containerId,
                                                    const std::string &afterMessageId) {
    std::vector<ServiceMessage> messages;
    HttpResponse r = request("GET", apiBaseUrl + "/tracks/" + urlEncode(containerId));
    if (r.status == 404) {
        return messages; // not created yet
    }
    throwForStatus(r, "get track " + containerId);
    nlohmann::json j = nlohmann::json::parse(r.body);
    nlohmann::json data = j.value("data", nlohmann::json::object());
    long long blockNumber = data.value("block_number", 0LL);
    std::string msgId = std::to_string(blockNumber);

    bool newer = afterMessageId.empty();
    if (!newer) {
        try {
            unsigned long long cur = std::stoull(afterMessageId);
            unsigned long long bn = static_cast<unsigned long long>(blockNumber);
            newer = bn > cur;
        } catch (...) {
            newer = true;
        }
    }
    if (!newer) return messages;

    std::string desc = data.value("description", "");
    std::vector<uint8_t> content = base64Decode(desc);
    messages.push_back({msgId, content});
    return messages;
}

std::string AudiusClient::base64Encode(const std::vector<uint8_t> &data) {
    std::string out;
    int i = 0;
    unsigned char arr3[3], arr4[4];
    for (size_t n = 0; n < data.size(); n++) {
        arr3[i++] = data[n];
        if (i == 3) {
            arr4[0] = (arr3[0] & 0xfc) >> 2;
            arr4[1] = ((arr3[0] & 0x03) << 4) + ((arr3[1] & 0xf0) >> 4);
            arr4[2] = ((arr3[1] & 0x0f) << 2) + ((arr3[2] & 0xc0) >> 6);
            arr4[3] = arr3[2] & 0x3f;
            for (int j = 0; j < 4; j++) out += kBase64Chars[arr4[j]];
            i = 0;
        }
    }
    if (i) {
        for (int j = i; j < 3; j++) arr3[j] = '\0';
        arr4[0] = (arr3[0] & 0xfc) >> 2;
        arr4[1] = ((arr3[0] & 0x03) << 4) + ((arr3[1] & 0xf0) >> 4);
        arr4[2] = ((arr3[1] & 0x0f) << 2) + ((arr3[2] & 0xc0) >> 6);
        for (int j = 0; j < i + 1; j++) out += kBase64Chars[arr4[j]];
        while (i++ < 3) out += '=';
    }
    return out;
}

std::vector<uint8_t> AudiusClient::base64Decode(const std::string &text) {
    std::string in = text;
    // strip non-base64
    std::string clean;
    for (char c : in) {
        if (kBase64Chars.find(c) != std::string::npos || c == '=') clean += c;
    }
    std::vector<uint8_t> out;
    int i = 0;
    unsigned char arr4[4], arr3[3];
    for (size_t n = 0; n < clean.size(); n++) {
        if (clean[n] == '=') break;
        size_t pos = kBase64Chars.find(clean[n]);
        if (pos == std::string::npos) continue;
        arr4[i++] = static_cast<unsigned char>(pos);
        if (i == 4) {
            arr3[0] = (arr4[0] << 2) + ((arr4[1] & 0x30) >> 4);
            arr3[1] = ((arr4[1] & 0x0f) << 4) + ((arr4[2] & 0x3c) >> 2);
            arr3[2] = ((arr4[2] & 0x03) << 6) + arr4[3];
            for (int j = 0; j < 3; j++) out.push_back(arr3[j]);
            i = 0;
        }
    }
    if (i) {
        for (int j = i; j < 4; j++) arr4[j] = 0;
        arr3[0] = (arr4[0] << 2) + ((arr4[1] & 0x30) >> 4);
        arr3[1] = ((arr4[1] & 0x0f) << 4) + ((arr4[2] & 0x3c) >> 2);
        for (int j = 0; j < i - 1; j++) out.push_back(arr3[j]);
    }
    return out;
}
