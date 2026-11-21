#pragma once

#include "yr/input/input_event.h"

namespace yr::input {

  class IInputSink {
  public:
    virtual ~IInputSink() = default;
    virtual void submit(InputEvent event) = 0;
  };

  class IInputSource {
  public:
    IInputSource() = default;
    virtual ~IInputSource() = default;

    IInputSource(const IInputSource&) = delete;
    IInputSource& operator=(const IInputSource&) = delete;
    IInputSource(IInputSource&&) = delete;
    IInputSource& operator=(IInputSource&&) = delete;

    virtual void poll(IInputSink& sink) = 0;
  };

} // namespace yr::input
