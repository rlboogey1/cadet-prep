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

#else

namespace http {

Response request(const std::string&, const std::string&, const Headers&, const std::string&) {
    Response out;
    out.error = "Online sync isn't supported on this system";
    return out;
}

}  // namespace http

#endif
