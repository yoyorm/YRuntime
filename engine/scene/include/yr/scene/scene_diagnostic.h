#pragma once

#include <string>

namespace yr::scene {

  struct SceneDiagnostic {
    enum class Severity { kWarning, kError };

    Severity severity = Severity::kError;
    int line = 1;   // 1-based
    int column = 1; // 1-based
    std::string message;
  };

} // namespace yr::scene