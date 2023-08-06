#include "uthreads.h"
#include <list>
#include <map>
#include <set>
#include <iostream>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <algorithm>

#define INIT 0
#define BLOCK 1
#define SLEEP 2
#define TERMINATE 3
#define MICROSECONDS_REFACTOR 1000000


#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
            : "=g" (ret)
            : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 4
#define JB_PC 5


/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
                 "rol    $0x9,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}


#endif

class Thread {

public:
    int remaining_sleeping_time;
    int current_quantum_usec;
    int tid;
    char* stack;
    sigjmp_buf env;

    Thread() = default;

    Thread (int tid, thread_entry_point entry_point) {
        this->tid = tid;
        this->current_quantum_usec = 0;
        this->remaining_sleeping_time = 0;
        this->stack = new char[STACK_SIZE];
        address_t sp = (address_t) stack + STACK_SIZE - sizeof(address_t);
        address_t pc = (address_t) entry_point;
        sigsetjmp(env, 1);
        (env->__jmpbuf)[JB_SP] = translate_address(sp);
        (env->__jmpbuf)[JB_PC] = translate_address(pc);
        sigemptyset(&env->__saved_mask);
    }
};

Thread* running_thread;
int passed_quantum_usec; // How many quantums passed since the library was initialized
int quantum_value_usecs; // length of quantum in microseconds
std::list<Thread*> ready_threads;
std::set<Thread*> sleeping_threads;
std::map<int, Thread*> tid_to_threads;
std::set<int> available_threads;
std::set<int> blocked_threads;
struct itimerval timer;
struct sigaction sa;
sigset_t sig_set;


void round_robin_handler(int);


void reduce_sleeping_time();

int get_min_id_available();

void handle_switch_threads();

void terminate_thread(int);

void delete_threads() {
    for (auto curr : tid_to_threads) {
        delete curr.second->stack;
        delete curr.second;
    }
}


void handle_block_unblock(int action) {
    if (sigprocmask(action, &sig_set, nullptr) == -1) {
        std::cerr << "system error: sigprocmask has failed\n";
        delete_threads();
        exit(1);
    }
}


void initialize_available_set() {
    for(int i = 1; i < MAX_THREAD_NUM; i++) {
        available_threads.insert(i);
    }
}

void set_timer() {
    sa.sa_handler = &round_robin_handler;
    if (sigaction(SIGVTALRM, &sa, NULL) == -1) {
        std::cerr << "system error: sigaction has failed\n";
        uthread_terminate(0);
        exit(1);
    }
    timer.it_interval.tv_sec = quantum_value_usecs / MICROSECONDS_REFACTOR;
    timer.it_interval.tv_usec = quantum_value_usecs % MICROSECONDS_REFACTOR;
    timer.it_value.tv_sec = quantum_value_usecs / MICROSECONDS_REFACTOR;
    timer.it_value.tv_usec = quantum_value_usecs % MICROSECONDS_REFACTOR;
    if (setitimer(ITIMER_VIRTUAL, &timer, NULL)) {
        std::cerr << "system error: setitimer has failed\n";
        uthread_terminate(0);
        exit(1);
    }
}

int get_min_id_available() {
    if (available_threads.empty())
        return -1;
    int min_id = MAX_THREAD_NUM;
    for (int id : available_threads) {
        if (id < min_id)
            min_id = id;
    }
    return min_id;
}

int uthread_init(int quantum_usecs) {
    if (quantum_usecs <= 0) {
        std::cerr << "thread library error: quantum usecs must have a positive value\n";
        return -1;
    }
    if (sigemptyset(&sig_set) == -1) {
        std::cerr << "system error: sigemptyset has failed\n";
        delete_threads();
        exit(1);
    }
    if (sigaddset(&sig_set, SIGVTALRM) == -1) {
        std::cerr << "system error: sigaddset has failed\n";
        delete_threads();
        exit(1);
    }
    passed_quantum_usec = 0;
    quantum_value_usecs = quantum_usecs;
    initialize_available_set();
    available_threads.erase(0);
    auto t = new Thread();
    tid_to_threads[0] = t;
    running_thread = t;
    round_robin_handler(INIT);
    set_timer();
    return 0;
}

int uthread_spawn(thread_entry_point entry_point) {
    handle_block_unblock(SIG_BLOCK);
    if (entry_point == nullptr) {
        std::cerr << "thread library error: entry_point cannot be null\n";
        handle_block_unblock(SIG_UNBLOCK);
        return -1;
    }
    int id = get_min_id_available();
    if (id == -1) {
        std::cerr << "thread library error: there aren't available threads\n";
        handle_block_unblock(SIG_UNBLOCK);
        return -1;
    }
    try {
        auto new_thread = new Thread(id, entry_point);
        ready_threads.push_back(new_thread);
        tid_to_threads[id] = new_thread;
        available_threads.erase(id);
    }
    catch (std::bad_alloc&) {
        std::cerr << "system error: thread couldn't be created\n";
        uthread_terminate(0);
        handle_block_unblock(SIG_UNBLOCK);
        exit(1);
    }
    handle_block_unblock(SIG_UNBLOCK);
    return id;
}


void round_robin_handler(int action) {
    int res = sigsetjmp(running_thread->env, 1);
    if (res == 0) {
        if (action == SLEEP) {
            sleeping_threads.insert(running_thread);
            handle_switch_threads();
        }
        else if (action == BLOCK) {
            handle_switch_threads();
        }
        else if (action == TERMINATE) {
            delete running_thread->stack;
            delete running_thread;
            handle_switch_threads();
        }
        else {
            if (!ready_threads.empty()) {
                ready_threads.push_back(running_thread);
                handle_switch_threads();
            }
            else {
                running_thread->current_quantum_usec++;
                passed_quantum_usec++;
                reduce_sleeping_time();
            }
        }
    }
}

void reduce_sleeping_time() {
    if (sleeping_threads.empty())
        return;
    auto thread = sleeping_threads.begin();
    while (thread != sleeping_threads.end()) {
        (*thread)->remaining_sleeping_time--;
        if ((*thread)->remaining_sleeping_time == 0) {
            auto temp = (*thread);
            thread++;
            if(blocked_threads.find(temp->tid) == blocked_threads.end())
                ready_threads.push_back(temp);
            sleeping_threads.erase(temp);
        }
        else {
            thread++;
        }
    }
}

int uthread_sleep(int num_quantums) {
    handle_block_unblock(SIG_BLOCK);
    if (num_quantums <= 0) {
        handle_block_unblock(SIG_UNBLOCK);
        return -1;
    }
    if (running_thread->tid == 0) {
        std::cerr << "thread library error: cannot block main thread\n";
        handle_block_unblock(SIG_UNBLOCK);
        return -1;
    }
    running_thread->remaining_sleeping_time = num_quantums;
    round_robin_handler(SLEEP);
    handle_block_unblock(SIG_UNBLOCK);
    return 0;
}

int uthread_terminate(int tid) {
    handle_block_unblock(SIG_BLOCK);
    if (tid < 0 || tid >= MAX_THREAD_NUM) {
        std::cerr << "thread library error: tid is not in the valid range\n";
        handle_block_unblock(SIG_UNBLOCK);
        return -1;
    }
    if (tid_to_threads.find(tid) == tid_to_threads.end()) {
        std::cerr << "thread library error: there isn't a thread with this tid\n";
        handle_block_unblock(SIG_UNBLOCK);
        return -1;
    }
    if (tid == 0) {
        delete_threads();
        handle_block_unblock(SIG_UNBLOCK);
        exit(0);
    }
    if (running_thread->tid != tid) {
        terminate_thread(tid);
    }
    else {
        available_threads.insert(tid);
        tid_to_threads.erase(tid);
        round_robin_handler(TERMINATE);
    }
    handle_block_unblock(SIG_UNBLOCK);
    return 0;
}


void terminate_thread(int tid) {
    // A function that gets a tid of a thread (not the running thread) and terminates it.
    auto thread = tid_to_threads[tid];
    available_threads.insert(tid);
    blocked_threads.erase(tid);
    tid_to_threads.erase(tid);
    sleeping_threads.erase(thread);
    ready_threads.remove(thread);
    delete thread->stack;
    delete thread;
}

void handle_switch_threads() {
    running_thread = ready_threads.front();
    running_thread->current_quantum_usec++;
    passed_quantum_usec++;
    ready_threads.pop_front();
    reduce_sleeping_time();
    set_timer();
    siglongjmp(running_thread->env, 1);
}


int uthread_block(int tid) {
    handle_block_unblock(SIG_BLOCK);
    if (tid < 0 || tid >= MAX_THREAD_NUM) {
        std::cerr << "thread library error: tid is not in the valid range\n";
        handle_block_unblock(SIG_UNBLOCK);
        return -1;
    }
    if (tid == 0) {
        std::cerr << "thread library error: cannot block main thread\n";
        handle_block_unblock(SIG_UNBLOCK);
        return -1;
    }
    if (tid_to_threads.find(tid) == tid_to_threads.end()) {
        std::cerr << "thread library error: there isn't a thread with this tid\n";
        handle_block_unblock(SIG_UNBLOCK);
        return -1;
    }
    if (running_thread->tid != tid) {
        blocked_threads.insert(tid);
        ready_threads.remove(tid_to_threads[tid]);
    }
    else {
        blocked_threads.insert(tid);
        round_robin_handler(BLOCK);
    }
    handle_block_unblock(SIG_UNBLOCK);
    return 0;
}


int uthread_resume(int tid) {
    handle_block_unblock(SIG_BLOCK);
    if (tid < 0 || tid >= MAX_THREAD_NUM) {
        std::cerr << "thread library error: tid is not in the valid range\n";
        handle_block_unblock(SIG_UNBLOCK);
        return -1;
    }
    if (tid_to_threads.find(tid) == tid_to_threads.end()) {
        std::cerr << "thread library error: there isn't a thread with this tid\n";
        handle_block_unblock(SIG_UNBLOCK);
        return -1;
    }
    blocked_threads.erase(tid);
    if(sleeping_threads.find(tid_to_threads[tid]) == sleeping_threads.end() &&
       std::find(ready_threads.begin(), ready_threads.end(),
                 tid_to_threads.at(tid)) == ready_threads.end()) {
        ready_threads.push_back(tid_to_threads[tid]);
    }
    handle_block_unblock(SIG_UNBLOCK);
    return 0;
}


int uthread_get_tid() {
    return running_thread->tid;
}


int uthread_get_total_quantums() {
    return passed_quantum_usec;
}

int uthread_get_quantums(int tid) {
    if (tid < 0 || tid >= MAX_THREAD_NUM) {
        std::cerr << "thread library error: tid is not in the valid range\n";
        return -1;
    }
    if (available_threads.find(tid) != available_threads.end()) {
        std::cerr << "thread library error: the thread with the current tid doesn't exist\n";
        return -1;
    }
    return tid_to_threads[tid]->current_quantum_usec;
}