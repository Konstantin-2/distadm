#include "gtk_nodespage.h"
#include <gtkmm/stock.h>
#include <gtkmm/messagedialog.h>
#include <libintl.h>
#include "warn.h"
#include "utils.h"
#include "utils_iface.h"
#define _(STRING) gettext(STRING)

using std::unique_ptr;
using std::vector;
using std::string;
using std::string_view;
using std::ostringstream;

NodesPage::NodesCols::NodesCols(int x)
{
	add(N);
	add(uuid);
	add(nodename);
	add(online);
	cols.resize(x);
	for (auto& c : cols)
		add(c);
	add(netmsgcnt);
}

NodesPage::NodesPage() :
	Gtk::Box(Gtk::Orientation::ORIENTATION_VERTICAL),
	delbtn(Gtk::Stock::DELETE)
{
	add(swnd);
	add(toolbox);
	swnd.set_vexpand(true);
	swnd.add(treeview);
	toolbox.add(delbtn);
	delbtn.set_tooltip_text(_("Delete node"));

	treeview.signal_cursor_changed().connect(sigc::mem_fun(*this, &NodesPage::on_click_row));
	delbtn.signal_clicked().connect(sigc::mem_fun(*this, &NodesPage::on_del));
}

void NodesPage::refill()
{
	if (fd == -1)
		return;
	string answr = unix_getline(fd, "nodesinfo\n");
	vector<string_view> nodes = split(answr, false, '\n');
	size_t N = nodes.size();
	cols = unique_ptr<NodesCols>(new NodesCols(N + 5));
	list = Gtk::ListStore::create(*cols);
	treeview.remove_all_columns();
	treeview.set_model(list);
	treeview.append_column(_("#"), cols->N);
	treeview.append_column(_("UUID"), cols->uuid);
	treeview.append_column(_("Hostname"), cols->nodename);
	treeview.append_column(_("Online"), cols->online);
	for (size_t i = 0; i < N; i++) {
		ostringstream str;
		str << i + 1;
		treeview.append_column(str.str(), cols->cols[i]);
	}
	treeview.append_column(_("Net Messages Counter"), cols->netmsgcnt);

	ssize_t i = 1;
	for (const string_view& node : nodes) {
		vector<string_view> items = split(node, true, '\t');
		if (items.size() != N + 4) {
			warn << _("Bad answer from daemon");
			continue;
		}
		auto r = *(list->append());
		r[cols->N] = i++;
		r[cols->uuid] = string(items[0]);
		r[cols->nodename] = string(items[1]);
		r[cols->online] = string(items[2]);
		for (size_t j = 0; j < N; j++)
			r[cols->cols[j]] = string(items[3 + j]);
		r[cols->netmsgcnt] = string(items[3 + N]);
	}
	delbtn.set_sensitive(false);
}

void NodesPage::on_click_row()
{
	delbtn.set_sensitive(true);
}

void NodesPage::on_del()
{
	Gtk::TreeModel::iterator s = treeview.get_selection()->get_selected();
	if (!s) {
		delbtn.set_sensitive(false);
		return;
	}
	string name = s->get_value(cols->uuid);
	Gtk::MessageDialog dlg(string(_("Delete")) + ' ' + name + '?', false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_YES_NO, true);
	dlg.set_title("distadm");
	dlg.add_button(_("Forced"), 1);
	int response = dlg.run();
	if (response == Gtk::RESPONSE_YES)
		unix_getline_X(fd, "delnode " + name + "\n");
	else if (response == 1)
		unix_getline_X(fd, "delnode " + name + " force\n");
}
