#include "gtk_queuepage.h"
#include <libintl.h>
#include <sstream>
#include "utils.h"
#include "warn.h"
#define _(STRING) gettext(STRING)

using std::string;
using std::string_view;
using std::vector;
using std::istringstream;

QueuePage::QueueCols::QueueCols()
{
	add(nodename);
	add(qlen);
}

QueuePage::QueuePage() :
	list(Gtk::ListStore::create(cols)),
	treeview(list)
{
	set_vexpand(true);
	add(treeview);
	treeview.append_column(_("Node"), cols.nodename);
	treeview.append_column(_("Queue length"), cols.qlen);
}

void QueuePage::refill()
{
	if (fd == -1)
		return;
	string ans = unix_getline(fd, "queue\n");
	vector<string_view> lines = split(ans, false, '\n');
	list->clear();
	for (const string_view& row : lines) {
		vector<string_view> items = split(row);
		if (items.size() != 2) {
			warn << _("Bad answer from daemon");
			continue;
		}
		size_t len;
		istringstream(string(items[1])) >> len;

		auto r = *(list->append());
		r[cols.nodename] = string(items[0]);
		r[cols.qlen] = len;
	}
}
