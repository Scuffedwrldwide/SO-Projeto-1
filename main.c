#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "constants.h"
#include "operations.h"
#include "parser.h"

void process_file(const char *filename);
void *thread_function(void *params);
unsigned int *wait_queue;

struct thread_params {
  int fd;
  int out_fd;
  int thread_id;
};

int MAX_PROC = 20;
int MAX_THREADS = 2;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
unsigned int barrier_flag = 0;

int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;
  int option;

  DIR *dir = NULL;

  if (argc < 2) {
    fprintf(stderr,
            "Usage: %s -d <state_access_delay_ms> -p <path> -m <max_proc> -t "
            "<max_threads>\n",
            argv[0]);
    return 1;
  }

  while ((option = getopt(argc, argv, "d:p:m:t:")) != -1) {
    printf("option %c\n", option);
    printf("optarg %s\n", optarg);
    switch (option) {
    case 'd':
      if (optarg == NULL) {
        fprintf(stderr, "Invalid delay value\n");
        return 1;
      }

      char *endptr;
      unsigned long int delay = strtoul(optarg, &endptr, 10);

      if (*endptr != '\0' || delay > UINT_MAX) {
        fprintf(stderr, "Invalid delay value or value too large\n");
        return 1;
      }

      state_access_delay_ms = (unsigned int)delay;
      printf("state_access_delay_ms %d\n", state_access_delay_ms);
      break;
    case 'p':
      if ((dir = opendir(optarg)) == NULL || chdir(optarg) == -1) {
        fprintf(stderr, "Failed to open directory %s: %s\n", optarg,
                strerror(errno));
        return 1;
      }
      break;
    case 'm':
      if (optarg == NULL) {
        fprintf(stderr, "Invalid max proc value, defaulting to %d\n", MAX_PROC);
      }
      MAX_PROC = atoi(optarg);
      break;
    case 't':
      if (optarg == NULL) {
        fprintf(stderr, "Invalid max thread value, defaulting to %d\n",
                MAX_THREADS);
      }
      MAX_THREADS = atoi(optarg);
      break;
    }
  }

  printf("MAX_PROC %d\n", MAX_PROC);
  printf("MAX_THREADS %d\n", MAX_THREADS);

  if (ems_init(state_access_delay_ms)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    closedir(dir);
    return 1;
  }

  struct dirent *file;
  int proc_count = 0;
  int status;
  pid_t pid;

  while ((file = readdir(dir)) != NULL) {
    if (file->d_name[0] != '.' &&
        strcmp(&file->d_name[strlen(file->d_name) - 4], "jobs") == 0) {
      if (proc_count > MAX_PROC) {
        // Wait for any child process to finish before starting a new one
        printf("Waiting for child process to finish\n");
        pid = wait(&status);
        if (pid > 0) {
          proc_count--;
        } else {
          fprintf(stderr, "Failed to wait for child process: %s\n",
                  strerror(errno));
          closedir(dir);
          return 1;
        }
      }

      if ((pid = fork()) == 0) {
        // Child process
        // printf("Processing file %s\n", file->d_name);
        process_file(file->d_name);
        exit(0);

      } else {
        // Parent process
        // printf("Child process %d started\n", pid);
        proc_count++;
      }
    }
  }

  // Wait for all child processes to finish
  while (proc_count > 0) {
    // printf("Waiting for child process to finish\n");
    pid = wait(&status);
    printf("Child process %d finished with status %d\n", pid,
           WEXITSTATUS(status));
    if (pid > 0) {
      proc_count--;
    } else {
      fprintf(stderr, "Failed to wait for child process: %s\n",
              strerror(errno));
      closedir(dir);
      return 1;
    }
  }
  ems_terminate();
  free(wait_queue);
  closedir(dir);
  return 0;
}

void process_file(const char *filename) {
  int fd = -1;
  int out_fd = -1;

  char out_file_name[32];

  fd = open(filename, O_RDONLY);
  strncpy(out_file_name, filename, strlen(filename) - 4);
  strcpy(&out_file_name[strlen(filename) - 4], "out\0");
  printf("out_file_name %s\n", out_file_name);

  out_fd = open(out_file_name, O_WRONLY | O_CREAT | O_TRUNC,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

  if (fd == -1) {
    fprintf(stderr, "Failed to open file %s: %s\n", filename, strerror(errno));
    return;
  }
  if (out_fd == -1) {
    fprintf(stderr, "Failed to open file %s: %s\n", out_file_name,
            strerror(errno));
    close(fd);
    return;
  }

  pthread_t threads[MAX_THREADS];
  struct thread_params params[MAX_THREADS];
  pthread_mutex_init(&mutex, NULL);
  void *thread_status = &barrier_flag;
  wait_queue = malloc(sizeof(int) * (unsigned long)MAX_THREADS);

  while (thread_status != NULL) {
    barrier_flag = 0;
    for (int i = 0; i < MAX_THREADS; i++) {
      params[i].fd = fd;
      params[i].out_fd = out_fd;
      params[i].thread_id = i;

      if (pthread_create(&threads[i], NULL, thread_function, &params[i]) != 0) {
        fprintf(stderr, "Failed to create thread\n");
        return;
      }
    }

    for (int i = 0; i < MAX_THREADS; i++) {
      if (pthread_join(threads[i], &thread_status) != 0) {
        fprintf(stderr, "Failed to join thread\n");
        close(fd);
        close(out_fd);
        return;
      }
    }
  }
  close(fd);
  close(out_fd);
}

void *thread_function(void *params) {
  struct thread_params *thread_params = (struct thread_params *)params;

  int fd = thread_params->fd;
  int out_fd = thread_params->out_fd;
  int thread_id = thread_params->thread_id;

  while (1) {
    unsigned int event_id, delay, *target_id;
    int do_wait;
    size_t num_rows, num_columns, num_coords;
    size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

    fflush(stdout);
    pthread_mutex_lock(&mutex);
    if (wait_queue[thread_id] > 0) {
      printf("thread number %d ordered to wait for %dms\n", thread_id,
             wait_queue[thread_id]);
      ems_wait(wait_queue[thread_id]);
      wait_queue[thread_id] = 0;
      printf("thread number %d done waiting\n", thread_id);
    }
    if (barrier_flag != 0) {
      printf("thread number %d found closed barrier\n", thread_id);
      pthread_mutex_unlock(&mutex);
      pthread_exit(&barrier_flag);
    }
    switch (get_next(fd)) {
    case CMD_CREATE:
      if (parse_create(fd, &event_id, &num_rows, &num_columns) != 0) {
        pthread_mutex_unlock(&mutex);
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }
      pthread_mutex_unlock(&mutex);
      if (ems_create(event_id, num_rows, num_columns)) {
        fprintf(stderr, "Failed to create event\n");
      }
      break;

    case CMD_RESERVE:
      num_coords = parse_reserve(fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);
      pthread_mutex_unlock(&mutex);
      if (num_coords == 0) {
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }

      if (ems_reserve(event_id, num_coords, xs, ys)) {
        fprintf(stderr, "Failed to reserve seats\n");
      }
      break;

    case CMD_SHOW:
      if (parse_show(fd, &event_id) != 0) {
        pthread_mutex_unlock(&mutex);
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }
      if (ems_show(event_id, out_fd)) {
        fprintf(stderr, "Failed to show event\n");
      }
      pthread_mutex_unlock(&mutex);
      break;

    case CMD_LIST_EVENTS:
      pthread_mutex_unlock(&mutex);
      if (ems_list_events(out_fd)) {
        fprintf(stderr, "Failed to list events\n");
      }
      break;

    case CMD_WAIT:
      target_id = malloc(sizeof(unsigned int));
      do_wait = parse_wait(fd, &delay, target_id);
      if (do_wait == -1) {
        free(target_id);
        pthread_mutex_unlock(&mutex);
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        continue;
      }
      if (delay > 0) {
        if (do_wait == 1 && *target_id != (unsigned int)thread_id) {
          printf("thread number %d NOT waiting, thread %d should be\n",
                 thread_id, *target_id); // threads might skip wait for others
          wait_queue[*target_id] += delay;
          // target id will be freed by the thread that is waiting
          pthread_mutex_unlock(&mutex);
          break;
        }
        printf("thread number %d waiting for %dms as targeted by %d\n",
               thread_id, delay, *target_id);
        free(target_id);
        pthread_mutex_unlock(&mutex);
        ems_wait(delay);
        printf("thread number %d done waiting\n", thread_id);
      }
      break;

    case CMD_INVALID:
      pthread_mutex_unlock(&mutex);
      fprintf(stderr, "Invalid command. See HELP for usage\n");
      break;

    case CMD_HELP:
      pthread_mutex_unlock(&mutex);
      printf("Available commands:\n"
             "  CREATE <event_id> <num_rows> <num_columns>\n"
             "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
             "  SHOW <event_id>\n"
             "  LIST\n"
             "  WAIT <delay_ms> [thread_id]\n" // thread_id is not implemented
             "  BARRIER\n"                     // Not implemented
             "  HELP\n");
      break;

    case CMD_BARRIER:
      if (barrier_flag == 0) {
        printf("thread number %d triggered barrier\n", thread_id);
        barrier_flag = 1;
      }
      pthread_mutex_unlock(&mutex);
      pthread_exit(&barrier_flag);
    case CMD_EMPTY:
      pthread_mutex_unlock(&mutex);
      break;

    case EOC:
      // printf("End of commands\n");
      pthread_mutex_unlock(&mutex);
      pthread_exit(NULL);
    }
  }
}