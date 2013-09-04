/* (c) SZABO Gergely <szg@subogero.com>, license GNU GPL v2 */
#define LINE_LENGTH 256
#define LINE_MAX (LINE_LENGTH - 1)

#define OMX_CMDS "frFRpkoms-+"
#define LIST_CMDS "iIHJaA.hjnNxXdD"
#define CLIENT_CMDS "S"

/* From omxd.c */
extern int logfd; /* logfile descriptor */
int writedec(int fd, int num);
int writestr(int fd, char *str);
int printfd(int fd, char *fmt, ...);

/* From playlist.c */
char *playlist(char *cmd, char *file);

/* From client.c */
int client(int argc, char *argv[]);
