#pragma once
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>

struct MesgsPage : Gtk::ScrolledWindow {
	MesgsPage();
	void refill();
	Gtk::Label output;
	int fd = -1;
};
