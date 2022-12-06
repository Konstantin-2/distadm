#pragma once
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>

struct LocalPage : Gtk::ScrolledWindow {
	LocalPage();
	void refill();
	Gtk::Label output;
	int fd = -1;
};
