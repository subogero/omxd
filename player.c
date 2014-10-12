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

static struct player p;

struct player *player_new(char *file, char *out, enum pstate state)
{
	if (p.state != P_DEAD)
		return NULL;
	if (file == NULL || *file == 0)
		return NULL;
	struct player *this = &p;
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
		strcpy(this->file, file);
		LOG(0, "player: PID=%d %s\n", this->pid, file);
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
	LOG(0, "player: Send %s to omxplayer\n", cmd);
	cmd[1] = 0; /* Just one character normally */
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
}

void player_off(struct player *this)
{
	if (this == NULL || this->state == P_DEAD)
		return;
	write(this->wpipe, "q", 1);
	close(this->wpipe);
	this->pid = 0;
	this->file[0] = 0;
	this->state = P_DEAD;
}

static void player_quit(int signum)
{
	int status;
	pid_t pid = wait(&status);
	if (pid != p.pid)
		return;
	status = WEXITSTATUS(status);
	LOG(0, "player_quit: PID=%d (%d) with %d\n", pid, status);
	if (p.state != P_DEAD) {
		close(p.wpipe);
		p.pid = 0;
		p.file[0] = 0;
		p.state = P_DEAD;
		quit_callback(&p);
	}
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
