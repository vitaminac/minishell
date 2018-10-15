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
	if (line->ncommands > 0) {
		pid_t pid = fork();
		if (pid == 0) {
			// redirecciona la entra
			if (line->redirect_input != NULL) {
				fd_in = open(line->redirect_input, O_RDONLY);
				if (fd_in > 0) {
					dup2(fd_in, FD_STDIN);
				}
				else {
					printf(ERR_FILE(line->redirect_input));
					exit(errno);
				}
			}
			// ejecutamos hasta penultimo comando
			for (int i = 0; i < line->ncommands - 1; i++) {
				status = execute(line->commands[i]);
				// early exit if failing
				if (status != 0) {
					break;
				}
			}
			if (line->redirect_output != NULL) {
				// redireccciona la salida
				fd_out = open(line->redirect_output, O_WRONLY | O_CREAT, 0666);
				if (fd_out > 0) {
					dup2(fd_out, FD_STDOUT);
				}
				else {
					printf(ERR_FILE(line->redirect_output));
					exit(errno);
				}
			}
			if (line->redirect_error != NULL) {
				// redirecciona la salida de error
				fd_err = open(line->redirect_error, O_WRONLY | O_APPEND | O_CREAT, 0666);
				if (fd_err > 0) {
					dup2(fd_err, FD_STDERR);
				}
				else {
					printf(ERR_FILE(line->redirect_error));
					exit(errno);
				}
			}
			// ejecuta el ultimo comando
			status = execute(line->commands[line->ncommands - 1]);
			// para que no vuelva a programa principal en subprocess
			exit(status);
		}
		else {
			// si no es una tarea backgound, esperamos a que termina
			if (!line->background) {
				wait(&status);
			}
		}
	}
	// termina devolviendo el estado
	return status;
}