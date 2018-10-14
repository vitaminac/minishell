#include "shell.h"

pid_t backgound[MAX_BGTASK];

int fd_in, fd_out, fd_err;

void prompt() {
	printf("msh> ");
}

// Ejecuta el commando
int execute(tcommand command) {
	int status;
	pid_t pid = fork();
	if (pid == 0) {
		return execvp(command.filename, command.argv);
	}
	else {
		wait(&status);
		return status;
	}
}

int execline(tline * line) {
	int status = 0;
	pid_t pid = fork();
	if (pid == 0) {
		if (line->redirect_input != NULL) {
			fd_in = open(line->redirect_input, O_RDONLY);
			if (fd_in > 0) {
				dup2(fd_in, FD_STDIN);
			}
			else {
				printf("Fallo %d al leer desde nueva entrada", errno);
				exit(errno);
			}
		}
		for (int i = 0; i < line->ncommands; i++) {
			status = execute(line->commands[i]);
			// early exit if failing
			if (status != 0) {
				break;
			}
		}
		// para que no vuelva a programa principal en subprocess
		exit(status);
	}
	else {
		if (!line->background) {
			wait(&status);
		}
		return status;
	}
}