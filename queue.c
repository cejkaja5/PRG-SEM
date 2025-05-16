#include "queue.h"
#include <stdlib.h>
#include <stdbool.h>


typedef int (*Comparer)(const void *, const void *);
typedef void (*ContentDisposer)(void *);

typedef struct Node_{
    void *content;
    struct Node_* next;
} Node;

typedef struct Queue_
{
    Node *head;
    Node *tail;
    Comparer compare;
    ContentDisposer clear;
} Queue;


/* 
 * Allocate a new queue structure or return NULL on an error.
 * Particular type is implementation dependent
 */
void* create(void){
    Queue *queue = malloc(sizeof(Queue));
    if (queue == NULL) return NULL;
    queue->head = NULL;
    queue->tail = NULL;
    queue->compare = NULL;
    queue->clear = NULL;
    return queue;
}
 
/*
 * Release all memory accessible from the queue, i.e., all dynamic
 * data entries stored in the individual queue entries. The clear 
 * function must be properly set, see setClear() below.
 */
void clear(void *queue){
    if (queue == NULL) return;
    Queue *q = (Queue *)queue;
    Node *node = q->head, *prev = NULL;
    while (node != NULL){
        prev = node;
        node = node->next;
        if (q->clear != NULL) q->clear(prev->content);
        free(prev);
    }
    q->head = NULL;
    q->tail = NULL;    
}
 
/*
 * Push the given item into the queue. The pointer to the entry
 * should not be NULL, i.e., storing NULL entries is not allowed.
 * return: true on success and false otherwise.
 */
bool push(void *queue, void *entry){
    if (entry == NULL || queue == NULL) return false;
    Queue *q = (Queue *)queue;
    Node *new_node = malloc(sizeof(Node));
    if (new_node == NULL) return false;
    new_node->next = NULL;
    new_node->content = entry;
    if (size(queue) == 0) {
        q->head = new_node;
        q->tail = new_node;
    }
    else {
        q->tail->next = new_node;
        q->tail = new_node;
    }
    return true;
}
 
/*
 * Pop an entry from the head of the queue
 * return: the stored pointer to the entry or NULL if the queue is empty
 */
void* pop(void *queue){
    if (queue == NULL) return NULL;
    Queue *q = (Queue *)queue;
    if (size(queue) == 0) return NULL;
    void *content = q->head->content;
    Node *tmp = q->head;
    q->head = q->head->next;
    free(tmp);
    if (q->head == NULL) q->tail = NULL;
    return content;
}
 
/*
 * Insert the given entry to the queue in the InsertSort style using 
 * the set compare function (by the setCompare() ). If such a function
 * is not set or an error occurs during the insertion it returns false.
 *
 * Since push and insert functions can be combined, it cannot be 
 * guaranteed, the internal structure of the queue is always sorted.
 *
 * The expected behaviour is that insert proceeds from the head of
 * the queue to the tail in such a way that it is insert before the entry
 * with the lower value, i.e., it becomes a new head if its value is the
 * new maximum or a new tail if its value is a new minimum of the values
 * in the queue.
 *
 * return: true on success; false otherwise
 */
bool insert(void *queue, void *entry){
    if (entry == NULL || queue == NULL) return false;
    Queue *q = (Queue *)queue;
    if (q->compare == NULL) return false;
    Node *node = q->head, *prev = NULL;
    Node *entry_node = malloc(sizeof(Node));
    if (entry_node == NULL) return false;
    entry_node->content = entry;

    while (node != NULL){
        if (q->compare(entry, node->content) >= 0){
            if (prev != NULL) prev->next = entry_node;
            else q->head = entry_node;
            entry_node->next = node;
            return true;
        }
        prev = node;
        node = node->next;
    }
    push(queue, entry);
    return true;
}

/*
 * Erase all entries with the value entry, if such exists
 * return: true on success; false to indicate no such value has been removed
 */
bool erase(void *queue, void *entry){
    if (queue == NULL) return false;
    Queue *q = (Queue *)queue;
    if (q->compare == NULL) return false;    
    bool erased_smt = false;
    Node *node = q->head, *matching, *prev = NULL;
    while (node != NULL){
        if (q->compare(node->content, entry) == 0){
            matching = node;
            node = node->next;
            erased_smt = true;
            if (matching == q->head) {
                free(q->head);
                q->head = node;
            } else if (matching == q->tail) {
                free(q->tail);
                q->tail = prev;
                q->tail->next = NULL;
            } else {
                free(matching);
                prev->next = node;
            }
        } else {
            prev = node;
            node = node->next;
        }
    }
    return erased_smt;
}
 
/*
 * For idx >= 0 and idx < size(queue), it returns the particular item
 * stored at the idx-th position of the queue. The head of the queue
 * is the entry at idx = 0.
 *
 * return: pointer to the stored item at the idx position of the queue
 * or NULL if such an entry does not exists at such a position
 */
void* getEntry(const void *queue, int idx){
    if (queue == NULL || idx < 0) return NULL;
    const Queue *q = (const Queue *)queue;
    int i = 0;
    Node *node = q->head;
    while (node != NULL){
        if (i++ == idx) return node->content;
        node = node->next;
    }
    return NULL;
}
 
/*
 * return: the number of stored items in the queue
 */
int size(const void *queue){
    if (queue == NULL) return -1;
    const Queue *q = (const Queue *)queue;
    int i = 0;
    Node *node = q->head;
    while (node != NULL){
        i++;
        node = node->next;
    }
    return i;
}
 
/*
 * Set a pointer to function for comparing particular inserted items
 * to the queue. It is similar to the function used in qsort, see man qsort:
 * "The comparison function must return an integer less than, equal to, or
 * greater than zero if the first argument is considered to be respectively
 * less than, equal to, or greater than the second."
 */
void setCompare(void *queue, Comparer compare){
    if (queue == NULL || compare == NULL) return;
    Queue *q = (Queue *)queue;
    q->compare = compare;
}
 
/*
 * Set a pointer to function which can properly delete the inserted 
 * items to the queue. If it is not set, all the items stored in the
 * queue are deleted calling standard free() in the clear() 
 */
void setClear(void *queue, ContentDisposer clear){
    if (queue == NULL || clear == NULL) return;
    Queue *q = (Queue *)queue;
    q->clear = clear;
}