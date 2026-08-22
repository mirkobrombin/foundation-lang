package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"strings"
	"time"

	"github.com/mirkobrombin/go-foundation/v2/core/logger"
)

func main() {
	printConsole(entry("info"))
	printCLEF(entry("info"))
	printCLEF(entry("warn"))
}

func entry(level string) logger.Entry {
	return logger.Entry{
		Level: level,
		Time:  time.Unix(0, 0).UTC(),
		Msg:   "served",
		Fields: map[string]interface{}{
			"service": "catalog",
			"request": "r-42",
		},
	}
}

func printConsole(entry logger.Entry) {
	var output bytes.Buffer
	if err := logger.NewConsoleSink(&output).Log(entry); err != nil {
		panic(err)
	}
	printCanonical(output.String())
}

func printCLEF(entry logger.Entry) {
	var output bytes.Buffer
	if err := logger.NewCLEFSink(&output).Log(entry); err != nil {
		panic(err)
	}
	printCanonical(output.String())
}

func printCanonical(line string) {
	var value map[string]interface{}
	if err := json.Unmarshal([]byte(strings.TrimSpace(line)), &value); err != nil {
		panic(err)
	}
	encoded, err := json.Marshal(value)
	if err != nil {
		panic(err)
	}
	fmt.Println(string(encoded))
}
