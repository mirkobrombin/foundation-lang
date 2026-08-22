package main

import (
	"context"
	"fmt"
	"time"

	"github.com/mirkobrombin/go-foundation/v2/core/worker"
)

func parallelJob(id int, started chan<- int, release <-chan struct{}, completed chan<- int) worker.Task {
	return func(context.Context) error {
		started <- id
		<-release
		completed <- id
		return nil
	}
}

func main() {
	started := make(chan int, 3)
	completed := make(chan int, 3)
	releases := []chan struct{}{make(chan struct{}), make(chan struct{}), make(chan struct{})}
	pool := worker.NewPool(2)
	if !pool.Submit(parallelJob(1, started, releases[0], completed)) {
		panic("first task was rejected")
	}
	if !pool.Submit(parallelJob(2, started, releases[1], completed)) {
		panic("second task was rejected")
	}
	thirdAccepted := make(chan bool, 1)
	go func() {
		thirdAccepted <- pool.Submit(parallelJob(3, started, releases[2], completed))
	}()

	first := <-started
	second := <-started
	if first+second != 3 {
		panic("worker limit changed")
	}
	select {
	case <-started:
		panic("third task started before a worker was released")
	case <-time.After(time.Millisecond):
	}
	fmt.Println("parallel limit: 2")

	close(releases[first-1])
	if third := <-started; third != 3 {
		panic("third task order changed")
	}
	if !<-thirdAccepted {
		panic("third task was rejected")
	}
	close(releases[second-1])
	close(releases[2])
	pool.Shutdown()
	if total := <-completed + <-completed + <-completed; total != 6 {
		panic("completed work changed")
	}
	fmt.Println("completed: 3")
	fmt.Println("shutdown drained")
	if pool.Submit(func(context.Context) error { return nil }) {
		panic("closed pool accepted work")
	}

	cancelled := worker.NewPool(1)
	ready := make(chan struct{}, 1)
	if !cancelled.Submit(func(ctx context.Context) error {
		ready <- struct{}{}
		<-ctx.Done()
		fmt.Println("cancelled running task")
		return ctx.Err()
	}) {
		panic("cancellation task was rejected")
	}
	<-ready
	cancelled.Shutdown()
	fmt.Println("cancellation joined")
	fmt.Println("worker compatibility ok")
}
