package main

import (
	"context"
	"fmt"
	"os"
	"sync/atomic"
	"time"

	"github.com/mirkobrombin/go-foundation/v2/core/scheduler"
)

func waitFor(signal <-chan struct{}) {
	select {
	case <-signal:
	case <-time.After(3 * time.Second):
		panic("scheduler compatibility signal timed out")
	}
}

func main() {
	storeDirectory := os.Getenv("FOUNDATION_SCHEDULER_STORE")
	if storeDirectory == "" {
		panic("scheduler compatibility store path is missing")
	}
	invalid := scheduler.New()
	if err := invalid.Register(scheduler.Job{
		Name: "invalid",
		Cron: "60 * * * *",
		Handler: func(context.Context) error {
			return nil
		},
	}); err == nil {
		panic("invalid cron was accepted")
	}
	fmt.Println("invalid cron rejected")

	cronSignal := make(chan struct{}, 1)
	active := scheduler.New()
	job := scheduler.Job{
		Name: "heartbeat",
		Cron: "* * * * *",
		Handler: func(context.Context) error {
			cronSignal <- struct{}{}
			return nil
		},
	}
	if err := active.Register(job); err != nil {
		panic(err)
	}
	if err := active.Register(job); err == nil {
		panic("duplicate job was accepted")
	}
	fmt.Println("duplicate rejected")

	if err := active.Start(context.Background()); err != nil {
		panic(err)
	}
	if err := active.Start(context.Background()); err == nil {
		panic("running scheduler started twice")
	}
	fmt.Println("already started rejected")

	immediateSignal := make(chan struct{}, 1)
	active.Enqueue(func(context.Context) error {
		immediateSignal <- struct{}{}
		return nil
	})
	delayedSignal := make(chan struct{}, 1)
	active.ScheduleAfter(10*time.Millisecond, func(context.Context) error {
		delayedSignal <- struct{}{}
		return nil
	})

	waitFor(immediateSignal)
	fmt.Println("enqueued")
	waitFor(delayedSignal)
	fmt.Println("delayed")
	waitFor(cronSignal)
	fmt.Println("cron")

	var cancelledRan atomic.Bool
	active.ScheduleAfter(time.Hour, func(context.Context) error {
		cancelledRan.Store(true)
		return nil
	})
	stopContext, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	if err := active.Stop(stopContext); err != nil {
		panic(err)
	}
	if cancelledRan.Load() {
		panic("cancelled delayed task ran")
	}
	fmt.Println("cancelled delay")

	store, err := scheduler.NewJobStore(storeDirectory)
	if err != nil {
		panic(err)
	}
	record := &scheduler.JobRecord{
		Name:        "stored",
		Cron:        "* * * * *",
		LastRun:     time.Unix(1768446000, 0).UTC(),
		LastStatus:  "ok \"quoted\" \\ value",
		LastLatency: "42ms",
	}
	if err := store.Save(record); err != nil {
		panic(err)
	}
	loaded, err := store.Load("stored")
	if err != nil || loaded.Name != record.Name || loaded.Cron != record.Cron ||
		!loaded.LastRun.Equal(record.LastRun) || loaded.LastStatus != record.LastStatus ||
		loaded.LastLatency != record.LastLatency {
		panic("scheduler store round trip changed")
	}
	fmt.Println("store round trip")
	records, err := store.List()
	if err != nil || len(records) != 1 || records[0].Name != "stored" {
		panic("scheduler store list changed")
	}
	fmt.Println("store list: 1")
	if _, err := store.Load("../escape"); err == nil {
		panic("invalid scheduler store name was accepted")
	}
	fmt.Println("invalid store name rejected")

	persistedSignal := make(chan struct{}, 1)
	persisted := scheduler.New(scheduler.WithStore(storeDirectory))
	if err := persisted.Register(scheduler.Job{
		Name: "heartbeat",
		Cron: "* * * * *",
		Handler: func(context.Context) error {
			persistedSignal <- struct{}{}
			return nil
		},
	}); err != nil {
		panic(err)
	}
	if err := persisted.Start(context.Background()); err != nil {
		panic(err)
	}
	waitFor(persistedSignal)
	persistedStop, persistedCancel := context.WithTimeout(context.Background(), 3*time.Second)
	if err := persisted.Stop(persistedStop); err != nil {
		persistedCancel()
		panic(err)
	}
	persistedCancel()
	persistedRecord, err := store.Load("heartbeat")
	if err != nil || persistedRecord.Name != "heartbeat" || persistedRecord.Cron != "* * * * *" ||
		persistedRecord.LastRun.IsZero() || persistedRecord.LastStatus != "ok" {
		panic("scheduler did not persist execution state")
	}
	fmt.Println("scheduler persisted state")

	persistedRecord.LastRun = time.Now().Add(time.Hour).UTC()
	persistedRecord.LastLatency = "0s"
	if err := store.Save(persistedRecord); err != nil {
		panic(err)
	}
	secondSignal := make(chan struct{}, 1)
	restored := scheduler.New(scheduler.WithStore(storeDirectory))
	if err := restored.Register(scheduler.Job{
		Name: "heartbeat",
		Cron: "* * * * *",
		Handler: func(context.Context) error {
			secondSignal <- struct{}{}
			return nil
		},
	}); err != nil {
		panic(err)
	}
	if err := restored.Start(context.Background()); err != nil {
		panic(err)
	}
	select {
	case <-secondSignal:
		panic("restored future state ran early")
	case <-time.After(1100 * time.Millisecond):
	}
	restoredStop, restoredCancel := context.WithTimeout(context.Background(), 3*time.Second)
	if err := restored.Stop(restoredStop); err != nil {
		restoredCancel()
		panic(err)
	}
	restoredCancel()
	fmt.Println("scheduler restored state")
	fmt.Println("scheduler compatibility ok")
}
