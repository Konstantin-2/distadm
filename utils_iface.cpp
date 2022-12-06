#include "utils_iface.h"
#include <termios.h>
#include <libintl.h>
#include <unistd.h>
#include <iostream>
#include <filesystem>
#ifdef USE_X
#include <gtkmm/label.h>
#include <gtkmm/entry.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/cssprovider.h>
#endif
#include "utils.h"
#define _(STRING) gettext(STRING)

namespace fs = std::filesystem;
using std::cin;
using std::cout;
using std::cerr;
using std::endl;
using std::flush;
using std::getline;
using std::string;

bool use_console = true;

#ifdef USE_X
bool useX = false;
#endif

#ifdef USE_X

static string file_dialog(Gtk::FileChooserAction act, Gtk::Window * win)
{
	Gtk::FileChooserDialog dialog(_("Choose a file"), act);
	if (win) dialog.set_transient_for(*win);
	dialog.set_modal(true);
	dialog.add_button(_("Cancel"), Gtk::RESPONSE_CANCEL);
	dialog.add_button(_("Open"), Gtk::RESPONSE_OK);
	int response = dialog.run();
	if (response != Gtk::RESPONSE_OK)
		return "";
	return fs::absolute(dialog.get_file()->get_path());
}

struct WinAskPwd : Gtk::Dialog {
	WinAskPwd(const string& prompt, Gtk::Window * = nullptr);
	void on_ok();
	bool on_key(GdkEventKey* event);

	Gtk::Label lbl;
	Gtk::Entry entry;
	Gtk::Button btn;
	std::string password;
};

struct WinAskPwd2 : Gtk::Dialog {
	WinAskPwd2(Gtk::Window* = nullptr);
	void on_ok();
	bool on_key1(GdkEventKey* event);
	bool on_key2(GdkEventKey* event);

	Gtk::Label lbl1, lbl2, lbl3;
	Gtk::Entry entry1, entry2;
	Gtk::Button btn;
	std::string password;
};

WinAskPwd::WinAskPwd(const string& prompt, Gtk::Window* w) :
	Gtk::Dialog("distadm", true),
	lbl(prompt),
	btn("OK")
{
	labels_1em({&lbl});
	if (w) set_transient_for(*w);
	Gtk::Box *ca = get_content_area();
	ca->add(lbl);
	ca->add(entry);
	ca->add(btn);
	entry.set_input_purpose(Gtk::INPUT_PURPOSE_PASSWORD);
	entry.set_visibility(false);
	btn.signal_clicked().connect(sigc::mem_fun(*this, &WinAskPwd::on_ok));
	entry.signal_key_press_event().connect(sigc::mem_fun(*this, &WinAskPwd::on_key), false);
	show_all();
}

void WinAskPwd::on_ok()
{
	password = entry.get_text();
	response(Gtk::RESPONSE_OK);
}

bool WinAskPwd::on_key(GdkEventKey* event)
{
	if (event->keyval == GDK_KEY_Return)
		on_ok();
	return false;
}

WinAskPwd2::WinAskPwd2(Gtk::Window * w) :
	Gtk::Dialog("distadm", true),
	lbl1(_("Password")),
	lbl2(_("Repeat password")),
	btn("OK")
{
	if (w) set_transient_for(*w);
	labels_1em({&lbl1, &lbl2});
	Gtk::Box *ca = get_content_area();
	ca->add(lbl1);
	ca->add(entry1);
	ca->add(lbl2);
	ca->add(entry2);
	ca->add(lbl3);
	ca->add(btn);
	entry1.set_input_purpose(Gtk::INPUT_PURPOSE_PASSWORD);
	entry1.set_visibility(false);
	entry2.set_input_purpose(Gtk::INPUT_PURPOSE_PASSWORD);
	entry2.set_visibility(false);
	btn.signal_clicked().connect(sigc::mem_fun(*this, &WinAskPwd2::on_ok));
	entry1.signal_key_press_event().connect(sigc::mem_fun(*this, &WinAskPwd2::on_key1), false);
	entry2.signal_key_press_event().connect(sigc::mem_fun(*this, &WinAskPwd2::on_key2), false);
	show_all();
}

void WinAskPwd2::on_ok()
{
	password = entry1.get_text();
	string pwd2 = entry2.get_text();
	if (password != pwd2) {
		lbl3.set_text(_("Passwords are different"));
		return;
	}
	response(Gtk::RESPONSE_OK);
}

bool WinAskPwd2::on_key1(GdkEventKey* event)
{
	if (event->keyval == GDK_KEY_Return)
		entry2.grab_focus();
	return false;
}

bool WinAskPwd2::on_key2(GdkEventKey* event)
{
	if (event->keyval == GDK_KEY_Return)
		on_ok();
	return false;
}

string get_open_file(Gtk::Window * win)
{
	return file_dialog(Gtk::FileChooserAction::FILE_CHOOSER_ACTION_OPEN, win);
}

string get_save_file(Gtk::Window * win)
{
	return file_dialog(Gtk::FileChooserAction::FILE_CHOOSER_ACTION_SAVE, win);
}

void labels_1em(std::initializer_list<Gtk::Label*> labels)
{
	Glib::RefPtr<Gtk::CssProvider> style_provider = Gtk::CssProvider::create();
	style_provider->load_from_data("label { margin: 1em; }");
	for (Gtk::Label* l : labels)
		l->get_style_context()->add_provider(style_provider, GTK_STYLE_PROVIDER_PRIORITY_FALLBACK);
}

void unix_getline_X(int fd, const string& str)
{
	string s = unix_getline(fd, str);
	while (!s.empty() && isspace(s.back())) s.pop_back();
	if (!s.empty())
		Gtk::MessageDialog(s).run();
}

#endif


bool prompt_yn_con(const string& prompt)
{
	if (!use_console)
		return false;
	string str;
	for (;;) {
		cout << prompt << " (yes/no): " << flush;
		if (!getline(cin, str))
			return false;
		if (str.empty())
			continue;
		if (str[0] == 'y' || str[0] == 'Y')
			return true;
		if (str[0] == 'n' || str[0] == 'N')
			return false;
	}
}

#ifdef USE_X
bool prompt_yn(const string& prompt)
{
	if (useX) {
		Gtk::MessageDialog md(prompt, false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_YES_NO, true);
		md.set_title("distadm");
		return md.run() == Gtk::RESPONSE_YES;
	} else
		return prompt_yn_con(prompt);
}
#else
bool prompt_yn(const string& prompt)
{
	return prompt_yn_con(prompt);
}
#endif

string readpasswd_con(const string& prompt)
{
	cout << prompt << ": " << flush;
	static struct termios oldt, newt;
	tcgetattr(STDIN_FILENO, &oldt);
	newt = oldt;
	newt.c_lflag &= ~(ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	string pwd;
	getline(cin, pwd);
	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	cout << endl;
	return pwd;
}

#ifdef USE_X
string readpasswd(const string& prompt, Gtk::Window * win)
{
	if (useX) {
		WinAskPwd dialog(prompt, win);
		int response = dialog.run();
		if (response != Gtk::RESPONSE_OK)
			return "";
		return std::move(dialog.password);
	} else
		return readpasswd_con(prompt);
}
#else
string readpasswd(const string& prompt)
{
	return readpasswd_con(prompt);
}
#endif

string readpasswd2_con()
{
	string pwd_str = readpasswd(_("Create password"));
	string pwd_str2 = readpasswd(_("Repeat password"));
	while (pwd_str != pwd_str2) {
		pwd_str2 = readpasswd(_("Password is different. Repeat again"));
		if (!cin)
			return "";
	}
	return pwd_str;
}

#ifdef USE_X
string readpasswd2(Gtk::Window * win)
{
	if (useX) {
		WinAskPwd2 dialog(win);
		int response = dialog.run();
		if (response != Gtk::RESPONSE_OK)
			return "";
		return std::move(dialog.password);
	} else
		return readpasswd2_con();
}
#else
string readpasswd2()
{
	return readpasswd2_con();
}
#endif

void print_message(const string& msg)
{
#ifdef USE_X
	Gtk::MessageDialog(msg).run();
#else
	cerr << msg << endl;
#endif
}
