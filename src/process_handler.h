// Copyright (c) 2013 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#pragma once

#include <vector>

#include "include/cef_app.h"

// Base class for customizing process-type-based behavior.
class ProcessHandler : public CefApp, CefClient {
 public:
  ProcessHandler();

  enum ProcessType {
    BrowserProcess,
    RendererProcess,
    ZygoteProcess,
    OtherProcess,
  };

  // Determine the process type based on command-line arguments.
  static ProcessType GetProcessType(CefRefPtr<CefCommandLine> command_line);
  static std::string ProcessTypeToString(ProcessType type);

 private:
  // Registers custom schemes. Implemented by cefclient in
  // client_app_delegates_common.cc
  static void RegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar);

  void OnRegisterCustomSchemes(
      CefRawPtr<CefSchemeRegistrar> registrar) override;

  DISALLOW_COPY_AND_ASSIGN(ProcessHandler);
};