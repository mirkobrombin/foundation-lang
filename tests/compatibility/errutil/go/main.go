package main

import (
	"errors"
	"fmt"

	"github.com/mirkobrombin/go-foundation/v2/core/errutil"
)

func main() {
	var aggregate errutil.MultiError
	fmt.Printf("empty:%t:%s\n", !aggregate.HasErrors(), aggregate.Error())
	aggregate.Append(errors.New("first"), nil, errors.New("second"))
	fmt.Printf("many:%d:%t:%s\n", len(aggregate.Errors), aggregate.HasErrors(), aggregate.Error())
	fmt.Printf("unwrap:%s:%s\n", aggregate.Unwrap()[0], aggregate.Unwrap()[1])
	fmt.Printf("join:%t\n", errutil.JoinErrors(errors.New("first"), errors.New("second")) != nil)
	fmt.Printf("join-empty:%t\n", errutil.JoinErrors(nil, nil) == nil)
	fmt.Printf("coded:%s\n", errutil.WithCode("E42", errors.New("second")))
	var empty errutil.MultiError
	fmt.Printf("finish:%t\n", empty.ErrorOrNil() == nil)
}
