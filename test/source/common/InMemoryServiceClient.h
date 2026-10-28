#ifndef __AUDIUS_IN_MEMORY_SERVICE_CLIENT_H__
#define __AUDIUS_IN_MEMORY_SERVICE_CLIENT_H__

#include <map>
#include <mutex>

#include "ServiceClient.h"

// Fake service used by every unit test, so the transport logic is tested without network access
// or credentials. Message ids are zero-padded counters, so lexical order == post order.
class InMemoryServiceClient : public ServiceClient {
public:
    std::string createContainer(const std::string &containerIdHint = "") override {
        std::lock_guard<std::mutex> lock(mutex);
        std::string id = containerIdHint.empty() ? ("box-" + std::to_string(++counter)) : containerIdHint;
        containers[id];
        return id;
    }

    bool containerExists(const std::string &containerId) override {
        std::lock_guard<std::mutex> lock(mutex);
        return containers.count(containerId) != 0;
    }

    void deleteContainer(const std::string &containerId) override {
        std::lock_guard<std::mutex> lock(mutex);
        containers.erase(containerId);
    }

    std::string post(const std::string &containerId, const std::vector<uint8_t> &content) override {
        std::lock_guard<std::mutex> lock(mutex);
        auto iter = containers.find(containerId);
        if (iter == containers.end()) {
            throw ServiceError(ServiceErrorKind::NotFound, "no container " + containerId);
        }
        char id[16];
        snprintf(id, sizeof(id), "%08zu", ++counter);
        iter->second[id] = content;
        return id;
    }

    std::vector<ServiceMessage> fetchAfter(const std::string &containerId,
                                           const std::string &afterMessageId) override {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<ServiceMessage> messages;
        auto iter = containers.find(containerId);
        if (iter == containers.end()) {
            return messages;
        }
        for (const auto &entry : iter->second) {
            if (entry.first > afterMessageId) {
                messages.push_back({entry.first, entry.second});
            }
        }
        return messages;
    }

private:
    std::mutex mutex;
    size_t counter = 0;
    std::map<std::string, std::map<std::string, std::vector<uint8_t>>> containers;
};

#endif
