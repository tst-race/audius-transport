#ifndef __AUDIUS_SERVICE_CLIENT_H__
#define __AUDIUS_SERVICE_CLIENT_H__

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------
// Phase 3 ("service interaction") boundary.
//
// ServiceClient is the ONLY place that talks to the third-party service. Link and PluginAudius use
// nothing but this interface, so the service code can be developed and tested on its own (see
// PHASE3) and replaced by an in-memory fake in unit tests (test/source/common/InMemoryServiceClient.h).
//
// The model is deliberately small: a "container" (repo, bucket, channel, hashtag, ...) holding an
// ordered list of immutable messages. Map your service onto it:
//   createContainer  -> create repo / bucket / channel / account
//   post             -> upload one object / commit / post / file; returns its service-assigned id
//   fetchAfter       -> list + download objects newer than a cursor (oldest first)
//   deleteContainer  -> delete what createContainer created (only the creator side calls this)
// ---------------------------------------------------------------------------------------------

enum class ServiceErrorKind { Transient, NotFound, Auth, RateLimited, Fatal };

class ServiceError : public std::runtime_error {
public:
    ServiceError(ServiceErrorKind kind, const std::string &what) :
        std::runtime_error(what), kind(kind) {}
    ServiceErrorKind kind;
};

struct ServiceMessage {
    std::string id;  // service-assigned, used as the fetch cursor
    std::vector<uint8_t> content;
};

class ServiceClient {
public:
    virtual ~ServiceClient() = default;

    // Idempotent: succeeds if the container already exists and is usable.
    virtual void createContainer(const std::string &containerId) = 0;
    virtual bool containerExists(const std::string &containerId) = 0;
    virtual void deleteContainer(const std::string &containerId) = 0;

    // Returns the new message id. Throws ServiceError on failure.
    virtual std::string post(const std::string &containerId,
                             const std::vector<uint8_t> &content) = 0;

    // Messages strictly newer than afterMessageId ("" = from the beginning), oldest first.
    // A missing container is NOT an error here: return an empty list (the creator may not have
    // created it yet) so the next fetch simply retries.
    virtual std::vector<ServiceMessage> fetchAfter(const std::string &containerId,
                                                   const std::string &afterMessageId) = 0;
};

#endif
