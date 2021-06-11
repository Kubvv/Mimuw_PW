#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include "cacti.h"

/* Declared structs */
#define UNUSED(x) (void)(x)

struct mQueue {
    int start;
    int end;
    int size;
    message_t **mess;
}; typedef struct mQueue mQueue;

struct action
{
    bool dead;
    bool spw;
    pthread_mutex_t aMutex;
    actor_id_t id;
    mQueue *mQ;
    const role_t *roles;
    void *sttptr;
}; typedef struct action actorAction;

typedef actorAction *act_action_t;

struct tQueue {
    int start;
    int end;
    int size;
    actorAction **aQ;
}; typedef struct tQueue tQueue;

struct tPool {
    bool eom;
    int threadCount;
    tQueue *workToDo;
    pthread_mutex_t tPoolMutex;
    pthread_cond_t noActions;
    pthread_cond_t everyTFree;
    pthread_t sigThread;
    pthread_t threads[POOL_SIZE];
}; typedef struct tPool tPool;

struct actors {
    actor_id_t end;
    actor_id_t size;
    int alive;
    actorAction **acts;
    pthread_mutex_t contMutex;
}; typedef struct actors actArr;

__thread actor_id_t globalId;
pthread_mutex_t globalMutex;
pthread_cond_t globalCond;
pthread_key_t self;
tPool *thrPool = NULL;
actArr *actorsCont = NULL;

void msg_spawn(void *data);
void msg_godie();

bool tpoolImm = false;
bool actImm = false;

/* Signals */

void signalHandler() {
    int err;
    if (!(tpoolImm || actImm)) {
        if ((err = pthread_mutex_lock(&(actorsCont->contMutex))) != 0) {
            exit(1);
        }
        for (int i = 0; i < actorsCont->end; i++) {
            actorsCont->acts[i]->dead = true;
        }
        if ((err = pthread_mutex_unlock(&(actorsCont->contMutex))) != 0) {
            exit(1);
        }
        if ((err = pthread_mutex_lock(&(thrPool->tPoolMutex))) != 0) {
            exit(1);
        }
        if (thrPool->workToDo->start == thrPool->workToDo->end) {
            actorsCont->alive = 0;
            if ((err = pthread_cond_broadcast(&(globalCond))) != 0) {
                exit(1);
            }
        }
        if ((err = pthread_mutex_unlock(&(thrPool->tPoolMutex))) != 0) {
            exit(1);
        }
    }
    actor_system_join(0);
}

void maskSignal(int how, int sig) {
    sigset_t masque;
    sigfillset(&masque);

    if (sig != -1) {
        sigdelset(&masque, sig);
    }

    pthread_sigmask(how, &masque, NULL);
}

void setSigInt() {
    struct sigaction sigact = {0};
    sigfillset(&sigact.sa_mask);
    sigact.sa_handler = signalHandler;
    sigaction(SIGINT, &sigact, NULL);
}

void *signalWork() {
    maskSignal(SIG_UNBLOCK, -1);
    setSigInt();
    int signum;
    sigset_t s;
    sigfillset(&s);
    sigwait(&s, &signum);
    if (signum == SIGINT) {
        signalHandler();
    }
    return NULL;
}

/* Thread queue */

// Initializes message queue, returns null if something went wrong and pointer to queue in other case.
tQueue *initializeTQueue() {
    tQueue *tQ = malloc(sizeof(tQueue));
    if (tQ == NULL) {
        return NULL;
    }

    tQ->size = 1000;
    tQ->aQ = malloc(sizeof(act_action_t) * tQ->size);
    if (tQ->aQ == NULL) {
        free(tQ);
        return NULL;
    }

    tQ->start = 0;
    tQ->end = 0;

    return tQ;
}

// Puts message at the end of the queue. Adjusts mQueue variables.
// returns false if something went wrong and true if element was put int the queue.
int pushTQueue(tQueue *tQ, actorAction *a) {
    if (tQ->end == tQ->size) {
        tQueue *rollback = tQ;
        tQ->aQ = realloc(tQ->aQ, sizeof(tQueue) * 2 * tQ->size);
        if (tQ == NULL) {
            tQ = rollback;
            return -1;
        } else {
            tQ->size = tQ->size * 2;
        }
    }

    tQ->aQ[tQ->end] = a;
    tQ->end += 1;
    return 0;
}

// Pops the first element of the queue and places null in place of popped element. Adjusts mQueue variables.
// Returns popped element if queue was not empty and null if queue was empty.
actorAction *popTQueue(tQueue *tQ) {
    if (tQ->start == tQ->end) {
        return NULL;
    }

    actorAction *result = tQ->aQ[tQ->start];
    tQ->aQ[tQ->start] = NULL;

    for (int i = 0; i < tQ->end - 1; i++) {
        tQ->aQ[i] = tQ->aQ[i + 1];
    }
    tQ->end -= 1;
    return result;
}

// Destroys tQueue if it wasn't null.
void destroyTQueue(tQueue *tQ) {
    if (tQ == NULL) {
        return;
    }
    free(tQ->aQ);
    free(tQ);
    tQ = NULL;
}

// Destroys given tQueue and returns a newly initialized tQueue.
tQueue *clearTQueue(tQueue *tQ) {
    destroyTQueue(tQ);
    return initializeTQueue();
}

/* Message queue */

// Initializes message queue, returns null if something went wrong and pointer to queue in other case.
mQueue* initializeMQueue() {
    mQueue* mQ = malloc(sizeof(mQueue));
    if (mQ == NULL) {
        return NULL;
    }

    mQ->size = ACTOR_QUEUE_LIMIT;
    mQ->mess = calloc(mQ->size, sizeof(message_t*));
    if (mQ->mess == NULL) {
        free(mQ);
        return NULL;
    }

    mQ->start = -1;
    mQ->end = -1;

    return mQ;
}

// Puts message at the end of the queue. Adjusts mQueue variables.
// returns false if something went wrong and true if element was put int the queue.
int pushMQueue(mQueue *mQ, message_t *message) {
    if (mQ->start == mQ->end + 1 || (mQ->start == 0 && mQ->size - 1 == mQ->end)) {
        return -1;
    }
    else if (mQ->start == -1) {
        mQ->mess[0] = message;
        mQ->start = 0;
        mQ->end = 0;
    }
    else if (mQ->start != 0 && mQ->end == mQ->start - 1) {
        mQ->mess[0] = message;
        mQ->end = 0;
    }
    else {
        mQ->end += 1;
        mQ->mess[mQ->end] = message;
    }

    return 0;
}

// Pops the first element of the queue and places null in place of popped element. Adjusts mQueue variables.
// returns popped element if queue was not empty and null if queue was empty.
message_t* popMQueue(mQueue *mQ) {
    if (mQ->start == -1) {
        return NULL;
    }

    message_t *result = mQ->mess[mQ->start];
    mQ->mess[mQ->start] = NULL;
    if (mQ->start == mQ->size - 1) {
        mQ->start = 0;
    }
    else if (mQ->start == mQ->end) {
        mQ->start = -1;
        mQ->end = -1;
    }
    else {
        mQ->start += 1;
    }

    return result;
}

// Destroys mQueue if it wasn't null.
void destroyMQueue(mQueue *mQ)
{
    if (mQ == NULL) {
        return;
    }
    for (int i = 0; i < mQ->size; i++) {
        if (mQ->mess[i] != NULL) {
            free(mQ->mess[i]);
        }
    }
    free(mQ->mess);
    free(mQ);
    mQ = NULL;
}

// Adds action related to actor with given id to thread pool.
// Returns true if action was correctly added and false in other case.
int addActionThreadPool(tPool *tp, actorAction *toAdd) {
    int err;
    if (tp == NULL) {
        return -1;
    }
    else if (tp->eom) {
        return -2;
    }

    if ((err = pthread_mutex_lock(&(tp->tPoolMutex))) != 0) {
        exit(1);
    }

    if (pushTQueue(tp->workToDo, toAdd) != 0) {
        return -3;
    }
    if ((err = pthread_cond_broadcast(&(tp->noActions))) != 0) {
        exit(1);
    }
    if ((err = pthread_mutex_unlock(&(tp->tPoolMutex))) != 0) {
        exit(1);
    }

    return 0;
}

/* Working Thread's work */
void* workThreadPool(void *t)
{
    maskSignal(SIG_SETMASK, -1);
    int err;
    actorAction *action;
    tPool *tp = t;
    while (1)
    {
        if ((err = pthread_mutex_lock(&(tp->tPoolMutex))) != 0) {
            exit(1);
        }

        while (!tp->eom && tp->workToDo->start == tp->workToDo->end) {
            if ((err = pthread_cond_wait(&(tp->noActions), &(tp->tPoolMutex))) != 0) {
                exit(1);
            }
        }
        if (tp->eom) {
            break;
        }
        action = popTQueue(tp->workToDo);
        if ((err = pthread_mutex_unlock(&(tp->tPoolMutex))) != 0) {
            exit(1);
        }
        if (action != NULL) {
            globalId = action->id;

            if ((err = pthread_mutex_lock(&(action->aMutex))) != 0) {
                exit(1);
            }
            message_t* toDo = popMQueue(action->mQ);
            bool cond = (action->mQ->start == -1);
            if ((err = pthread_mutex_unlock(&(action->aMutex))) != 0) {
                exit(1);
            }
            if (toDo->message_type == MSG_GODIE) {
                msg_godie();
            }
            else if (toDo->message_type == MSG_SPAWN) {
                msg_spawn(toDo->data);
            }
            else {
                action->roles->prompts[toDo->message_type]
                    (&action->sttptr, toDo->nbytes, toDo->data);
                if (action->spw && toDo->message_type == MSG_HELLO) {
                    action->spw = false;
                    free(toDo->data);
                }
            }
            free(toDo);
            if ((err = pthread_mutex_lock(&(actorsCont->contMutex))) != 0) {
                exit(1);
            }
            if ((err = pthread_mutex_lock(&(action->aMutex))) != 0) {
                exit(1);
            }
            if (!cond) {
                addActionThreadPool(thrPool, action);
            }
            else if (action->dead) {
                actorsCont->alive -= 1;
            }
            if ((err = pthread_mutex_unlock(&(action->aMutex))) != 0) {
                exit(1);
            }
            if (actorsCont->alive <= 0) {
                if ((err = pthread_cond_broadcast(&(globalCond))) != 0) {
                    exit(1);
                }
            }
            if ((err = pthread_mutex_unlock(&(actorsCont->contMutex))) != 0) {
                exit(1);
            }

        }
    }

    tp->threadCount--;
    if ((err = pthread_cond_broadcast(&(tp->everyTFree))) != 0) {
        exit(1);
    }
    if ((err = pthread_mutex_unlock(&(tp->tPoolMutex))) != 0) {
        exit(1);
    }

    return NULL;
}

/* Thread pool */

// Creates a thread pool consisting of POOL_SIZE threads and initializes variables and tQueue,
// returns null if something went wrong and pointer to thread pool in other case.
tPool* createThreadPool()
{
    int err;
    tPool *tp;

    if (POOL_SIZE == 0) {
        return NULL;
    }
    maskSignal(SIG_SETMASK, -1);

    tp = malloc(sizeof(tPool));
    if (tp == NULL) {
        return NULL;
    }

    tp->workToDo = initializeTQueue();
    if (tp->workToDo == NULL) {
        free(tp);
        return NULL;
    }

    tp->eom = false;
    tp->threadCount = POOL_SIZE;
    if ((err = pthread_cond_init(&(tp->noActions), NULL)) != 0) {
        exit(1);
    }
    if ((err = pthread_cond_init(&(tp->everyTFree), NULL)) != 0) {
        exit(1);
    }
    if ((err = pthread_mutex_init(&(tp->tPoolMutex), NULL)) != 0) {
        exit(1);
    }

    if ((err = pthread_create(&tp->sigThread, NULL, signalWork, NULL)) != 0) {
        exit(1);
    }

    for (int i = 0; i < tp->threadCount; i++) {
        if ((err = pthread_create(&tp->threads[i], NULL, workThreadPool, tp)) != 0) {
            exit(1);
        }
    }

    return tp;
}

// Waits until every thread ends their job, if thread pool exists.
void waitThreadPool(tPool *tp)
{
    int err;
    if (tp == NULL) {
        return;
    }

    if ((err = pthread_mutex_lock(&(tp->tPoolMutex))) != 0) {
        exit(1);
    }
    while (tp->threadCount != 0) {
        if ((err = pthread_cond_wait(&(tp->everyTFree), &(tp->tPoolMutex))) != 0) {
            exit(1);
        }
    }
    if ((err = pthread_mutex_unlock(&(tp->tPoolMutex))) != 0) {
        exit(1);
    }
}

// Destroys given thread pool, if it existed.
static void destroyThreadPool(tPool *tp)
{
    tpoolImm = true;
    int err;

    if (tp == NULL) {
        tpoolImm = false;
        return;
    }

    if ((err = pthread_mutex_lock(&(tp->tPoolMutex))) != 0) {
        exit(1);
    }
    tp->eom = true;
    if ((err = pthread_cond_broadcast(&(tp->noActions))) != 0) {
        exit(1);
    }
    if ((err = pthread_mutex_unlock(&(tp->tPoolMutex))) != 0) {
        exit(1);
    }

    waitThreadPool(tp);

    if ((err = pthread_mutex_destroy(&(tp->tPoolMutex))) != 0) {
        //exit(1);
    }
    if ((err = pthread_cond_destroy(&(tp->noActions))) != 0) {
        exit(1);
    }
    if ((err = pthread_cond_destroy(&(tp->everyTFree))) != 0) {
        exit(1);
    }
    destroyTQueue(tp->workToDo);
    for (int i = 0; i < POOL_SIZE; i++) {
        if ((err = pthread_join(tp->threads[i], NULL)) != 0) {
            exit(1);
        }
    }
    pthread_cancel(tp->sigThread);
    if ((err = pthread_join(tp->sigThread, NULL)) != 0) {
        exit(1);
    }
    maskSignal(SIG_UNBLOCK, -1);
    tpoolImm = true;
    free(tp);
    tp = NULL;

}

/* Actor */

// Initializes global variable actorsCont, which is an actors struct. Returns 0 on success
bool initializeActorsCont(actorAction *first) {
    int err;
    actorsCont = malloc(sizeof(actArr));
    if (actorsCont == NULL) {
        return false;
    }
    actorsCont->size = 1000;
    actorsCont->end = 1;

    actorsCont->acts = malloc(sizeof(actorAction*) * 1000);
    if (actorsCont->acts == NULL) {
        free(actorsCont);
        return false;
    }

    if ((err = pthread_mutex_init(&(actorsCont->contMutex), NULL)) != 0) {
        exit(1);
    }

    actorsCont->alive = 1;
    first->id = 0;
    actorsCont->acts[0] = first;

    return true;
}

// Initializes an actorAction struct.
// Returns initialized actorAction on success and null in other cases.
actorAction* initializeAct(role_t *const r) {
    int err;
    actorAction* result = malloc(sizeof(actorAction));
    if (result == NULL) {
        return NULL;
    }

    result->roles = r;
    result->dead = false;
    result->sttptr = NULL;
    result->spw = true;
    result->mQ = initializeMQueue();
    if (result->mQ == NULL) {
        free(result);
        return NULL;
    }
    if ((err = pthread_mutex_init(&(result->aMutex), NULL)) != 0) {
        exit(1);
    }
    return result;
}

// Pushes an actorAction object to actorsCont struct.
// Reallocs more size if needed. Returns 0 on success.
int pushActorsCont(actorAction *toAdd) {
    int err;
    if ((err = pthread_mutex_lock(&(actorsCont->contMutex))) != 0) {
        exit(1);
    }
    if (CAST_LIMIT <= actorsCont->end)
    {
        if ((err = pthread_mutex_unlock(&(actorsCont->contMutex))) != 0) {
            exit(1);
        }
        return -1;
    }

    if (actorsCont->end == actorsCont->size) {
        actorAction **rollback = actorsCont->acts;
        actorsCont->acts = realloc(actorsCont->acts, sizeof(actorAction*) * 2 * actorsCont->size);
        if (actorsCont->acts == NULL) {
            actorsCont->acts = rollback;
            if ((err = pthread_mutex_unlock(&(actorsCont->contMutex))) != 0) {
                exit(1);
            }
            return -2;
        }
        else {
            actorsCont->size *= 2;
        }
    }

    toAdd->id = actorsCont->end;
    actorsCont->alive++;
    actorsCont->acts[actorsCont->end] = toAdd;
    actorsCont->end++;
    if ((err = pthread_mutex_unlock(&(actorsCont->contMutex))) != 0) {
        exit(1);
    }

    return 0;
}

// Destroys single actorAction element.
void destroyAct(actorAction* a) {
    if (a == NULL) {
        return;
    }
    actImm = true;
    int err;
    if (a->sttptr != NULL) free(a->sttptr);
    if ((err = pthread_mutex_destroy(&(a->aMutex))) != 0) {
        exit(1);
    }
    destroyMQueue(a->mQ);

    free(a);
    a = NULL;
    actImm = false;
}

// Destorys global struct actorsCont.
void destroyActorsCont() {
    if (actorsCont == NULL) {
        return;
    }
    int err;
    if ((err = pthread_mutex_destroy(&(actorsCont->contMutex))) != 0) {
        exit(1);
    }
    for (int i = 0; i < actorsCont->end; i++) {
        destroyAct(actorsCont->acts[i]);
    }
    free(actorsCont->acts);
    free(actorsCont);
    actorsCont = NULL;
}

/* Messages */

void msg_godie() {
    actorsCont->acts[actor_id_self()]->dead = true;
}

void msg_spawn(void *data) {
    actorAction *a = initializeAct((role_t* const)data);
    if (a == NULL) {
        return;
    }
    if (pushActorsCont(a) != 0) {
        destroyAct(a);
        return;
    }
    message_t toSend;
    actor_id_t* id = malloc(sizeof(actor_id_t));
    *id = actor_id_self();
    toSend.data = id;
    toSend.nbytes = sizeof(toSend.data);
    toSend.message_type = MSG_HELLO;

    send_message(actorsCont->end - 1, toSend);
}

actor_id_t actor_id_self() {
    return globalId;
}

int actor_system_create(actor_id_t *actor, role_t *const role) {
    if (actorsCont != NULL) {
        return -1;
    }
    int err;
    actorAction* first;
    if ((first = initializeAct(role)) == NULL) {
        return -1;
    }
    if (!initializeActorsCont(first)) {
        free(first);
        return -1;
    }
    first->spw = false;
    *actor = first->id;

    thrPool = createThreadPool();
    if (thrPool == NULL) {
        free(first);
        return -1;
    }
    message_t firstMess;
    actor_id_t id = -1;
    firstMess.data = &id;
    firstMess.nbytes = sizeof(firstMess.data);
    firstMess.message_type = MSG_HELLO;

    if ((err = pthread_mutex_init(&(globalMutex), NULL)) != 0) {
        exit(1);
    }
    if ((err = pthread_cond_init(&(globalCond), NULL)) != 0) {
        exit(1);
    }

    first = NULL;
    int result = send_message(0, firstMess);
    if (result != 0) {
        return -1;
    }
    return 0;
}

bool join = false;

void actor_system_join(actor_id_t actor) {
    UNUSED(actor);
    if (join) {
        return;
    }
    join = true;
    if (actorsCont == NULL) {
        join = false;
        return;
    }
    int err;

    if ((err = pthread_mutex_lock(&(globalMutex))) != 0) {
        exit(1);
    }
    while (actorsCont->alive > 0) {
        if ((err = pthread_cond_wait(&(globalCond), &(globalMutex))) != 0) {
            exit(1);
        }
    }
    destroyThreadPool(thrPool);
    destroyActorsCont();
    if ((err = pthread_mutex_unlock(&(globalMutex))) != 0) {
        exit(1);
    }
    if ((err = pthread_cond_destroy(&(globalCond))) != 0) {
        exit(1);
    }
    if ((err = pthread_mutex_destroy(&(globalMutex))) != 0) {
        exit(1);
    }
    join = false;
}

int send_message(actor_id_t actor, message_t message) {

    const role_t* r = actorsCont->acts[actor]->roles;
    if ((message.message_type < 0
        || (size_t)message.message_type >=
        r->nprompts)
        && message.message_type != MSG_GODIE && message.message_type != MSG_SPAWN) {
        return -3;
    }
    else if (actorsCont->end <= actor) {
        return -2;
    }
    else if (actorsCont->acts[actor]->dead) {
        return -1;
    }
    else {
        int err;
        if ((err = pthread_mutex_lock(&(actorsCont->acts[actor]->aMutex))) != 0) {
            exit(1);
        }
        message_t* toSend = malloc(sizeof(message_t));
        if (toSend == NULL) {
            if ((err = pthread_mutex_unlock(&(actorsCont->acts[actor]->aMutex))) != 0) {
                exit(1);
            }
            return -4;
        }
        toSend->nbytes = message.nbytes;
        toSend->message_type = message.message_type;
        toSend->data = message.data;
        bool c = actorsCont->acts[actor]->mQ->start == -1;
        if (pushMQueue(actorsCont->acts[actor]->mQ, toSend) != 0) {
            if ((err = pthread_mutex_unlock(&(actorsCont->acts[actor]->aMutex))) != 0) {
                exit(1);
            }
            free(toSend);
            return -6;
        }
        if (c) {
            if (addActionThreadPool(thrPool, actorsCont->acts[actor]) != 0) {
                if ((err = pthread_mutex_unlock(&(actorsCont->acts[actor]->aMutex))) != 0) {

                    exit(1);
                }
                free(toSend);
                return -5;
            }
        }
        if ((err = pthread_mutex_unlock(&(actorsCont->acts[actor]->aMutex))) != 0) {
            exit(1);
        }
        toSend = NULL;
        return 0;
    }
}