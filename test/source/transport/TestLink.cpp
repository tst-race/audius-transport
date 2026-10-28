#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <future>

#include "InMemoryServiceClient.h"
#include "Link.h"
#include "MockTransportSdk.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;

namespace {

const std::vector<uint8_t> payload{0x31, 0x41, 0x59};

class TestLink : public ::testing::Test {
protected:
    std::shared_ptr<Link> makeLink(bool isCreator) {
        auto link = std::make_shared<Link>("link-1", LinkAddress{"box", ""}, LinkProperties{},
                                           isCreator, isCreator, service, &sdk);
        link->start();
        return link;
    }

    template <typename T>
    static bool completes(std::future<T> &future) {
        return future.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    }

    NiceMock<MockTransportSdk> sdk;
    std::shared_ptr<InMemoryServiceClient> service = std::make_shared<InMemoryServiceClient>();
};

}  // namespace

TEST_F(TestLink, link_properties_carry_the_address_for_the_sdk_to_publish) {
    auto link = makeLink(true);
    EXPECT_EQ(R"({"container":"box","initialCursor":""})", link->getProperties().linkAddress);
}

TEST_F(TestLink, post_uploads_enqueued_content_and_reports_sent) {
    service->createContainer("box");
    auto link = makeLink(true);

    std::promise<PackageStatus> sent;
    EXPECT_CALL(sdk, onPackageStatusChanged(42, _)).WillOnce(Invoke([&](RaceHandle, PackageStatus s) {
        sent.set_value(s);
        return ChannelResponse{CM_OK, 0};
    }));

    link->enqueueContent(7, payload);
    link->post({42}, 7);

    auto future = sent.get_future();
    ASSERT_TRUE(completes(future));
    EXPECT_EQ(PACKAGE_SENT, future.get());

    auto messages = service->fetchAfter("box", "");
    ASSERT_EQ(1u, messages.size());
    EXPECT_EQ(payload, messages[0].content);
}

TEST_F(TestLink, post_without_content_sends_nothing) {
    service->createContainer("box");
    auto link = makeLink(true);
    EXPECT_CALL(sdk, onPackageStatusChanged(_, _)).Times(0);

    link->post({42}, 99);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_TRUE(service->fetchAfter("box", "").empty());
}

TEST_F(TestLink, dequeued_content_is_not_posted) {
    service->createContainer("box");
    auto link = makeLink(true);
    link->enqueueContent(7, payload);
    link->dequeueContent(7);
    link->post({42}, 7);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_TRUE(service->fetchAfter("box", "").empty());
}

TEST_F(TestLink, post_failure_is_reported_to_the_sdk) {
    // The container was never created, so the fake service throws NotFound.
    auto link = makeLink(true);
    std::promise<PackageStatus> failed;
    EXPECT_CALL(sdk, onPackageStatusChanged(42, _)).WillOnce(Invoke([&](RaceHandle, PackageStatus s) {
        failed.set_value(s);
        return ChannelResponse{CM_OK, 0};
    }));

    link->enqueueContent(7, payload);
    link->post({42}, 7);

    auto future = failed.get_future();
    ASSERT_TRUE(completes(future));
    EXPECT_EQ(PACKAGE_FAILED_GENERIC, future.get());
}

TEST_F(TestLink, fetch_delivers_new_messages_once_in_order) {
    service->createContainer("box");
    service->post("box", {1});
    service->post("box", {2});
    auto link = makeLink(false);

    std::promise<void> delivered;
    std::vector<std::vector<uint8_t>> received;
    EXPECT_CALL(sdk, onReceive("link-1", _, _))
        .WillRepeatedly(Invoke([&](const LinkID &, const EncodingParameters &params,
                                   const std::vector<uint8_t> &bytes) {
            EXPECT_EQ("link-1", params.linkId);
            EXPECT_FALSE(params.encodePackage);
            received.push_back(bytes);
            if (received.size() == 2) {
                delivered.set_value();
            }
            return ChannelResponse{CM_OK, 0};
        }));

    link->fetch({});
    auto future = delivered.get_future();
    ASSERT_TRUE(completes(future));

    // A second fetch with nothing new must not redeliver.
    link->fetch({});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ASSERT_EQ(2u, received.size());
    EXPECT_EQ(std::vector<uint8_t>{1}, received[0]);
    EXPECT_EQ(std::vector<uint8_t>{2}, received[1]);
}

TEST_F(TestLink, fetch_on_a_missing_container_is_not_an_error) {
    auto link = makeLink(false);
    EXPECT_CALL(sdk, onReceive(_, _, _)).Times(0);
    EXPECT_CALL(sdk, onPackageStatusChanged(_, _)).Times(0);
    link->fetch({});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

TEST_F(TestLink, creator_links_never_fetch_their_own_posts) {
    service->createContainer("box");
    service->post("box", {1});
    auto link = makeLink(true);
    EXPECT_CALL(sdk, onReceive(_, _, _)).Times(0);
    link->fetch({});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}
