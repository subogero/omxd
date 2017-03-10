define DESCR
Description: Playlist daemon
 Playlist daemon for the Raspberry Pi using omxplayer for playback
endef
export DESCR

SHELL := bash
REL := .release
omxd: omxd.c omxd.h client.c player.c utils.c m_list.c Makefile omxd_help.h version.h
	gcc -Wall -g -o omxd omxd.c client.c player.c utils.c m_list.c
omxd_help.h: README Makefile
	sed -rn '1,/^\.$$/ s/^(.*)$$/"\1\\n"/p' README >omxd_help.h
%.h: %.txt Makefile
	@sed -e 's/[ \t]*$$//g' -e 's/^/"/g' -e 's/$$/\\n"/g' <$< >$@
omxd.1: README Makefile
	curl -F page=@README http://mantastic.herokuapp.com > omxd.1
install:
	-./preinst
	cp omxd $(DESTDIR)/usr/bin
	cp omxd.1 $(DESTDIR)/usr/share/man/man1/
	-mkdir -p $(DESTDIR)/usr/share/doc/omxd
	cp init $(DESTDIR)/usr/share/doc/omxd/
	cp omxd.service $(DESTDIR)/usr/share/doc/omxd/
	cp logrotate $(DESTDIR)/usr/share/doc/omxd/
	cp omxwd $(DESTDIR)/usr/bin
	-perl -lne 'print unless /^omxd$$/' -i $(DESTDIR)/etc/rc.local # Auto migrate from rc.local
uninstall:
	-./prerm
	-perl -lne 'print unless /^omxd$$/' -i $(DESTDIR)/etc/rc.local # Auto migrate from rc.local
	rm $(DESTDIR)/usr/bin/omxd
	-rm $(DESTDIR)/usr/bin/rpyt
	rm $(DESTDIR)/usr/share/man/man1/omxd.1
	-rm $(DESTDIR)/usr/share/man/man1/rpyt.1
	rm $(DESTDIR)/usr/share/doc/omxd/init
	rm $(DESTDIR)/usr/share/doc/omxd/logrotate
	rm $(DESTDIR)/usr/bin/omxwd
	-./postrm
start:
	-./postinst
stop:
	-./prerm
	-./postrm
	-killall omxd
	-killall omxplayer.bin
restart: stop install start
clean:
	-rm omxd m_list utils omxplay omxlog omxctl omxd_help.h omxd.pid st version.h
	-rm -rf .release
# Testing
m_list: test_m_list.c m_list.c utils.c omxd.h
	gcc -g -o m_list test_m_list.c m_list.c utils.c
utils: utils.c test_utils.c omxd.h
	gcc -g -o utils test_utils.c utils.c
debug: omxd stop
	strace -p `./omxd -d | sed 's/.*PID //'` -o st &
ps:
	pstree -pu | grep omx
# Release
tag:
	@git status | grep -q 'nothing to commit' || ( echo Worktree dirty; exit 1 )
	@echo 'Chose old tag to follow: '; \
	select OLD in `git tag`; do break; done; \
	export TAG; \
	read -p 'Please Enter new tag name: ' TAG; \
	sed -r -e "s/^omxd.*$$/omxd $$TAG/" \
	       -e 's/([0-9]{4}-)[0-9]*/\1'`date +%Y`/ \
	       -i version.txt || exit 1; \
	git commit -a -m "version $$TAG"; \
	echo Adding git tag $$TAG; \
	echo "omxd ($$TAG)" > changelog; \
	if [ -n "$$OLD" ]; then \
	  git log --pretty=format:"  * %h %an %s" $$OLD.. >> changelog; \
	  echo >> changelog; \
	else \
	  echo '  * Initial release' >> changelog; \
	fi; \
	echo " -- `git config user.name` <`git config user.email`>  `date -R`" >> changelog; \
	git tag -a -F changelog $$TAG HEAD; \
	rm changelog
utag:
	TAG=`git log --oneline --decorate | head -n1 | sed -rn 's/^.+ version (.+)/\1/p'`; \
	[ "$$TAG" ] && git tag -d $$TAG && git reset --hard HEAD^
tarball: clean
	export TAG=`sed -rn 's/^omxd (.+)$$/\1/p' version.txt`; \
	$(MAKE) balls
balls:
	mkdir -p $(REL)/omxd-$(TAG); \
	cp -rt $(REL)/omxd-$(TAG) *; \
	cd $(REL); \
	tar -czf omxd_$(TAG).tar.gz omxd-$(TAG)
deb: tarball omxd
	export TAG=`sed -rn 's/^omxd (.+)$$/\1/p' version.txt`; \
	export DEB=$(REL)/omxd-$${TAG}/debian; \
	$(MAKE) debs
debs:
	-rm $(REL)/*.deb
	cp -f $(REL)/omxd_$(TAG).tar.gz $(REL)/omxd_$(TAG).orig.tar.gz
	mkdir -p $(DEB)
	echo 'Source: omxd'                                           >$(DEB)/control
	echo 'Section: video'                                        >>$(DEB)/control
	echo 'Priority: optional'                                    >>$(DEB)/control
	sed -nr 's/^C.+ [-0-9]+ (.+)$$/Maintainer: \1/p' version.txt >>$(DEB)/control
	echo 'Build-Depends: debhelper             '                 >>$(DEB)/control
	echo 'Standards-version: 3.8.4'                              >>$(DEB)/control
	echo                                                         >>$(DEB)/control
	echo 'Package: omxd'                                         >>$(DEB)/control
	echo 'Architecture: any'                                     >>$(DEB)/control
	echo 'Depends: $${shlibs:Depends}, $${misc:Depends} omxplayer' >>$(DEB)/control
	echo "$$DESCR"                                               >>$(DEB)/control
	grep Copyright version.txt                    >$(DEB)/copyright
	echo 'License: GNU GPL v2'                   >>$(DEB)/copyright
	echo ' See /usr/share/common-licenses/GPL-2' >>$(DEB)/copyright
	echo 7 > $(DEB)/compat
	for i in `git tag | grep '^[0-9]' | sort -rV`; do git show $$i | sed -n '/^omxd/,/^ --/p'; done \
	| sed -r 's/^omxd \((.+)\)$$/omxd (\1-1) UNRELEASED; urgency=low/' \
	| sed -r 's/^(.{,79}).*/\1/' \
	> $(DEB)/changelog
	echo '#!/usr/bin/make -f' > $(DEB)/rules
	echo '%:'                >> $(DEB)/rules
	echo '	dh $$@'          >> $(DEB)/rules
	echo usr/bin             > $(DEB)/omxd.dirs
	echo usr/share/man/man1 >> $(DEB)/omxd.dirs
	echo usr/share/doc/omxd >> $(DEB)/omxd.dirs
	chmod 755 $(DEB)/rules
	mkdir -p $(DEB)/source
	echo '3.0 (quilt)' > $(DEB)/source/format
	cp preinst postinst prerm postrm $(DEB)
	@cd $(REL)/omxd-$(TAG) && \
	echo && echo List of PGP keys for signing package: && \
	gpg -K | grep uid && \
	read -ep 'Enter key ID (part of name or alias): ' KEYID; \
	if [ "$$KEYID" ]; then \
	  dpkg-buildpackage -k$$KEYID; \
	else \
	  dpkg-buildpackage -us -uc; \
	fi
	fakeroot alien -kr --scripts $(REL)/*.deb; mv *.rpm $(REL)
release: tag deb
