#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define SO_PATH "./libxxx.so"

void print_help()
{
	fprintf(stderr, "\nUsage: btrecorder [-w/r] program program_args\n\n");
	fprintf(stderr, "\t-w: record the program\n");
	fprintf(stderr, "\t-r: replay the program\n");
	fprintf(stderr, "\t-h: print this help\n\n");
	_exit(-1);
}

void write_mode_file (int mode)
{
	char mode_file[100];
	int fd;

	strcpy (mode_file, getenv("HOME"));
	strcat (mode_file, "/.mode");

	fd = open(mode_file, O_CREAT | O_RDWR | O_TRUNC, 00664);
	assert (fd != -1);
	write (fd, &mode, sizeof(int));

	close (fd);
}

void start_cmd(int argc, char **argv)
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0)
		assert (0);
	else if (pid == 0)
	{
		char env_string[200] = "LD_PRELOAD=";
		strcat(env_string, SO_PATH);
		putenv(env_string);
		execvp(argv[2], argv+2);
	}
	else if (pid > 0)
	{
		waitpid(pid, &status, __WALL);
	}
}

int main(int argc, char **argv)
{
	int opt;
	int mode;

	opt = getopt(argc, argv, "wrh");
	switch(opt)
	{
		case 'w':/*record*/
			mode = 0;
			break;
		case 'r':
			mode = 1;
			break;
		case 'h':
		default:
			print_help();
	}

	if (!argv[2])
		print_help();

	write_mode_file (mode);

	start_cmd (argc, argv);

	printf("bye\n");
	return 0;
}
