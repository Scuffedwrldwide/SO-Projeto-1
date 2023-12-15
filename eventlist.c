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

  // Checks if rwlock_init failed
  if (pthread_rwlock_init(&list->rwlock, NULL) != 0) {
    free(list);
    return NULL;
  }

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

  if (pthread_rwlock_wrlock(&list->rwlock)) {
    free(new_node);
    return 1;
  }

  // If the list is empty, set the new node as both head and tail
  if (list->head == NULL) {
    list->head = new_node;
    list->tail = new_node;
  } else {
    list->tail->next = new_node;
    list->tail = new_node;
  }
  if (pthread_rwlock_unlock(&list->rwlock) != 0) {
    if (list->head == new_node)
      list->head = NULL;
    if (list->tail == new_node)
      list->tail = NULL;
    free(new_node);
    return 1;
  }

  return 0;
}

// Function to free the memory of an event
static void free_event(struct Event *event) {
  if (!event)
    return;

  if (pthread_rwlock_destroy(&event->rwlock) != 0) {
    // Error happening here makes no difference
  }

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

  if (pthread_rwlock_destroy(&list->rwlock) != 0) {
    // Error happening here makes no difference
  }
  free(list);
}

// Function to retrieve an event from the EventList based on its ID
struct Event *get_event(struct EventList *list, unsigned int event_id) {
  // Checks if the list is valid
  if (!list)
    return NULL;

  if (pthread_rwlock_rdlock(&list->rwlock) != 0)
    return NULL;

  struct ListNode *current = list->head;

  while (current) {
    struct Event *event = current->event;
    if (event->id == event_id) {
      if (pthread_rwlock_unlock(&list->rwlock) != 0)
        return NULL;
      return event;
    }
    current = current->next;
  }

  if (pthread_rwlock_unlock(&list->rwlock) != 0)
    return NULL;

  return NULL;
}
