#pragma once
#include <memory>
#include <gtkmm/box.h>
#include <gtkmm/treeview.h>
#include <gtkmm/toolbutton.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/liststore.h>
#include <gtkmm/filechooserdialog.h>
#include "treeview_s.h"
#include "core.h"

struct FilesPage : Gtk::Box {
	struct FilesCols : public Gtk::TreeModel::ColumnRecord {
		FilesCols();
		Gtk::TreeModelColumn<Glib::ustring> filename;
	};

	FilesPage(Config&, Gtk::Window&);
	void refill();
	void on_click_row();
	void on_add();
	void on_del();

	FilesCols cols;
	Gtk::ScrolledWindow swnd;
	Glib::RefPtr<Gtk::ListStore> list;
	TreeViewSort treeview;
	Gtk::Box toolbox;
	Gtk::ToolButton addbtn;
	Gtk::ToolButton delbtn;
	Config& cfg;
	Gtk::Window& window;
	int fd = -1;
};
