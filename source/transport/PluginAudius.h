#ifndef __AUDIUS_PLUGIN_AUDIUS_H__
#define __AUDIUS_PLUGIN_AUDIUS_H__

#include <ChannelProperties.h>
#include <ITransportComponent.h>
#include <LinkProperties.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "JsonTypes.h"
#include "Link.h"
#include "LinkMap.h"
#include "ServiceClient.h"

class PluginAudius : public ITransportComponent {
public:
    // Production constructor: asks the user (race-cli --param <composition>.server / .token) for
    // the service URL and credential, then builds a AudiusClient and reports COMPONENT_STATE_STARTED.
    explicit PluginAudius(ITransportSdk *sdk);
    // Test constructor: uses the supplied client and is ready immediately.
    PluginAudius(ITransportSdk *sdk, std::shared_ptr<ServiceClient> serviceClient);
    ~PluginAudius() override;

    ComponentStatus onUserInputReceived(RaceHandle handle, bool answered,
                                        const std::string &response) override;

    TransportProperties getTransportProperties() override;
    LinkProperties getLinkProperties(const LinkID &linkId) override;

    ComponentStatus createLink(RaceHandle handle, const LinkID &linkId) override;
    ComponentStatus loadLinkAddress(RaceHandle handle, const LinkID &linkId,
                                    const std::string &linkAddress) override;
    ComponentStatus loadLinkAddresses(RaceHandle handle, const LinkID &linkId,
                                      const std::vector<std::string> &linkAddresses) override;
    ComponentStatus createLinkFromAddress(RaceHandle handle, const LinkID &linkId,
                                          const std::string &linkAddress) override;
    ComponentStatus destroyLink(RaceHandle handle, const LinkID &linkId) override;

    std::vector<EncodingParameters> getActionParams(const Action &action) override;
    ComponentStatus enqueueContent(const EncodingParameters &params, const Action &action,
                                   const std::vector<uint8_t> &content) override;
    ComponentStatus dequeueContent(const Action &action) override;
    ComponentStatus doAction(const std::vector<RaceHandle> &handles, const Action &action) override;

protected:
    // Overridden in unit tests to inject a MockLink.
    virtual std::shared_ptr<Link> createLinkInstance(const LinkID &linkId,
                                                     const LinkAddress &address,
                                                     const LinkProperties &properties,
                                                     bool isCreator, bool ownsContainer);

private:
    bool preLinkCreate(const std::string &logPrefix, RaceHandle handle, const LinkID &linkId,
                       LinkSide invalidRoleLinkSide);
    ComponentStatus postLinkCreate(const std::string &logPrefix, RaceHandle handle,
                                   const LinkID &linkId, const std::shared_ptr<Link> &link,
                                   LinkStatus linkStatus);
    void finishStartupIfInputsReceived();
    std::vector<std::shared_ptr<Link>> linksForAction(const ActionJson &action);

    ITransportSdk *sdk;
    std::string racePersona;
    ChannelProperties channelProperties;
    LinkProperties defaultLinkProperties;
    LinkMap links;
    std::shared_ptr<ServiceClient> serviceClient;
    std::atomic<bool> ready{false};

    RaceHandle apiKeyHandle = NULL_RACE_HANDLE;
    RaceHandle apiSecretHandle = NULL_RACE_HANDLE;
    RaceHandle userIdHandle = NULL_RACE_HANDLE;
    RaceHandle bearerTokenHandle = NULL_RACE_HANDLE;
    RaceHandle contentNodeHandle = NULL_RACE_HANDLE;
    bool apiKeyAnswered = false;
    bool apiSecretAnswered = false;
    bool userIdAnswered = false;
    bool bearerTokenAnswered = false;
    bool contentNodeAnswered = false;
    std::string apiKey;
    std::string apiSecret;
    std::string userId;
    std::string bearerToken;
    std::string contentNode;
};

#endif
