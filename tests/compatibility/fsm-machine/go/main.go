package main

import (
	"errors"
	"fmt"

	"github.com/mirkobrombin/go-foundation/v2/core/fsm/machine"
)

type order struct {
	State string `fsm:"initial:draft; draft->confirmed"`
	allow bool
}

type hookFlow struct {
	State string `fsm:"initial:idle; idle->active; active->done; *->cancelled"`
}

func (h *hookFlow) OnExitIdle() {
	fmt.Println("hook exit idle")
}

func (h *hookFlow) OnEnterActive() {
	fmt.Println("hook enter active")
}

func (h *hookFlow) OnExitActive() {
	fmt.Println("hook exit active")
}

func (h *hookFlow) OnEnterDone() {
	fmt.Println("hook enter done")
}

func (h *hookFlow) OnExitDone() {
	fmt.Println("hook exit done")
}

func (h *hookFlow) OnEnterCancelled() {
	fmt.Println("hook enter cancelled")
}

func (h *hookFlow) OnExitCancelled() {
	fmt.Println("hook exit cancelled")
}

func (o *order) CanConfirmed() error {
	if !o.allow {
		return errors.New("confirmation rejected")
	}
	return nil
}

func (o *order) OnExitDraft() {
	fmt.Println("effect exit")
}

func (o *order) OnEnterConfirmed() {
	fmt.Println("effect enter")
}

func eventName(kind machine.EventType) string {
	switch kind {
	case machine.BeforeTransition:
		return "before"
	case machine.ExitState:
		return "exit"
	case machine.EnterState:
		return "enter"
	case machine.AfterTransition:
		return "after"
	default:
		panic("unknown event type")
	}
}

func main() {
	blocked, err := machine.New(&order{})
	if err != nil {
		panic(err)
	}
	fmt.Printf("guarded:%t\n", blocked.Transition("confirmed") != nil)

	value := &order{allow: true}
	runtime, err := machine.New(value)
	if err != nil {
		panic(err)
	}
	runtime.Subscribe(func(event machine.Event) {
		fmt.Printf("%s:%s->%s\n", eventName(event.Type), event.From, event.To)
	})

	fmt.Println("initial:" + runtime.CurrentState())
	if err := runtime.Transition("confirmed"); err != nil {
		panic(err)
	}
	fmt.Println("current:" + runtime.CurrentState())
	history := runtime.History()
	fmt.Printf("history:%s->%s:%s\n", history[0].From, history[0].To, history[0].Trigger)
	fmt.Printf("invalid:%t\n", runtime.Transition("confirmed") != nil)

	wildcard, err := machine.New(&hookFlow{})
	if err != nil {
		panic(err)
	}
	fmt.Println("wildcard-initial:" + wildcard.CurrentState())
	if err := wildcard.Transition("active"); err != nil {
		panic(err)
	}
	if err := wildcard.Transition("done"); err != nil {
		panic(err)
	}
	if err := wildcard.Transition("cancelled"); err != nil {
		panic(err)
	}
	fmt.Println("wildcard-current:" + wildcard.CurrentState())
	if err := wildcard.Transition("cancelled"); err != nil {
		panic(err)
	}
	fmt.Println("wildcard-self:" + wildcard.CurrentState())
}
