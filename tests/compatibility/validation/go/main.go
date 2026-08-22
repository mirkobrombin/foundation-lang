package main

import (
	"bufio"
	"encoding/csv"
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"

	"github.com/mirkobrombin/go-foundation/v2/core/validation"
)

type profile struct {
	Name  string  `validate:"required"`
	Age   int     `validate:"min=18,max=99"`
	Email string  `validate:"email"`
	Code  string  `validate:"pattern=^[A-Z][a-z]+$"`
	Score float64 `validate:"min=-10.5,max=10.5"`
}

func main() {
	if len(os.Args) != 2 {
		os.Exit(1)
	}
	input, err := os.Open(os.Args[1])
	if err != nil {
		panic(err)
	}
	defer input.Close()

	reader := csv.NewReader(input)
	reader.Comma = '\t'
	reader.FieldsPerRecord = 6
	output := bufio.NewWriter(os.Stdout)
	defer output.Flush()
	validator := validation.New()

	for {
		record, err := reader.Read()
		if err == io.EOF {
			return
		}
		if err != nil {
			panic(err)
		}
		age, err := strconv.Atoi(record[2])
		if err != nil {
			panic(err)
		}
		score, err := strconv.ParseFloat(record[5], 64)
		if err != nil {
			panic(err)
		}
		errors := validator.Validate(profile{
			Name:  record[1],
			Age:   age,
			Email: record[3],
			Code:  record[4],
			Score: score,
		})
		fmt.Fprintf(output, "%s\t", record[0])
		if len(errors) == 0 {
			fmt.Fprintln(output, "ok")
			continue
		}
		for index, validationError := range errors {
			if index != 0 {
				fmt.Fprint(output, ",")
			}
			fmt.Fprintf(output, "%s:%s", validationError.Field, kind(validationError.Message))
		}
		fmt.Fprintln(output)
	}
}

func kind(message string) string {
	switch {
	case message == "required":
		return "Required"
	case strings.HasPrefix(message, "min "):
		return "Min"
	case strings.HasPrefix(message, "max "):
		return "Max"
	case message == "invalid email":
		return "Email"
	case strings.HasPrefix(message, "pattern "):
		return "Pattern"
	default:
		panic("unexpected validation error: " + message)
	}
}
