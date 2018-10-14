#include "shell.h"

pid_t backgound[MAX_BGTASK];

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
	int status;
	pid_t pid = fork();
	if (pid == 0) {
		for (int i = 0; i < line->ncommands; i++) {
			status = execute(line->commands[i]);
			// early exit if failing
			if (status != 0) {
				return status;
			}
		}
	}
	else {
		if (line->background) {
			return 0;
		}
		else {
			wait(&status);
			return status;
		}
	}
}