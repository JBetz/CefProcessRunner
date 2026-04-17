#pragma once

#include <vector>
#include "include/cef_response_filter.h"

class ResponseBodyFilter : public CefResponseFilter {
 public:
  ResponseBodyFilter();

  bool InitFilter() override;

  FilterStatus Filter(void* data_in,
                      size_t data_in_size,
                      size_t& data_in_read,
                      void* data_out,
                      size_t data_out_size,
                      size_t& data_out_written) override;

  const std::vector<uint8_t>& GetCapturedData() const;

 private:
  std::vector<uint8_t> capturedData;

  IMPLEMENT_REFCOUNTING(ResponseBodyFilter);
  DISALLOW_COPY_AND_ASSIGN(ResponseBodyFilter);
};