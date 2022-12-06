#include <libintl.h>
#include <gtkmm/box.h>
#include <gtkmm/applicationwindow.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/treeview.h>
#include <gtkmm/viewport.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/liststore.h>
#include <gtkmm/toolbutton.h>
#include <gtkmm/stack.h>
#include <gtkmm/stock.h>
#include <gtkmm/stackswitcher.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/cssprovider.h>
#include <iostream>
#include <memory>
#include <filesystem>
#include "iface_main.h"
#include "gtk_nodespage.h"
#include "gtk_userspage.h"
#include "gtk_filespage.h"
#include "gtk_execpage.h"
#include "gtk_queuepage.h"
#include "gtk_mesgspage.h"
#include "gtk_localpage.h"
#include "utils.h"
#include "utils_iface.h"
#include "warn.h"
#include "core.h"
#define _(STRING) gettext(STRING)

namespace fs = std::filesystem;
using std::cout;
using std::endl;
using std::vector;
using std::unique_ptr;
using std::exception;
using std::string;
using std::string_view;
using std::ostringstream;

enum class Initializer {
	online,
	offline,
	finalize
};

struct MainWindow : Gtk::ApplicationWindow {
	MainWindow(Config& cfg, int fd);
	~MainWindow();
	void refill();
	void write_invite(bool online);
	void write_net_invite();
	void write_off_invite();
	void fin_invite();
	void cancel_invite();
	void on_read_packet();
	void on_write_packet();
	void on_rw_packet();
	void on_rw_packet_again();
	void rw_packet(const string&);

	Gtk::Image wrnetimg;
	Gtk::Image wroffimg;
	Gtk::Image offfnimg;
	Gtk::Image offclimg;
	Gtk::Image imprt;
	Gtk::Image exprt;
	Gtk::Image impexp;
	Gtk::Image impexp2;

	Gtk::Box rootbox;
	Gtk::Box toolbox;
	Gtk::StackSwitcher stk_swtchr;
	Gtk::Stack stack;
	NodesPage nodespage;
	UsersPage userspage;
	FilesPage filespage;
	ExecPage execpage;
	QueuePage queuepage;
	MesgsPage cmndspage;
	LocalPage localpage;
	Gtk::ToolButton btn_refrsh;
	Gtk::ToolButton btn_wrnetinit;
	Gtk::ToolButton btn_wroffinit;
	Gtk::ToolButton btn_finlzinit;
	Gtk::ToolButton btn_canclinit;
	Gtk::ToolButton btn_readpkt;
	Gtk::ToolButton btn_writepkt;
	Gtk::ToolButton btn_rwpkt;
	Gtk::ToolButton btn_rwpkt2;
	int fd = -1;
	Config& cfg;
};

struct DlgNotInined : Gtk::Dialog {
	DlgNotInined();
	template <int N> void on_btn();
	Gtk::Label lbl;
	Gtk::Button btn1, btn2, btn3;
	enum {
		NewGroup,
		JoinGroup,
		Exit
	};
};

// -----------------------

MainWindow::MainWindow(Config& c, int conn_fd) :
	wrnetimg(SHARE_DIR "/init-online.png"),
	wroffimg(SHARE_DIR "/init-offline.png"),
	offfnimg(SHARE_DIR "/init-finalize.png"),
	offclimg(SHARE_DIR "/init-cancel.png"),
	imprt(SHARE_DIR "/imp.png"),
	exprt(SHARE_DIR "/exp.png"),
	impexp(SHARE_DIR "/imp-exp.png"),
	impexp2(SHARE_DIR "/imp-exp-2.png"),
	rootbox(Gtk::Orientation::ORIENTATION_VERTICAL),
	toolbox(Gtk::Orientation::ORIENTATION_HORIZONTAL),
	userspage(*this),
	filespage(c, *this),
	btn_refrsh(Gtk::Stock::REFRESH),
	btn_wrnetinit(wrnetimg),
	btn_wroffinit(wroffimg),
	btn_finlzinit(offfnimg),
	btn_canclinit(offclimg),
	btn_readpkt(imprt),
	btn_writepkt(exprt),
	btn_rwpkt(impexp),
	btn_rwpkt2(impexp2),
	fd(conn_fd),
	cfg(c)
{
	CoreBase base(cfg);
	base.load();
	execpage.fd =
	userspage.fd =
	nodespage.fd =
	filespage.fd =
	queuepage.fd =
	cmndspage.fd =
	localpage.fd = fd;

	set_default_size(660, 300);
	show_all();
	add(rootbox);
	rootbox.add(toolbox);
	rootbox.add(stk_swtchr);
	rootbox.add(stack);
	stk_swtchr.set_stack(stack);
	stack.add(nodespage, "nodespage", _("Nodes"));
	stack.add(userspage, "userspage", _("Users"));
	stack.add(filespage, "filespage", _("Files"));
	stack.add(execpage, "execpage", _("Console"));
	stack.add(queuepage, "queuepage", _("Queue"));
	stack.add(cmndspage, "cmndspage", _("Commands"));
	stack.add(localpage, "localpage", _("Local"));

	toolbox.add(btn_refrsh);
	toolbox.add(btn_wrnetinit);
	toolbox.add(btn_wroffinit);
	toolbox.add(btn_finlzinit);
	toolbox.add(btn_canclinit);
	toolbox.add(btn_readpkt);
	toolbox.add(btn_writepkt);
	toolbox.add(btn_rwpkt);
	toolbox.add(btn_rwpkt2);

	btn_refrsh.set_tooltip_text(_("Update"));
	btn_wrnetinit.set_tooltip_text(_("Write online invite"));
	btn_wroffinit.set_tooltip_text(_("Write offline invite"));
	btn_finlzinit.set_tooltip_text(_("Finalize invitation"));
	btn_canclinit.set_tooltip_text(_("Cancel offline invitation"));
	btn_readpkt.set_tooltip_text(_("Read packet"));
	btn_writepkt.set_tooltip_text(_("Write packet"));
	btn_rwpkt.set_tooltip_text(_("Read-write packet"));
	btn_rwpkt2.set_tooltip_text(_("Read-write packet again"));

	btn_refrsh.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::refill));
	btn_wrnetinit.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::write_net_invite));
	btn_wroffinit.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::write_off_invite));
	btn_finlzinit.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::fin_invite));
	btn_canclinit.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::cancel_invite));
	btn_readpkt.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_read_packet));
	btn_writepkt.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_write_packet));
	btn_rwpkt.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_rw_packet));
	btn_rwpkt2.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_rw_packet_again));
	refill();
}

MainWindow::~MainWindow()
{
	::close(fd);
}

void MainWindow::refill()
{
	nodespage.refill();
	userspage.refill();
	filespage.refill();
	execpage.refill();
	queuepage.refill();
	cmndspage.refill();
	localpage.refill();
	show_all();
}

void MainWindow::write_invite(bool online)
{
	string cmd = online ?  "write-online-invite " :  "write-offline-invite ";
	string filename = get_save_file(this);
	if  (filename.empty())
		return;
	string password = readpasswd2(this);
	unix_getline_X(fd, cmd
		+ quote_string(filename) + ' '
		+ quote_string(password) + "\n");
}

void MainWindow::write_net_invite()
{
	write_invite(true);
}

void MainWindow::write_off_invite()
{
	string status = unix_getline(fd, "status\n");
	if (status == "inviter") {
		if (!prompt_yn(_("Node already write invite. Do you want to cancel previous invite and write new one?")))
			return;
		unix_getline_X(fd, "cancel-invite\n");
	}
	write_invite(false);
	refill();
}

void MainWindow::fin_invite()
{
	string filename = get_save_file(this);
	if  (filename.empty())
		return;
	unix_getline_X(fd, "finalize-invite "
		+ quote_string(filename) + "\n");
	refill();
}

void MainWindow::cancel_invite()
{
	if (prompt_yn(_("Do you want to cancel invite?")))
		return;
	unix_getline_X(fd, "cancel-invite\n");
	refill();
}

void MainWindow::on_read_packet()
{
	string filename = get_open_file(this);
	if (filename.empty())
		return;
	unix_getline_X(fd, "read-packet " + quote_string(filename) + "\n");
	refill();
}

void MainWindow::on_write_packet()
{
	string filename = get_save_file(this);
	if (filename.empty())
		return;
	unix_getline_X(fd, "write-packet " + quote_string(filename) + "\n");
}

void MainWindow::on_rw_packet()
{
	string filename = get_save_file(this);
	if (filename.empty())
		return;
	rw_packet(filename);
}

void MainWindow::on_rw_packet_again()
{
	string filename = cfg.packet_file;
	if (filename.empty()) {
		filename = get_save_file(this);
		if (filename.empty())
			return;
	}
	rw_packet(filename);
}

void MainWindow::rw_packet(const string& filename)
{
	unix_getline_X(fd, "read-packet " + quote_string(filename) + "\n");
	unix_getline_X(fd, "write-packet " + quote_string(filename) + "\n");
	cfg.set_packet_file(filename);
	refill();
}

DlgNotInined::DlgNotInined() :
	Gtk::Dialog("distadm", true),
	lbl(_("Node is not initialized")),
	btn1(_("New group")),
	btn2(_("Join to group")),
	btn3(_("Exit"))
{
	labels_1em({&lbl});
	Gtk::Box *ca = get_content_area();
	ca->add(lbl);
	ca->add(btn1);
	ca->add(btn2);
	ca->add(btn3);
	btn1.signal_clicked().connect(sigc::mem_fun(*this, &DlgNotInined::on_btn<NewGroup>));
	btn2.signal_clicked().connect(sigc::mem_fun(*this, &DlgNotInined::on_btn<JoinGroup>));
	btn3.signal_clicked().connect(sigc::mem_fun(*this, &DlgNotInined::on_btn<Exit>));
	show_all();
}

template <int N>
void DlgNotInined::on_btn()
{
	response(N);
}

// return false to restart
bool iface_matrix_int(int argc, char ** argv, Config& cfg)
{
	Glib::RefPtr<Gtk::Application> app(Gtk::Application::create("org.gtkmm.distadm"));
	CoreBase base(cfg);
	bool ok = base.load();
	if (!ok) {
		int response = DlgNotInined().run();
		switch (response) {
		case DlgNotInined::NewGroup:
			new_group(cfg);
			return false;
		case DlgNotInined::JoinGroup:
			{
				string filename = get_open_file();
				if (filename.empty())
					return false;
				join_group(cfg, filename);
				return false;
			}
		case DlgNotInined::Exit:
		default:
			return true;
		}
	}
	int fd = ::create_unix_socket_client_fd(base.unix_socket_name());
	if (fd == -1) {
		Gtk::MessageDialog md(_("Can't connect to server"));
		md.set_title("distadm");
		md.add_button(_("Retry"), 1);
		int response = md.run();
		return response != 1;
	}
	MainWindow window(cfg, fd);
	window.show_all();
	app->run(window, 1, argv);
	return true;
}

void iface_main(int argc, char ** argv, Config& cfg)
{
	useX = true;
	cfg.force_yes = true;
	while (!iface_matrix_int(argc, argv, cfg))
		nsleep(); // I hope it would be enough to start daemon
}
