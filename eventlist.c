#include "eventlist.h"

#include <stdlib.h>

struct EventList* create_list() {
  struct EventList* list = (struct EventList*)malloc(sizeof(struct EventList));
  if (!list) return NULL;
  list->head = NULL;
  list->tail = NULL;

  list->rwlock = (pthread_rwlock_t)PTHREAD_RWLOCK_INITIALIZER;
  pthread_rwlock_init(&list->rwlock, NULL);

  return list;
}

int append_to_list(struct EventList* list, struct Event* event) {
  if (!list) return 1;

  struct ListNode* new_node = (struct ListNode*)malloc(sizeof(struct ListNode));
  if (!new_node) return 1;

  new_node->event = event;
  new_node->next = NULL;

  pthread_rwlock_wrlock(&list->rwlock);
  if (list->head == NULL) {
    list->head = new_node;
    list->tail = new_node;
  } else {
    list->tail->next = new_node;
    list->tail = new_node;
  }
  pthread_rwlock_unlock(&list->rwlock);

  return 0;
}

static void free_event(struct Event* event) {
  if (!event) return;

  free(event->data);
  free(event);
}

void free_list(struct EventList* list) {
  if (!list) return;

  struct ListNode* current = list->head;
  while (current) {
    struct ListNode* temp = current;
    current = current->next;

    free_event(temp->event);
    free(temp);
  }

  pthread_rwlock_destroy(&list->rwlock);
  free(list);
}

struct Event* get_event(struct EventList* list, unsigned int event_id) {
  if (!list) return NULL;
  pthread_rwlock_rdlock(&list->rwlock);
  struct ListNode* current = list->head;
  while (current) {
    struct Event* event = current->event;
    if (event->id == event_id) {
      pthread_rwlock_unlock(&list->rwlock);
      return event;
    }
    current = current->next;
  }
  pthread_rwlock_unlock(&list->rwlock);

  return NULL;
}
