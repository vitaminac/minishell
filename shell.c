#include "shell.h"

int fd_stdout, fd_stderr;
JobInfo * background[MAX_BGTASK];
int last_bg_task;
char cwd[BUFFER_SIZE];
pid_t current = 0;
int groupId = 88;


void prompt() {
	printf("msh:>%s$", getcwd(cwd, BUFFER_SIZE));
}

pid_t debug_wait(pid_t pid, int * status, int options) {
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
			fprintf(stdout, "task %d exited with status code %d\n", result, WEXITSTATUS(*status));
#endif
		}
		else if (WIFSIGNALED(*status)) {
#ifdef DEBUG
			fprintf(stdout, "task %d was terminated by signal %d \n", result, WTERMSIG(*status));
#endif
		}
		else if (WIFSTOPPED(*status)) {
#ifdef DEBUG
			fprintf(stdout, "task %d was stopped by signal %d\n", result, WSTOPSIG(*status));
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
			background[i] = (JobInfo *)malloc(sizeof(JobInfo));
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
	JobInfo * task;
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

// Ejecuta el commando por separado y redirecciona la entrada y salida si es necesario
pid_t execute(tcommand command, int input, int * output, int groupId) {
	pid_t pid;
	int pipeline[2];
	if (output != NULL) {
		pipe(pipeline);
	}
	pid = fork();
	if (pid == 0) {
		if (background) {
			// TODO: cambiar a proceso de background
			setpgid(0, groupId);
			// setsid();
		}
		dup2(input, FD_STDIN);
		if (output != NULL) {
			close(pipeline[0]);
			dup2(pipeline[1], FD_STDOUT);
		}
		execvp(command.filename, command.argv);
		// Si alguno de los mandatos a ejecutar no existe, 
		// el programa debe mostrar el error 
		// “mandato: No se encuentra el mandato”.
		if ((errno & (EACCES | EIO | EISDIR | ELIBBAD | ENOENT | ENOEXEC | ENOTDIR)) != 0) {
			printf(ERR_COMMAND);
		}
		exit(EXIT_FAILURE);
	}
	else {
		if (output != NULL) {
			close(pipeline[1]);
			*output = pipeline[0];
		}
	}
	return pid;
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
	int input;
	int output;
	int i;
	check_bg_task();
	// comprobar el argumentos
	if (line != NULL) {
		if (inlinecommand(line) == 0) {
			return 0;
		}
		if (line->ncommands > 0) {
			groupId++;
			if (line->redirect_output != NULL) {
				dup2(line->redirect_output, FD_STDOUT);
			}
			if (line->redirect_error != NULL) {
				dup2(line->redirect_error, FD_STDERR);
			}
			input = line->redirect_input || FD_STDIN;
			for (i = 0; i < line->ncommands - 1; i++) {
				current = execute(line->commands[i], input, &output, groupId);
#ifdef DEBUG
				fprintf(stdout, "create new process %d for %s\n", current, line->commands[i].filename);
#endif // DEBUG

				input = output;
			}
			current = execute(line->commands[i], input, NULL, groupId);

			/* recuperar stdout y stderr */
			if (line->redirect_output != NULL) {
				dup2(fd_stdout, FD_STDOUT);
			}
			if (line->redirect_error != NULL) {
				dup2(fd_stderr, FD_STDERR);
			}

			if (line->background) {
#ifdef DEBUG
				fprintf(stdout, "Ejecutamos la tarea %d en el segundo plano\n", current);
#endif 
				if (bg(current, line) < 0) {
#ifdef DEBUG
					fprintf(stderr, "Ha llegado al maximo numero %d de tareas\n", MAX_BGTASK);
#endif 
					exit(EXIT_FAILURE);
				}
			}
			else {
				// esperar a que termine
				debug_wait(current, &status, NULL);
			}
			current = 0;
		}
	}
	// termina devolviendo el estado
	return status;
}

void wait_child(int signum) {
	int status;
	pid_t pid;
#ifdef DEBUG
	fprintf(stdout, "there is a child died\n");
#endif // DEBUG
	while (TRUE) {
		pid = debug_wait(-1, &status, WNOHANG);
		if (pid == 0) break;
		else if (pid == -1) break;
	}
	if (signal(SIGCHLD, wait_child) == SIG_ERR)
	{
#ifdef DEBUG
		fprintf(stderr, "register signal SIGCHILD handler failed\n");
#endif
		exit(EXIT_FAILURE);
	}
}

/* tarea de preparacion */
void init() {
	if (signal(SIGINT, SIG_IGN) == SIG_ERR)
	{
#ifdef DEBUG
		fprintf(stderr, "ignorar signal SIGINT failed\n");
#endif
		exit(EXIT_FAILURE);
	}
	if (signal(SIGQUIT, SIG_IGN) == SIG_ERR)
	{
#ifdef DEBUG
		fprintf(stderr, "ignorar signal SIGQUIT failed\n");
#endif
		exit(EXIT_FAILURE);
	}
	if (signal(SIGCHLD, wait_child) == SIG_ERR)
	{
#ifdef DEBUG
		fprintf(stderr, "register signal SIGCHILD handler failed\n");
#endif
		exit(EXIT_FAILURE);
	}
	// hacer backup de stdout y stderr
#ifdef DEBUG
	fprintf(stdout, "realizando backup de stdout y stderr\n");
#endif // DEBUG
	fd_stdout = dup(FD_STDOUT);
	fd_stderr = dup(FD_STDERR);
#ifdef DEBUG
	fprintf(stdout, "inicializando memoria de JobInfo\n");
#endif // DEBUG
	memset(background, 0, MAX_BGTASK * sizeof(JobInfo *));
}

/* Liberar la memoria de los contenidos de jobs */
void destroy() {
	for (int i = 0; i < MAX_BGTASK; i++) {
		if (background[i] != NULL) {
#ifdef DEBUG
			fprintf(stdout, "liberando memoria asosiada a %d\n", background[i]->pid);
#endif
			if (background[i]->info != NULL) {
				free(background[i]->info);
			}
			free(background[i]);
		}
	}
}