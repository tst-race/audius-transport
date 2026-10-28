#include "AudiusClient.h"

#include <curl/curl.h>

#include <memory>
#include <nlohmann/json.hpp>

namespace {

size_t appendToString(char *data, size_t size, size_t count, void *userdata) {
    static_cast<std::string *>(userdata)->append(data, size * count);
    return size * count;
}

struct CurlHandle {
    CURL *curl = curl_easy_init();
    curl_slist *headers = nullptr;
    ~CurlHandle() {
        curl_slist_free_all(headers);
        if (curl) {
            curl_easy_cleanup(curl);
        }
    }
};

}  // namespace

AudiusClient::AudiusClient(std::string serverUrl, std::string accessToken) :
    serverUrl(std::move(serverUrl)), accessToken(std::move(accessToken)) {
    while (!this->serverUrl.empty() && this->serverUrl.back() == '/') {
        this->serverUrl.pop_back();
    }
}

std::string AudiusClient::escape(const std::string &value) {
    CurlHandle handle;
    char *escaped = curl_easy_escape(handle.curl, value.c_str(), static_cast<int>(value.size()));
    std::string result = escaped ? escaped : "";
    curl_free(escaped);
    return result;
}

AudiusClient::HttpResponse AudiusClient::request(const std::string &method, const std::string &path,
                                             const std::string &body,
                                             const std::string &contentType) {
    CurlHandle handle;
    if (!handle.curl) {
        throw ServiceError(ServiceErrorKind::Fatal, "curl_easy_init failed");
    }

    HttpResponse response;
    curl_easy_setopt(handle.curl, CURLOPT_URL, (serverUrl + path).c_str());
    curl_easy_setopt(handle.curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(handle.curl, CURLOPT_WRITEFUNCTION, appendToString);
    curl_easy_setopt(handle.curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(handle.curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(handle.curl, CURLOPT_TIMEOUT, 60L);
    if (!body.empty()) {
        curl_easy_setopt(handle.curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(handle.curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        handle.headers =
            curl_slist_append(handle.headers, ("Content-Type: " + contentType).c_str());
    }
    // TODO(service): authentication scheme (bearer token, API key header, basic auth, ...).
    if (!accessToken.empty()) {
        handle.headers =
            curl_slist_append(handle.headers, ("Authorization: Bearer " + accessToken).c_str());
    }
    curl_easy_setopt(handle.curl, CURLOPT_HTTPHEADER, handle.headers);

    CURLcode code = curl_easy_perform(handle.curl);
    if (code != CURLE_OK) {
        throw ServiceError(ServiceErrorKind::Transient, curl_easy_strerror(code));
    }
    curl_easy_getinfo(handle.curl, CURLINFO_RESPONSE_CODE, &response.status);
    return response;
}

void AudiusClient::throwForStatus(const HttpResponse &response, const std::string &what) {
    if (response.status >= 200 && response.status < 300) {
        return;
    }
    std::string message = what + " failed with HTTP " + std::to_string(response.status);
    if (response.status == 401 || response.status == 403) {
        throw ServiceError(ServiceErrorKind::Auth, message);
    } else if (response.status == 404) {
        throw ServiceError(ServiceErrorKind::NotFound, message);
    } else if (response.status == 429) {
        throw ServiceError(ServiceErrorKind::RateLimited, message);
    } else if (response.status >= 500) {
        throw ServiceError(ServiceErrorKind::Transient, message);
    }
    throw ServiceError(ServiceErrorKind::Fatal, message);
}

void AudiusClient::createContainer(const std::string &containerId) {
    HttpResponse response = request("PUT", "/containers/" + escape(containerId));
    if (response.status == 409) {
        return;  // already exists: creation is idempotent
    }
    throwForStatus(response, "create container " + containerId);
}

bool AudiusClient::containerExists(const std::string &containerId) {
    HttpResponse response = request("GET", "/containers/" + escape(containerId));
    if (response.status == 404) {
        return false;
    }
    throwForStatus(response, "check container " + containerId);
    return true;
}

void AudiusClient::deleteContainer(const std::string &containerId) {
    HttpResponse response = request("DELETE", "/containers/" + escape(containerId));
    if (response.status == 404) {
        return;
    }
    throwForStatus(response, "delete container " + containerId);
}

std::string AudiusClient::post(const std::string &containerId, const std::vector<uint8_t> &content) {
    // TODO(service): wrap/encode the bytes if the service only accepts text or a particular file
    // type. (The *content* encoding itself is the encoding component's job, not the transport's.)
    HttpResponse response = request("POST", "/containers/" + escape(containerId) + "/messages",
                                    std::string(content.begin(), content.end()));
    throwForStatus(response, "post to " + containerId);
    try {
        return nlohmann::json::parse(response.body).at("id").get<std::string>();
    } catch (const nlohmann::json::exception &error) {
        throw ServiceError(ServiceErrorKind::Fatal,
                           std::string("unexpected post response: ") + error.what());
    }
}

std::vector<ServiceMessage> AudiusClient::fetchAfter(const std::string &containerId,
                                                   const std::string &afterMessageId) {
    std::string base = "/containers/" + escape(containerId) + "/messages";
    HttpResponse listing = request("GET", base + "?after=" + escape(afterMessageId));
    if (listing.status == 404) {
        return {};  // creator has not created the container yet; retry on the next fetch
    }
    throwForStatus(listing, "list " + containerId);

    std::vector<ServiceMessage> messages;
    try {
        // Keep the parsed JSON alive for the loop (iterating a temporary's member dangles).
        nlohmann::json ids = nlohmann::json::parse(listing.body).at("ids");
        for (const auto &id : ids) {
            HttpResponse item = request("GET", base + "/" + escape(id.get<std::string>()));
            throwForStatus(item, "download " + id.get<std::string>());
            messages.push_back({id.get<std::string>(), {item.body.begin(), item.body.end()}});
        }
    } catch (const nlohmann::json::exception &error) {
        throw ServiceError(ServiceErrorKind::Fatal,
                           std::string("unexpected list response: ") + error.what());
    }
    return messages;
}
