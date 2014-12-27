SHELL = bash
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
install: stop
	cp omxd $(DESTDIR)/usr/bin
	IFS=''; while read i; do [ "$$i" = HELP ] && sed -n '/rpyt/,$$p' README; echo "$$i"; done <rpyt >/usr/bin/rpyt
	chmod +x $(DESTDIR)/usr/bin/rpyt
	cp logrotate $(DESTDIR)/etc/logrotate.d/omxd
	cp omxd.1 $(DESTDIR)/usr/share/man/man1/
	cp rpyt.1 $(DESTDIR)/usr/share/man/man1/
	perl -ne 'print unless /omxd/' -i $(DESTDIR)/etc/rc.local # Auto migrate from rc.local
	cp init $(DESTDIR)/etc/init.d/omxd
	update-rc.d omxd defaults
	service omxd start
uninstall: stop
	perl -ne 'print unless /omxd/' -i $(DESTDIR)/etc/rc.local # Auto migrate from rc.local
	rm $(DESTDIR)/etc/init.d/omxd
	update-rc.d omxd remove
	rm $(DESTDIR)/usr/bin/omxd
	rm $(DESTDIR)/usr/bin/rpyt
	rm $(DESTDIR)/etc/logrotate.d/omxd
	rm $(DESTDIR)/usr/share/man/man1/omxd.1
	rm $(DESTDIR)/usr/share/man/man1/rpyt.1
stop:
	-service omxd stop
	-killall omxd
	-killall omxplayer.bin
clean: stop
	-rm omxd m_list omxplay omxlog omxctl omxd_help.h omxd.pid st
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
