#pragma once

#include <Arduino.h>

struct UrlParts {
  String host;
  String path;
};

struct HttpHead {
  int  status;
  String location;
  long content_length;
};
