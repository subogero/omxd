omxd: omxd.c playlist.c omxd.h client.c Makefile
	gcc -g -o omxd omxd.c playlist.c client.c
omxd.1: README
	curl -F page=@README http://mantastic.herokuapp.com > omxd.1
install:
	-killall omxd
	-killall omxplayer.bin
	cp omxd /usr/bin
	cp rpyt /usr/bin
	omxd
	perl -pe '$$o=1 if /omxd/; print "omxd\n" if !$$o && /^exit 0/' -i /etc/rc.local
	cp logrotate /etc/logrotate.d/omxd
	cp omxd.1 /usr/share/man/man1/
uninstall:
	-killall omxd
	rm /usr/bin/omxd
	rm /usr/bin/rpyt
	perl -ne 'print unless /omxd/' -i /etc/rc.local
	rm /etc/logrotate.d/omxd
	rm /usr/share/man/man1/omxd.1 
