#include <gtkmm/messagedialog.h>
#include "utils.h"
#include "gtk_msg.h"

using std::string;

void unix_getline_X(int fd, const string& str)
{
	string s = unix_getline(fd, str);
	while (!s.empty() && isspace(s.back())) s.pop_back();
	if (!s.empty())
		Gtk::MessageDialog md(s);
}
