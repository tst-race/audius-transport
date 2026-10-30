#ifndef __AUDIUS_AUDIUS_CLIENT_H__
#define __AUDIUS_AUDIUS_CLIENT_H__

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "ServiceClient.h"

// HTTP implementation of ServiceClient over libcurl for the Audius service.
// All Audius quirks (tus upload, base64-in-metadata, server-assigned track ids,
// block-number cursors) live here. Nothing outside this class knows about HTTP.
class AudiusClient : public ServiceClient {
public:
    AudiusClient(std::string apiKey, std::string apiSecret,
                std::string userId,
                std::string bearerToken = "",
                std::string contentNode = "https://creatornode.audius.co",
                std::string apiBaseUrl = "https://api.audius.co/v1");
    // Backward-compat (stub-style) constructor: server URL + access token.
    AudiusClient(std::string serverUrl, std::string accessToken);

    std::string createContainer(const std::string &containerIdHint = "") override;
    bool containerExists(const std::string &containerId) override;
    void deleteContainer(const std::string &containerId) override;
    std::string post(const std::string &containerId,
                     const std::vector<uint8_t> &content) override;
    std::vector<ServiceMessage> fetchAfter(const std::string &containerId,
                                           const std::string &afterMessageId) override;

private:
    struct HttpResponse {
        long status = 0;
        std::string body;
        std::map<std::string, std::string> headers;
    };

    HttpResponse request(const std::string &method, const std::string &url,
                         const std::string &body = "",
                         const std::string &contentType = "",
                         const std::vector<std::pair<std::string, std::string>> &extraHeaders = {});

    static void throwForStatus(const HttpResponse &response, const std::string &what);

    std::string resolveUserId();
    std::string uploadPlaceholderTrackCid();
    std::string buildPlaceholderWav();

    static std::string base64Encode(const std::vector<uint8_t> &data);
    static std::vector<uint8_t> base64Decode(const std::string &text);

    static std::string urlEncode(const std::string &value);

    std::string apiKey;
    std::string apiSecret;
    std::string userId;       // as supplied (handle or id)
    std::string resolvedUserId; // Audius encoded user id
    std::string bearerToken;
    std::string contentNode;
    std::string apiBaseUrl;

    // hint -> track_id cache and helpers
    std::string resolveOwner(const std::string &owner);
    std::string resolveTrackId(const std::string &container);
    void cacheTrackId(const std::string &container, const std::string &trackId);
    std::map<std::string, std::string> trackIdCache;
    std::mutex cacheMutex;
};

#endif
