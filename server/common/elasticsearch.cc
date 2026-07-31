#include "elasticsearch.h"

#include "util.h"

#ifdef VOD_ENABLE_REFERENCE_RUNTIME

#include <curl/curl.h>

#include <utility>

namespace bitesearch {
namespace {

std::size_t appendBody(char* data, std::size_t size,
                       std::size_t count, void* target) {
    const std::size_t bytes = size * count;
    static_cast<std::string*>(target)->append(data, bytes);
    return bytes;
}

std::string trimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

}  // namespace

ElasticsearchVideoIndex::ElasticsearchVideoIndex(
    biteconfig::ElasticsearchSettings settings)
    : settings_(std::move(settings)) {
    settings_.endpoint = trimTrailingSlash(settings_.endpoint);
}

bool ElasticsearchVideoIndex::request(const std::string& method,
                                       const std::string& path,
                                       const std::string& body,
                                       long& status,
                                       Json::Value& response,
                                       std::string& error) const {
    static const int initialized = [] {
        return curl_global_init(CURL_GLOBAL_DEFAULT);
    }();
    if (initialized != CURLE_OK) {
        error = "cannot initialize libcurl";
        return false;
    }
    CURL* handle = curl_easy_init();
    if (!handle) {
        error = "cannot create Elasticsearch client";
        return false;
    }
    const std::string url = settings_.endpoint + path;
    std::string responseBody;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, appendBody);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS,
                     static_cast<long>(settings_.timeoutMs));
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS,
                     static_cast<long>(settings_.timeoutMs));
    if (!body.empty()) {
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(body.size()));
    }
    const CURLcode code = curl_easy_perform(handle);
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);
    if (code != CURLE_OK) {
        error = std::string("Elasticsearch request failed: ") +
            curl_easy_strerror(code);
        return false;
    }
    if (!responseBody.empty()) {
        const auto parsed = biteutil::JSON::unserialize(responseBody);
        if (!parsed || !parsed->isObject()) {
            error = "Elasticsearch returned invalid JSON";
            return false;
        }
        response = *parsed;
    } else {
        response = Json::Value(Json::objectValue);
    }
    if (status < 200 || status >= 300) {
        error = "Elasticsearch returned HTTP " + std::to_string(status);
        if (response["error"].isObject() &&
            response["error"]["reason"].isString()) {
            error += ": " + response["error"]["reason"].asString();
        }
        return false;
    }
    return true;
}

std::string ElasticsearchVideoIndex::escapedPathSegment(
    const std::string& value) const {
    CURL* handle = curl_easy_init();
    if (!handle) return value;
    char* escaped = curl_easy_escape(handle, value.data(), value.size());
    const std::string result = escaped ? escaped : value;
    if (escaped) curl_free(escaped);
    curl_easy_cleanup(handle);
    return result;
}

bool ElasticsearchVideoIndex::ensureIndex(std::string& error) {
    long status = 0;
    Json::Value response;
    if (request("HEAD", "/" + settings_.indexAlias, "", status,
                response, error)) {
        return true;
    }
    if (status != 404) return false;

    Json::Value mapping;
    mapping["mappings"]["dynamic"] = "strict";
    mapping["mappings"]["properties"]["video_id"]["type"] = "keyword";
    mapping["mappings"]["properties"]["title"]["type"] = "text";
    mapping["mappings"]["properties"]["description"]["type"] = "text";
    mapping["mappings"]["properties"]["category"]["type"] = "keyword";
    mapping["mappings"]["properties"]["user_name"]["type"] = "keyword";
    mapping["mappings"]["properties"]["published_on"]["type"] = "date";
    mapping["aliases"][settings_.indexAlias] = Json::Value(Json::objectValue);
    const auto body = biteutil::JSON::serialize(mapping);
    if (!body) {
        error = "cannot serialize Elasticsearch mapping";
        return false;
    }
    error.clear();
    return request("PUT", "/vod_videos_v1", *body, status, response, error);
}

bool ElasticsearchVideoIndex::upsert(const bitevideo::Video& video,
                                     std::string& error) {
    Json::Value document;
    document["video_id"] = video.id;
    document["title"] = video.title;
    document["description"] = video.description;
    document["category"] = video.category;
    document["user_name"] = video.userName;
    document["published_on"] = video.date;
    const auto body = biteutil::JSON::serialize(document);
    if (!body) {
        error = "cannot serialize video search document";
        return false;
    }
    long status = 0;
    Json::Value response;
    return request("PUT", "/" + settings_.indexAlias + "/_doc/" +
                       escapedPathSegment(video.id) + "?refresh=wait_for",
                   *body, status, response, error);
}

bool ElasticsearchVideoIndex::remove(const std::string& videoId,
                                     std::string& error) {
    long status = 0;
    Json::Value response;
    if (request("DELETE", "/" + settings_.indexAlias + "/_doc/" +
                    escapedPathSegment(videoId) + "?refresh=wait_for",
                "", status, response, error)) {
        return true;
    }
    return status == 404;
}

bool ElasticsearchVideoIndex::searchIds(
    const std::string& keyword,
    std::vector<std::string>& videoIds,
    std::string& error) {
    videoIds.clear();
    Json::Value body;
    body["size"] = 100;
    body["query"]["multi_match"]["query"] = keyword;
    body["query"]["multi_match"]["fields"].append("title^3");
    body["query"]["multi_match"]["fields"].append("description");
    body["query"]["multi_match"]["fields"].append("category");
    body["sort"].append("_score");
    body["sort"].append(Json::Value(Json::objectValue));
    body["sort"][1]["published_on"] = "desc";
    body["_source"] = false;
    const auto serialized = biteutil::JSON::serialize(body);
    if (!serialized) {
        error = "cannot serialize Elasticsearch query";
        return false;
    }
    long status = 0;
    Json::Value response;
    if (!request("POST", "/" + settings_.indexAlias + "/_search",
                 *serialized, status, response, error)) {
        return false;
    }
    const Json::Value& hits = response["hits"]["hits"];
    if (!hits.isArray()) {
        error = "Elasticsearch response is missing hits";
        return false;
    }
    for (const auto& hit : hits) {
        if (hit["_id"].isString()) videoIds.push_back(hit["_id"].asString());
    }
    return true;
}

}  // namespace bitesearch

#endif
