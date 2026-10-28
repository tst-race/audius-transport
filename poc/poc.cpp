#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "AudiusClient.h"

static std::string env(const char *name, const std::string &fallback = "") {
    const char *v = std::getenv(name);
    return v ? std::string(v) : fallback;
}

int main() {
    std::string apiKey = env("AUDIUS_API_KEY");
    std::string apiSecret = env("AUDIUS_API_SECRET");
    std::string userId = env("AUDIUS_USER_ID");
    std::string bearer = env("AUDIUS_BEARER_TOKEN");
    std::string contentNode = env("AUDIUS_CONTENT_NODE", "https://creatornode.audius.co");
    std::string apiBase = env("AUDIUS_API_BASE", "https://api.audius.co/v1");

    if (apiKey.empty() || apiSecret.empty() || userId.empty()) {
        std::cerr << "Missing AUDIUS_API_KEY/AUDIUS_API_SECRET/AUDIUS_USER_ID\n";
        return 2;
    }

    AudiusClient client(apiKey, apiSecret, userId, bearer, contentNode, apiBase);

    const std::vector<uint8_t> payload{0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x21}; // "Hello!"

    std::string containerId;
    try {
        containerId = client.createContainer("poc-container");
        std::cout << "createContainer -> " << containerId << "\n";
    } catch (const std::exception &e) {
        std::cerr << "createContainer FAILED: " << e.what() << "\n";
        return 1;
    }

    std::string msgId;
    try {
        msgId = client.post(containerId, payload);
        std::cout << "post -> " << msgId << "\n";
    } catch (const std::exception &e) {
        std::cerr << "post FAILED: " << e.what() << "\n";
        client.deleteContainer(containerId);
        return 1;
    }

    bool ok = false;
    try {
        auto msgs = client.fetchAfter(containerId, "");
        std::cout << "fetchAfter -> " << msgs.size() << " message(s)\n";
        if (!msgs.empty()) {
            std::cout << "  id=" << msgs[0].id << " bytes=" << msgs[0].content.size() << "\n";
            if (msgs[0].content == payload) {
                ok = true;
                std::cout << "  PAYLOAD MATCHES\n";
            } else {
                std::cout << "  PAYLOAD MISMATCH\n";
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "fetchAfter FAILED: " << e.what() << "\n";
    }

    try {
        client.deleteContainer(containerId);
        std::cout << "deleteContainer -> ok (exists now: " << client.containerExists(containerId) << ")\n";
    } catch (const std::exception &e) {
        std::cerr << "deleteContainer FAILED: " << e.what() << "\n";
    }

    return ok ? 0 : 1;
}
