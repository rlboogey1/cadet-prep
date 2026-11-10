// HTTPS requests with NSURLSession (Mac, iPhone, iPad). See Http.h.
#include "Http.h"

#if defined(__APPLE__)
#import <Foundation/Foundation.h>

namespace http {

Response request(const std::string& method, const std::string& url, const Headers& headers,
                 const std::string& body) {
    Response out;
    @autoreleasepool {
        NSURL* u = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
        if (!u) {
            out.error = "Bad server address";
            return out;
        }
        NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:u
                                                           cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                       timeoutInterval:20];
        req.HTTPMethod = [NSString stringWithUTF8String:method.c_str()];
        for (const auto& [name, value] : headers)
            [req setValue:[NSString stringWithUTF8String:value.c_str()]
                forHTTPHeaderField:[NSString stringWithUTF8String:name.c_str()]];
        if (!body.empty()) req.HTTPBody = [NSData dataWithBytes:body.data() length:body.size()];

        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        Response* result = &out;
        NSURLSessionDataTask* task = [[NSURLSession sharedSession]
            dataTaskWithRequest:req
              completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                if (error) {
                    const char* why = error.localizedDescription.UTF8String;
                    result->error = why ? why : "Network error";
                } else {
                    result->status = static_cast<int>(((NSHTTPURLResponse*)response).statusCode);
                    if (data) result->body.assign(static_cast<const char*>(data.bytes), data.length);
                }
                dispatch_semaphore_signal(done);
              }];
        [task resume];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    }
    return out;
}

}  // namespace http

#elif defined(__EMSCRIPTEN__)
// The website: the browser's fetch(). It answers later, as an event, so there's
// only requestAsync here.
#include <emscripten.h>
#include <map>
#include <nlohmann/json.hpp>

namespace {

std::map<int, std::function<void(const http::Response&)>> gWaiting;  // requests in flight, by id
int gNextId = 1;

EM_JS_DEPS(cadet_fetch_deps, "$UTF8ToString,$stringToNewUTF8");
EM_JS(void, cadetFetch, (int id, const char* method, const char* url, const char* headers, const char* body), {
    var init = { method: UTF8ToString(method), headers: JSON.parse(UTF8ToString(headers)), cache: 'no-store' };
    var text = UTF8ToString(body);
    if (text) init.body = text;
    function finish(status, answer) {
        var p = stringToNewUTF8(answer);
        Module._cadet_http_done(id, status, p);
        _free(p);
    }
    fetch(UTF8ToString(url), init)
        .then(function (r) { return r.text().then(function (t) { finish(r.status, t); }); })
        .catch(function () { finish(0, "Can't reach the server"); });
});

}  // namespace

// Called by the page when a request finishes (status 0 = it never reached the server)
extern "C" EMSCRIPTEN_KEEPALIVE void cadet_http_done(int id, int status, const char* text) {
    auto it = gWaiting.find(id);
    if (it == gWaiting.end()) return;
    auto done = std::move(it->second);
    gWaiting.erase(it);
    http::Response r;
    r.status = status;
    if (status == 0) r.error = text;
    else r.body = text;
    done(r);
}

namespace http {

Response request(const std::string&, const std::string&, const Headers&, const std::string&) {
    Response out;
    out.error = "Use requestAsync in the browser";
    return out;
}

void requestAsync(const std::string& method, const std::string& url, const Headers& headers,
                  const std::string& body, std::function<void(const Response&)> done) {
    nlohmann::json h = nlohmann::json::object();
    for (const auto& [name, value] : headers) h[name] = value;
    const int id = gNextId++;
    gWaiting[id] = std::move(done);
    cadetFetch(id, method.c_str(), url.c_str(), h.dump().c_str(), body.c_str());
}

}  // namespace http

#else

namespace http {

Response request(const std::string&, const std::string&, const Headers&, const std::string&) {
    Response out;
    out.error = "Online sync isn't supported on this system";
    return out;
}

}  // namespace http

#endif
