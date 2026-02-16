#pragma once

#include <SDL3/sdl.h>
#include <queue>

template <typename T>
class ThreadSafeQueue {
 public:
  ThreadSafeQueue() {
    mtx = SDL_CreateMutex();
    cv = SDL_CreateCondition();
    if (!mtx || !cv) {
      SDL_Log("Failed to create SDLThreadSafeQueue synchronization objects");
      abort();
    }
  }

  ~ThreadSafeQueue() {
    SDL_DestroyMutex(mtx);
    SDL_DestroyCondition(cv);
  }

  // Push item into queue (non-blocking)
  void push(const T& val) {
    SDL_LockMutex(mtx);
    q.push(val);
    SDL_SignalCondition(cv);  // wake one waiting thread
    SDL_UnlockMutex(mtx);
  }

  // Blocking pop: waits until an item is available
  T pop() {
    SDL_LockMutex(mtx);
    while (q.empty()) {
      SDL_WaitCondition(cv, mtx);  // releases mtx + waits, then reacquires mtx
    }
    T val = q.front();
    q.pop();
    SDL_UnlockMutex(mtx);
    return val;
  }

  // Non-blocking pop, returns false if queue was empty
  bool try_pop(T& out) {
    SDL_LockMutex(mtx);
    if (q.empty()) {
      SDL_UnlockMutex(mtx);
      return false;
    }
    out = q.front();
    q.pop();
    SDL_UnlockMutex(mtx);
    return true;
  }

 private:
  SDL_Mutex* mtx;
  SDL_Condition* cv;
  std::queue<T> q;
};

// Per-request synchronization entry
struct ResponseEntry {
  SDL_Mutex* mutex = nullptr;
  SDL_Condition* cond = nullptr;
  std::string payload;
  bool ready = false;

  ResponseEntry() {
    mutex = SDL_CreateMutex();
    cond = SDL_CreateCondition();
  }
  ~ResponseEntry() {
    if (cond)
      SDL_DestroyCondition(cond);
    if (mutex)
      SDL_DestroyMutex(mutex);
  }
};