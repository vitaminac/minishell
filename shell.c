#include "shell.h"

pid_t backgound[MAX_BGTASK];
char cwd[BUFFER_SIZE];

void prompt() {
	printf("msh:>%s$", getcwd(cwd, BUFFER_SIZE));
}

// Ejecuta el commando
int execute(tcommand command) {
	int status;
	pid_t pid = fork();
	if (pid == 0) {
		status = execve(command.filename, command.argv, NULL);
		// Si alguno de los mandatos a ejecutar no existe, 
		// el programa debe mostrar el error 
		// “mandato: No se encuentra el mandato”.
		if ((errno & (EACCES | EIO | EISDIR | ELIBBAD | ENOENT | ENOEXEC | ENOTDIR)) != 0) {
			printf(ERR_COMMAND);
		}
		exit(status);
	}
	else {
		wait(&status);
		return status;
	}
}

/* cerrar todos los descriptores y liberar la memoria */
void close_fds(tpipeline pipeline) {
	for (int i = 0; i < pipeline.n; i++) {
		// no cierre stdin, stdout, stderr
		if (pipeline.fds[i] > 2) {
			close(pipeline.fds[i]);
		}
	}
	// liberar la memoria
	free(pipeline.fds);
}

tpipeline create_fds(tline * line) {
	// initializa el espacio
	tpipeline pipeline = {
		.n = (line->ncommands) + 2,
		.fds = calloc((line->ncommands) + 2, sizeof(int))
	};

	// la primera entra es line->redirect_input o por defecto stdin
	if (line->redirect_input == NULL) {
		pipeline.fds[0] = dup(FD_STDIN);
	}
	else {
		pipeline.fds[0] = open(line->redirect_input, O_RDONLY);
	}
	// comprueba que no ha habido errores
	if (pipeline.fds[0] < 0) {
		printf(ERR_FILE(line->redirect_input));
		close_fds(pipeline);
		exit(errno);
	}

	// la ultima salida es line->redirect_output o por defecto stdout
	if (line->redirect_output == NULL) {
		pipeline.fds[line->ncommands] = dup(FD_STDOUT);
	}
	else {
		// redireccciona la salida
		pipeline.fds[line->ncommands] = open(line->redirect_output, O_WRONLY | O_CREAT, 0666);
	}
	// comprueba que no ha habido errores
	if (pipeline.fds[line->ncommands] < 0) {
		printf(ERR_FILE(line->redirect_output));
		close_fds(pipeline);
		exit(errno);
	}

	// la salida de error es line->redirect_error o por defecto stderr
	if (line->redirect_error == NULL) {
		pipeline.fds[line->ncommands + 1] = dup(FD_STDERR);
	}
	else {
		// redirecciona la salida de error
		pipeline.fds[line->ncommands + 1] = open(line->redirect_error, O_WRONLY | O_APPEND | O_CREAT, 0666);
	}
	// comprueba que no ha habido errores
	if (pipeline.fds[line->ncommands + 1] < 0) {
		printf(ERR_FILE(line->redirect_error));
		close_fds(pipeline);
		exit(errno);
	}

	for (int i = 1; i < line->ncommands; i++) {
		pipeline.fds[i] = fileno(tmpfile());
	}

	return pipeline;
}

void pipe(int i, tpipeline pipeline) {
	fsync(pipeline.fds[i]);
	lseek(pipeline.fds[i], 0, SEEK_SET);
	dup2(pipeline.fds[i], FD_STDIN);
	dup2(pipeline.fds[i + 1], FD_STDOUT);
	dup2(pipeline.fds[pipeline.n - 1], FD_STDERR);
}

int execline(tline * line) {
	int status = 0;
	if (line->ncommands == 1 && (strcmp("cd", line->commands[0].argv[0]) == 0)) {
		if (line->commands->argc > 1) {
			chdir(line->commands[0].argv[1]);
		}
		else {
			chdir(getenv("HOME"));
		}
	}
	else {
		if (line->ncommands > 0) {
			pid_t pid = fork();
			if (pid == 0) {
				tpipeline pipeline = create_fds(line);
				// ejecutamos hasta penultimo comando
				for (int i = 0; i < line->ncommands; i++) {
					pipe(i, pipeline);
					status = execute(line->commands[i]);
					// early exit if failing
					if (status != 0) {
						break;
					}
				}
				close_fds(pipeline);
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
	}
	// termina devolviendo el estado
	return status;
}