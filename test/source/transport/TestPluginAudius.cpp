#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <future>

#include "InMemoryServiceClient.h"
#include "MockTransportSdk.h"
#include "PluginAudius.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

namespace {

const std::vector<uint8_t> payload{0x31, 0x41, 0x59};

class TestPluginAudius : public ::testing::Test {
protected:
    TestPluginAudius() {
        channelProperties.maxLinks = 2;
        channelProperties.currentRole.roleName = "default";
        channelProperties.currentRole.linkSide = LS_BOTH;
        ON_CALL(sdk, getActivePersona()).WillByDefault(Return("test-persona"));
        ON_CALL(sdk, getChannelProperties()).WillByDefault(Invoke([this] { return channelProperties; }));
    }

    std::unique_ptr<PluginAudius> makePlugin() {
        return std::make_unique<PluginAudius>(&sdk, service);
    }

    // The container a link published, read back from the link properties the SDK would read.
    LinkAddress addressOf(PluginAudius &plugin, const LinkID &linkId) {
        LinkAddress address;
        EXPECT_TRUE(parseLinkAddress(plugin.getLinkProperties(linkId).linkAddress, address));
        return address;
    }

    NiceMock<MockTransportSdk> sdk;
    ChannelProperties channelProperties;
    std::shared_ptr<InMemoryServiceClient> service = std::make_shared<InMemoryServiceClient>();
};

Action makeAction(const std::string &json, uint64_t id = 1) {
    return Action{0.0, id, json};
}

}  // namespace

TEST_F(TestPluginAudius, transport_properties_declare_post_and_fetch) {
    auto plugin = makePlugin();
    auto props = plugin->getTransportProperties();
    EXPECT_EQ(1u, props.supportedActions.count("post"));
    EXPECT_EQ(1u, props.supportedActions.count("fetch"));
}

TEST_F(TestPluginAudius, createLink_creates_container_and_reports_created) {
    auto plugin = makePlugin();
    EXPECT_CALL(sdk, onLinkStatusChanged(1u, "link-1", LINK_CREATED, _));
    ASSERT_EQ(COMPONENT_OK, plugin->createLink(1, "link-1"));
    EXPECT_TRUE(service->containerExists(addressOf(*plugin, "link-1").container));
}

TEST_F(TestPluginAudius, loadLinkAddress_reports_loaded_without_touching_the_service) {
    auto plugin = makePlugin();
    EXPECT_CALL(sdk, onLinkStatusChanged(2u, "link-2", LINK_LOADED, _));
    ASSERT_EQ(COMPONENT_OK, plugin->loadLinkAddress(2, "link-2", R"({"container": "peer-box"})"));
    EXPECT_FALSE(service->containerExists("peer-box"));
    EXPECT_EQ("peer-box", addressOf(*plugin, "link-2").container);
}

TEST_F(TestPluginAudius, loadLinkAddress_rejects_malformed_address) {
    auto plugin = makePlugin();
    EXPECT_CALL(sdk, onLinkStatusChanged(2u, "link-2", LINK_DESTROYED, _));
    EXPECT_EQ(COMPONENT_ERROR, plugin->loadLinkAddress(2, "link-2", "garbage"));
}

TEST_F(TestPluginAudius, loadLinkAddresses_is_unsupported) {
    auto plugin = makePlugin();
    EXPECT_CALL(sdk, onLinkStatusChanged(3u, "link-3", LINK_DESTROYED, _));
    EXPECT_EQ(COMPONENT_ERROR, plugin->loadLinkAddresses(3, "link-3", {"a", "b"}));
}

TEST_F(TestPluginAudius, role_restrictions_are_enforced) {
    channelProperties.currentRole.linkSide = LS_LOADER;
    auto plugin = makePlugin();
    EXPECT_CALL(sdk, onLinkStatusChanged(1u, "link-1", LINK_DESTROYED, _));
    EXPECT_EQ(COMPONENT_ERROR, plugin->createLink(1, "link-1"));

    channelProperties.currentRole.linkSide = LS_CREATOR;
    auto creator = makePlugin();
    EXPECT_CALL(sdk, onLinkStatusChanged(2u, "link-2", LINK_DESTROYED, _));
    EXPECT_EQ(COMPONENT_ERROR, creator->loadLinkAddress(2, "link-2", R"({"container": "c"})"));
}

TEST_F(TestPluginAudius, maxLinks_is_enforced) {
    auto plugin = makePlugin();
    ASSERT_EQ(COMPONENT_OK, plugin->createLink(1, "link-1"));
    ASSERT_EQ(COMPONENT_OK, plugin->createLink(2, "link-2"));
    EXPECT_CALL(sdk, onLinkStatusChanged(3u, "link-3", LINK_DESTROYED, _));
    EXPECT_EQ(COMPONENT_ERROR, plugin->createLink(3, "link-3"));
}

TEST_F(TestPluginAudius, duplicate_link_ids_are_rejected_without_destroying_the_existing_link) {
    auto plugin = makePlugin();
    ASSERT_EQ(COMPONENT_OK, plugin->createLink(1, "link-1"));
    EXPECT_CALL(sdk, onLinkStatusChanged(_, "link-1", LINK_DESTROYED, _)).Times(0);
    EXPECT_EQ(COMPONENT_ERROR, plugin->createLink(2, "link-1"));
}

TEST_F(TestPluginAudius, only_the_link_that_created_the_container_deletes_it) {
    auto plugin = makePlugin();
    const std::string address = R"({"container": "shared-box"})";
    ASSERT_EQ(COMPONENT_OK, plugin->createLinkFromAddress(1, "first", address));
    ASSERT_EQ(COMPONENT_OK, plugin->createLinkFromAddress(2, "reply", address));
    ASSERT_TRUE(service->containerExists("shared-box"));

    // The reply link reuses the container, so destroying it must leave the container in place.
    EXPECT_EQ(COMPONENT_OK, plugin->destroyLink(3, "reply"));
    EXPECT_TRUE(service->containerExists("shared-box"));

    EXPECT_EQ(COMPONENT_OK, plugin->destroyLink(4, "first"));
    EXPECT_FALSE(service->containerExists("shared-box"));
}

TEST_F(TestPluginAudius, destroyLink_reports_destroyed_even_for_unknown_links) {
    auto plugin = makePlugin();
    EXPECT_CALL(sdk, onLinkStatusChanged(9u, "nope", LINK_DESTROYED, _));
    EXPECT_EQ(COMPONENT_OK, plugin->destroyLink(9, "nope"));
}

TEST_F(TestPluginAudius, getActionParams_requests_content_per_action_type) {
    auto plugin = makePlugin();

    auto post = plugin->getActionParams(makeAction(R"({"linkId": "*", "type": "post"})"));
    ASSERT_EQ(1u, post.size());
    EXPECT_EQ("*", post[0].linkId);
    EXPECT_TRUE(post[0].encodePackage);

    auto fetch = plugin->getActionParams(makeAction(R"({"linkId": "*", "type": "fetch"})"));
    ASSERT_EQ(1u, fetch.size());
    EXPECT_FALSE(fetch[0].encodePackage);

    EXPECT_TRUE(plugin->getActionParams(makeAction(R"({"linkId": "*", "type": "cleanup"})")).empty());
}

TEST_F(TestPluginAudius, getActionParams_fails_the_component_on_unknown_actions) {
    auto plugin = makePlugin();
    EXPECT_CALL(sdk, updateState(COMPONENT_STATE_FAILED));
    EXPECT_TRUE(plugin->getActionParams(makeAction(R"({"linkId": "*", "type": "dance"})")).empty());
}

TEST_F(TestPluginAudius, enqueue_then_post_reaches_the_service_and_a_loader_receives_it) {
    auto plugin = makePlugin();
    ASSERT_EQ(COMPONENT_OK, plugin->createLink(1, "send-link"));
    std::string container = addressOf(*plugin, "send-link").container;
    ASSERT_EQ(COMPONENT_OK, plugin->loadLinkAddress(2, "recv-link", R"({"container": ")" + container + R"("})"));

    std::promise<PackageStatus> sent;
    EXPECT_CALL(sdk, onPackageStatusChanged(55, _)).WillOnce(Invoke([&](RaceHandle, PackageStatus s) {
        sent.set_value(s);
        return ChannelResponse{CM_OK, 0};
    }));
    std::promise<std::vector<uint8_t>> received;
    EXPECT_CALL(sdk, onReceive("recv-link", _, _))
        .WillOnce(Invoke([&](const LinkID &, const EncodingParameters &, const std::vector<uint8_t> &bytes) {
            received.set_value(bytes);
            return ChannelResponse{CM_OK, 0};
        }));

    Action post = makeAction(R"({"linkId": "*", "type": "post"})", 7);
    EncodingParameters params{"*", kContentType, true, "{}"};
    ASSERT_EQ(COMPONENT_OK, plugin->enqueueContent(params, post, payload));
    ASSERT_EQ(COMPONENT_OK, plugin->doAction({55}, post));
    auto sentFuture = sent.get_future();
    ASSERT_EQ(std::future_status::ready, sentFuture.wait_for(std::chrono::seconds(5)));
    EXPECT_EQ(PACKAGE_SENT, sentFuture.get());

    ASSERT_EQ(COMPONENT_OK, plugin->doAction({}, makeAction(R"({"linkId": "*", "type": "fetch"})", 8)));
    auto receivedFuture = received.get_future();
    ASSERT_EQ(std::future_status::ready, receivedFuture.wait_for(std::chrono::seconds(5)));
    EXPECT_EQ(payload, receivedFuture.get());
}

TEST_F(TestPluginAudius, doAction_on_unknown_link_is_an_error_but_cleanup_is_not) {
    auto plugin = makePlugin();
    EXPECT_EQ(COMPONENT_ERROR, plugin->doAction({}, makeAction(R"({"linkId": "ghost", "type": "fetch"})")));
    EXPECT_EQ(COMPONENT_OK, plugin->doAction({}, makeAction(R"({"linkId": "*", "type": "cleanup"})")));
    EXPECT_EQ(COMPONENT_OK, plugin->doAction({}, makeAction(R"({"linkId": "*", "type": "fetch"})")));
}

TEST_F(TestPluginAudius, requests_server_and_token_then_starts) {
    EXPECT_CALL(sdk, requestPluginUserInput("server", _, _)).WillOnce(Return(ChannelResponse{CM_OK, 11}));
    EXPECT_CALL(sdk, requestPluginUserInput("token", _, _)).WillOnce(Return(ChannelResponse{CM_OK, 12}));
    PluginAudius plugin(&sdk);

    EXPECT_CALL(sdk, updateState(COMPONENT_STATE_STARTED));
    EXPECT_EQ(COMPONENT_OK, plugin.onUserInputReceived(11, true, "http://service.example"));
    EXPECT_EQ(COMPONENT_OK, plugin.onUserInputReceived(12, true, "secret"));
}

TEST_F(TestPluginAudius, fails_when_a_required_parameter_is_missing) {
    EXPECT_CALL(sdk, requestPluginUserInput("server", _, _)).WillOnce(Return(ChannelResponse{CM_OK, 11}));
    EXPECT_CALL(sdk, requestPluginUserInput("token", _, _)).WillOnce(Return(ChannelResponse{CM_OK, 12}));
    PluginAudius plugin(&sdk);

    EXPECT_CALL(sdk, updateState(COMPONENT_STATE_FAILED));
    plugin.onUserInputReceived(11, true, "http://service.example");
    plugin.onUserInputReceived(12, false, "");
    EXPECT_EQ(COMPONENT_ERROR, plugin.onUserInputReceived(99, true, "stray"));
}
