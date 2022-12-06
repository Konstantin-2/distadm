#pragma once
#include <string>
#ifdef USE_X
#include <gtkmm/window.h>
#endif

// Unteractive with user via console (true) or there is no user (false)
extern bool use_console;

/* Show prompt added by  " (yes/no): " and ask user to select yes/no.
 * Can return 'y', 'n', '\0' (EOF) */
bool prompt_yn(const std::string& prompt);

// Print message to console or GTK (if GTK used)
void print_message(const std::string& msg);

#ifdef USE_X //GTK versions

// Unteractive with user via console (false) or GTK dialogs (true)
extern bool useX;

// Show prompt and ask user to enter password on console
std::string readpasswd(const std::string& prompt, Gtk::Window * = nullptr);

/* Show prompt and ask user to enter password on console like readpasswd()
 * but ask to enter same password twice to be sure password is correct. */
std::string readpasswd2(Gtk::Window * = nullptr);

/* Show GTK dialog to open file and return file specified or empty string */
std::string get_open_file(Gtk::Window * = nullptr);

/* Show GTK dialog to save file and return file specified or empty string */
std::string get_save_file(Gtk::Window * = nullptr);

// Set margin 1em for provided labels
void labels_1em(std::initializer_list<Gtk::Label*>);

/* Pass 2nd parameter to daemon vi 1st parameter socket.
 * Output answer in GTK message dialog */
void unix_getline_X(int fd, const std::string&);

#else // Console only versions

std::string readpasswd(const std::string& prompt);
std::string readpasswd2();
std::string get_open_file();
std::string get_save_file();

#endif
