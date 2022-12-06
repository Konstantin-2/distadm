#pragma once
#include <iostream>
#include <gtkmm/treeview.h>
#include <gtkmm/liststore.h>

struct TreeViewSort : Gtk::TreeView
{
	TreeViewSort() = default;
	TreeViewSort(Glib::RefPtr<Gtk::ListStore>& l) : Gtk::TreeView(l) {}
	template <typename T>
	int append_column(const std::string& name, const Gtk::TreeModelColumn<T>& col)
	{
		int x = Gtk::TreeView::append_column(name, col);
		auto pColumn = get_column(cnt++);
		if(pColumn) pColumn->set_sort_column(col);
		return x;
	}
	int cnt = 0;
};
