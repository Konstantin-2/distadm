#pragma once
#include <fstream>
#include <random>
#include <json/json.h>

enum class ProgramStatus : char {
	work, // Main status
	reload, // Program is to reload config and rerun network server
	exit // Program is to end
};

extern ProgramStatus prog_status;
extern std::ifstream rnd; // random
extern std::random_device rd; // random too
extern Json::StreamWriterBuilder json_builder; // json builder
extern Json::StreamWriterBuilder json_cbuilder; // json compact builder
