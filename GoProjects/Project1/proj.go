package main

import (
	"fmt"
	"os"
	"say_hi"
)

func main() {
	s := "Привет Hello"

	if len(os.Args) > 1 {
		switch os.Args[1] {
		case "en", "En":
			s = say_hi.SayEn()
		case "ru", "Ru":
			s = say_hi.SayRu()
		}
	}
	fmt.Println(s)
}
