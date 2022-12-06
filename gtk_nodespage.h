#pragma once
#include <memory>
#include <vector>
#include <gtkmm/box.h>
#include <gtkmm/treeview.h>
#include <gtkmm/toolbutton.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/liststore.h>
#include "treeview_s.h"

struct NodesPage : Gtk::Box {
	struct NodesCols : Gtk::TreeModel::ColumnRecord {
		NodesCols(int x);

		Gtk::TreeModelColumn<size_t> N;
		Gtk::TreeModelColumn<Glib::ustring> uuid;
		Gtk::TreeModelColumn<Glib::ustring> nodename;
		Gtk::TreeModelColumn<Glib::ustring> online;
		std::vector<Gtk::TreeModelColumn<Glib::ustring>> cols;
		Gtk::TreeModelColumn<Glib::ustring> netmsgcnt;
	};

	NodesPage();
	void refill();
	void on_click_row();
	void on_del();

	std::unique_ptr<NodesCols> cols;
	Gtk::ScrolledWindow swnd;
	Gtk::Box toolbox;
	TreeViewSort treeview;
	Glib::RefPtr<Gtk::ListStore> list;
	Gtk::ToolButton delbtn;
	int fd = -1;
};
