#ifndef OMXD_H
#define OMXD_H
/* (c) SZABO Gergely <szg@subogero.com>, license GNU GPL v2 */
#define LINE_LENGTH 512
#define LINE_MAX (LINE_LENGTH - 1)

#define OMX_CMDS "frFRpkoms-+"
#define LIST_CMDS "iaAIHJL.hjnNdDxXgu"
#define STOP_CMDS "P"
#define CLIENT_CMDS "S"
#define OPT_CMDS "O"

/* From player.c */
#include <unistd.h>
enum pstate {
	P_DEAD,
	P_PLAYING,
	P_PAUSED
};
struct player;
struct player *player_new(char *file, char *out, enum pstate state);
void player_cmd(struct player *this, char *cmd);
void player_off(struct player *this);
const char *player_file(struct player *this);
void player_add_opt(char *opt);

/* From omxd.c */
void quit_callback(struct player *this);

/* From utils.c */
int writedec(int fd, int num);
int writestr(int fd, char *str);
int printfd(int fd, char *fmt, ...);
int sscand(char *str, int *num);

#include <time.h>
extern int logfd; /* logfile descriptor */
extern int loglevel;
#define LOG(level, ...) { \
	if ((level) <= loglevel) { \
		lseek(logfd, 0, SEEK_END); \
		printfd(logfd, "%d ", time(NULL)); \
		printfd(logfd, __VA_ARGS__); \
	} \
}

/* Filenames */
extern int I_root;
#define LIST_FILE (I_root ? "/var/local/omxplay" : "omxplay")
#define LOG_FILE  (I_root ? "/var/log/omxlog"    : "omxlog")
#define PID_FILE  (I_root ? "/var/run/omxd.pid"  : "omxd.pid")

/* From m_list.c */
char **m_list(char *cmd, char *file);

/* From client.c */
#include <sys/stat.h>
mode_t get_ftype(char *file);
int client(int argc, char *argv[]);

#endif
