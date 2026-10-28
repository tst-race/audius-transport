#ifndef __AUDIUS_AUDIUS_CLIENT_H__
#define __AUDIUS_AUDIUS_CLIENT_H__

#include <string>

#include "ServiceClient.h"

// Reference HTTP implementation of ServiceClient over libcurl. It talks to an *illustrative* REST
// API; replace the endpoints, authentication and payload handling in AudiusClient.cpp with the real
// service's (everything marked TODO(service)). Nothing outside this class knows about HTTP.
//
//   PUT    {server}/containers/{id}                create (idempotent)
//   GET    {server}/containers/{id}                200 = exists, 404 = missing
//   DELETE {server}/containers/{id}
//   POST   {server}/containers/{id}/messages       body = raw bytes -> {"id": "<message id>"}
//   GET    {server}/containers/{id}/messages?after=<id>  -> {"ids": ["<id>", ...]} oldest first
//   GET    {server}/containers/{id}/messages/{mid} -> raw bytes
class AudiusClient : public ServiceClient {
public:
    AudiusClient(std::string serverUrl, std::string accessToken);

    void createContainer(const std::string &containerId) override;
    bool containerExists(const std::string &containerId) override;
    void deleteContainer(const std::string &containerId) override;
    std::string post(const std::string &containerId, const std::vector<uint8_t> &content) override;
    std::vector<ServiceMessage> fetchAfter(const std::string &containerId,
                                           const std::string &afterMessageId) override;

private:
    struct HttpResponse {
        long status = 0;
        std::string body;
    };

    HttpResponse request(const std::string &method, const std::string &path,
                         const std::string &body = "",
                         const std::string &contentType = "application/octet-stream");
    static std::string escape(const std::string &value);
    static void throwForStatus(const HttpResponse &response, const std::string &what);

    std::string serverUrl;
    std::string accessToken;
};

#endif
