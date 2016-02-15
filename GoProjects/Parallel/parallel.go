package main

import (
  "fmt"
)

type Worker interface {
  Work()
}

type CurrentWorker struct {
  Message string
}

func( worker *CurrentWorker )Work(){
  fmt.Println( worker.Message )
}

func main(){
  if false {
    res1 := make( chan string )
    go func() {
      fmt.Println( "Привет" )
      res1 <- "Пока"
    }()
    go func() {
      fmt.Println( "Привет2" )
      res1 <- "Пока2"
    }()
    s := <-res1
    fmt.Println( s )
    s = <-res1
    fmt.Println( s )
    return
  }
  
  workers := []CurrentWorker{ { "Hello" }, { "Привет" }, { "Здорово" }, { "Konichi wa" }, { "Здрав будь" }, { "Здоровеньки, булы!" }, { "Hi" }, { "Yo!" }, { "Wazzup, nigga!" } }
  res := make( chan interface{} )
  ch := make( chan Worker, len( workers ) )
  for i := 0; i < len( workers ); i++ {
    go func() {
      w := <-ch
      w.Work()
      res <- 1
    }()
  }
  
  for i, _ := range workers {
    ch <- &( workers[ i ] )
  }
  
  for i := 0; i < len( workers ); i++ {
    _ = <- res
  }
}
