package main

import (
	"fmt"
	"strings"

	"github.com/mirkobrombin/go-foundation/v2/core/guard"
)

type document struct {
	Title   string            `guard:"role:admin; can:read,write"`
	Content string            `guard:"role:editor; can:edit"`
	Public  string            `guard:"role:*; can:view"`
	Members map[string]string `guard:"role:*; can:manage"`
}

type user struct {
	id   string
	role string
}

func (u user) GetID() string      { return u.id }
func (u user) GetRoles() []string { return []string{u.role} }

func main() {
	engine := guard.NewGuard()
	resource := &document{Public: "guest", Members: map[string]string{"3": "owner"}}
	admin := user{id: "1", role: "admin"}
	editor := user{id: "2", role: "editor"}
	owner := user{id: "3", role: "owner"}
	stranger := user{id: "4", role: "guest"}

	report("admin-read", engine.Can(admin, resource, "read"))
	report("admin-edit", engine.Can(admin, resource, "edit"))
	report("editor-edit", engine.Can(editor, resource, "edit"))
	report("guest-view", engine.Can(stranger, resource, "view"))
	report("owner-manage", engine.Can(owner, resource, "manage"))
	report("stranger-manage", engine.Can(stranger, resource, "manage"))
	report("unknown", engine.Can(admin, resource, "unknown"))

	roles, err := engine.GetRoles(owner, resource)
	if err != nil {
		panic(err)
	}
	fmt.Printf("owner-roles\t%t,%t,%t\n", has(roles, "owner"), has(roles, "admin"), has(roles, "guest"))
}

func report(name string, err error) {
	status := "allowed"
	if err != nil {
		status = "denied"
		if strings.HasPrefix(err.Error(), "no policy defined") {
			status = "no-policy"
		}
	}
	fmt.Printf("%s\t%s\n", name, status)
}

func has(roles []string, expected string) bool {
	for _, role := range roles {
		if role == expected {
			return true
		}
	}
	return false
}
