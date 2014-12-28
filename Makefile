define DESCR
Description: Playlist daemon
 Playlist daemon for the Raspberry Pi using omxplayer for playback
endef
export DESCR

SHELL := bash
REL := .release
omxd: omxd.c omxd.h client.c player.c utils.c m_list.c Makefile omxd_help.h version.h
	gcc -g -o omxd omxd.c client.c player.c utils.c m_list.c
omxd_help.h: README Makefile
	sed -rn '1,/^\.$$/ s/^(.*)$$/"\1\\n"/p' README >omxd_help.h
%.h: %.txt Makefile
	@sed -e 's/[ \t]*$$//g' -e 's/^/"/g' -e 's/$$/\\n"/g' <$< >$@
omxd.1: README Makefile
	sed -n '1,/^\.$$/p' README >README.omxd
	curl -F page=@README.omxd http://mantastic.herokuapp.com > omxd.1
	rm README.omxd
rpyt.1: README Makefile
	sed -n '/rpyt/,$$p' README >README.rpyt
	curl -F page=@README.rpyt http://mantastic.herokuapp.com > rpyt.1
	rm README.rpyt
install:
	cp omxd $(DESTDIR)/usr/bin
	IFS=''; while read i; do [ "$$i" = HELP ] && sed -n '/rpyt/,$$p' README; echo "$$i"; done <rpyt >$(DESTDIR)/usr/bin/rpyt
	chmod +x $(DESTDIR)/usr/bin/rpyt
	cp logrotate $(DESTDIR)/etc/logrotate.d/omxd
	cp omxd.1 $(DESTDIR)/usr/share/man/man1/
	cp rpyt.1 $(DESTDIR)/usr/share/man/man1/
	-perl -ne 'print unless /omxd/' -i $(DESTDIR)/etc/rc.local # Auto migrate from rc.local
	cp init $(DESTDIR)/etc/init.d/omxd
	-./postinst
uninstall: stop
	-perl -ne 'print unless /omxd/' -i $(DESTDIR)/etc/rc.local # Auto migrate from rc.local
	rm $(DESTDIR)/etc/init.d/omxd
	rm $(DESTDIR)/usr/bin/omxd
	rm $(DESTDIR)/usr/bin/rpyt
	rm $(DESTDIR)/etc/logrotate.d/omxd
	rm $(DESTDIR)/usr/share/man/man1/omxd.1
	rm $(DESTDIR)/usr/share/man/man1/rpyt.1
stop:
	-./postrm
	-service omxd stop
	-killall omxd
	-killall omxplayer.bin
clean:
	-rm omxd m_list omxplay omxlog omxctl omxd_help.h omxd.pid st version.h
	-rm rpyt.fifo jar
	-rm -rf .release
# Testing
m_list: test_m_list.c m_list.c utils.c omxd.h
	gcc -g -o m_list test_m_list.c m_list.c utils.c
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
	$$EDITOR changelog; \
	git tag -a -F changelog $$TAG HEAD; \
	rm changelog
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
	for i in `git tag | sort -rg`; do git show $$i | sed -n '/^omxd/,/^ --/p'; done \
	| sed -r 's/^omxd \((.+)\)$$/omxd (\1-1) UNRELEASED; urgency=low/' \
	| sed -r 's/^(.{,79}).*/\1/' \
	> $(DEB)/changelog
	echo '#!/usr/bin/make -f' > $(DEB)/rules
	echo '%:'                >> $(DEB)/rules
	echo '	dh $$@'          >> $(DEB)/rules
	echo usr/bin             > $(DEB)/omxd.dirs
	echo usr/share/man/man1 >> $(DEB)/omxd.dirs
	echo etc/logrotate.d    >> $(DEB)/omxd.dirs
	echo etc/init.d         >> $(DEB)/omxd.dirs
	chmod 755 $(DEB)/rules
	mkdir -p $(DEB)/source
	echo '3.0 (quilt)' > $(DEB)/source/format
	cp postinst postrm $(DEB)
	@cd $(REL)/omxd-$(TAG) && \
	echo && echo List of PGP keys for signing package: && \
	gpg -K | grep uid && \
	read -ep 'Enter key ID (part of name or alias): ' KEYID; \
	if [ "$$KEYID" ]; then \
	  dpkg-buildpackage -k$$KEYID; \
	else \
	  dpkg-buildpackage -us -uc; \
	fi
	lintian $(REL)/*.deb
	fakeroot alien -kr $(REL)/*.deb; mv *.rpm $(REL)
release: tag deb
