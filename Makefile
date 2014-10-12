omxd: omxd.c playlist.c omxd.h client.c player.c Makefile omxd_help.h
	gcc -g -o omxd omxd.c playlist.c client.c player.c
omxd_help.h: README Makefile
	sed -rn '1,/^\.$$/ s/^(.*)$$/"\1\\n"/p' README >omxd_help.h
omxd.1: README Makefile
	sed -n '1,/^\.$$/p' README >README.omxd
	curl -F page=@README.omxd http://mantastic.herokuapp.com > omxd.1
	rm README.omxd
rpyt.1: README Makefile
	sed -n '/rpyt/,$$p' README >README.rpyt
	curl -F page=@README.rpyt http://mantastic.herokuapp.com > rpyt.1
	rm README.rpyt
install:
	-killall omxd
	-killall omxplayer.bin
	cp omxd /usr/bin
	IFS=''; while read i; do [ "$$i" = HELP ] && sed -n '/rpyt/,$$p' README; echo "$$i"; done <rpyt >/usr/bin/rpyt
	chmod +x /usr/bin/rpyt
	omxd
	perl -pe '$$o=1 if /omxd/; print "omxd\n" if !$$o && /^exit 0/' -i /etc/rc.local
	cp logrotate /etc/logrotate.d/omxd
	cp omxd.1 /usr/share/man/man1/
	cp rpyt.1 /usr/share/man/man1/
uninstall:
	-killall omxd
	rm /usr/bin/omxd
	rm /usr/bin/rpyt
	perl -ne 'print unless /omxd/' -i /etc/rc.local
	rm /etc/logrotate.d/omxd
	rm /usr/share/man/man1/omxd.1 
	rm /usr/share/man/man1/rpyt.1 
