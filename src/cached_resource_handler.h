#pragma once

#include <string>
#include <vector>
#include "include/cef_resource_handler.h"
#include "rpc.hpp"

class CachedResourceHandler : public CefResourceHandler {
 public:
  CachedResourceHandler(const CachedResourceResponse& cachedResponse);
  ~CachedResourceHandler();

  bool Open(CefRefPtr<CefRequest> request,
            bool& handle_request,
            CefRefPtr<CefCallback> callback) override;

  void GetResponseHeaders(CefRefPtr<CefResponse> response,
                          int64_t& response_length,
                          CefString& redirectUrl) override;

  bool Read(void* data_out,
            int bytes_to_read,
            int& bytes_read,
            CefRefPtr<CefResourceReadCallback> callback) override;

  void Cancel() override;

 private:
  std::string mimeType;
  int statusCode;
  std::map<std::string, std::string> responseHeaders;
  std::vector<uint8_t> data;
  size_t offset = 0;

  IMPLEMENT_REFCOUNTING(CachedResourceHandler);
  DISALLOW_COPY_AND_ASSIGN(CachedResourceHandler);
};