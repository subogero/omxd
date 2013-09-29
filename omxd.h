/* (c) SZABO Gergely <szg@subogero.com>, license GNU GPL v2 */
#define LINE_LENGTH 256
#define LINE_MAX (LINE_LENGTH - 1)

#define OMX_CMDS "frFRpkoms-+"
#define LIST_CMDS "iIHJaA.hjnNxXdD"
#define CLIENT_CMDS "S"

/* From omxd.c */
int writedec(int fd, int num);
int writestr(int fd, char *str);
int printfd(int fd, char *fmt, ...);
extern int logfd; /* logfile descriptor */
#define LOG(level, ...) {lseek(logfd,0,SEEK_END);printfd(logfd,__VA_ARGS__);}

/* From playlist.c */
char *playlist(char *cmd, char *file);

/* From client.c */
int client(int argc, char *argv[]);
