#include "main.h"
#include <error.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>
#include <sha1.h>
#include <json/writer.h>
#include <libintl.h>
#include <iostream>
#include "exc_error.h"
#include "warn.h"
#include "showdebug.h"
#include "utils.h"
#include "utils_iface.h"
#include "daemon.h"
#include "alarmer.h"
#ifdef USE_X
#include "iface_info.h"
#include "iface_main.h"
#endif
#define _(STRING) gettext(STRING)

using std::min;
using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::vector;
using std::ofstream;
using std::ifstream;
using std::mutex;
using std::random_device;

ProgramStatus prog_status = ProgramStatus::work;
ifstream rnd("/dev/urandom");
Json::StreamWriterBuilder json_builder;
Json::StreamWriterBuilder json_cbuilder;
random_device rd;

void global_init()
{
	std::ios::sync_with_stdio(false);
	setlocale(LC_ALL, "");
	bindtextdomain("distadm", DATAROOTDIR "/locale");
	textdomain("distadm");

	if (!rnd)
		error(1, errno, "Can't open /dev/urandom");
	json_builder["commentStyle"] = "None";
	json_builder["emitUTF8"] = true;
	json_cbuilder["commentStyle"] = "None";
	json_cbuilder["indentation"] = "";
	json_cbuilder["emitUTF8"] = true;
	int res = gnutls_global_init();
	if (res)
		error(1, 0, "gnutls_global_init %s", gnutls_strerror(res));
	signal(SIGPIPE, SIG_IGN);
}

void global_deinit()
{
	gnutls_global_deinit();
}

static void show_version()
{
	cout << "distadm 1.0\nCopyright (C) 2022 Oshepkov Kosntantin\n"
	"License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>\n"
	"This is free software: you are free to change and redistribute it.\n"
	"There is NO WARRANTY, to the extent permitted by law.\n";
}

static void show_help()
{
	cout << _("Usage: distadm <command>\n"
	"Execute command over group of computers\n"
	"Commands:\n"
	"  -d, --daemon             run in daemon mode\n"
	"  -i, --info               show nodes info\n"
	"  -t, --textmode           run in text mode\n"
	"  -I, --initialize         initialize new group\n"
	"  -J, --join <filename>    join to group by file\n"
	"  -c, --config <filename>  use optional config file\n"
	"  -v, --verbose            show additional debug info\n"
	"      --help               show this help\n"
	"      --version            show version info\n"
	"If no command specified, program run in interactive mode.\n"
	"In textmode You can type internal commands.\n"
	"List of them is printed if type 'help'.\n"
	"You can also execute internal command if run program with this command\n"
	"as parameter.\n"
	"Report bugs to: oks-mgn@mail.ru\n"
	"General help using GNU software: <https://www.gnu.org/gethelp/>\n");
}

static void parse_params(int argc, char ** argv)
{
	int c;
	int longIndex = 0;
	bool start_daemon = false;
	bool init_network = false;
	bool show_info = false;
	bool textmode = false;
#ifdef USE_X
	bool hasX = getenv("DISPLAY");
#else
	bool hasX = false;
#endif
	string group_id_file;
	vector<string> cmd;
	Config cfg;

	static const struct option longOpts[] = {
		{"help", no_argument, 0, 2},
		{"version", no_argument, 0, 3},
		{"verbose", no_argument, 0,'v'},
		{"config", required_argument, 0, 'c'},
		{"initialize", no_argument, 0, 'I'},
		{"join", required_argument, 0, 'J'},
		{"daemon", no_argument, 0, 'd'},
		{"info", no_argument, 0,'i'},
		{"textmode", no_argument, 0,'t'},
		{0, no_argument, 0,'f'},
		{0, no_argument, 0, 0}
	};
	while ((c = getopt_long(argc, argv, "vc:IJ:ditf", longOpts, &longIndex)) != -1) {
		switch (c) {
		case 2:
			show_help();
			return;
		case 3:
			show_version();
			return;
		case 'v':
			print_debug = true;
			break;
		case 'c':
			cfg.config_filename = optarg;
			break;
		case 'I':
			init_network = true;
			break;
		case 'J':
			group_id_file = optarg;
			break;
		case 'd':
			start_daemon = true;
			break;
		case 'i':
			show_info = true;
			break;
		case 't':
			textmode = true;
			break;
		case 'f':
			cfg.force_yes = true;
			break;
		default:
			cerr << _("Unknown argument") << endl;
			return;
		}
	}
	while (optind < argc)
		cmd.push_back(argv[optind++]);
	bool has_commands = !cmd.empty();
	cfg.load();
	if (init_network) {
		new_group(cfg);
	} else if (!group_id_file.empty()) {
		join_group(cfg, group_id_file);
	} else if (start_daemon) {
		init_signals();
		Daemon dmn(cfg);
		dmn.load();
		dmn.daemon();
	}
#ifdef USE_X
	else if (hasX && show_info)
		iface_info(argc, argv);
	else if (!has_commands && hasX && !textmode)
		iface_main(argc, argv, cfg);
#endif
	else {
		Core core(cfg);
		core.load();
		core.interactive(cmd);
		core.save();
	}
}

int main(int argc, char ** argv)
{
	global_init();
	parse_params(argc, argv);
	global_deinit();
}
