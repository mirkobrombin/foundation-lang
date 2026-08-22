package main

import (
	"fmt"
	"os"

	"github.com/mirkobrombin/go-foundation/v2/core/openapi"
)

type endpoint struct {
	_     struct{} `method:"GET" path:"/users/{id:int}"`
	ID    int64    `path:"id"`
	Role  string   `query:"role"`
	Trace string   `header:"X-Trace"`
}

func (*endpoint) OpenAPIMeta() map[string]any {
	return map[string]any{
		"summary":     "Find a user",
		"description": "Finds one user by the public identifier.",
		"responses": map[int]any{
			200: "User found",
			404: "User not found",
		},
	}
}

func main() {
	document, err := openapi.Build("test.compatibility.openapi", "1.2.3", &endpoint{})
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	fmt.Println(string(document))
}
