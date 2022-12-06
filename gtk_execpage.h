#pragma once
#include <memory>
#include <gtkmm/box.h>
#include <gtkmm/treeview.h>
#include <gtkmm/toolbutton.h>
#include <gtkmm/scrolledwindow.h>

struct ExecPage : Gtk::Box {
	ExecPage();
	void refill();
	void on_del();
	void new_cmd();
	bool on_key(GdkEventKey* event);

	Gtk::ScrolledWindow swnd;
	Gtk::Label output;
	Gtk::Box toolbox;
	Gtk::ToolButton addbtn;
	Gtk::ToolButton delbtn;
	Gtk::Label sh;
	Gtk::Entry input;
	int fd = -1;
};
