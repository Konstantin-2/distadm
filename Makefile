EXECUTABLE = distadm
DESTDIR ?= /
prefix ?= $(DESTDIR)/usr/local
exec_prefix ?= $(prefix)
sbindir ?= $(exec_prefix)/sbin
datarootdir ?= $(prefix)/share

#read only, local only
sysconfdir ?= $(prefix)/etc

#writable, local only
localstatedir ?= $(DESTDIR)/var/local

#writable, short live period (while program run)
runstatedir ?= $(localstatedir)/run

#where Makefile
srcdir ?= .

# for console version
LDFLAGS += -Wall -Wl,-pie -flto -pipe -lpthread -luuid `pkg-config --libs jsoncpp gnutls libmd libbsd zlib readline`
CXXFLAGS += -std=c++20 -pipe -I$(srcdir) `pkg-config --cflags jsoncpp gnutls libmd libbsd zlib` -O2 -march=native -D NDEBUG -DDATAROOTDIR=\"$(datarootdir)\" -D CONFIG_FILE=\"$(sysconfdir)/$(EXECUTABLE)\" -D DIR_LOCALSTATE=\"$(localstatedir)/$(EXECUTABLE)\" -D RUN_DIR=\"$(runstatedir)\" -D SHARE_DIR=\"$(datarootdir)/$(EXECUTABLE)\" -fpie
#CXXFLAGSD = $(CXXFLAGS) -O0 -Wall -ggdb -U NDEBUG
OBJS = alarmer.o bdmsg.o ccstream.o cmd_local.o commands.o config.o coremt.o corenet.o core.o cryptkey.o daemon.o incm.o interactive.o locdatetime.o main.o network.o sha.o showdebug.o tmpdir.o usernames.o utils.o uuid.o warn.o utils_iface.o

# for GTK version
ifndef NO_X
LDFLAGS += `pkg-config --libs gtkmm-3.0`
CXXFLAGS += `pkg-config --cflags gtkmm-3.0` -D USE_X
OBJS += gtk_mesgspage.o gtk_execpage.o gtk_filespage.o gtk_localpage.o gtk_nodespage.o gtk_queuepage.o gtk_userspage.o iface_info.o iface_main.o
endif

.PHONY : clean install uninstall deb

all : $(EXECUTABLE) ru/LC_MESSAGES/$(EXECUTABLE).mo

$(EXECUTABLE) : $(OBJS)
	@echo 'LINK $@'
	@$(CXX) -o $@ $^ $(LDFLAGS)

%.o : $(srcdir)/%.cpp
	@echo 'CPP  $@'
	@$(CXX) $(CXXFLAGS) -c $<

$(EXECUTABLE).pot : *.cpp
	@echo 'POT  $@'
	xgettext -k_ --c++ -s --no-wrap --omit-header --no-location --from-code=UTF-8 -o $@ $^

ru/$(EXECUTABLE).po : $(EXECUTABLE).pot
	@echo 'PO   $@'
	msgmerge --update --no-wrap $@ $<
	touch $@

ru/LC_MESSAGES/$(EXECUTABLE).mo : ru/$(EXECUTABLE).po
	@echo 'MO   $@'
	@mkdir -p ru/LC_MESSAGES
	@msgfmt --output $@ $<

i18n : ru/LC_MESSAGES/$(EXECUTABLE).mo

clean :
	$(RM) *.o $(EXECUTABLE) ru/$(EXECUTABLE).po~ ru/LC_MESSAGES/$(EXECUTABLE).mo
	$(RM) -r deb

run : $(EXECUTABLE)
	@./$(EXECUTABLE)

copy : $(EXECUTABLE)
	sudo -u user scp *.cpp *.h $(EXECUTABLE) slave:/home/user/src/distadm/

copyid : $(EXECUTABLE)
	sudo -u user scp group-id slave:/home/user/src/distadm/group-id

install : $(EXECUTABLE) ru/LC_MESSAGES/$(EXECUTABLE).mo
	mkdir -p $(sbindir)
	install -g 0 -o 0 -m 755 -s $(EXECUTABLE) $(sbindir)
	mkdir -p $(localstatedir)/$(EXECUTABLE)/files
	chmod 0600 $(localstatedir)/$(EXECUTABLE)
	mkdir -p $(runstatedir)
	mkdir -p $(datarootdir)/locale/ru
	cp -r $(srcdir)/ru/LC_MESSAGES $(datarootdir)/locale/ru
	mkdir -p $(datarootdir)/man/man8
	install -g 0 -o 0 -m 0644 $(srcdir)/$(EXECUTABLE).8 $(datarootdir)/man/man8
	gzip -f $(datarootdir)/man/man8/$(EXECUTABLE).8
	mkdir -p $(datarootdir)/man/ru/man8
	install -g 0 -o 0 -m 0644 $(srcdir)/ru/$(EXECUTABLE).8 $(datarootdir)/man/ru/man8
	gzip -f $(datarootdir)/man/ru/man8/$(EXECUTABLE).8
	mkdir -p $(sysconfdir)
	cp -n $(srcdir)/$(EXECUTABLE).cfg $(sysconfdir)/$(EXECUTABLE)
	mkdir -p $(DESTDIR)/etc/systemd/system
	cp $(srcdir)/$(EXECUTABLE).service $(DESTDIR)/etc/systemd/system
ifndef NO_X
	mkdir -p $(datarootdir)/applications
	cp *.desktop $(datarootdir)/applications
	mkdir -p $(datarootdir)/$(EXECUTABLE)
	cp $(srcdir)/images/*.png $(datarootdir)/$(EXECUTABLE)
endif

uninstall :
	$(RM) $(sbindir)/$(EXECUTABLE)
	$(RM) -r $(datarootdir)/$(EXECUTABLE)
	$(RM) $(datarootdir)/locale/ru/LC_MESSAGES/$(EXECUTABLE).mo
	$(RM) $(datarootdir)/man/man8/$(EXECUTABLE).1.gz
	$(RM) $(datarootdir)/man/ru/man8/$(EXECUTABLE).1.gz
	$(RM) $(DESTDIR)/etc/systemd/system/$(EXECUTABLE).service
	$(RM) $(datarootdir)/man/man8/$(EXECUTABLE).8.gz
	$(RM) $(datarootdir)/man/ru/man8/$(EXECUTABLE).8.gz
	$(RM) $(datarootdir)/applications/distadm.desktop
	$(RM) $(datarootdir)/applications/distadm-info.desktop
	$(RM) $(sysconfdir)/$(EXECUTABLE)
	$(RM) $(localstatedir)/$(EXECUTABLE)/files

$(EXECUTABLE).deb :
	mkdir -p deb/DEBIAN
	chmod +x calcmd5.sh for_deb/postinst for_deb/prerm
	cp -u $(srcdir)/for_deb/conffiles $(srcdir)/for_deb/control $(srcdir)/for_deb/postinst $(srcdir)/for_deb/prerm deb/DEBIAN
	DESTDIR=deb make install -j
	mkdir -p deb/usr/share/polkit-1/actions
	cp -u $(srcdir)/distadm.policy deb/usr/share/polkit-1/actions/local.distadm.policy
	./calcmd5.sh
	dpkg-deb --build deb
	mv deb.deb $@

$(EXECUTABLE)-console.deb :
	mkdir -p deb/DEBIAN
	chmod +x calcmd5.sh for_deb/postinst for_deb/prerm
	cp -u $(srcdir)/for_deb/conffiles $(srcdir)/for_deb/postinst $(srcdir)/for_deb/prerm deb/DEBIAN
	cp -u $(srcdir)/for_deb/controlc deb/DEBIAN/control
	DESTDIR=deb NO_X=y make install -j
	./calcmd5.sh
	dpkg-deb --build deb
	mv deb.deb $@
