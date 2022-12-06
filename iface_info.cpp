#include "iface_info.h"
#include <libintl.h>
#include <gtkmm/applicationwindow.h>
#include <gtkmm/treeview.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/liststore.h>
#include <vector>
#include <string>
#include <filesystem>
#include "utils.h"
#include "utils_iface.h"
#define _(STRING) gettext(STRING)

namespace fs = std::filesystem;
using std::vector;
using std::string;
using std::string_view;

class Columns : public Gtk::TreeModel::ColumnRecord {
public:
	Columns() {
		add(nodename);
		add(online);
		add(updated);
		add(scanned);
		add(result);
		add(smart);
	}

	Gtk::TreeModelColumn<Glib::ustring> nodename;
	Gtk::TreeModelColumn<Glib::ustring> online;
	Gtk::TreeModelColumn<Glib::ustring> updated;
	Gtk::TreeModelColumn<Glib::ustring> scanned;
	Gtk::TreeModelColumn<Glib::ustring> result;
	Gtk::TreeModelColumn<Glib::ustring> smart;
};

static vector<vector<string>> read_state(const string& filename)
{
	vector<vector<string>> res;
	int fd = ::create_unix_socket_client_fd(filename);
	if (fd == -1)
		return res;
	string str;
	char buf[0x1000];
	for (;;) {
		ssize_t x = read(fd, buf, sizeof(buf));
		if (x <= 0)
			break;
		str.append(buf, x);
	}
	for (const auto& row: split(str, false, '\n')) {
		vector<string> resr;
		auto rowsv = split(row, true, '\t');
		if (rowsv.size() < 4)
			continue;
		resr.emplace_back(rowsv[0]);
		resr.emplace_back(rowsv[1]);
		resr.emplace_back(rowsv[2]);
		resr.emplace_back(rowsv[3]);
		resr.emplace_back(rowsv.size() > 4 ? rowsv[4] : "");
		resr.emplace_back(rowsv.size() > 5 ? rowsv[5] : "");
		res.push_back(move(resr));
	}
	sort(res.begin(), res.end(), [](const auto& r1, const auto& r2) {
		return !r1.empty() && !r2.empty() && r1[0] < r2[0];
	});
	close(fd);
	return res;
}

static vector<vector<string>> read_state()
{
	for (const auto & entry : fs::directory_iterator(RUN_DIR)) {
		if (!fs::is_socket(entry))
			continue;
		auto res = read_state(entry.path());
		if (!res.empty())
			return res;
	}
	return vector<vector<string>>();
}

void iface_info(int argc, char ** argv)
{
	useX = true;
	Glib::RefPtr<Gtk::Application> app(Gtk::Application::create("org.gtkmm.distadm"));
	vector<vector<string>> state = read_state();
	if (state.empty()) {
		Gtk::MessageDialog md(_("Bad answer from daemon"));
		md.set_title("distadm");
		md.run();
		return;
	}
	Gtk::ApplicationWindow window;
	window.set_default_size(660, 200);
	Gtk::ScrolledWindow swnd;
	swnd.set_vexpand(true);
	window.add(swnd);
	Gtk::TreeView treeview;
	swnd.add(treeview);
	Columns moldelcols;
	Glib::RefPtr<Gtk::ListStore> liststore = Gtk::ListStore::create(moldelcols);
	treeview.set_model(liststore);
	treeview.append_column(_("Computer"), moldelcols.nodename);
	treeview.append_column(_("Online"), moldelcols.online);
	treeview.append_column(_("Antivirus update"), moldelcols.updated);
	treeview.append_column(_("Scanned"), moldelcols.scanned);
	treeview.append_column(_("Result"), moldelcols.result);
	treeview.append_column(_("S.M.A.R.T."), moldelcols.result);
	for (const auto& row : state) {
		if (row.size() != 6)
			continue;
		auto r = *(liststore->append());
		r[moldelcols.nodename] = row[0];
		r[moldelcols.online] = row[1];
		r[moldelcols.updated] = row[2];
		r[moldelcols.scanned] = row[3];
		r[moldelcols.result] = row[4] == "true" ? _("Warning!") : "";
		r[moldelcols.result] = row[5] == "true" ? _("Warning!") : "";
	}
	window.show_all();
	app->run(window, 1, argv);
}
