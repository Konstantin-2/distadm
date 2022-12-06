#include "gtk_filespage.h"
#include <gtkmm/stock.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/filechooser.h>
#include <libintl.h>
#include <iostream>
#include <string>
#include <filesystem>
#include "utils.h"
#include "utils_iface.h"
#include "cmd_local.h"
#define _(STRING) gettext(STRING)

namespace fs = std::filesystem;
using std::vector;
using std::string;

FilesPage::FilesCols::FilesCols()
{
	add(filename);
}

FilesPage::FilesPage(Config& c, Gtk::Window& parent) :
	Gtk::Box(Gtk::Orientation::ORIENTATION_VERTICAL),
	list(Gtk::ListStore::create(cols)),
	treeview(list),
	toolbox(Gtk::Orientation::ORIENTATION_HORIZONTAL),
	addbtn(Gtk::Stock::ADD),
	delbtn(Gtk::Stock::DELETE),
	cfg(c),
	window(parent)
{
	swnd.set_vexpand(true);
	add(swnd);
	swnd.add(treeview);
	treeview.append_column(_("Files"), cols.filename);

	add(toolbox);
	toolbox.add(addbtn);
	toolbox.add(delbtn);
	addbtn.set_tooltip_text(_("Add file"));
	delbtn.set_tooltip_text(_("Delete file"));
	addbtn.signal_clicked().connect(sigc::mem_fun(*this, &FilesPage::on_add));
	delbtn.signal_clicked().connect(sigc::mem_fun(*this, &FilesPage::on_del));
	treeview.signal_cursor_changed().connect(sigc::mem_fun(*this, &FilesPage::on_click_row));
}

void FilesPage::refill()
{
	if (fd == -1)
		return;
	string ans = unix_getline(fd, "listfiles\n");
	vector<string> names = splitq(ans, false, '\n');
	list->clear();
	for (const string& row : names) {
		auto r = *(list->append());
		r[cols.filename] = row;
	}
	delbtn.set_sensitive(false);
}

void FilesPage::on_click_row()
{
	delbtn.set_sensitive(true);
}

void FilesPage::on_add()
{
	Gtk::FileChooserDialog dialog("Please choose a file", Gtk::FileChooserAction::FILE_CHOOSER_ACTION_OPEN);
	dialog.set_transient_for(window);
	dialog.set_modal(true);
	dialog.set_title("distadm");
	dialog.add_button(_("Cancel"), Gtk::RESPONSE_CANCEL);
	dialog.add_button(_("Open"), Gtk::RESPONSE_OK);
	int response = dialog.run();
	if (response != Gtk::RESPONSE_OK)
		return;
	string fn = dialog.get_file()->get_path();
	vector<string> cmd = {"addfile", fn};
	local_preprocess(cmd, cfg);
	fn = fs::path(fn).filename();
	unix_getline_X(fd, "addfile " + quote_string(fn) + '\n');
	refill();
}

void FilesPage::on_del()
{
	Gtk::TreeModel::iterator s = treeview.get_selection()->get_selected();
	if (!s) {
		delbtn.set_sensitive(false);
		return;
	}
	string name = s->get_value(cols.filename);
	if (name.empty())
		return;

	Gtk::MessageDialog md(string(_("Delete")) + ' ' + name + '?', false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_YES_NO, true);
	md.set_title("distadm");
	int ret = md.run();
	if (ret != Gtk::RESPONSE_YES)
		return;
	unix_getline_X(fd, "delfile " + quote_string(name) + '\n');
	refill();
}

