#pragma once
#include <string>
#include <set>
#include "network.h"

struct Config {
	// Set config_filename and call this function
	void load();
	std::string config_filename = CONFIG_FILE;

	const std::string& workdir() const;
	const std::string& filesdir() const;
	const std::string& tmpfilesdir() const;

	void set_packet_file(const std::string& filename);

	/* Interface names (ex.: 'eth0') to listen on
	 * If user specify interface names on config file, they will be here and
	 * listen_specified be true.
	 * Otherwise all available interfaces be here and
	 * listen_specified be false */
	std::set<IFName> listen;
	bool listen_specified = false;

	// Network port number
	int port = 13132;

	// Don't ask user anything. Assume he is agreed
	bool force_yes = false;

	/* If defined, split files to this granularity when transmitted by networks
	 * or files. Useful to transmit big files with small-sized disks */
	size_t files_granularity = -1UL;

	/* Check disk free space while write packet file and stop writing
	 * if there is no space left (approximately) */
	bool chk_free_space = true;

	/* Filename which touched (stat write time) when last scan occured */
	std::string av_scan_date_file_date;

	/* Filename which contain string with last scan date */
	std::string av_scan_date_file;

	/* Command to execute to get last scan date */
	std::string av_scan_date_exec;

	/* Filename which touched (stat write time) when last update occured */
	std::string av_update_date_file_date;

	/* Filename which contain string with last update date */
	std::string av_update_date_file;

	/* Command to execute to get last update date */
	std::string av_update_date_exec;

	/* File containing info about found viruses or emptyfile otherwise */
	std::string av_results_file;

	/* Packet file used to import-export data (exchange between nodes) */
	std::string packet_file;

protected:

	/* Function called on each line of config file.
	 * First parameter is the first word of the line, second parameter is text left on the line.
	 * Return true if line successfully processed. */
	bool process_line(std::string_view, std::string_view);

private:
	void load_common();
	void load_home();

	std::string workdir_str = DIR_LOCALSTATE;
	std::string filesdir_str = DIR_LOCALSTATE "/files";
	std::string tmpfilesdir_str = DIR_LOCALSTATE "/tmp";

	std::string home_file;
};
