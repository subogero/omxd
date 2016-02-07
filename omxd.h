#ifndef OMXD_H
#define OMXD_H
/* (c) SZABO Gergely <szg@subogero.com>, license GNU GPL v2 */
#define LINE_LENGTH 512
#define LINE_MAX (LINE_LENGTH - 1)

#define OMX_CMDS "frFRpkoms-+"
#define VOL_CMDS "-+"
#define LIST_CMDS "iaAIHJL.hjnNdDxXgule"
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
const char *player_logfile(struct player *this);
int player_dt(struct player *this);
enum pstate player_state(struct player *this);
void player_add_opt(char *opt);
void player_set_vol(int vol_mB); /* Set volume in milliBel */

/* From omxd.c */
void quit_callback(struct player *this);

/* From utils.c */
int writedec(int fd, int num);
int writestr(int fd, char *str);
int printfd(int fd, char *fmt, ...);
int sscand(char *str, int *num);
int scatd(char *str, int num);

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
#define LOG_FILE  (I_root ? "/var/log/omxlog" : "omxlog")
#define STAT_FILE (I_root ? "/var/log/omxstat" : "omxstat")
#define OMX_FILE  (I_root ? "/var/log/omxplayer.log." : "omxplayer.log.")
#define PID_FILE  (I_root ? "/var/run/omxd.pid" : "omxd.pid")

/* From m_list.c */
char **m_list(char *cmd, char *file);
enum e_lmode { LOOP, END, SHUFFLE };
extern enum e_lmode lmode;

/* From client.c */
#include <sys/stat.h>
mode_t get_ftype(char *file);
int client(int argc, char *argv[]);

#endif
