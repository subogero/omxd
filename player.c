/* (c) SZABO Gergely <szg@subogero.com>, license GNU GPL v2 */
#include "omxd.h"
#include <pwd.h>
#include <grp.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>

static void player_quit(int signum);
static void drop_priv(void);

struct player {
	pid_t pid;
	int wpipe;
	enum pstate state;
	char file[LINE_LENGTH];
};
#define NUM_PLAYERS 3
static struct player p[NUM_PLAYERS];
static struct player *find_free(void);
static struct player *find_pid(pid_t pid);

struct player *player_new(char *file, char *out, enum pstate state)
{
	if (file == NULL || *file == 0)
		return NULL;
	struct player *this = find_free();
	if (this == NULL)
		return NULL;
	int ctrlpipe[2];
	pipe(ctrlpipe);
	char *argv[5];
	argv[0] = "/usr/bin/omxplayer";
	argv[1] = out;
	argv[2] = "-I";
	argv[3] = file;
	argv[4] = NULL;
	this->pid = fork();
	if (this->pid < 0) { /* Fork error */
		this->pid = 0;
		close(ctrlpipe[0]);
		close(ctrlpipe[1]);
		this->state = P_DEAD;
		return NULL;
	} else if (this->pid > 0) { /* Parent: set SIGCHLD handler */
		close(ctrlpipe[0]);
		this->wpipe = ctrlpipe[1];
		if (state == P_PAUSED)
			write(this->wpipe, "p", 1);
		this->state = state;
		signal(SIGCHLD, player_quit);
		signal(SIGPIPE, player_quit);
		strcpy(this->file, file);
		LOG(0, "player_new: PID=%d %s\n", this->pid, file);
		return this;
	} else { /* Child: exec omxplayer */
		drop_priv();
		close(ctrlpipe[1]);
		/* Redirect read end of control pipe to 0 stdin */
		if (ctrlpipe[0] != 0) {
			close(0);
			dup(ctrlpipe[0]);
			close(ctrlpipe[0]);
		}
		execve(argv[0], argv, NULL);
		_exit(20);
	}
}

void player_cmd(struct player *this, char *cmd)
{
	if (this == NULL || this->state == P_DEAD)
		return;
	if (strchr(OMX_CMDS, *cmd) == NULL)
		return;
	/* Replace FRfr with arrow-key escape sequences */
	if      (*cmd == 'F')
		cmd = "\033[A";
	else if (*cmd == 'R')
		cmd = "\033[B";
	else if (*cmd == 'f')
		cmd = "\033[C";
	else if (*cmd == 'r')
		cmd = "\033[D";
	writestr(this->wpipe, cmd);
	LOG(0, "player_cmd: Send %s to omxplayer PID/fd %d/%d\n",
		cmd, this->pid, this->wpipe);
}

void player_off(struct player *this)
{
	if (this == NULL || this->state == P_DEAD)
		return;
	LOG(1, "player_off: PID %d\n", this->pid);
	write(this->wpipe, "q", 1);
	close(this->wpipe);
	this->pid = 0;
	this->file[0] = 0;
	this->state = P_DEAD;
}

static void player_quit(int signum)
{
	if (signum == SIGPIPE)
		return;
	int status;
	pid_t pid = wait(&status);
	struct player *this = find_pid(pid);
	if (this == NULL)
		return;
	status = WEXITSTATUS(status);
	if (this->state != P_DEAD) {
		close(this->wpipe);
		this->pid = 0;
		this->file[0] = 0;
		this->state = P_DEAD;
		quit_callback(this);
	}
	LOG(0, "player_quit: PID=%d (%d) with %d\n", pid, status);
}

static struct player *find_free(void)
{
	int i;
	for (i = 0; i < NUM_PLAYERS; ++i)
		if (p[i].state == P_DEAD)
			return p + i;
	return NULL;
}

static struct player *find_pid(pid_t pid)
{
	int i;
	for (i = 0; i < NUM_PLAYERS; ++i)
		if (p[i].pid == pid)
			return p + i;
	return NULL;
}

/* Drop root privileges before execing omxplayer */
static void drop_priv(void)
{
	int cfg = open("/etc/omxd.conf", O_RDONLY);
	if (cfg < 0)
		return;
	char buffer[4096];
	if (read(cfg, buffer, 4096) == 0)
		return;
	char *line = strstr(buffer, "user=");
	if (line == NULL)
		return;
	strtok(line, "=");
	char *user = strtok(NULL, "\n");
	struct passwd *pwd = getpwnam(user);
	if (pwd == NULL)
		return;
	chdir(pwd->pw_dir);
	initgroups(pwd->pw_name, pwd->pw_gid);
	setgid(pwd->pw_gid);
	setuid(pwd->pw_uid);
}
