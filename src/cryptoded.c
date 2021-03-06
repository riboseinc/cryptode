/*
 * Copyright (c) 2017, [Ribose Inc](https://www.cryptode.com).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include <sys/stat.h>
#include <json-c/json.h>

#include "cryptoded.h"
#include "cryptoded.nos.h"

static bool g_end_flag;
static bool g_reload_config;

/*
 * print version
 */

static void
print_version(void)
{
	static const char build_time[] = { __DATE__ " " __TIME__ };

	printf("Relaxed VPN client daemon - %s (built on %s)\n%s\n",
			PACKAGE_VERSION,
			build_time,
			COC_COPYRIGHT_MSG);

	exit(0);
}

/*
 * signal handler
 */

static void
signal_handler(const int signum)
{
	COD_DEBUG_MSG("Main: Received signal %d", signum);

	/* if signal is SIGUSR1, then reload a config */
	if (signum == SIGUSR1) {
		g_reload_config = true;
		return;
	}

	/* set end flag */
	g_end_flag = true;
}

/*
 * initialize signal handler
 */

static void
init_signal()
{
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGUSR1, signal_handler);
}

/*
 * check if cryptoded process is running
 */

static pid_t
check_process_running()
{
	FILE *pid_fp;
	pid_t pid = 0;

	char *buf = NULL;
	size_t buf_size = 0;
	ssize_t buf_len;

	/* open pid file */
	pid_fp = fopen(COD_PID_FPATH, "r");
	if (!pid_fp)
		return 0;

	buf_len = getline(&buf, &buf_size, pid_fp);
	if (buf_len > 0) {
		if (buf[buf_len - 1] == '\n')
			buf[buf_len - 1] = '\0';
		pid = atoi(buf);
		free(buf);
	}
	fclose(pid_fp);

	/* check if pid is valid */
	if (pid < 1)
		return 0;

	/* check if the process is running with given pid */
	if (kill(pid, 0) == 0)
		return pid;

	return 0;
}

/*
 * daemonize the process
 */

static void
daemonize(void)
{
	pid_t pid = 0;
	int fd;

	/* fork off the parent process */
	pid = fork();
	if (pid < 0)
		exit(1);
	else if (pid > 0)
		exit(0);

	/* set child process to session leader */
	if (setsid() < 0)
		exit(1);

	/* ignore signal sent from child to parent */
	signal(SIGCHLD, SIG_IGN);

	/* fork for the second time */
	pid = fork();
	if (pid < 0)
		exit(1);
	else if (pid > 0)
		exit(0);

	/* set new file permissions */
	umask(0);

	/* change working directory to root */
	if (chdir("/") < 0)
		exit(1);

	/* close all open file descriptors */
	for (fd = sysconf(_SC_OPEN_MAX); fd > 0; fd--)
		close(fd);

	/* reopen stdin, stdout, stderr */
	stdin = fopen("/dev/null", "r");
	stdout = fopen("/dev/null", "w+");
	stderr = fopen("/dev/null", "w+");
}

/*
 * write PID file
 */

static void
write_pid_file(void)
{
	FILE *pid_fp = NULL;
	int fd;

	/* remove old pid file */
	mkdir(COD_PID_DPATH, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	unlink(COD_PID_FPATH);

	/* open pid file and write */
	fd = open(COD_PID_FPATH, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		fprintf(stderr, "Could not open PID file '%s' for writing\n", COD_PID_FPATH);
		return;
	}

	pid_fp = fdopen(fd, "w");
	if (!pid_fp) {
		fprintf(stderr, "Could not open PID file '%s' for writing\n", COD_PID_FPATH);
		close(fd);
		return;
	}

	fprintf(pid_fp, "%d\n", getpid());
	fclose(pid_fp);
}

/*
 * remove pid file
 */

static void
remove_pid_file(void)
{
	unlink(COD_PID_FPATH);
}

/*
 * check for validation of configuration file
 */

static int
check_config_validataion(const char *config_path)
{
	struct stat st;

	/* check whether configuration file has valid permission */
	if (stat(config_path, &st) != 0 || st.st_size <= 0) {
		fprintf(stderr, "Invalid configuration file '%s'\n", config_path);
		return -1;
	}

	if (!is_owned_by_user(config_path, "root") ||
	    !is_valid_permission(config_path, S_IRUSR | S_IWUSR)) {
		fprintf(stderr, "The configuration file '%s' isn't owned by root or has the leak permission\n",
			config_path);
		return -1;
	}

	fprintf(stdout, "The configuration file '%s' is valid\n", config_path);

	return 0;
}

/*
 * parse config options
 */

static int
parse_config_options(cod_options_t *opts, int argc, char **argv)
{
	const char *nos_cfg;
	int ret = -1;

	bool require_exit = false;

	nereon_config_option_t cod_opts[] = {
		{"config_file", NEREON_TYPE_CONFIG, false, NULL, &opts->config_fpath},
		{"go_daemon", NEREON_TYPE_BOOL, false, NULL, &opts->go_daemon},
		{"check_config", NEREON_TYPE_BOOL, false, NULL, &opts->check_config},
		{"print_version", NEREON_TYPE_BOOL, false, NULL, &opts->print_version},
		{"helper", NEREON_TYPE_BOOL, false, NULL, &opts->print_help},
		{"openvpn_bin", NEREON_TYPE_STRING, true, NULL, &opts->ovpn_bin_path},
		{"openvpn_root_check", NEREON_TYPE_BOOL, false, NULL, &opts->ovpn_root_check},
		{"openvpn_updown_scripts", NEREON_TYPE_BOOL, false, NULL, &opts->ovpn_use_scripts},
		{"user_id", NEREON_TYPE_INT, true, NULL, &opts->allowed_uid},
		{"restrict_socket", NEREON_TYPE_BOOL, false, NULL, &opts->restrict_cmd_sock},
		{"log_directory", NEREON_TYPE_STRING, true, NULL, &opts->log_dir_path},
		{"vpn_config_paths", NEREON_TYPE_STRING, true, NULL, &opts->vpn_config_dir}
	};

	/* initialize libnereon context */
	nos_cfg = get_cryptoded_nos_cfg();
	if (nereon_ctx_init(&opts->nctx, nos_cfg) != 0) {
		fprintf(stderr, "Failed to parse cryptoded NOS configuration\n");
		return -1;
	}

	/* parse command line */
	ret = nereon_parse_cmdline(&opts->nctx, argc, argv, &require_exit);
	if (ret != 0 || require_exit) {
		if (ret != 0)
			fprintf(stderr, "Failed to parse command line(err:%s)\n", nereon_get_errmsg());

		nereon_ctx_finalize(&opts->nctx);
		nereon_print_usage(&opts->nctx);

		exit(ret);
	}

	/* parse configuration file */
	ret = nereon_parse_config_file(&opts->nctx, CRYPTODED_CONFIG_PATH);
	if (ret != 0) {
		fprintf(stderr, "Could not parse NOC configuration(err:%s)\n", nereon_get_errmsg());
		goto end;
	}

	ret = nereon_get_config_options(&opts->nctx, cod_opts);
	if (ret != 0) {
		fprintf(stderr, "Failed to get configuration options(err:%s)\n", nereon_get_errmsg());
		goto end;
	}

	ret = check_config_validataion(opts->config_fpath);

end:
	if (ret != 0)
		nereon_ctx_finalize(&opts->nctx);

	if (opts->check_config)
		exit(ret);

	return ret;
}

/*
 * initialize cryptoded context
 */

static int
cod_ctx_init(cod_ctx_t *c)
{
	/* initialize logging */
	if (cod_log_init(c->opt.log_dir_path) != 0) {
		fprintf(stderr, "Couldn't create log file in directory '%s'\n", c->opt.log_dir_path);
		return -1;
	}

	COD_DEBUG_MSG("Main: Initializing cryptoded context");

	/* initialize command manager */
	if (cod_cmd_proc_init(c) != 0) {
		COD_DEBUG_ERR("Main: Couldn't initialize command processor");
		return -1;
	}

	/* initialize VPN connection manager */
	if (cod_vpnconn_mgr_init(c) != 0) {
		COD_DEBUG_ERR("Main: Couldn't initialize VPN connection manager");
		return -1;
	}

	return 0;
}

/*
 * finalize cryptoded context
 */

static void
cod_ctx_finalize(cod_ctx_t *c)
{
	COD_DEBUG_MSG("Main: Finalizing cryptoded context");

	/* finalize VPN connection manager */
	cod_vpnconn_mgr_finalize(&c->vpnconn_mgr);

	/* finalize command manager */
	cod_cmd_proc_finalize(&c->cmd_proc);

	/* finalize logging */
	cod_log_finalize();
}

/*
 * reload cryptoded context
 */

static void
cod_ctx_reload(cod_ctx_t *c)
{
	cod_options_t opt;

	COD_DEBUG_MSG("Main: Reloading cryptoded context");

	memcpy(&opt, &c->opt, sizeof(cod_options_t));

	/* finalize cryptoded context */
	cod_ctx_finalize(c);

	/* init context object */
	memset(c, 0, sizeof(cod_ctx_t));
	memcpy(&c->opt, &opt, sizeof(cod_options_t));

	/* init cryptoded context */
	if (cod_ctx_init(c) != 0) {
		COD_DEBUG_ERR("Main: Couldn't reload cryptoded context");
		cod_ctx_finalize(c);
	}
}

/*
 * main function
 */

int
main(int argc, char *argv[])
{
	cod_ctx_t ctx;
	pid_t pid;

	/* initialize context object */
	memset(&ctx, 0, sizeof(cod_ctx_t));

	/* check UID */
	if (getuid() != 0) {
		fprintf(stderr, "Please run as root\n");
		exit(1);
	}

	/* read configruation */
	if (parse_config_options(&ctx.opt, argc, argv) != 0)
		exit(1);

	/* check if the process is already running */
	if ((pid = check_process_running()) > 0) {
		fprintf(stderr, "The cryptoded process is already running with PID %d. Exiting...\n", pid);
		exit(1);
	}

	/* if daemon mode is enabled, then daemonize the process */
	if (ctx.opt.go_daemon)
		daemonize();

	/* write PID file */
	write_pid_file();

	/* init signal */
	init_signal();

	/* initialize cryptoded context */
	if (cod_ctx_init(&ctx) != 0) {
		COD_DEBUG_ERR("Main: Failed initializing cryptoded context.");

		cod_ctx_finalize(&ctx);
		exit(1);
	}

	while (!g_end_flag) {
		if (g_reload_config) {
			cod_ctx_reload(&ctx);
			g_reload_config = false;
		}

		sleep(1);
	}

	/* finalize cryptoded context */
	cod_ctx_finalize(&ctx);

	/* remove PID file */
	remove_pid_file();

	/* finalize nereon context */
	nereon_ctx_finalize(&ctx.opt.nctx);

	return 0;
}
