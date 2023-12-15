#include "eventlist.h"

#include <stdlib.h>

struct EventList *create_list() {
  struct EventList *list = (struct EventList *)malloc(sizeof(struct EventList));
  // Checks if malloc failed
  if (!list)
    return NULL;

  // Initializes
  list->head = NULL;
  list->tail = NULL;
  list->rwlock = (pthread_rwlock_t)PTHREAD_RWLOCK_INITIALIZER;
  pthread_rwlock_init(&list->rwlock, NULL);

  return list;
}

// Function to append a new event to the EventList
int append_to_list(struct EventList *list, struct Event *event) {
  // Checks if the list is valid
  if (!list)
    return 1;
  // Allocate memory for a new list node
  struct ListNode *new_node =
      (struct ListNode *)malloc(sizeof(struct ListNode));
  // Checks if malloc failed
  if (!new_node)
    return 1;

  new_node->event = event;
  new_node->next = NULL;

  pthread_rwlock_wrlock(&list->rwlock);

  // If the list is empty, set the new node as both head and tail
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

// Function to free the memory of an event
static void free_event(struct Event *event) {
  if (!event)
    return;

  free(event->data);
  free(event);
}

// Function to free the memory of an EventList
void free_list(struct EventList *list) {
  if (!list)
    return;

  struct ListNode *current = list->head;
  while (current) {
    struct ListNode *temp = current;
    current = current->next;

    free_event(temp->event);
    free(temp);
  }

  pthread_rwlock_destroy(&list->rwlock);
  free(list);
}

// Function to retrieve an event from the EventList based on its ID
struct Event *get_event(struct EventList *list, unsigned int event_id) {
  // Checks if the list is valid
  if (!list)
    return NULL;

  pthread_rwlock_rdlock(&list->rwlock);
  struct ListNode *current = list->head;

  while (current) {
    struct Event *event = current->event;
    if (event->id == event_id) {
      pthread_rwlock_unlock(&list->rwlock);
      return event;
    }
    current = current->next;
  }

  pthread_rwlock_unlock(&list->rwlock);

  return NULL;
}
