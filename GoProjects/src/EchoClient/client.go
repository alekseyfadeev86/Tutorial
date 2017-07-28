package main

import (
	"fmt"
	"net"
)

func test_connect( host string, port int, steps int ) {
	addr := net.TCPAddr{ IP: net.ParseIP(host), Port: port }
	conn, err := net.DialTCP("tcp4", nil, &addr)
	if err != nil {
		fmt.Printf("Ошибка подключения к серверу: %s\n", err.Error())
		return
	}

	defer conn.Close()

	msg_str := "qazqwerty123wsxcderfvbgtyhmjuik,.lop;/'[]"
	msg_len := len(msg_str)

	for ; steps > 0; steps-- {
		// Отправка данных
		buf := []byte(msg_str)
		for len(buf) > 0 {
			sz, e := conn.Write(buf)
			if (e != nil) || (sz <= 0) {
				if e != nil {
					fmt.Printf( "Ошибка отправки данных: %s\n", e.Error() )
				} else {
					fmt.Printf( "Ошибка отправки данных: sz=%d\n", sz )
				}
				return
			}

			buf = buf[sz:]
		} // for len(buf) > 0

		// Чтение ответа
		read_buf := make([]byte, msg_len)
		read_sz := 0
		for read_sz < msg_len {
			sz, e := conn.Read(read_buf[read_sz:])
			if (e != nil) || (sz < 0) {
				if e != nil {
					fmt.Printf( "Ошибка чтения данных: %s\n", e.Error() )
				} else {
					fmt.Printf( "Ошибка чтения данных: sz=%d\n", sz )
				}
				return
			} else if sz == 0 {
				fmt.Println( "Соединение закрыто сервером" )
				return
			}

			read_sz += sz
		} // for read_sz < msg_len

		// Сравнение отправленных и полученных данных
		rcv_msg := string(read_buf)
		if rcv_msg != msg_str {
			fmt.Printf( "Несоответствие отправленной (%s) и принятой (%s) строк\n", msg_str, rcv_msg )
			return
		}
	} // for ; steps > 0; steps--
} // func test_connect( host string, port int, steps int )
