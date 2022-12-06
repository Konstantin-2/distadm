#pragma once
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/liststore.h>
#include "treeview_s.h"

struct QueuePage : Gtk::ScrolledWindow {
	struct QueueCols : public Gtk::TreeModel::ColumnRecord {
		QueueCols();
		Gtk::TreeModelColumn<Glib::ustring> nodename;
		Gtk::TreeModelColumn<size_t> qlen;
	};

	QueuePage();
	void refill();

	QueueCols cols;
	Glib::RefPtr<Gtk::ListStore> list;
	TreeViewSort treeview;
	int fd = -1;
};
