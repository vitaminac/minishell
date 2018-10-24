#include "shell.h"

Task * background[MAX_BGTASK];
int last_bg_task;
char cwd[BUFFER_SIZE];
pid_t current = 0;

void prompt() {
	printf("msh:>%s$", getcwd(cwd, BUFFER_SIZE));
}

int debug_wait(pid_t pid, int * status, int options) {
	pid_t result;
	options |= WUNTRACED;
#ifdef DEBUG
	fprintf(stdout, "waiting pid %d\n", pid);
#endif 
	result = waitpid(current, status, options);
	if (result == 0) {
#ifdef DEBUG
		fprintf(stdout, "child process %d's state haven't changed yet\n", pid);
#endif
	}
	else {
		if (result < 0) {
#ifdef DEBUG
			fprintf(stderr, "wait pid %d failed\n", pid);
			if (errno == ECHILD) {
				fprintf(stderr, "Child does not exist\n");
			}
			else if (errno == EINVAL) {
				fprintf(stderr, "Bad argument passed to waitpid\n");
			}
			else if (errno == ECHILD) {
				fprintf(stderr, "child process %d doesn't exist!\n", pid);
			}
			else {
				fprintf(stderr, "Unknown error\n");
			}
#endif 
			// si ha fallado una vez, reintentamos
			waitpid(current, status, options);
#ifdef DEBUG
			fprintf(stdout, "waited pid %d again\n", pid);
#endif 
		}
		if (WIFEXITED(*status)) {
#ifdef DEBUG
			fprintf(stdout, "task %d exited, status=%d\n", pid, WEXITSTATUS(*status));
#endif
		}
		else if (WIFSIGNALED(*status)) {
#ifdef DEBUG
			fprintf(stdout, "task %d was terminated with a status of: %d \n", pid, WTERMSIG(*status));
#endif
		}
		else if (WIFSTOPPED(*status)) {
#ifdef DEBUG
			fprintf(stdout, "task %d stopped by signal %d\n", pid, WSTOPSIG(*status));
#endif
		}
	}
	return result;
}

#pragma region BackgroundTask

/* buscar un slot libre de backgroud */
char * deepcopy(tline * line) {
	int old_length = 0;
	int new_length;
	char * info = malloc(2 * sizeof(char));
	info[0] = ' ';
	info[1] = '\0';
	tcommand command;
	for (int i = 0; i < line->ncommands; i++) {
		command = line->commands[i];
		for (int j = 0; j < command.argc; j++) {
			new_length = old_length + strlen(command.argv[j]) + 1;
			realloc(info, new_length * sizeof(char));
			strcpy(info + old_length + 1, command.argv[j]);
			if (j == 0) {
				if (old_length != 0) {
					info[old_length] = '|';
				}
			}
			else {
				info[old_length] = ' ';
			}
			old_length = new_length;
		}
	}
	return info;
}

int bg(pid_t pid, tline * line) {
	for (int i = 0; i < MAX_BGTASK; i++) {
		if (background[i] == NULL) {
			background[i] = (Task *)malloc(sizeof(Task));
			background[i]->pid = pid;
			background[i]->info = deepcopy(line);
			last_bg_task = i;
			return i;
		}
	}
	return -1;
}

int fg(int id) {
	int status = 0;
	/* comprueba que esta ejecutando en backgroud */
	if (id < MAX_BGTASK && background[id] != NULL) {
		current = background[id]->pid;
		free(background[id]->info);
		free(background[id]);
		background[id] = NULL;
		debug_wait(current, &status, 0);
		current = 0;
	}
	return status;
}

void jobs() {
	Task * task;
	for (int i = 0; i < MAX_BGTASK; i++) {
		if (background[i] != NULL) {
			task = background[i];
			printf("[%d]+ Running \t %s\n", i, background[i]->info);
		}
	}
}

/* comprobar que si hay tarea terminada en el segundo plano */
void check_bg_task() {
	int status;
	for (int i = 0; i < MAX_BGTASK; i++) {
		if (background[i] != NULL) {
			if (debug_wait(background[i]->pid, &status, WNOHANG)) {
				free(background[i]->info);
				free(background[i]);
				background[i] = NULL;
			}
		}
	}
}
#pragma endregion

// Ejecuta el commando
pid_t execute(tcommand command) {
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
		return pid;
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

/* ejecutar mandato interno de shell */
int inlinecommand(tline * line) {
	if (line->ncommands == 1) {
		if (strcmp("cd", line->commands[0].argv[0]) == 0) {
			if (line->commands->argc > 1) {
				chdir(line->commands[0].argv[1]);
				return 0;
			}
			else {
				chdir(getenv("HOME"));
				return 0;
			}
		}
		else if (strcmp("exit", line->commands[0].argv[0]) == 0) {
			exit(EXIT_SUCCESS);
		}
		else if (strcmp("jobs", line->commands[0].argv[0]) == 0) {
			jobs();
			return 0;
		}
		else if (strcmp("fg", line->commands[0].argv[0]) == 0) {
			if (line->commands->argc > 1) {
				fg(atoi(line->commands[0].argv[1]));
			}
			else {
				fg(last_bg_task);
			}
			return 0;
		}
	}
	return -1;
}

int execline(tline * line) {
	int status = 0;
	check_bg_task();
	if (inlinecommand(line) == 0) {
		return 0;
	}
	// comprobar el argumentos
	if (line != NULL) {
		if (line->ncommands > 0) {
			current = fork();
			pid_t program;
			if (current == 0) {
				tpipeline pipeline = create_fds(line);
				// ejecutamos hasta ultimo comando
				for (int i = 0; i < line->ncommands; i++) {
					pipe(i, pipeline);
					program = execute(line->commands[i]);
					debug_wait(program, &status, 0);
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
				if (line->background) {
#ifdef DEBUG
					fprintf(stdout, "Ejecutamos la tarea en el segundo plano\n");
#endif 
					if (bg(current, line) < 0) {
#ifdef DEBUG
						fprintf(stderr, "Ha llegado al maximo numero de tareas\n");
#endif 
						exit(EXIT_FAILURE);
					}
				}
				else {
					// esperar a que termine
					debug_wait(current, &status, 0);
				}
				current = 0;
			}
		}
	}
	// termina devolviendo el estado
	return status;
}

void redirect_signal(int signum) {
#ifdef DEBUG
	fprintf(stdout, "process %d received signal %d\n", getpid(), signum);
#endif
	if (current != 0) {
		kill(current, signum);
#ifdef DEBUG
		fprintf(stderr, "sent signal %d to current process %d\n", signum, current);
#endif
	}
	else {
#ifdef DEBUG
		fprintf(stderr, "try to registering signal handler SIGINT\n");
#endif
		if (signal(SIGINT, redirect_signal) == SIG_ERR)
		{
#ifdef DEBUG
			fprintf(stderr, "register signal handler SIGINT failed\n");
#endif
			exit(EXIT_FAILURE);
		}
	}
}

void init() {
	if (signal(SIGINT, redirect_signal) == SIG_ERR)
	{
#ifdef DEBUG
		fprintf(stderr, "register signal handler SIGINT failed\n");
#endif
		exit(EXIT_FAILURE);
	}
	if (signal(SIGQUIT, redirect_signal) == SIG_ERR)
	{
#ifdef DEBUG
		fprintf(stderr, "register signal handler SIGQUIT failed\n");
#endif
		exit(EXIT_FAILURE);
	}
	memset(background, 0, MAX_BGTASK * sizeof(Task *));
}

void destroy() {
	for (int i = 0; i < MAX_BGTASK; i++) {
		if (background[i] != NULL) {
			free(background[i]->info);
			free(background[i]);
		}
	}
}