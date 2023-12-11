#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "constants.h"
#include "operations.h"
#include "parser.h"

void process_file(const char *filename);

int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;
  int option;
  int MAX_PROC = 20;
  DIR *dir = NULL;

  
  while((option = getopt(argc, argv, "d:p:m:t")) != -1){
    switch(option){
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
          fprintf(stderr, "Failed to open directory %s: %s\n", optarg, strerror(errno));
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
        //PLACEHOLDER FOR MAX THREADS
        printf("t option\n");
        break;
    }
  }

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
    if (file->d_name[0] != '.' && strcmp(&file->d_name[strlen(file->d_name) - 4], "jobs") == 0) {
      if (proc_count > MAX_PROC) {
        // Wait for any child process to finish before starting a new one
        printf("Waiting for child process to finish\n");
        pid = wait(&status);
        if (pid > 0) {
          proc_count--;
        } else {
          fprintf(stderr, "Failed to wait for child process: %s\n", strerror(errno));
          closedir(dir);
          return 1;
        }
      }

      if ((pid = fork()) == 0) {
        // Child process
        //printf("Processing file %s\n", file->d_name);
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
    //printf("Waiting for child process to finish\n");
    pid = wait(&status);
    printf("Child process %d finished\n", pid);
    if (pid > 0) {
      proc_count--;
    } else {
      fprintf(stderr, "Failed to wait for child process: %s\n", strerror(errno));
      closedir(dir);
      return 1;
    }
  }

  ems_terminate();
  closedir(dir);
  return 0;
}

void process_file(const char *filename){
  int fd = -1;
  int out_fd = -1;

  char out_file_name[32];

  fd = open(filename, O_RDONLY);
  strncpy(out_file_name, filename, strlen(filename) - 4);
  strcpy(&out_file_name[strlen(filename) - 4], "out\0");
  //printf("out_file_name %s\n", out_file_name);

  out_fd = open(out_file_name, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

  while (fd != -1) {
    unsigned int event_id, delay;
    size_t num_rows, num_columns, num_coords;
    size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

    fflush(stdout);
    switch (get_next(fd)) {
      case CMD_CREATE:
        if (parse_create(fd, &event_id, &num_rows, &num_columns) != 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }

        if (ems_create(event_id, num_rows, num_columns)) {
          fprintf(stderr, "Failed to create event\n");
          
        }
        break;

      case CMD_RESERVE:
        num_coords = parse_reserve(fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);

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
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }

        if (ems_show(event_id, out_fd)) {
          fprintf(stderr, "Failed to show event\n");
        }

        break;

      case CMD_LIST_EVENTS:
        if (ems_list_events(out_fd)) {
          fprintf(stderr, "Failed to list events\n");
        }
        
        break;

      case CMD_WAIT:
        if (parse_wait(fd, &delay, NULL) == -1) {  // thread_id is not implemented
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }

        if (delay > 0) {
          printf("Waiting...\n");
          ems_wait(delay);
        }

        break;

      case CMD_INVALID:
        fprintf(stderr, "Invalid command. See HELP for usage\n");
        break;

      case CMD_HELP:
        printf(
            "Available commands:\n"
            "  CREATE <event_id> <num_rows> <num_columns>\n"
            "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
            "  SHOW <event_id>\n"
            "  LIST\n"
            "  WAIT <delay_ms> [thread_id]\n"  // thread_id is not implemented
            "  BARRIER\n"                      // Not implemented
            "  HELP\n");

        break;

      case CMD_BARRIER:  // Not implemented
        break;
      case CMD_EMPTY:
        break;

      case EOC:
        //printf("End of commands\n"); 
        close(fd);
        close(out_fd);
        return;
    }
  }
}