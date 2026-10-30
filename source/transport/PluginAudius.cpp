#include "PluginAudius.h"

#include <mutex>
#include <nlohmann/json.hpp>
#include <random>

#include "JsonTypes.h"
#include "PersistentStorageHelpers.h"
#include "AudiusClient.h"
#include "log.h"

static const std::string logPrefix = "[PluginAudius] ";

namespace {

LinkProperties createDefaultLinkProperties(const ChannelProperties &channelProperties) {
    LinkProperties linkProperties;

    // The framework does not act on linkType; every reference transport reports LT_BIDI.
    linkProperties.linkType = LT_BIDI;
    linkProperties.transmissionType = channelProperties.transmissionType;
    linkProperties.connectionType = channelProperties.connectionType;
    linkProperties.sendType = channelProperties.sendType;
    linkProperties.reliable = channelProperties.reliable;
    linkProperties.isFlushable = channelProperties.isFlushable;
    linkProperties.duration_s = channelProperties.duration_s;
    linkProperties.period_s = channelProperties.period_s;
    linkProperties.mtu = channelProperties.mtu;

    linkProperties.worst = channelProperties.creatorExpected;
    linkProperties.expected = channelProperties.creatorExpected;
    linkProperties.best = channelProperties.creatorExpected;

    linkProperties.supportedHints = channelProperties.supportedHints;
    linkProperties.channelGid = channelProperties.channelGid;
    return linkProperties;
}

std::string randomContainerId() {
    static std::mutex mutex;
    static std::mt19937_64 generator{std::random_device{}()};
    std::lock_guard<std::mutex> lock(mutex);
    static const char digits[] = "0123456789abcdef";
    std::string id = "audius-";
    for (int i = 0; i < 16; i++) {
        id += digits[generator() & 0xf];
    }
    return id;
}

}  // namespace

PluginAudius::PluginAudius(ITransportSdk *sdk) :
    sdk(sdk),
    racePersona(sdk->getActivePersona()),
    channelProperties(sdk->getChannelProperties()),
    defaultLinkProperties(createDefaultLinkProperties(channelProperties)) {
    TRACE_METHOD();

    // Answers come from `race-cli --param <composition id>.<key>=<value>` (the composition id from
    // manifest.json, NOT the plugin/library name) and arrive asynchronously in onUserInputReceived.
    apiKeyHandle = sdk->requestPluginUserInput("apiKey", "Audius developer app API key", false).handle;
    apiSecretHandle = sdk->requestPluginUserInput("apiSecret", "Audius developer app API secret", true).handle;
    userIdHandle = sdk->requestPluginUserInput("userId", "Audius user id owning this node's tracks", false).handle;
    bearerTokenHandle = sdk->requestPluginUserInput("bearerToken", "Optional Audius bearer token", true).handle;
    contentNodeHandle = sdk->requestPluginUserInput("contentNode", "Optional Audius content node URL", false).handle;
}

PluginAudius::PluginAudius(ITransportSdk *sdk, std::shared_ptr<ServiceClient> client) :
    sdk(sdk),
    racePersona(sdk->getActivePersona()),
    channelProperties(sdk->getChannelProperties()),
    defaultLinkProperties(createDefaultLinkProperties(channelProperties)),
    serviceClient(std::move(client)) {
    ready = true;
}

PluginAudius::~PluginAudius() {
    for (const auto &link : links.getAll()) {
        link->shutdown();
    }
}

ComponentStatus PluginAudius::onUserInputReceived(RaceHandle handle, bool answered,
                                                const std::string &response) {
    TRACE_METHOD(handle, answered);

    if (handle == apiKeyHandle) {
        apiKeyHandle = NULL_RACE_HANDLE;
        apiKeyAnswered = true;
        apiKey = response;
    } else if (handle == apiSecretHandle) {
        apiSecretHandle = NULL_RACE_HANDLE;
        apiSecretAnswered = true;
        apiSecret = response;
    } else if (handle == userIdHandle) {
        userIdHandle = NULL_RACE_HANDLE;
        userIdAnswered = true;
        userId = response;
    } else if (handle == bearerTokenHandle) {
        bearerTokenHandle = NULL_RACE_HANDLE;
        bearerTokenAnswered = true;
        bearerToken = response;
    } else if (handle == contentNodeHandle) {
        contentNodeHandle = NULL_RACE_HANDLE;
        contentNodeAnswered = true;
        contentNode = response;
    } else {
        logWarning(logPrefix + "Unknown user input handle " + std::to_string(handle));
        return COMPONENT_ERROR;
    }

    finishStartupIfInputsReceived();
    return COMPONENT_OK;
}

void PluginAudius::finishStartupIfInputsReceived() {
    if (apiKeyHandle != NULL_RACE_HANDLE || apiSecretHandle != NULL_RACE_HANDLE ||
        userIdHandle != NULL_RACE_HANDLE || bearerTokenHandle != NULL_RACE_HANDLE ||
        contentNodeHandle != NULL_RACE_HANDLE) {
        return;  // still waiting for an answer
    }
    if (apiKey.empty() || apiSecret.empty() || userId.empty()) {
        logError(logPrefix + "Missing required parameter(s): apiKey, apiSecret and userId are required");
        sdk->updateState(COMPONENT_STATE_FAILED);
        return;
    }
    serviceClient = std::make_shared<AudiusClient>(apiKey, apiSecret, userId, bearerToken, contentNode);
    ready = true;
    sdk->updateState(COMPONENT_STATE_STARTED);
    logInfo(logPrefix + "Ready");
}

TransportProperties PluginAudius::getTransportProperties() {
    TRACE_METHOD();
    // action name -> MIME types of the content that action needs. The user model schedules these
    // actions; the encoding whose type matches produces/consumes the bytes.
    return {{{"post", {kContentType}}, {"fetch", {kContentType}}}};
}

LinkProperties PluginAudius::getLinkProperties(const LinkID &linkId) {
    TRACE_METHOD(linkId);
    auto link = links.get(linkId);
    return link ? link->getProperties() : defaultLinkProperties;
}

bool PluginAudius::preLinkCreate(const std::string &prefix, RaceHandle handle, const LinkID &linkId,
                               LinkSide invalidRoleLinkSide) {
    if (links.contains(linkId)) {
        // Do not report LINK_DESTROYED: that would tear down the existing link with this id.
        logError(prefix + "link already exists");
        return false;
    }
    if (!ready) {
        logError(prefix + "component is not ready");
    } else if (channelProperties.maxLinks > 0 && links.size() >= channelProperties.maxLinks) {
        logError(prefix + "too many links (max " + std::to_string(channelProperties.maxLinks) + ")");
    } else if (channelProperties.currentRole.linkSide == LS_UNDEF ||
               channelProperties.currentRole.linkSide == invalidRoleLinkSide) {
        logError(prefix + "invalid role '" + channelProperties.currentRole.roleName +
                 "' for this call");
    } else {
        return true;
    }
    // Always tell the SDK the link is not coming, or it waits for it forever.
    sdk->onLinkStatusChanged(handle, linkId, LINK_DESTROYED, {});
    return false;
}

ComponentStatus PluginAudius::postLinkCreate(const std::string &prefix, RaceHandle handle,
                                           const LinkID &linkId, const std::shared_ptr<Link> &link,
                                           LinkStatus linkStatus) {
    if (link == nullptr) {
        logError(prefix + "link was null");
        sdk->onLinkStatusChanged(handle, linkId, LINK_DESTROYED, {});
        return COMPONENT_ERROR;
    }
    links.add(link);
    link->start();
    // The link address is NOT passed here: the SDK reads it from getLinkProperties().linkAddress.
    sdk->onLinkStatusChanged(handle, linkId, linkStatus, {});
    return COMPONENT_OK;
}

std::shared_ptr<Link> PluginAudius::createLinkInstance(const LinkID &linkId,
                                                     const LinkAddress &address,
                                                     const LinkProperties &properties,
                                                     bool isCreator, bool ownsContainer) {
    return std::make_shared<Link>(linkId, address, properties, isCreator, ownsContainer,
                                  serviceClient, sdk);
}

// Creator side, brand-new link: generate an address and create the service-side container.
ComponentStatus PluginAudius::createLink(RaceHandle handle, const LinkID &linkId) {
    TRACE_METHOD(handle, linkId);
    std::string prefix = logPrefix + "createLink(" + linkId + "): ";
    if (!preLinkCreate(prefix, handle, linkId, LS_LOADER)) {
        return COMPONENT_ERROR;
    }

    LinkAddress address;
    try {
        address.container = serviceClient->createContainer("");
    } catch (const std::exception &error) {
        logError(prefix + "could not create container: " + error.what());
        sdk->onLinkStatusChanged(handle, linkId, LINK_DESTROYED, {});
        return COMPONENT_ERROR;
    }

    auto link = createLinkInstance(linkId, address, defaultLinkProperties, true, true);
    return postLinkCreate(prefix, handle, linkId, link, LINK_CREATED);
}

// Loader side: attach to a container some creator published. Nothing is created on the service
// (it may not exist yet; fetch retries until it does).
ComponentStatus PluginAudius::loadLinkAddress(RaceHandle handle, const LinkID &linkId,
                                            const std::string &linkAddress) {
    TRACE_METHOD(handle, linkId, linkAddress);
    std::string prefix = logPrefix + "loadLinkAddress(" + linkId + "): ";
    if (!preLinkCreate(prefix, handle, linkId, LS_CREATOR)) {
        return COMPONENT_ERROR;
    }

    LinkAddress address;
    if (!parseLinkAddress(linkAddress, address)) {
        logError(prefix + "invalid link address: " + linkAddress);
        sdk->onLinkStatusChanged(handle, linkId, LINK_DESTROYED, {});
        return COMPONENT_ERROR;
    }

    auto link = createLinkInstance(linkId, address, defaultLinkProperties, false, false);
    return postLinkCreate(prefix, handle, linkId, link, LINK_LOADED);
}

ComponentStatus PluginAudius::loadLinkAddresses(RaceHandle handle, const LinkID &linkId,
                                              const std::vector<std::string> & /*linkAddresses*/) {
    TRACE_METHOD(handle, linkId);
    // Multi-address links are not supported by this transport.
    sdk->onLinkStatusChanged(handle, linkId, LINK_DESTROYED, {});
    return COMPONENT_ERROR;
}

// Creator side with a PRE-DETERMINED address (e.g. the address passed as --send-address).
ComponentStatus PluginAudius::createLinkFromAddress(RaceHandle handle, const LinkID &linkId,
                                                  const std::string &linkAddress) {
    TRACE_METHOD(handle, linkId, linkAddress);
    std::string prefix = logPrefix + "createLinkFromAddress(" + linkId + "): ";
    if (!preLinkCreate(prefix, handle, linkId, LS_LOADER)) {
        return COMPONENT_ERROR;
    }

    LinkAddress address;
    if (!parseLinkAddress(linkAddress, address)) {
        logError(prefix + "invalid link address: " + linkAddress);
        sdk->onLinkStatusChanged(handle, linkId, LINK_DESTROYED, {});
        return COMPONENT_ERROR;
    }

    // Several links may be built from the same address (e.g. a reply link reusing the container).
    // Only the link that actually created the container may delete it in destroyLink().
    bool ownsContainer = false;
    try {
        if (!serviceClient->containerExists(address.container)) {
            address.container = serviceClient->createContainer(address.container);
            ownsContainer = true;
        }
    } catch (const std::exception &error) {
        logError(prefix + "could not prepare container: " + error.what());
        sdk->onLinkStatusChanged(handle, linkId, LINK_DESTROYED, {});
        return COMPONENT_ERROR;
    }

    auto link = createLinkInstance(linkId, address, defaultLinkProperties, true, ownsContainer);
    return postLinkCreate(prefix, handle, linkId, link, LINK_CREATED);
}

ComponentStatus PluginAudius::destroyLink(RaceHandle handle, const LinkID &linkId) {
    TRACE_METHOD(handle, linkId);
    std::string prefix = logPrefix + "destroyLink(" + linkId + "): ";

    auto link = links.remove(linkId);
    if (link) {
        link->shutdown();
        if (link->getIsCreator() && link->getOwnsContainer()) {
            try {
                serviceClient->deleteContainer(link->getAddress().container);
            } catch (const std::exception &error) {
                logWarning(prefix + "could not delete container: " + error.what());
            }
        }
        psh::clearValue(sdk, psh::cursorKey(linkId));
    } else {
        logWarning(prefix + "unknown link");
    }

    sdk->onLinkStatusChanged(handle, linkId, LINK_DESTROYED, {});
    return COMPONENT_OK;
}

// Tells the framework which content (MIME type) each scheduled action needs from the encoding
// component. A linkId of "*" is passed through: the framework treats the action as applying to
// every link. A fetch must also be reported (encodePackage=false) so the framework can match the
// decoder; a post is reported with encodePackage=true.
std::vector<EncodingParameters> PluginAudius::getActionParams(const Action &action) {
    TRACE_METHOD(action.actionId, action.json);
    try {
        ActionJson actionJson = nlohmann::json::parse(action.json);
        switch (actionJson.type) {
            case ACTION_POST:
                return {{actionJson.linkId, kContentType, true, "{}"}};
            case ACTION_FETCH:
                return {{actionJson.linkId, kContentType, false, "{}"}};
            case ACTION_CLEANUP:
                return {};
            default:
                logError(logPrefix + "unrecognized action type in " + action.json);
        }
    } catch (const nlohmann::json::exception &error) {
        logError(logPrefix + "bad action JSON: " + error.what());
    }
    sdk->updateState(COMPONENT_STATE_FAILED);
    return {};
}

std::vector<std::shared_ptr<Link>> PluginAudius::linksForAction(const ActionJson &action) {
    if (action.linkId == "*") {
        return links.getAll();
    }
    auto link = links.get(action.linkId);
    return link ? std::vector<std::shared_ptr<Link>>{link} : std::vector<std::shared_ptr<Link>>{};
}

// Receives bytes the encoding component produced for a scheduled post action.
ComponentStatus PluginAudius::enqueueContent(const EncodingParameters &params, const Action &action,
                                           const std::vector<uint8_t> &content) {
    TRACE_METHOD(params.linkId, action.actionId, content.size());
    if (content.empty()) {
        return COMPONENT_OK;
    }

    try {
        ActionJson actionJson = nlohmann::json::parse(action.json);
        if (actionJson.type != ACTION_POST) {
            return COMPONENT_OK;
        }
        // Prefer a concrete link id chosen by the framework over the action's wildcard.
        if (!params.linkId.empty() && params.linkId != "*") {
            actionJson.linkId = params.linkId;
        }

        auto targets = linksForAction(actionJson);
        if (targets.empty()) {
            logError(logPrefix + "no link to enqueue content on for '" + actionJson.linkId + "'");
            return COMPONENT_ERROR;
        }
        for (const auto &link : targets) {
            // Only creator links post. Queueing on a loader link would just leak the bytes.
            if (actionJson.linkId != "*" || link->getIsCreator()) {
                link->enqueueContent(action.actionId, content);
            }
        }
        return COMPONENT_OK;
    } catch (const nlohmann::json::exception &error) {
        logError(logPrefix + "bad action JSON: " + error.what());
        return COMPONENT_ERROR;
    }
}

ComponentStatus PluginAudius::dequeueContent(const Action &action) {
    TRACE_METHOD(action.actionId);
    try {
        ActionJson actionJson = nlohmann::json::parse(action.json);
        for (const auto &link : linksForAction(actionJson)) {
            link->dequeueContent(action.actionId);
        }
        return COMPONENT_OK;
    } catch (const nlohmann::json::exception &error) {
        logError(logPrefix + "bad action JSON: " + error.what());
        return COMPONENT_ERROR;
    }
}

// Must return quickly; Link does the slow service work on its own thread.
ComponentStatus PluginAudius::doAction(const std::vector<RaceHandle> &handles, const Action &action) {
    TRACE_METHOD(action.actionId, action.json);
    try {
        ActionJson actionJson = nlohmann::json::parse(action.json);
        if (actionJson.type == ACTION_CLEANUP) {
            return COMPONENT_OK;
        }
        if (actionJson.type != ACTION_POST && actionJson.type != ACTION_FETCH) {
            logError(logPrefix + "unknown action type in " + action.json);
            return COMPONENT_ERROR;
        }

        auto targets = linksForAction(actionJson);
        if (targets.empty() && actionJson.linkId != "*") {
            logError(logPrefix + "link not found: " + actionJson.linkId);
            return COMPONENT_ERROR;
        }
        for (const auto &link : targets) {
            if (actionJson.type == ACTION_POST) {
                link->post(handles, action.actionId);
            } else {
                link->fetch(handles);
            }
        }
        return COMPONENT_OK;
    } catch (const nlohmann::json::exception &error) {
        logError(logPrefix + "bad action JSON: " + error.what());
        return COMPONENT_ERROR;
    }
}

#ifndef TESTBUILD
extern "C" {

ITransportComponent *createTransport(const std::string &transport, ITransportSdk *sdk,
                                     const std::string &roleName,
                                     const PluginConfig &pluginConfig) {
    TRACE_FUNCTION(transport, roleName, pluginConfig.pluginDirectory);
    return new PluginAudius(sdk);
}

void destroyTransport(ITransportComponent *component) {
    TRACE_FUNCTION();
    delete component;
}

const RaceVersionInfo raceVersion = RACE_VERSION;

}  // extern "C"
#endif
