package main

import (
	"fmt"
	"net"
	)

func main() {
   if conn, err := net.Dial( "tcp", "127.0.0.1:45000" ); err == nil {
    fmt.Println( "Подключились" )
    defer conn.Close()	
    
    recv_buf := make( []byte, 101 )
    if bytes_num, err := conn.Read( recv_buf ); err == nil {
      fmt.Printf( "Получено %d байтов: %s\n", bytes_num, recv_buf )
      if bytes_num, err = conn.Write( recv_buf ); err == nil {
	fmt.Printf( "Отправлено %d байтов\n", bytes_num )
      } else {
	fmt.Printf( "Ошибка записи: %s\n", err.Error() )
      }
    } else {
      fmt.Printf( "Ошибка чтения: %s\n", err.Error() )
    }
  } else {
    fmt.Printf( "Ошибка подключения: %s\n", err.Error() )
  }
}
