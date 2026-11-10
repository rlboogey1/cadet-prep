#pragma once
// A tiny HTTPS client for online sync. On the Mac, iPhone and iPad it uses Apple's
// NSURLSession (Http.mm); elsewhere every request fails with "not supported".
// Requests block, so call them from a background thread. A web page can't block:
// the website version uses requestAsync (the browser's fetch) instead.
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace http {

struct Response {
    int status = 0;     // HTTP status, or 0 if the request never reached the server
    std::string body;
    std::string error;  // set when status is 0 (no connection, timeout, ...)
    bool ok() const { return status >= 200 && status < 300; }
};

using Headers = std::vector<std::pair<std::string, std::string>>;

Response request(const std::string& method, const std::string& url, const Headers& headers,
                 const std::string& body = "");

#if defined(__EMSCRIPTEN__)
// Doesn't wait: `done` is called later, on the main thread, with the answer.
void requestAsync(const std::string& method, const std::string& url, const Headers& headers,
                  const std::string& body, std::function<void(const Response&)> done);
#endif

}  // namespace http
