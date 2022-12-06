#pragma once
#include "core.h"

extern std::map<std::string, void (Core::*)(std::ostream&, std::vector<std::string>&)> incm_list;
