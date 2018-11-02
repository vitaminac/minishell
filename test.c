#include "shell.h"

/*
   TODO: check if we have correctly close the file return zero
   TODO: open entrada.txt failed
*/
int main(int argc, char * argv[]) {
	/* echo "0" | sudo tee /proc/sys/kernel/yama/ptrace_scope > /dev/null */
	char buf[BUFFER_SIZE];
	tline * line;
	int i, j;

	init();

	prompt();
	while (true) {
		if (fgets(buf, BUFFER_SIZE, stdin) > 0) {
			/* Leer una linea del taclado */
			line = tokenize(buf);
			if (line == NULL) {
				continue;
			}

#ifdef DEBUG
			if (line->redirect_input != NULL) {
				printf("redirección de entrada: %s\n", line->redirect_input);
			}
			if (line->redirect_output != NULL) {
				printf("redirección de salida: %s\n", line->redirect_output);
			}
			if (line->redirect_error != NULL) {
				printf("redirección de error: %s\n", line->redirect_error);
			}
			if (line->background) {
				printf("comando a ejecutarse en background\n");
			}
			for (i = 0; i < line->ncommands; i++) {
				printf("orden %d (%s):\n", i, line->commands[i].filename);
				for (j = 0; j < line->commands[i].argc; j++) {
					printf("  argumento %d: %s\n", j, line->commands[i].argv[j]);
				}
			}
#endif
			execline(line);
			prompt();
		}
	}

	destroy();
	return 0;
}
