#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#define NEED_FCNTL
#endif

typedef enum {
  exit_usage = 1,
  exit_open_script = 2,
  exit_reading = 3,
  exit_eof = 4,
  exit_mem = 5,
  exit_null = 6,
  exit_unknown_escape = 7,
  exit_exec = 8,
  exit_no_shebang = 9,
  exit_stat = 10,
  exit_no_args = 11,
  exit_bad_script_arg = 12,
  exit_bad_a_escape = 13
} exit_code;

typedef struct {
  int fd;
  char * buf;
  size_t offset;
  size_t fill;
  size_t capacity;
  char * filename;
} state;

static void read_more(state * st) {
  if (st->fill == st->capacity) {
    st->capacity *= 2;
    if (st->fill >= st->capacity) {
      if (st->fill == SIZE_MAX) {
        fprintf(stderr, "argument buffer requires ridiculous amount of space (did you really hit this code path?)\n");
        exit(exit_mem);
      }
      st->capacity = SIZE_MAX;
    }
    st->buf = realloc(st->buf, st->capacity);
    if (!st->buf) {
      perror("allocating argument buffer");
      exit(exit_mem);
    }
  }

  ssize_t nread = read(st->fd, st->buf + st->fill, st->capacity - st->fill);
  if (nread == -1) {
    fprintf(stderr, "reading from %s: %s\n", st->filename, strerror(errno));
    exit(exit_reading);
  } else if (nread == 0) {
    fprintf(stderr, "unexpected EOF reading from %s\n", st->filename);
    exit(exit_eof);
  }
  st->fill += (size_t) nread;
}

static char next_char(state * st) {
  if (st->offset == st->fill) {
    st->fill = 0;
    st->offset = 0;
    read_more(st);
  }
  return st->buf[st->offset++];
}

int main(int argc, char ** argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage (meant for shebang only): %s SCRIPT\n", argv[0]);
    return exit_usage;
  }

  state st;

  st.filename = argv[1];

  st.fd = open(st.filename, O_RDONLY | O_CLOEXEC);
  if (st.fd == -1) {
    fprintf(stderr, "opening %s for reading: %s\n", st.filename, strerror(errno));
    return exit_open_script;
  }
#ifdef NEED_FCNTL
  fcntl(st.fd, F_SETFD, fcntl(st.fd, F_GETFD) | FD_CLOEXEC);
#endif

  struct stat buf;
  if (fstat(st.fd, &buf) == -1) {
    fprintf(stderr, "getting file status of %s: %s\n", st.filename, strerror(errno));
    return exit_stat;
  }

  st.capacity = buf.st_blksize;
  st.fill = 0;
  st.offset = 0;
  st.buf = malloc(st.capacity);
  if (!st.buf) {
    perror("allocating argument buffer");
    exit(exit_mem);
  }

  /* Skip first line */
  while (next_char(&st) != '\n');

  /* Ensure we start with a shebang */
  if (next_char(&st) != '#' || next_char(&st) != '!') {
    fprintf(stderr, "second line of %s doesn't begin with #!\n", st.filename);
    return exit_no_shebang;
  }

  /* Skip leading spaces */
  while (next_char(&st) == ' ');

  st.fill = st.fill - (st.offset - 1);
  memmove(st.buf, st.buf + st.offset - 1, st.fill);
  st.offset = 0;

  int separate_arg = 0;
  size_t arg_count = 0;
  /* Read long shebang line */
  while (1) {
    for (; st.offset < st.fill; ++st.offset) {
      switch (st.buf[st.offset]) {
        case '\0':
          fprintf(stderr, "unexpected null character in long shebang line of %s\n", st.filename);
          return exit_null;
        case '\n':
          st.buf[st.offset] = '\0';
          arg_count++;
          goto args_done;
        case ' ':
          st.buf[st.offset] = '\0';
          arg_count++;
          /* arg_count overflow would require st.capacity overflow, which we already check for */
          break;
        case '\\':
          ++st.offset;
          if (st.offset == st.fill)
            read_more(&st);
          switch (st.buf[st.offset]) {
            case 'n':
              st.buf[st.offset] = '\n';
            case '\\':
            case ' ':
              memmove(st.buf + st.offset - 1, st.buf + st.offset, st.fill - st.offset);
              st.fill--;
              st.offset--;
              break;
            case 'a':
              if (st.offset == st.fill)
                read_more(&st);
              if (st.offset != 1 || st.buf[2] != ' ' || separate_arg) {
                fprintf(stderr, "\\a escape doesn't appear as the first argument of long shebang line %s\n", st.filename);
                return exit_bad_a_escape;
              }
              if (st.fill == 2)
                read_more(&st);
              memmove(st.buf, st.buf + 3, st.fill - 3);
              st.offset = 0;
              separate_arg = 1;
              break;
            default:
              fprintf(stderr, "unknown escape %c in long shebang line of %s\n", st.buf[st.offset], st.filename);
              return exit_unknown_escape;
          }
          break;
      }
    }
    read_more(&st);
  }
args_done:
  arg_count -= separate_arg;

  if (arg_count == 0) {
    fprintf(stderr, "no arguments in long shebang line of %s\n", st.filename);
    return exit_no_args;
  }

  char ** args = malloc(sizeof(char**) * (arg_count + 2 /* script, trailing null */));
  if (!args) {
    perror("allocating arg pointer buffer");
    return exit_mem;
  }

  args[0] = st.buf;
  if (separate_arg) {
    while (*args[0] != '\0')
      args[0]++;
    args[0]++;
  }

  for (size_t i = 1; i < arg_count; ++i) {
    args[i] = args[i-1] + 1;
    while (*args[i] != '\0')
      args[i]++;
    args[i]++;
  }
  args[arg_count] = st.filename;
  args[arg_count + 1] = NULL;

  execvp(st.buf, args);
  fprintf(stderr, "executing %s: %s\n", args[0], strerror(errno));
  return exit_exec;
}
