// Copyright (c) 2013 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include <optional>

#include "render_process_handler.h"

#include "include/base/cef_logging.h"
#include "json.hpp"
#include <map>
#include <rpc.h>
#include <string>
#include "rpc.hpp"
#include <SDL3/sdl.h>

using json = nlohmann::json;

const char kOnFocusMessage[] = "RenderProcessHandler.OnFocus";
const char kOnFocusOutMessage[] = "RenderProcessHandler.OnFocusOut";
const char kOnMouseOverMessage[] = "RenderProcessHandler.OnMouseOver";
const char kOnNavigateMessage[] = "RenderProcessHandler.OnNavigate";
const char kOnMessageMessage[] = "RenderProcessHandler.OnMessage";
const char kOnEvalMessage[] = "RenderProcessHandler.OnEval";

RenderProcessHandler::RenderProcessHandler() {}

CefRefPtr<CefRenderProcessHandler> RenderProcessHandler::GetRenderProcessHandler() {
  return this;
}

void RenderProcessHandler::OnWebKitInitialized() {
}

void RenderProcessHandler::OnBrowserCreated(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDictionaryValue> extra_info) {
}

void RenderProcessHandler::OnBrowserDestroyed(CefRefPtr<CefBrowser> browser) {
}

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

    UUID id;
    UuidCreate(&id);

    MouseOverEvent mouseOverEvent;
    mouseOverEvent.id = id;
    mouseOverEvent.browserId = frame->GetBrowser()->GetIdentifier();
    mouseOverEvent.tagName = target->GetValue("tagName")->GetStringValue().ToString();
    if (mouseOverEvent.tagName == "INPUT") {
      mouseOverEvent.inputType = target->GetValue("type")->GetStringValue().ToString();
    }
    CefV8ValueList closestArguments;
    closestArguments.push_back(CefV8Value::CreateString("a"));
    CefRefPtr<CefV8Value> closestResult =
        target->GetValue("closest")->ExecuteFunction(target, closestArguments);
    CefV8ValueList getBoundingClientRectArguments;
    CefRefPtr<CefV8Value> rectangle;
    if (closestResult->IsObject()) {
      std::string href = closestResult->GetValue("href")->GetStringValue().ToString();
      if (!href.empty()) {
        mouseOverEvent.href = href;
      }
      rectangle =
          closestResult->GetValue("getBoundingClientRect")
              ->ExecuteFunction(closestResult, getBoundingClientRectArguments);
    } else {
      rectangle = target->GetValue("getBoundingClientRect")
                      ->ExecuteFunction(target, getBoundingClientRectArguments);
    }
    mouseOverEvent.rectangle.x = rectangle->GetValue("left")->GetDoubleValue();
    mouseOverEvent.rectangle.y = rectangle->GetValue("top")->GetDoubleValue();
    mouseOverEvent.rectangle.width = rectangle->GetValue("right")->GetDoubleValue() - mouseOverEvent.rectangle.x;
    mouseOverEvent.rectangle.height =
        rectangle->GetValue("bottom")->GetDoubleValue() -
        mouseOverEvent.rectangle.y;
    
    json j = mouseOverEvent;
    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create(kOnMouseOverMessage);
    message->GetArgumentList()->SetString(0, j.dump());
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

class NavigateHandler : public CefV8Handler {
 public:
  NavigateHandler() {}

  virtual bool Execute(const CefString& name,
                       CefRefPtr<CefV8Value> object,
                       const CefV8ValueList& arguments,
                       CefRefPtr<CefV8Value>& retval,
                       CefString& exception) override {
    CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
    CefRefPtr<CefFrame> frame = context->GetFrame();
    CefRefPtr<CefV8Value> event = arguments.front();
    
    UUID id;
    UuidCreate(&id);

    NavigateEvent navigateEvent;
    navigateEvent.id = id;
    navigateEvent.browserId = frame->GetBrowser()->GetIdentifier();
    navigateEvent.destination.id =
        event->GetValue("destination")->GetValue("id")->GetStringValue();
    navigateEvent.destination.url =
        event->GetValue("destination")->GetValue("url")->GetStringValue();
    navigateEvent.destination.index =
        event->GetValue("destination")->GetValue("index")->GetIntValue();
    navigateEvent.destination.key =
        event->GetValue("destination")->GetValue("key")->GetStringValue();
    navigateEvent.destination.sameDocument = event->GetValue("destination")
                                           ->GetValue("sameDocument")
                                           ->GetBoolValue();
    navigateEvent.formData = std::nullopt;  // TODO: Populate form data
    navigateEvent.hashChange = event->GetValue("hashChange")->GetBoolValue();
    navigateEvent.navigationType =
        event->GetValue("navigationType")->GetStringValue();
    navigateEvent.userInitiated = event->GetValue("userInitiated")->GetBoolValue();
    
    json j = navigateEvent;
    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create(kOnNavigateMessage);
    message->GetArgumentList()->SetString(0, j.dump());
    frame->SendProcessMessage(PID_BROWSER, message);
    return true;
  }

  // Provide the reference counting implementation for this class.
  IMPLEMENT_REFCOUNTING(NavigateHandler);
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
    CefRefPtr<CefProcessMessage> message =
        CefProcessMessage::Create(kOnFocusOutMessage);
    CefRefPtr<CefDictionaryValue> messageArguments =
        CefDictionaryValue::Create();

    UUID id;
    UuidCreate(&id);

    FocusOutEvent response;
    response.id = id;
    if (!relatedTarget->IsNull()) {
      response.tagName = relatedTarget->GetValue("tagName")->GetStringValue();
      CefRefPtr<CefV8Value> attributes = relatedTarget->GetValue("attributes");
      std::string inputType =  attributes->GetValue("type")->GetStringValue();
      if (!inputType.empty()) {
        response.inputType = inputType;
      }
      response.isEditable = attributes->GetValue("isEditable")->GetBoolValue();
    }
    json jsonResponse = response;
    message->GetArgumentList()->SetString(0, jsonResponse.dump());
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
  mouseOverArguments.push_back(CefV8Value::CreateFunction("onMouseOver", mouseOverHandler));
  window->GetValue("addEventListener")->ExecuteFunction(window, mouseOverArguments);

  // navigation
  CefRefPtr<CefV8Value> navigation = window->GetValue("navigation");
  CefRefPtr<CefV8Handler> navigateHandler = new NavigateHandler();
  CefV8ValueList navigateArguments;
  navigateArguments.push_back(CefV8Value::CreateString("navigate"));
  navigateArguments.push_back(
      CefV8Value::CreateFunction("onNavigate", navigateHandler));
  navigation->GetValue("addEventListener")->ExecuteFunction(navigation, navigateArguments);

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
                                          CefRefPtr<CefV8Context> context) {
}

void RenderProcessHandler::OnUncaughtException(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefV8Context> context,
    CefRefPtr<CefV8Exception> exception,
    CefRefPtr<CefV8StackTrace> stackTrace) {
}

void RenderProcessHandler::OnFocusedNodeChanged(CefRefPtr<CefBrowser> browser,
                                             CefRefPtr<CefFrame> frame,
                                             CefRefPtr<CefDOMNode> node) {
  if (node.get()) {
    CefRefPtr<CefProcessMessage> responseMessage =
        CefProcessMessage::Create(kOnFocusMessage);
    CefRefPtr<CefDictionaryValue> messageArguments =
        CefDictionaryValue::Create();
    FocusedNodeChangedEvent response;
    UUID id;
    UuidCreate(&id);
    response.id = id;
    response.browserId = browser->GetIdentifier();
    response.tagName = node->GetElementTagName();
    std::string inputType = node->GetElementAttribute("type");
    if (!inputType.empty()) {
      response.inputType = inputType;
    }
    response.isEditable = node->IsEditable();
    json jsonResponse = response;
    responseMessage->GetArgumentList()->SetString(0, jsonResponse.dump());
    frame->SendProcessMessage(PID_BROWSER, responseMessage);
  }
}

// Custom handler for our promise "then" callback
class PromiseThenHandler : public CefV8Handler {
 public:
  explicit PromiseThenHandler(CefRefPtr<CefFrame> frame, CefProcessId sourceProcessId, const UUID messageId)
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
    EvalJavaScriptResponse response;
    response.id = messageId;
    response.result = result.ToString();
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
    SDL_Log("RenderProcessHandler received CefEvalRequest");
    const CefString& payload = message->GetArgumentList()->GetString(0);
    Browser_EvalJavaScript evalRequest = json::parse(payload.ToString()).get<Browser_EvalJavaScript>();
    CefRefPtr<CefV8Value> retval;
    CefRefPtr<CefV8Exception> exception;
    bool success =
        context->Eval(CefString(evalRequest.evalJavaScript), CefString(evalRequest.scriptUrl), evalRequest.startLine, retval, exception);
    if (success && retval->IsPromise()) {
      CefRefPtr<CefV8Value> thenFunction = retval->GetValue("then");
      CefRefPtr<PromiseThenHandler> handler =
          new PromiseThenHandler(frame, source_process, evalRequest.id);
      CefRefPtr<CefV8Value> onResolvedFunc =
          CefV8Value::CreateFunction("onPromiseResolved", handler);
      thenFunction->ExecuteFunction(retval, {onResolvedFunc});
     } else {
      EvalJavaScriptResponse response;
      response.id = evalRequest.id;
      response.browserId = frame->GetBrowser()->GetIdentifier();
      response.success = success;
      if (success) {
        CefRefPtr<CefV8Value> window = context->GetGlobal();
        CefRefPtr<CefV8Value> jsonObj = window->GetValue("JSON");
        CefRefPtr<CefV8Value> stringifyFunction = jsonObj->GetValue("stringify");
        CefV8ValueList stringifyArguments;
        stringifyArguments.push_back(retval);
        const CefString& result =
            stringifyFunction->ExecuteFunction(jsonObj, stringifyArguments)
                ->GetStringValue();
        response.result = result.ToString();
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
        response.error = error;
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