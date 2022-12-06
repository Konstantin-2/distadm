#include "gtk_execpage.h"
#include <gtkmm/stock.h>
#include <libintl.h>
#include <iostream>
#include "utils.h"
#include "utils_iface.h"
#define _(STRING) gettext(STRING)

using std::vector;
using std::string;

ExecPage::ExecPage() :
	Gtk::Box(Gtk::Orientation::ORIENTATION_VERTICAL),
	toolbox(Gtk::Orientation::ORIENTATION_HORIZONTAL),
	addbtn(Gtk::Stock::EXECUTE),
	delbtn(Gtk::Stock::DELETE),
	sh("sh:"),
	input()
{
	swnd.set_vexpand(true);
	input.set_hexpand(true);
	add(swnd);
	swnd.add(output);
	add(toolbox);
	toolbox.add(delbtn);
	toolbox.add(sh);
	toolbox.add(input);
	toolbox.add(addbtn);
	addbtn.set_tooltip_text(_("Execute command"));
	delbtn.set_tooltip_text(_("Clear"));
	addbtn.signal_clicked().connect(sigc::mem_fun(*this, &ExecPage::new_cmd));
	delbtn.signal_clicked().connect(sigc::mem_fun(*this, &ExecPage::on_del));
	input.signal_key_press_event().connect(sigc::mem_fun(*this, &ExecPage::on_key), false);
}

bool ExecPage::on_key(GdkEventKey* event)
{
	if (event->keyval == GDK_KEY_Return)
		new_cmd();
	return false;
}

void ExecPage::refill()
{
	if (fd == -1)
		return;
	output.set_text(unix_getline(fd, "showexec\n"));
}

void ExecPage::on_del()
{
	unix_getline(fd, "delexec\n");
	refill();
}

void ExecPage::new_cmd()
{
	string cmd = input.get_text();
	if (cmd.empty())
		return;
	input.set_text("");
	unix_getline_X(fd, "exec " + cmd + '\n');
	refill();
}
