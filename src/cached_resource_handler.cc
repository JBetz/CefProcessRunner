#include "cached_resource_handler.h"
#include <algorithm>
#include <windows.h>

CachedResourceHandler::CachedResourceHandler(
    const CachedResourceResponse& cachedResponse)
    : mimeType(cachedResponse.mimeType),
      statusCode(cachedResponse.statusCode),
      responseHeaders(cachedResponse.responseHeaders),
      offset(0) {
  HANDLE handle =
      reinterpret_cast<HANDLE>(cachedResponse.sharedMemoryHandle);
  int size = cachedResponse.sharedMemorySize;
  if (handle && size > 0) {
    void* mappedView = MapViewOfFile(handle, FILE_MAP_READ, 0, 0, size);
    if (mappedView) {
      const uint8_t* bytes = static_cast<const uint8_t*>(mappedView);
      data.assign(bytes, bytes + size);
      UnmapViewOfFile(mappedView);
    }
    CloseHandle(handle);
  }
}

CachedResourceHandler::~CachedResourceHandler() {}

bool CachedResourceHandler::Open(CefRefPtr<CefRequest> request,
                                 bool& handle_request,
                                 CefRefPtr<CefCallback> callback) {
  handle_request = true;
  return true;
}

void CachedResourceHandler::GetResponseHeaders(CefRefPtr<CefResponse> response,
                                               int64_t& response_length,
                                               CefString& redirectUrl) {
  response->SetMimeType(mimeType);
  response->SetStatus(statusCode);
  CefResponse::HeaderMap headerMap;
  for (const auto& [key, value] : responseHeaders) {
    headerMap.insert(std::make_pair(key, value));
  }
  response->SetHeaderMap(headerMap);
  response_length = static_cast<int64_t>(data.size());
}

bool CachedResourceHandler::Read(void* data_out,
                                 int bytes_to_read,
                                 int& bytes_read,
                                 CefRefPtr<CefResourceReadCallback> callback) {
  if (offset >= data.size()) {
    bytes_read = 0;
    return false;
  }
  int available = static_cast<int>(data.size() - offset);
  bytes_read = std::min(bytes_to_read, available);
  memcpy(data_out, data.data() + offset, bytes_read);
  offset += bytes_read;
  return true;
}

void CachedResourceHandler::Cancel() {}