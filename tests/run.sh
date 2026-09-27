#!/bin/sh
# Baut und startet die Tests der Regellogik auf dem PC (g++ >= 9).
# ArduinoJson wird in derselben Version wie auf dem Board (ESPHome 2026.9) geladen.
set -eu
cd "$(dirname "$0")/.."
AJ_VERSION=7.4.3
AJ_SHA256=ab5fbb8268b846b5f4bc5a5fee11bb2c96f7b8b846f5bef6540afb6a9cc76a5b
DEPS=tests/.deps
mkdir -p "$DEPS"
if [ ! -f "$DEPS/ArduinoJson.h" ]; then
  curl -fsSL -o "$DEPS/ArduinoJson.h.tmp" \
    "https://github.com/bblanchon/ArduinoJson/releases/download/v$AJ_VERSION/ArduinoJson-v$AJ_VERSION.h"
  echo "$AJ_SHA256  $DEPS/ArduinoJson.h.tmp" | sha256sum -c - >/dev/null
  mv "$DEPS/ArduinoJson.h.tmp" "$DEPS/ArduinoJson.h"
fi
g++ -std=gnu++17 -Wall -Wextra -Werror -I "$DEPS" -o "$DEPS/test_regeln" tests/test_regeln.cpp
"$DEPS/test_regeln"
