omxd: omxd.c playlist.c omxd.h client.c Makefile
	gcc -g -o omxd omxd.c playlist.c client.c
install:
	cp omxd /usr/bin
	perl -pe '$$o=1 if /omxd/; print "omxd\n" if !$$o && /exit 0/' -i /etc/rc.local
uninstall:
	rm /usr/bin/omxd
	perl -ne 'print unless /omxd/' -i /etc/rc.local
