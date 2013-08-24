/* (c) SZABO Gergely <szg@subogero.com>, license GPLv3 */
#define LINE_LENGTH 256
#define LINE_MAX (LINE_LENGTH - 1)

#define OMX_CMDS "frFRpkoms-+"
#define LIST_CMDS "iIaA.nNxXdD"

/* From omxd.c */
extern int logfd; /* logfile descriptor */
int writedec(int fd, int num);
int writestr(int fd, char *str);
int printfd(int fd, char *fmt, void *vals[]);

/* From playlist.c */
char *playlist(char *cmd, char *file);
