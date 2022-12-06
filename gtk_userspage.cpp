#include "gtk_userspage.h"
#include <gtkmm/stock.h>
#include <gtkmm/messagedialog.h>
#include <libintl.h>
#include <iostream>
#include "utils.h"
#include "utils_iface.h"
#include "warn.h"
#define _(STRING) gettext(STRING)

using std::cout;
using std::endl;
using std::vector;
using std::string;
using std::string_view;

struct NewUserDlg : Gtk::Dialog {
	NewUserDlg(Gtk::Window&);
	void on_ok();
	Gtk::Label l1, l2, l3, l4;
	Gtk::Entry e1, e2, e3;
	Gtk::Button btn;
	string login, password;
};

//------------------------------

UsersPage::UsersCols::UsersCols()
{
	add(username);
}

UsersPage::UsersPage(Gtk::Window& p) :
	Gtk::Box(Gtk::Orientation::ORIENTATION_VERTICAL),
	list(Gtk::ListStore::create(cols)),
	treeview(list),
	toolbox(Gtk::Orientation::ORIENTATION_HORIZONTAL),
	addbtn(Gtk::Stock::ADD),
	delbtn(Gtk::Stock::DELETE),
	parent(p)
{
	swnd.set_vexpand(true);
	add(swnd);
	swnd.add(treeview);
	treeview.append_column(_("Users"), cols.username);
	add(toolbox);
	toolbox.add(addbtn);
	toolbox.add(delbtn);
	addbtn.set_tooltip_text(_("Add user"));
	delbtn.set_tooltip_text(_("Delete user"));
	addbtn.signal_clicked().connect(sigc::mem_fun(*this, &UsersPage::on_add));
	delbtn.signal_clicked().connect(sigc::mem_fun(*this, &UsersPage::on_del));
	treeview.signal_cursor_changed().connect(sigc::mem_fun(*this, &UsersPage::on_click_row));
}

void UsersPage::refill()
{
	if (fd == -1)
		return;
	string  ans = unix_getline(fd, "listusers\n");
	vector<string_view> names = split(ans);
	list->clear();
	for (const string_view& row : names) {
		auto r = *(list->append());
		r[cols.username] = string(row);
	}
	delbtn.set_sensitive(false);
}

void UsersPage::on_click_row()
{
	delbtn.set_sensitive(true);
}

void UsersPage::on_add()
{
	NewUserDlg dialog(parent);
	int response = dialog.run();
	if (response != Gtk::RESPONSE_OK)
		return;
	exec_prog({"userdel", dialog.login});
	if (!exec_prog({"adduser", dialog.login})) {
		warn << _("Error add user") << ' ' << dialog.login;
		return;
	}
	string str = dialog.password + '\n' + dialog.password + '\n';
	if (!exec_prog_input({"passwd", dialog.login}, str)) {
		warn << _("Error set password for user") << ' ' << dialog.login;
		return;
	}
	unix_getline_X(fd, "adduser " + dialog.login + " quiet\n");
	refill();
}

void UsersPage::on_del()
{
	Gtk::TreeModel::iterator s = treeview.get_selection()->get_selected();
	if (!s) {
		delbtn.set_sensitive(false);
		return;
	}
	string name = s->get_value(cols.username);

	Gtk::MessageDialog md(string(_("Delete")) + ' ' + name + '?', false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_YES_NO, true);
	md.set_title("distadm");
	int ret = md.run();
	if (ret != Gtk::RESPONSE_YES)
		return;
	unix_getline_X(fd, "deluser " + name + "\n");
	refill();
}

NewUserDlg::NewUserDlg(Gtk::Window& p) :
	Gtk::Dialog("distadm", true),
	l1(_("New login"), Gtk::Align::ALIGN_START),
	l2(_("Password"), Gtk::Align::ALIGN_START),
	l3(_("Repeat password"), Gtk::Align::ALIGN_START),
	btn("OK")
{
	labels_1em({&l1, &l2, &l3});
	set_transient_for(p);
	set_default_size(200, 200);
	Gtk::Box *ca = get_content_area();
	ca->add(l1);
	ca->add(e1);
	ca->add(l2);
	ca->add(e2);
	ca->add(l3);
	ca->add(e3);
	ca->add(l4);
	ca->add(btn);
	e2.set_input_purpose(Gtk::INPUT_PURPOSE_PASSWORD);
	e2.set_visibility(false);
	e3.set_input_purpose(Gtk::INPUT_PURPOSE_PASSWORD);
	e3.set_visibility(false);
	btn.signal_clicked().connect(sigc::mem_fun(*this, &NewUserDlg::on_ok));
	show_all();
}

void NewUserDlg::on_ok()
{
	login = e1.get_text();
	for (char ch : login)
		if (!isalnum(ch)) {
			l4.set_text(_("Bad user name"));
			return;
		}
	password = e2.get_text();
	string pwd2 = e3.get_text();
	if (password != pwd2) {
		l4.set_text(_("Passwords are different"));
		return;
	}
	response(Gtk::RESPONSE_OK);
}
