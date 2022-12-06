#pragma once
#include <memory>
#include <gtkmm/box.h>
#include <gtkmm/treeview.h>
#include <gtkmm/toolbutton.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/liststore.h>
#include "treeview_s.h"

struct UsersPage : Gtk::Box {
	struct UsersCols : public Gtk::TreeModel::ColumnRecord {
		UsersCols();
		Gtk::TreeModelColumn<Glib::ustring> username;
	};

	UsersPage(Gtk::Window&);
	void refill();
	void on_click_row();
	void on_add();
	void on_del();

	UsersCols cols;
	Gtk::ScrolledWindow swnd;
	Glib::RefPtr<Gtk::ListStore> list;
	TreeViewSort treeview;
	Gtk::Box toolbox;
	Gtk::ToolButton addbtn;
	Gtk::ToolButton delbtn;
	Gtk::Window& parent;
	int fd = -1;
};
