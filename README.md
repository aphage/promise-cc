# ✨ Welcome to promise-cc! ✨

Hey there! 👋 Ever wished C++ could handle asynchronous tasks with the same elegance as a beautiful dance? Well, `promise-cc` is here to make that wish come true! 💖 It's a lovely little library that brings the magic of JavaScript-style Promises right into your C++ projects. Let's make programming feel like a wonderful party! 🎉

## 🌸 Introduction 🌸
With `promise-cc`, you can chain your asynchronous tasks together with `then`, `catch`, and `finally`, just like telling a beautiful, flowing story. It's perfect for keeping your code tidy and your heart happy! 😊

## 🛠️ Installation 🛠️
Getting started is as easy as a gentle breeze~ 🌬️ Just follow these little steps, and you'll be ready to create magic! ✨

```sh
# First, let's get everything ready
cmake --preset x64-debug

# Now, let's build it!
cmake --build out/build/x64-debug
```

And if you want to see the tests sparkle, just do this~
```sh
# Let's make sure everything is perfect
cmake --build out/build/x64-debug --target test
ctest --test-dir out/build/x64-debug
```

## 🚀 Quick Start 🚀
Ready for your first spell? 🪄 Just whisper the magic words by including the header:
```cpp
#include <promise/promise.h>
```

And here's a little piece of magic to get you started!
```cpp
using promise::Promise;

std::make_shared<Promise<int, Async>>([](auto resolve, auto reject) {
    // A little gift for you~
    resolve(42);
})->then<int>([](auto v) {
    // And then, something wonderful happens!
    SPDLOG_INFO("resolved with {}", v);
    return v * 2;
})->then([](auto v) {
    // And the story continues...
    SPDLOG_INFO("resolved with {}", v);
});
```

## 🎀 API Documentation 🎀

Let me introduce you to my most wonderful creation, the `Promise`!

### Promise<T, Executor>
- **Constructor**: `Promise(std::function<void(resolve, reject)>)` - This is where the magic begins! 🌟
- **`then(onFulfilled, onRejected)`**: To continue our beautiful story, step by step.
- **`catch_err(onRejected)`**: If something unexpected happens, don't worry! We'll catch it gracefully.
- **`finally(onFinally)`**: No matter what, we'll always have a beautiful finale. 💖

### Executor
You can even choose how your magic is performed! How cool is that?
```cpp
// For things that happen in a flash!
struct Executor {
    template<typename F>
    void operator()(F f) { f(); }
};

// For a little performance in the background~
struct Async {
    template<typename F>
    void operator()(F f) { std::thread{f}.detach(); }
};
```

## 💖 CMake FetchContent Usage 💖
Want to invite me to your own project? It's super easy! Just add these lines to your `CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(
    promise-cc
    GIT_REPOSITORY https://github.com/aphage/promise-cc.git
)
FetchContent_MakeAvailable(promise-cc)

# And we're linked! Just like that!
target_link_libraries(${PROJECT_NAME} PRIVATE promise-cc::promise)
```

## 🧪 Testing 🧪
I've prepared some little tests in `test/test.cc` to make sure everything is as perfect as it can be! Feel free to have a look!

## ✨ Contributing ✨
Everyone is welcome to make this little world even more beautiful! Please feel free to share your ideas or send a little pull request. I'll be waiting! 🥰

## License
It's under the MIT License, which means we can all share the love freely! 💖