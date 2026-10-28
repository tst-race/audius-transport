#ifndef __AUDIUS_LINK_H__
#define __AUDIUS_LINK_H__

#include <ComponentTypes.h>
#include <ITransportSdk.h>
#include <LinkProperties.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "LinkAddress.h"
#include "ServiceClient.h"

// MIME type of the bytes this transport posts/fetches. The framework picks the encoding component
// whose type matches (wildcards such as "*/*" match any encoding, and with several encodings in
// the composition the FIRST is silently chosen - so name the real type from the Phase 2 design).
// Keep identical to what PluginAudius::getTransportProperties() and getActionParams() report.
inline constexpr const char *kContentType = "application/octet-stream";

// One Link = one service container + one worker thread.
//
// The SDK calls PluginAudius::doAction() from its own threads and expects it to return quickly, so
// post()/fetch() only queue a request; the worker thread performs the (slow, blocking) service
// calls and reports results through sdk->onPackageStatusChanged() / sdk->onReceive().
class Link {
public:
    // isCreator:  this side created the container and posts into it (LS_CREATOR).
    //             The other side (LS_LOADER) fetches from it.
    // ownsContainer: this side created the container and must delete it in destroyLink().
    //             A link rebuilt from an address whose container already exists does NOT own it.
    Link(const LinkID &linkId, const LinkAddress &address, const LinkProperties &properties,
         bool isCreator, bool ownsContainer, std::shared_ptr<ServiceClient> service,
         ITransportSdk *sdk);
    virtual ~Link();

    LinkID getId() const { return linkId; }
    const LinkAddress &getAddress() const { return address; }
    bool getIsCreator() const { return isCreator; }
    bool getOwnsContainer() const { return ownsContainer; }
    // linkAddress is filled in so the SDK can publish it to the peer.
    const LinkProperties &getProperties() const { return properties; }

    virtual ComponentStatus enqueueContent(uint64_t actionId, const std::vector<uint8_t> &content);
    virtual ComponentStatus dequeueContent(uint64_t actionId);
    virtual ComponentStatus post(std::vector<RaceHandle> handles, uint64_t actionId);
    virtual ComponentStatus fetch(std::vector<RaceHandle> handles);

    virtual void start();
    virtual void shutdown();

protected:
    // Run on the worker thread. Overridden by MockLink in unit tests.
    virtual void postOnActionThread(const std::vector<RaceHandle> &handles, uint64_t actionId);
    virtual void fetchOnActionThread();

    void updatePackageStatus(const std::vector<RaceHandle> &handles, PackageStatus status);
    void emitEvent(const std::string &status, uint64_t actionId);

    LinkID linkId;
    LinkAddress address;
    LinkProperties properties;
    bool isCreator;
    bool ownsContainer;
    std::shared_ptr<ServiceClient> service;
    ITransportSdk *sdk;

private:
    struct QueuedAction {
        bool isPost;
        std::vector<RaceHandle> handles;
        uint64_t actionId;
    };

    void runActionThread();

    std::thread thread;
    std::atomic<bool> isShutdown{false};
    std::mutex mutex;
    std::condition_variable conditionVariable;
    std::deque<QueuedAction> actionQueue;
    std::unordered_map<uint64_t, std::vector<uint8_t>> contentQueue;  // actionId -> encoded bytes

    std::string lastFetchedId;  // fetch cursor (persisted)
};

#endif
