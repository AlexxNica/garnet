#pragma once

#include <stdio.h>

#define WARN_ON(x) if (x) printf("ath10k: unexpected condition %s at %s:%d\n", #x, __FILE__, __LINE__)
