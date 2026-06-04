#pragma once

#include "nocopy.h"

namespace spotykach {

class Application {
  public:
    Application()  = default;
    ~Application() = default;

    void Init();
    void Loop();

  private:
    NOCOPY(Application)
};
};
