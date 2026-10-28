#include "Link.h"

#include <nlohmann/json.hpp>

#include "PersistentStorageHelpers.h"
#include "log.h"

Link::Link(const LinkID &linkId, const LinkAddress &address, const LinkProperties &properties,
           bool isCreator, bool ownsContainer, std::shared_ptr<ServiceClient> service,
           ITransportSdk *sdk) :
    linkId(linkId),
    address(address),
    properties(properties),
    isCreator(isCreator),
    ownsContainer(ownsContainer),
    service(std::move(service)),
    sdk(sdk) {
    this->properties.linkAddress = linkAddressToString(this->address);

    // Resume where a previous run stopped; otherwise start from the address's baseline.
    lastFetchedId = psh::readValue(sdk, psh::cursorKey(linkId), address.initialCursor);
}

Link::~Link() {
    shutdown();
}

void Link::start() {
    if (thread.joinable()) {
        return;
    }
    isShutdown = false;
    thread = std::thread(&Link::runActionThread, this);
}

void Link::shutdown() {
    isShutdown = true;
    conditionVariable.notify_all();
    if (thread.joinable()) {
        thread.join();
    }
}

ComponentStatus Link::enqueueContent(uint64_t actionId, const std::vector<uint8_t> &content) {
    std::lock_guard<std::mutex> lock(mutex);
    contentQueue[actionId] = content;
    return COMPONENT_OK;
}

ComponentStatus Link::dequeueContent(uint64_t actionId) {
    std::lock_guard<std::mutex> lock(mutex);
    contentQueue.erase(actionId);
    return COMPONENT_OK;
}

ComponentStatus Link::post(std::vector<RaceHandle> handles, uint64_t actionId) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        actionQueue.push_back({true, std::move(handles), actionId});
    }
    conditionVariable.notify_one();
    return COMPONENT_OK;
}

ComponentStatus Link::fetch(std::vector<RaceHandle> handles) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        actionQueue.push_back({false, std::move(handles), 0});
    }
    conditionVariable.notify_one();
    return COMPONENT_OK;
}

void Link::runActionThread() {
    while (true) {
        QueuedAction action;
        {
            std::unique_lock<std::mutex> lock(mutex);
            conditionVariable.wait(lock, [this] { return isShutdown || !actionQueue.empty(); });
            if (isShutdown) {
                return;
            }
            action = std::move(actionQueue.front());
            actionQueue.pop_front();
        }

        try {
            if (action.isPost) {
                postOnActionThread(action.handles, action.actionId);
            } else {
                fetchOnActionThread();
            }
        } catch (const ServiceError &error) {
            logError("[Link " + linkId + "] service error: " + error.what());
            updatePackageStatus(action.handles, error.kind == ServiceErrorKind::Transient
                                                    ? PACKAGE_FAILED_NETWORK_ERROR
                                                    : PACKAGE_FAILED_GENERIC);
            emitEvent("failed", action.actionId);
        } catch (const std::exception &error) {
            logError("[Link " + linkId + "] unexpected error: " + error.what());
            updatePackageStatus(action.handles, PACKAGE_FAILED_GENERIC);
            emitEvent("failed", action.actionId);
        }
    }
}

void Link::postOnActionThread(const std::vector<RaceHandle> &handles, uint64_t actionId) {
    std::vector<uint8_t> content;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto iter = contentQueue.find(actionId);
        if (iter == contentQueue.end()) {
            // A wildcard ("*") post is delivered to every link, but only the link the content was
            // enqueued on has anything to send.
            logDebug("[Link " + linkId + "] no content for action " + std::to_string(actionId));
            return;
        }
        content = std::move(iter->second);
        contentQueue.erase(iter);
    }

    std::string messageId = service->post(address.container, content);
    logInfo("[Link " + linkId + "] posted " + std::to_string(content.size()) + " bytes to " +
            address.container + " as " + messageId);
    updatePackageStatus(handles, PACKAGE_SENT);
    emitEvent("posted", actionId);
}

void Link::fetchOnActionThread() {
    // A creator only posts. Fetching on it would hand the node its own messages back (a
    // send/receive state machine would mistake them for the peer's reply).
    if (isCreator) {
        return;
    }

    std::vector<ServiceMessage> messages = service->fetchAfter(address.container, lastFetchedId);
    logDebug("[Link " + linkId + "] fetched " + std::to_string(messages.size()) +
             " new message(s) from " + address.container + " after '" + lastFetchedId + "'");
    for (const ServiceMessage &message : messages) {
        if (!message.content.empty()) {
            sdk->onReceive(linkId, {linkId, kContentType, false, "{}"}, message.content);
            emitEvent("fetched", 0);
        }
        // Advance the cursor only after delivery so an error above retries the same message.
        lastFetchedId = message.id;
        psh::saveValue(sdk, psh::cursorKey(linkId), lastFetchedId);
    }
}

void Link::updatePackageStatus(const std::vector<RaceHandle> &handles, PackageStatus status) {
    for (RaceHandle handle : handles) {
        sdk->onPackageStatusChanged(handle, status);
    }
}

// Events are free-form JSON consumed by the user model (see COMPONENT_INTEGRATION.md).
void Link::emitEvent(const std::string &status, uint64_t actionId) {
    nlohmann::json eventJson = {{"linkId", linkId}, {"actionId", actionId}, {"status", status}};
    sdk->onEvent({eventJson.dump()});
}
