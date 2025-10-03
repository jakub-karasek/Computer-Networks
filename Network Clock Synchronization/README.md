Here’s the English translation of your text:

---

### Asynchronous Executor in C

#### Motivation

Modern computer systems often need to handle many tasks simultaneously: serving numerous network connections, processing large amounts of data, or communicating with external devices. The traditional multithreading approach can lead to significant overhead in thread management, as well as scalability issues in resource-constrained systems.

Asynchronous executors provide a modern solution to this problem. Instead of running each task in a separate thread, the executor manages tasks cooperatively – tasks "yield" the processor to each other when they are waiting for resources, e.g., file data, device signals, or a database response. Thanks to this, it is possible to handle thousands or even millions of concurrent operations using a limited number of threads.

#### Example Use Cases

* **HTTP servers.** Servers such as Nginx or Tokio in Rust use asynchronous executors to handle thousands of network connections while minimizing CPU and memory usage.
* **Embedded systems programming.** In resource-constrained systems (e.g., microcontrollers), efficient management of multiple tasks such as sensor reading or device control is crucial. Asynchronicity helps avoid costly multithreading.
* **Interactive applications.** In graphical or mobile applications, asynchronicity ensures smooth performance – for example, when downloading data from the network, the user interface remains responsive.
* **Data processing.** An asynchronous approach allows efficient management of many simultaneous I/O operations, e.g., in data analysis systems where disk operations may be a bottleneck.

#### Assignment Goal

In this homework, you will implement a simplified single-threaded asynchronous executor in C (inspired by the Tokio library in Rust). In this assignment, no threads are created: both the executor and the tasks it executes run entirely in the main thread (or yield when, e.g., waiting for data to be read).

You will be given an extensive project skeleton and will have to implement key elements. The result will be a simple but functional asynchronous library. Example usages with descriptions can be found in the source files under `tests/` (which also serve as basic example tests).

A key role in this task is played by the **epoll** mechanism, which allows waiting on a selected set of events. This mainly concerns monitoring the readiness of file descriptors (e.g., pipes or sockets) for reading and writing. See:

* *epoll in 3 easy steps*
* `man epoll`, specifically we will only use:

  * `epoll_create1(0)` (i.e., `epoll_create` without suggested size, no special flags);
  * `epoll_ctl(...)` only on pipe/socket descriptors, only for EPOLLIN/EPOLLOUT events (read/write readiness), without special flags (default level-triggered mode, i.e., no EPOLLET flag, not edge-triggered);
  * `epoll_wait(...);`
  * `close()` to close the created `epoll_fd`.

#### Specification

**Structures**
The most important structures needed for the implementation:

* **Executor**: responsible for running tasks on the processor – implements the main event loop `executor_run()` and contains the task queue (in general, it could contain multiple queues and threads for parallel processing).
* **Mio**: responsible for communicating with the operating system – implements waiting for resource availability (e.g., readiness for reading) using `epoll_*`.
* **Future**: in this assignment, it is not only a placeholder for a value to wait for. A Future also contains a coroutine, i.e.:

  * what task should be performed (as a pointer to the `progress` function),
  * state that needs to be preserved between execution steps.
    Since C has no inheritance, instead of subclassing Future we will use `FutureFoo` structures containing `Future base` as the first field, and cast `Future*` to `FutureFoo*`.
* **Waker**: a callback (here: a function pointer with arguments) defining how to notify the executor that a task can proceed further (e.g., a task was waiting for data to be read, and the data is now available).

The homework provides a skeleton implementation (header files and some structures), as well as example futures. You will need to implement the executor, Mio, and additional futures (details below).

---

**Task State Machine**
A task (Future) can be in one of four states:

* **COMPLETED**: task finished successfully and has a result.
* **FAILURE**: task failed or was interrupted.
* **PENDING (queued)**: ready to make progress, i.e., is being executed by the executor or is waiting in the queue.
* **PENDING (waker holds it)**: task could not proceed in the executor, and someone (e.g., Mio or a helper thread) holds its Waker and will call it later when progress can continue.

State diagram:

```
executor_spawn                     COMPLETED / FAILURE
     │                                       ▲
     ▼               executor calls          │
   PENDING  ───► fut->progress(fut, waker) ──+
(queued)                                     │
     ▲                                       │
     │                                       ▼
     └─── someone (e.g. mio_poll) calls ◄── PENDING
              waker_wake(waker)       (waker holds it)
```

---

**Execution Scheme**

* The program creates an executor (which creates its own Mio instance).
* Tasks are spawned in the executor (`executor_spawn`).
* `executor_run` is called, which executes tasks, which may spawn subtasks, until the program finishes.
* The executor does not create threads: `executor_run()` works in the calling thread.

Executor loop:

1. If no pending tasks remain, exit the loop.
2. If no active tasks, call `mio_poll()` to block until something changes.
3. For each active future, call `future.progress(future, waker)` (creating a Waker that re-queues the task if necessary).

---

**Future `progress()` behavior**

* Attempts to make progress in the task (e.g., read more bytes from a socket, or perform a computation step).
* On error → return **FAILURE**.
* On success → return **COMPLETED**.
* Otherwise → return **PENDING**, ensuring that someone (Mio, another thread, or itself) will call its waker later.

---

**Mio responsibilities**

* `mio_register`: register that a given waker should be called when an event occurs (e.g., descriptor ready for read/write). Mio must ensure the waker is called only when the resource is ready for non-blocking read/write.
* `mio_poll`: put the executor thread to sleep until at least one registered event occurs, then call the corresponding wakers.

---

**Possible actions in `future.progress()`**

* Call `mio_register(..., waker)` → to return PENDING and be called later when ready.
* Call `waker_wake(waker)` → to re-queue itself immediately, yielding to other tasks first.
* Call `other_future->progress(other_future, waker)` → to attempt running a subtask inline.
* Call `executor_spawn` → to delegate an independent subtask to the executor.
* (Not in this assignment) use `pthread_create` for parallel background work.

Examples of futures can be found in `future_examples.h/c` and `tests/hard_work_test.c`.

---

#### Notes on Waker

* Waker always calls the same function (re-queueing in the executor), since we use only one executor implementation.
* Therefore, Waker does not store a function pointer, but only arguments for `waker_wake()`: `Executor*` and `Future*`.
* Important: in `future1.progress(waker)`, `waker.future` may differ from `future1` – it may be a parent task that calls `future1` as a subtask.
* In Mio, you may assume `waker.executor` is the same executor that was passed to `mio_create()` (so in `mio_register` you only need to remember the `Future*`, not allocate a full Waker).

---

#### Futures to Implement: Combinators

The final part of the homework is to implement three combinators – Futures that combine other Futures:

* **ThenFuture**: runs `fut1`, then `fut2`, passing the result of `fut1` as the input argument to `fut2`.
* **JoinFuture**: runs `fut1` and `fut2` concurrently, completing when both have finished.
* **SelectFuture**: runs `fut1` and `fut2` concurrently, completing when either one finishes successfully. If `fut1` finishes (COMPLETED), `fut2` is abandoned (its `progress()` is no longer called).

Detailed interface and behavior (including error handling) are specified in `future_combinators.h`.

---

#### Formal Requirements

**Provided files (do not modify unless specified):**

* `executor.h` – asynchronous executor,
* `future.h` – asynchronous computation/task (coroutine),
* `waker.h` – mechanism for waking async tasks,
* `mio.h` – abstraction layer over `epoll`,
* `future_combinators.h` – logical combinators of async computations.

**Files to implement:**

* `executor.c`, `mio.c`, `future_combinators.c`.

**Additional headers provided:**

* `err.h` – small error-handling library,
* `debug.h` – debug macro enabled by `DEBUG_PRINTS`.

**Other files (`future_examples.{h,c}`, `tests/`)** serve testing and demonstration purposes.

---

#### Grading

* Three components to implement: `executor.c`, `mio.c`, `future_combinators.c` (largest).
* Tested individually (unit tests) and together (integration tests).
* Points awarded for both individual correctness and overall system behavior.

---

#### Guarantees

* No threads will be created; everything happens in the main thread.
* Each `executor_create` is matched with exactly one `executor_run` and `executor_destroy`.
* `executor_destroy` is always called after the corresponding `executor_run`.
* `executor_spawn` may be called both outside and during `executor_run`. Both must work.
* The number of pending Futures will not exceed `max_queue_size` given to `executor_create`.

---

#### Constraints

* Solution must be submitted as a ZIP archive `ab12345.zip`, containing folder `ab12345` (username + student ID).
* Only `executor.c`, `mio.c`, and `future_combinators.c` will be graded. New/modified files will be ignored.
* No busy-waiting or semi-busy-waiting is allowed.
* Do not use sleep/usleep/nanosleep or timeouts (e.g., in `epoll_wait`).
* Code will be tested for memory/resource leaks.
* Implementations must be reasonably efficient.
* Must use C (gnu11 or C11).
* Allowed: standard C library, system functions (`unistd.h`, etc.).
* Forbidden: external libraries.
* You may reuse lab code; other borrowed code must be credited in comments.

