#include "gtk_localpage.h"
#include <iostream>
#include <libintl.h>
#include "utils.h"
#define _(STRING) gettext(STRING)

using std::cout;
using std::endl;
using std::vector;
using std::ostringstream;
using std::string;
using std::string_view;

LocalPage::LocalPage()
{
	set_vexpand(true);
	add(output);
}

void LocalPage::refill()
{
	if (fd == -1)
		return;
	string ans = unix_getline(fd, "local-id\n");
	vector<string_view> s = split(ans, true, '/');
	while (s.size() < 2) s.push_back("");

	ostringstream str;
	str << _("Group UUID") << ": " << s[1] << '\n';
	str << _("Node UUID") << ": " << s[0] << '\n';
	str << _("Node status") << ": " << unix_getline(fd, "status\n");
	output.set_text(str.str());
}
