package main

import (
    "fmt"
    "os"
    "say_hello"
)

func main() {
    s := "Привет Hello"
    
    if len( os.Args ) > 1 {
	switch os.Args[ 1 ] {
	case "en", "En": s = say_hello.SayEn()
	case "ru", "Ru": s = say_hello.SayRu()
	}
    }
    fmt.Println( s )
}