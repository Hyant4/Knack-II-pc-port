/* SPDX-License-Identifier: GPL-2.0-or-later
 * Diagnostic: verify the Windows pthread rwlock primitive independently of LibAtrac9.
 */
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>

static pthread_rwlock_t gate;
static _Atomic int readers,writer,violation,lock_error,lock_operation;

static void *write_worker(void *opaque) {
    int iterations=*(int *)opaque;
    for (int i=0;i<iterations;++i) {
        int error=pthread_rwlock_wrlock(&gate);
        if (error) { atomic_store(&lock_error,error); atomic_store(&lock_operation,1); return NULL; }
        assert(!atomic_fetch_add(&writer,1));
        if (atomic_load(&readers)) atomic_store(&violation,1);
        sched_yield();
        atomic_fetch_sub(&writer,1);
        assert(!pthread_rwlock_unlock(&gate));
    }
    return NULL;
}

static void *read_worker(void *opaque) {
    int iterations=*(int *)opaque;
    for (int i=0;i<iterations;++i) {
        int error=pthread_rwlock_rdlock(&gate);
        if (error) { atomic_store(&lock_error,error); atomic_store(&lock_operation,2); return NULL; }
        if (atomic_load(&writer)) atomic_store(&violation,1);
        atomic_fetch_add(&readers,1);
        if (atomic_load(&writer)) atomic_store(&violation,1);
        sched_yield();
        atomic_fetch_sub(&readers,1);
        assert(!pthread_rwlock_unlock(&gate));
    }
    return NULL;
}

int main(void) {
    int iterations=10000; pthread_t threads[3];
    assert(!pthread_rwlock_init(&gate,NULL));
    assert(!pthread_create(&threads[0],NULL,write_worker,&iterations));
    assert(!pthread_create(&threads[1],NULL,read_worker,&iterations));
    assert(!pthread_create(&threads[2],NULL,read_worker,&iterations));
    for (unsigned i=0;i<3;++i) assert(!pthread_join(threads[i],NULL));
    if (atomic_load(&lock_error)) {
        fprintf(stderr,"pthread_rwlock_error=%d operation=%s\n",atomic_load(&lock_error),
                atomic_load(&lock_operation)==1 ? "write" : "read");
        return 2;
    }
    assert(!atomic_load(&violation));
    assert(!pthread_rwlock_destroy(&gate));
    puts("PASS: pthread rwlock direct exclusion");
    return 0;
}
