#pragma once
#include "config.h"

/* Preprocess interactive commands (from command line) on unix-client side
 * before send them to unix-server */

bool local_preprocess(std::vector<std::string>& cmd, Config&);
