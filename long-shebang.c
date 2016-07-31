#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

typedef enum {
  exit_usage = 1,
  exit_open_script = 2,
  exit_reading = 3,
  exit_eof = 4,
  exit_mem = 5,
  exit_null = 6,
  exit_unknown_escape = 7,
  exit_exec = 8,
  exit_no_shebang
} exit_code;

static void handle_eof(const char *script) {
  if (errno) {
    fprintf(stderr, "reading from %s: %s\n", script, strerror(errno));
    exit(exit_reading);
  }
  fprintf(stderr, "early EOF reading from %s", script);
  exit(exit_eof);
}

static void increase_buffer(void ** buf, size_t * sz, size_t el_sz) {
  if (*sz >= SIZE_MAX / el_sz) {
    fprintf(stderr, "asking for ridiculously large buffer\n");
    exit(exit_mem);
  } else if (SIZE_MAX / 2 * el_sz < *sz) {
    *sz = SIZE_MAX / el_sz;
  } else {
    *sz *= 2;
  }
  *buf = realloc(*buf, *sz * el_sz);
  if (!*buf) {
    perror("increasing buffer size");
    exit(exit_mem);
  }
}

int main(int argc, char ** argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage (meant for shebang only): %s SCRIPT\n", argv[0]);
    return exit_usage;
  }

  FILE * script = fopen(argv[1], "r");
  if (!script) {
    fprintf(stderr, "opening %s for reading: %s\n", argv[1], strerror(errno));
    return exit_open_script;
  }

  /* Randomly chosen starting size for number of args, should probably profile */
  size_t args_size = 4 * sizeof(char *);
  /* args[0] always used */
  size_t args_used = 1;
  char ** args = malloc(args_size);
  if (!args) {
    perror("allocating argument pointers");
    return exit_mem;
  }

  /* Randomly chosen starting size for the actual strings, should probably profile */
  size_t args_string_size = 1024;
  size_t args_string_used = 0;
  char * args_string = malloc(args_string_size);
  if (!args_string) {
    perror("allocating argument buffer");
    return exit_mem;
  }

  /* Skip real shebang line */
  errno = 0;
  while (1) {
    switch (fgetc(script)) {
      case EOF:
        handle_eof(argv[1]);
      case '\n':
        goto done_first_line;
    }
  }
done_first_line:

  switch (fgetc(script)) {
    case EOF:
      handle_eof(argv[1]);
    case '#':
      break;
    default:
      fprintf(stderr, "second line of %s doesn't begin with shebang line\n", argv[1]);
      return exit_no_shebang;
  }

  switch (fgetc(script)) {
    case EOF:
      handle_eof(argv[1]);
    case '!':
      break;
    default:
      fprintf(stderr, "second line of %s doesn't begin with shebang line\n", argv[1]);
      return exit_no_shebang;
  }

  /* Skip leading spaces */
  while (1) {
    int res = fgetc(script);
    switch (res) {
      case EOF:
        handle_eof(argv[1]);
      case ' ':
        continue;
      default:
        ungetc(res, script);
        break;
    }
    break;
  }

  /* Read second line */
  errno = 0;
  int done = 0;
  while (!done) {
    int res = fgetc(script);
    switch (res) {
      case EOF:
        handle_eof(argv[1]);
      case '\0':
        fprintf(stderr, "unexpected null character in long shebang line of %s\n", argv[1]);
        return exit_null;
      case '\n':
        done = 1;
        if ((args_used + 1) * sizeof(char *) >= args_size)
          increase_buffer((void**) &args, &args_size, sizeof(char *));
        args[args_used++] = argv[1];
	args[args_used++] = NULL;
	res = '\0';
	break;
      case ' ':
        res = '\0';
        if (args_used * sizeof(char *) == args_size)
          increase_buffer((void **) &args, &args_size, sizeof(char *));
        args[args_used++] = args_string + args_string_used + 1;
        break;
      case '\\':
        switch (fgetc(script)) {
          case EOF:
            handle_eof(argv[1]);
          case '\\':
            break;
          case 'n':
            res = '\n';
            break;
          case ' ':
            res = ' ';
            break;
          default:
            fprintf(stderr, "unknown escape %c in long shebang line of %s\n", res, argv[1]);
            return exit_unknown_escape;
        }
        break;
    }
    args_string[args_string_used++] = res;
    if (args_string_used == args_string_size && !done) {
      char * old_args_string = args_string;
      increase_buffer((void **) &args_string, &args_string_size, sizeof(char));
      if (old_args_string != args_string) {
        /* Update pointers... */
        for (size_t i = 1; i < args_used; ++i)
          args[i] = args_string + (args[i] - old_args_string);
      }
    }
  }

  args[0] = args_string;

  execvp(args[0], args);
  fprintf(stderr, "executing %s: %s\n", args[0], strerror(errno));
  return exit_exec;
}
