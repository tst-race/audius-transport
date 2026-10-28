#ifndef __AUDIUS_LINK_ADDRESS_H__
#define __AUDIUS_LINK_ADDRESS_H__

#include <nlohmann/json.hpp>
#include <string>

// What one node publishes so a peer can find the data it posts. It is exchanged as a JSON string:
//   - returned to the SDK in LinkProperties::linkAddress,
//   - passed back in on the peer via loadLinkAddress() / createLinkFromAddress(),
//   - supplied by the integration-test adapter as --send-address / --recv-address.
// Keep it small, stable, and free of secrets. Service-specific fields go here.
struct LinkAddress {
    // REQUIRED. Id of the service-side container (repo, bucket, channel, hashtag...) the creator
    // posts into and the loader fetches from.
    std::string container;
    // OPTIONAL. Message id to start fetching after ("" = from the beginning of the container).
    std::string initialCursor;
};

void to_json(nlohmann::json &j, const LinkAddress &address);
void from_json(const nlohmann::json &j, LinkAddress &address);

// Parses a JSON link address. Returns false (and leaves `out` untouched) if it is malformed or
// has no container. Unknown fields are ignored so adapters may add informational keys.
bool parseLinkAddress(const std::string &text, LinkAddress &out);

std::string linkAddressToString(const LinkAddress &address);

#endif
