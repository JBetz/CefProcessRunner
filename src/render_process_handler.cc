// Copyright (c) 2013 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include <optional>

#include "render_process_handler.h"

#include <SDL3/sdl.h>
#include <rpc.h>
#include <map>
#include <string>
#include "include/base/cef_logging.h"
#include "json.hpp"
#include "rpc.hpp"

using json = nlohmann::json;

const char kOnFocusMessage[] = "RenderProcessHandler.OnFocus";
const char kOnFocusOutMessage[] = "RenderProcessHandler.OnFocusOut";
const char kOnMouseOverMessage[] = "RenderProcessHandler.OnMouseOver";
const char kOnHistoryMessage[] = "RenderProcessHandler.OnHistory";
const char kOnNavigationMessage[] = "RenderProcessHandler.OnNavigation";
const char kOnMessageMessage[] = "RenderProcessHandler.OnMessage";
const char kOnEvalMessage[] = "RenderProcessHandler.OnEval";

RenderProcessHandler::RenderProcessHandler() {}

CefRefPtr<CefRenderProcessHandler>
RenderProcessHandler::GetRenderProcessHandler() {
  return this;
}

void RenderProcessHandler::OnWebKitInitialized() {}

void RenderProcessHandler::OnBrowserCreated(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDictionaryValue> extra_info) {}

void RenderProcessHandler::OnBrowserDestroyed(CefRefPtr<CefBrowser> browser) {}

CefRefPtr<CefLoadHandler> RenderProcessHandler::GetLoadHandler() {
  return nullptr;
}

class MouseOverHandler : public CefV8Handler {
 public:
  MouseOverHandler() {}

  virtual bool Execute(const CefString& name,
                       CefRefPtr<CefV8Value> object,
                       const CefV8ValueList& arguments,
                       CefRefPtr<CefV8Value>& retval,
                       CefString& exception) override {
    CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
    CefRefPtr<CefFrame> frame = context->GetFrame();
    CefRefPtr<CefV8Value> event = arguments.front();
    CefV8ValueList stopPropagationArguments;
    event->GetValue("stopPropagation")
        ->ExecuteFunction(event, stopPropagationArguments);
    CefRefPtr<CefV8Value> target = event->GetValue("target");

    RpcRequest request;
    request.id = CreateUuid();
    request.className = "Browser";
    request.methodName = "OnMouseOver";
    request.instanceId = frame->GetBrowser()->GetIdentifier();
    Browser_OnMouseOver mouseOverArguments;
    mouseOverArguments.tagName =
        target->GetValue("tagName")->GetStringValue().ToString();
    if (mouseOverArguments.tagName == "INPUT") {
      mouseOverArguments.inputType =
          target->GetValue("type")->GetStringValue().ToString();
    }
    CefV8ValueList closestArguments;
    closestArguments.push_back(CefV8Value::CreateString("a"));
    CefRefPtr<CefV8Value> closestResult =
        target->GetValue("closest")->ExecuteFunction(target, closestArguments);
    CefV8ValueList getBoundingClientRectArguments;
    CefRefPtr<CefV8Value> rectangle;
    if (closestResult->IsObject()) {
      std::string href =
          closestResult->GetValue("href")->GetStringValue().ToString();
      if (!href.empty()) {
        mouseOverArguments.href = href;
      }
      rectangle =
          closestResult->GetValue("getBoundingClientRect")
              ->ExecuteFunction(closestResult, getBoundingClientRectArguments);
    } else {
      rectangle = target->GetValue("getBoundingClientRect")
                      ->ExecuteFunction(target, getBoundingClientRectArguments);
    }
    mouseOverArguments.rectangle.x =
        rectangle->GetValue("left")->GetDoubleValue();
    mouseOverArguments.rectangle.y =
        rectangle->GetValue("top")->GetDoubleValue();
    mouseOverArguments.rectangle.width =
        rectangle->GetValue("right")->GetDoubleValue() -
        mouseOverArguments.rectangle.x;
    mouseOverArguments.rectangle.height =
        rectangle->GetValue("bottom")->GetDoubleValue() -
        mouseOverArguments.rectangle.y;

    request.arguments = mouseOverArguments;
    json jsonRequest = request;
    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create(kOnMouseOverMessage);
    message->GetArgumentList()->SetString(0, jsonRequest.dump());
    frame->SendProcessMessage(PID_BROWSER, message);
    return true;
  }

  // Provide the reference counting implementation for this class.
  IMPLEMENT_REFCOUNTING(MouseOverHandler);
};

class MessageHandler : public CefV8Handler {
 public:
  MessageHandler() {}

  virtual bool Execute(const CefString& name,
                       CefRefPtr<CefV8Value> object,
                       const CefV8ValueList& arguments,
                       CefRefPtr<CefV8Value>& retval,
                       CefString& exception) override {
    CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
    CefRefPtr<CefV8Value> window = context->GetGlobal();
    CefRefPtr<CefFrame> frame = context->GetFrame();
    CefRefPtr<CefV8Value> event = arguments.front();

    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create(kOnMessageMessage);

    CefRefPtr<CefDictionaryValue> messageArguments =
        CefDictionaryValue::Create();
    CefString source = event->GetValue("source")->GetStringValue();
    messageArguments->SetString("source", source);
    CefRefPtr<CefV8Value> data = event->GetValue("data");

    CefRefPtr<CefV8Value> json = window->GetValue("JSON");
    CefRefPtr<CefV8Value> stringifyFunction = json->GetValue("stringify");
    CefV8ValueList stringifyArguments;
    stringifyArguments.push_back(data);
    const CefString& stringifiedData =
        stringifyFunction->ExecuteFunction(json, stringifyArguments)
            ->GetStringValue();
    messageArguments->SetString("data", stringifiedData);
    message->GetArgumentList()->SetDictionary(0, messageArguments);
    frame->SendProcessMessage(PID_BROWSER, message);
    return true;
  }

  // Provide the reference counting implementation for this class.
  IMPLEMENT_REFCOUNTING(MessageHandler);
};

class HistoryHandler : public CefV8Handler {
 public:
  HistoryHandler() {}

  virtual bool Execute(const CefString& name,
                       CefRefPtr<CefV8Value> object,
                       const CefV8ValueList& arguments,
                       CefRefPtr<CefV8Value>& retval,
                       CefString& exception) override {
    CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
    CefRefPtr<CefFrame> frame = context->GetFrame();

    RpcRequest request;
    request.id = CreateUuid();
    request.className = "Browser";
    request.instanceId = frame->GetBrowser()->GetIdentifier();

    if (name == "onHistoryPushState") {
      request.methodName = "OnHistoryPushState";
      Browser_OnHistoryPushState args;
      args.url = arguments[0]->GetStringValue().ToString();
      args.state = arguments[1]->GetStringValue().ToString();
      request.arguments = args;
    } else if (name == "onHistoryReplaceState") {
      request.methodName = "OnHistoryReplaceState";
      Browser_OnHistoryReplaceState args;
      args.url = arguments[0]->GetStringValue().ToString();
      args.state = arguments[1]->GetStringValue().ToString();
      request.arguments = args;
    } else if (name == "onHistoryGo") {
      request.methodName = "OnHistoryGo";
      Browser_OnHistoryGo args;
      args.delta = arguments[0]->GetIntValue();
      request.arguments = args;
    } else {
      return false;
    }

    json jsonRequest = request;
    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create(kOnHistoryMessage);
    message->GetArgumentList()->SetString(0, jsonRequest.dump());
    frame->SendProcessMessage(PID_BROWSER, message);
    return true;
  }

  IMPLEMENT_REFCOUNTING(HistoryHandler);
};

class NavigationHandler : public CefV8Handler {
 public:
  NavigationHandler() {}

  virtual bool Execute(const CefString& name,
                       CefRefPtr<CefV8Value> object,
                       const CefV8ValueList& arguments,
                       CefRefPtr<CefV8Value>& retval,
                       CefString& exception) override {
    CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
    CefRefPtr<CefFrame> frame = context->GetFrame();

    RpcRequest request;
    request.id = CreateUuid();
    request.className = "Browser";
    request.instanceId = frame->GetBrowser()->GetIdentifier();

    if (name == "onNavigationNavigate") {
      request.methodName = "OnNavigationNavigate";
      Browser_OnNavigationNavigate args;
      args.url = arguments[0]->GetStringValue().ToString();
      args.state = arguments[1]->GetStringValue().ToString();
      args.info = arguments[2]->GetStringValue().ToString();
      args.history = arguments[3]->GetStringValue().ToString();
      request.arguments = args;
    } else if (name == "onNavigationBack") {
      request.methodName = "OnNavigationBack";
      Browser_OnNavigationBack args;
      args.info = arguments[0]->GetStringValue().ToString();
      request.arguments = args;
    } else if (name == "onNavigationForward") {
      request.methodName = "OnNavigationForward";
      Browser_OnNavigationForward args;
      args.info = arguments[0]->GetStringValue().ToString();
      request.arguments = args;
    } else if (name == "onNavigationTraverseTo") {
      request.methodName = "OnNavigationTraverseTo";
      Browser_OnNavigationTraverseTo args;
      args.key = arguments[0]->GetStringValue().ToString();
      args.info = arguments[1]->GetStringValue().ToString();
      request.arguments = args;
    } else {
      return false;
    }

    json jsonRequest = request;
    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create(kOnNavigationMessage);
    message->GetArgumentList()->SetString(0, jsonRequest.dump());
    frame->SendProcessMessage(PID_BROWSER, message);
    return true;
  }

  IMPLEMENT_REFCOUNTING(NavigationHandler);
};

class FocusOutHandler : public CefV8Handler {
 public:
  FocusOutHandler() {}

  virtual bool Execute(const CefString& name,
                       CefRefPtr<CefV8Value> object,
                       const CefV8ValueList& arguments,
                       CefRefPtr<CefV8Value>& retval,
                       CefString& exception) override {
    CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
    CefRefPtr<CefFrame> frame = context->GetFrame();
    CefRefPtr<CefV8Value> event = arguments.front();
    CefRefPtr<CefV8Value> relatedTarget = event->GetValue("relatedTarget");

    RpcRequest request;
    request.id = CreateUuid();
    request.className = "Browser";
    request.methodName = "OnFocusOut";
    request.instanceId = frame->GetBrowser()->GetIdentifier();
    Browser_FocusOut focusOutArguments;
    if (!relatedTarget->IsNull()) {
      focusOutArguments.tagName =
          relatedTarget->GetValue("tagName")->GetStringValue();
      CefRefPtr<CefV8Value> attributes = relatedTarget->GetValue("attributes");
      std::string inputType = attributes->GetValue("type")->GetStringValue();
      if (!inputType.empty()) {
        focusOutArguments.inputType = inputType;
      }
      focusOutArguments.isEditable =
          attributes->GetValue("isEditable")->GetBoolValue();
    }
    request.arguments = focusOutArguments;
    json jsonRequest = request;
    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create(kOnFocusOutMessage);
    message->GetArgumentList()->SetString(0, jsonRequest.dump());
    frame->SendProcessMessage(PID_BROWSER, message);
    return true;
  }

  // Provide the reference counting implementation for this class.
  IMPLEMENT_REFCOUNTING(FocusOutHandler);
};

void RenderProcessHandler::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                            CefRefPtr<CefFrame> frame,
                                            CefRefPtr<CefV8Context> context) {
  CefRefPtr<CefV8Value> window = context->GetGlobal();

  // mouse over
  CefRefPtr<CefV8Handler> mouseOverHandler = new MouseOverHandler();
  CefV8ValueList mouseOverArguments;
  mouseOverArguments.push_back(CefV8Value::CreateString("mouseover"));
  mouseOverArguments.push_back(
      CefV8Value::CreateFunction("onMouseOver", mouseOverHandler));
  window->GetValue("addEventListener")
      ->ExecuteFunction(window, mouseOverArguments);

  // history API shim
  CefRefPtr<CefV8Handler> historyHandler = new HistoryHandler();
  CefRefPtr<CefV8Value> onHistoryPushState =
      CefV8Value::CreateFunction("onHistoryPushState", historyHandler);
  CefRefPtr<CefV8Value> onHistoryReplaceState =
      CefV8Value::CreateFunction("onHistoryReplaceState", historyHandler);
  CefRefPtr<CefV8Value> onHistoryGo =
      CefV8Value::CreateFunction("onHistoryGo", historyHandler);

  std::string historyShimScript = R"(
    (function(onHistoryPushState, onHistoryReplaceState, onHistoryGo) {
      let _state = history.state;
      Object.defineProperty(history, 'state', { get: () => _state });
      Object.defineProperty(history, '_setState', {
        value: function(state) { _state = state; },
        enumerable: false
      });
      history.pushState = function(state, title, url) {
        _state = state;
        onHistoryPushState(String(url || ''), JSON.stringify(state));
      };
      history.replaceState = function(state, title, url) {
        _state = state;
        onHistoryReplaceState(String(url || ''), JSON.stringify(state));
      };
      history.back = function() { onHistoryGo(-1); };
      history.forward = function() { onHistoryGo(1); };
      history.go = function(delta) { onHistoryGo(delta || 0); };
    })
  )";

  CefRefPtr<CefV8Value> historyShimFunction;
  CefRefPtr<CefV8Exception> historyShimException;
  bool historyShimSuccess =
      context->Eval(CefString(historyShimScript), CefString(""), 0,
                    historyShimFunction, historyShimException);
  if (historyShimSuccess && historyShimFunction->IsFunction()) {
    CefV8ValueList historyShimArgs;
    historyShimArgs.push_back(onHistoryPushState);
    historyShimArgs.push_back(onHistoryReplaceState);
    historyShimArgs.push_back(onHistoryGo);
    historyShimFunction->ExecuteFunction(nullptr, historyShimArgs);
  }

  // navigation API shim
  CefRefPtr<CefV8Value> navigation = window->GetValue("navigation");
  if (navigation->IsObject()) {
    CefRefPtr<CefV8Handler> navigationHandler = new NavigationHandler();
    CefRefPtr<CefV8Value> onNavigationNavigate =
        CefV8Value::CreateFunction("onNavigationNavigate", navigationHandler);
    CefRefPtr<CefV8Value> onNavigationBack =
        CefV8Value::CreateFunction("onNavigationBack", navigationHandler);
    CefRefPtr<CefV8Value> onNavigationForward =
        CefV8Value::CreateFunction("onNavigationForward", navigationHandler);
    CefRefPtr<CefV8Value> onNavigationTraverseTo =
        CefV8Value::CreateFunction("onNavigationTraverseTo", navigationHandler);

    std::string navigationShimScript = R"(
      (function(onNavigationNavigate, onNavigationBack, onNavigationForward, onNavigationTraverseTo) {
        const resolved = { committed: Promise.resolve(), finished: Promise.resolve() };
        navigation.navigate = function(url, options) {
          var opts = options || {};
          onNavigationNavigate(
            String(url || ''),
            JSON.stringify(opts.state === undefined ? null : opts.state),
            JSON.stringify(opts.info === undefined ? null : opts.info),
            opts.history || 'auto'
          );
          return resolved;
        };
        navigation.back = function(options) {
          var opts = options || {};
          onNavigationBack(JSON.stringify(opts.info === undefined ? null : opts.info));
          return resolved;
        };
        navigation.forward = function(options) {
          var opts = options || {};
          onNavigationForward(JSON.stringify(opts.info === undefined ? null : opts.info));
          return resolved;
        };
        navigation.traverseTo = function(key, options) {
          var opts = options || {};
          onNavigationTraverseTo(
            String(key || ''),
            JSON.stringify(opts.info === undefined ? null : opts.info)
          );
          return resolved;
        };
      })
    )";

    CefRefPtr<CefV8Value> navigationShimFunction;
    CefRefPtr<CefV8Exception> navigationShimException;
    bool navigationShimSuccess =
        context->Eval(CefString(navigationShimScript), CefString(""), 0,
                      navigationShimFunction, navigationShimException);
    if (navigationShimSuccess && navigationShimFunction->IsFunction()) {
      CefV8ValueList navigationShimArgs;
      navigationShimArgs.push_back(onNavigationNavigate);
      navigationShimArgs.push_back(onNavigationBack);
      navigationShimArgs.push_back(onNavigationForward);
      navigationShimArgs.push_back(onNavigationTraverseTo);
      navigationShimFunction->ExecuteFunction(nullptr, navigationShimArgs);
    }
  }

  // focus out
  CefRefPtr<CefV8Value> document = window->GetValue("document");
  CefRefPtr<CefV8Handler> focusOutHandler = new FocusOutHandler();
  CefV8ValueList focusOutArguments;
  focusOutArguments.push_back(CefV8Value::CreateString("focusout"));
  focusOutArguments.push_back(
      CefV8Value::CreateFunction("onFocusOut", focusOutHandler));
  document->GetValue("addEventListener")
      ->ExecuteFunction(document, focusOutArguments);

  // message
  CefRefPtr<CefV8Handler> messageHandler = new MessageHandler();
  CefV8ValueList messageArguments;
  messageArguments.push_back(CefV8Value::CreateString("message"));
  messageArguments.push_back(
      CefV8Value::CreateFunction("onMessage", messageHandler));
  window->GetValue("addEventListener")
      ->ExecuteFunction(window, messageArguments);
}

void RenderProcessHandler::OnContextReleased(CefRefPtr<CefBrowser> browser,
                                             CefRefPtr<CefFrame> frame,
                                             CefRefPtr<CefV8Context> context) {}

void RenderProcessHandler::OnUncaughtException(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefV8Context> context,
    CefRefPtr<CefV8Exception> exception,
    CefRefPtr<CefV8StackTrace> stackTrace) {}

void RenderProcessHandler::OnFocusedNodeChanged(CefRefPtr<CefBrowser> browser,
                                                CefRefPtr<CefFrame> frame,
                                                CefRefPtr<CefDOMNode> node) {
  if (node.get()) {
    RpcRequest request;
    request.id = CreateUuid();
    request.className = "Browser";
    request.methodName = "OnFocusedNodeChanged";
    request.instanceId = browser->GetIdentifier();
    Browser_OnFocusedNodeChanged focusedNodeArguments;
    focusedNodeArguments.tagName = node->GetElementTagName();
    std::string inputType = node->GetElementAttribute("type");
    if (!inputType.empty()) {
      focusedNodeArguments.inputType = inputType;
    }
    focusedNodeArguments.isEditable = node->IsEditable();
    request.arguments = focusedNodeArguments;
    json jsonRequest = request;
    CefRefPtr<CefProcessMessage> responseMessage =
        CefProcessMessage::Create(kOnFocusMessage);
    responseMessage->GetArgumentList()->SetString(0, jsonRequest.dump());
    frame->SendProcessMessage(PID_BROWSER, responseMessage);
  }
}

// Custom handler for our promise "then" callback
class PromiseThenHandler : public CefV8Handler {
 public:
  explicit PromiseThenHandler(CefRefPtr<CefFrame> frame,
                              CefProcessId sourceProcessId,
                              const UUID messageId)
      : frame(frame), sourceProcessId(sourceProcessId), messageId(messageId) {}

  bool Execute(const CefString& name,
               CefRefPtr<CefV8Value> object,
               const CefV8ValueList& arguments,
               CefRefPtr<CefV8Value>& retval,
               CefString& exception) override {
    if (name != "onPromiseResolved" || arguments.size() == 0) {
      return false;
    }
    CefRefPtr<CefV8Context> context = frame->GetV8Context();
    CefRefPtr<CefV8Value> window = context->GetGlobal();
    CefRefPtr<CefV8Value> jsonObject = window->GetValue("JSON");
    CefRefPtr<CefV8Value> stringifyFunction = jsonObject->GetValue("stringify");
    CefV8ValueList stringifyArguments;
    stringifyArguments.push_back(arguments[0]);
    const CefString& result =
        stringifyFunction->ExecuteFunction(jsonObject, stringifyArguments)
            ->GetStringValue();
    CefRefPtr<CefProcessMessage> responseMessage =
        CefProcessMessage::Create(kOnEvalMessage);
    CefRefPtr<CefDictionaryValue> messageArguments =
        CefDictionaryValue::Create();
    RpcResponse response;
    response.requestId = messageId;
    response.success = true;
    response.returnValue = result.ToString();
    json jsonResponse = response;
    responseMessage->GetArgumentList()->SetString(0, jsonResponse.dump());
    frame->SendProcessMessage(sourceProcessId, responseMessage);
    retval = arguments[0];
    return true;
  }

 private:
  CefRefPtr<CefFrame> frame;
  CefProcessId sourceProcessId;
  const UUID messageId;
  IMPLEMENT_REFCOUNTING(PromiseThenHandler);
};

bool RenderProcessHandler::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  DCHECK_EQ(source_process, PID_BROWSER);
  CefRefPtr<CefV8Context> context = frame->GetV8Context();
  context->Enter();
  const CefString& name = message->GetName();
  SDL_Log("RenderProcessHandler received message: %s", name.ToString().c_str());
  bool handled = false;
  if (name == "Eval") {
    const CefString& payload = message->GetArgumentList()->GetString(0);
    RpcRequest request = json::parse(payload.ToString()).get<RpcRequest>();
    Browser_EvalJavaScript arguments =
        request.arguments.get<Browser_EvalJavaScript>();
    CefRefPtr<CefV8Value> retval;
    CefRefPtr<CefV8Exception> exception;
    bool success =
        context->Eval(CefString(arguments.code), CefString(arguments.scriptUrl),
                      arguments.startLine, retval, exception);
    if (success && retval->IsPromise()) {
      CefRefPtr<CefV8Value> thenFunction = retval->GetValue("then");
      CefRefPtr<PromiseThenHandler> handler =
          new PromiseThenHandler(frame, source_process, request.id);
      CefRefPtr<CefV8Value> onResolvedFunc =
          CefV8Value::CreateFunction("onPromiseResolved", handler);
      thenFunction->ExecuteFunction(retval, {onResolvedFunc});
    } else {
      RpcResponse response;
      response.requestId = request.id;
      response.success = success;
      if (success) {
        CefRefPtr<CefV8Value> window = context->GetGlobal();
        CefRefPtr<CefV8Value> jsonObj = window->GetValue("JSON");
        CefRefPtr<CefV8Value> stringifyFunction =
            jsonObj->GetValue("stringify");
        CefV8ValueList stringifyArguments;
        stringifyArguments.push_back(retval);
        const CefString& result =
            stringifyFunction->ExecuteFunction(jsonObj, stringifyArguments)
                ->GetStringValue();
        response.returnValue = result.ToString();
      } else {
        EvalJavaScriptError error;
        error.endColumn = exception->GetEndColumn();
        error.endPosition = exception->GetEndPosition();
        error.lineNumber = exception->GetLineNumber();
        error.message = exception->GetMessage().ToString();
        error.scriptResourceName =
            exception->GetScriptResourceName().ToString();
        error.sourceLine = exception->GetSourceLine().ToString();
        error.startColumn = exception->GetStartColumn();
        error.startPosition = exception->GetStartPosition();
        json errorJson = error;
        response.error = errorJson;
      }
      CefRefPtr<CefProcessMessage> responseMessage =
          CefProcessMessage::Create(kOnEvalMessage);
      json jsonResponse = response;
      responseMessage->GetArgumentList()->SetString(0, jsonResponse.dump());
      frame->SendProcessMessage(source_process, responseMessage);
    }
    handled = true;
  }
  context->Exit();
  return handled;
}
