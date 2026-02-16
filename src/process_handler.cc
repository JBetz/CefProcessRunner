// Copyright (c) 2013 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "process_handler.h"

#include "include/cef_command_line.h"

namespace {

// These flags must match the Chromium values.
const char kProcessType[] = "type";
const char kRendererProcess[] = "renderer";
#if defined(OS_LINUX)
const char kZygoteProcess[] = "zygote";
#endif

}  // namespace

ProcessHandler::ProcessHandler() {}

// static
ProcessHandler::ProcessType ProcessHandler::GetProcessType(
    CefRefPtr<CefCommandLine> command_line) {
  // The command-line flag won't be specified for the browser process.
  if (!command_line->HasSwitch(kProcessType)) {
    return BrowserProcess;
  }

  const std::string& process_type = command_line->GetSwitchValue(kProcessType);
  if (process_type == kRendererProcess) {
    return RendererProcess;
  }
#if defined(OS_LINUX)
  else if (process_type == kZygoteProcess) {
    return ZygoteProcess;
  }
#endif

  return OtherProcess;
}

std::string ProcessHandler::ProcessTypeToString(
    ProcessHandler::ProcessType type) {
  switch (type) {
    case BrowserProcess:
      return "Browser";
    case RendererProcess:
      return "Renderer";
    case ZygoteProcess:
      return "Zygote";
    case OtherProcess:
      return "Other";
    default:
      return "Unknown";
  }
}

void ProcessHandler::OnRegisterCustomSchemes(
    CefRawPtr<CefSchemeRegistrar> registrar) {}