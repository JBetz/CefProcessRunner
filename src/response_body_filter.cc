#include "response_body_filter.h"
#include <algorithm>
#include <cstring>

ResponseBodyFilter::ResponseBodyFilter() {}

bool ResponseBodyFilter::InitFilter() {
  return true;
}

CefResponseFilter::FilterStatus ResponseBodyFilter::Filter(
    void* data_in,
    size_t data_in_size,
    size_t& data_in_read,
    void* data_out,
    size_t data_out_size,
    size_t& data_out_written) {
  if (data_in_size == 0) {
    data_in_read = 0;
    data_out_written = 0;
    return RESPONSE_FILTER_DONE;
  }

  // Capture the incoming data
  const uint8_t* bytes = static_cast<const uint8_t*>(data_in);
  capturedData.insert(capturedData.end(), bytes, bytes + data_in_size);
  data_in_read = data_in_size;

  // Pass through to output unmodified
  size_t to_write = std::min(data_in_size, data_out_size);
  memcpy(data_out, data_in, to_write);
  data_out_written = to_write;

  if (to_write < data_in_size) {
    return RESPONSE_FILTER_NEED_MORE_DATA;
  }
  return RESPONSE_FILTER_NEED_MORE_DATA;
}

const std::vector<uint8_t>& ResponseBodyFilter::GetCapturedData() const {
  return capturedData;
}