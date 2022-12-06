#include "utils.h"
#include "gtk_mesgspage.h"

MesgsPage::MesgsPage()
{
	set_vexpand(true);
	add(output);
}

void MesgsPage::refill()
{
	if (fd == -1)
		return;
	output.set_text(unix_getline(fd, "stored-commands\n"));
}
